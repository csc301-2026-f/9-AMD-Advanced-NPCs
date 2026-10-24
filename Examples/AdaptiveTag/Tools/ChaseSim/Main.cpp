// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// chase_sim: console front end for ChaseSession. Parses commands, calls the session, prints. No game or learning logic here.
//   chase_sim.exe [--pattern straight|square|circle] [--seed N] [--script file.txt] [--name=value for any config key]
#include "ChaseSession.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

using namespace online_sac;

namespace {
constexpr int BoardColumns = 50, BoardRows = 25;
constexpr unsigned MaxScriptDepth = 8;

struct App {
    ChaseSession* session = nullptr;
    std::ofstream log;
    std::string logPath;
    unsigned watchIndex = 0;
    bool quit = false;
    bool interactiveTerminal = false;
};

void Print(const std::string& line) {
    std::fputs(line.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}
std::string Format(const char* format, ...) {
    char text[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    return text;
}
void Error(const std::string& message) { Print("error: " + message); }

bool ParseCount(const std::string& text, unsigned low, unsigned high, unsigned& value) {
    if (text.empty() || text.size() > 9) return false;
    for (char c : text) if (c < '0' || c > '9') return false;
    const unsigned long parsed = std::stoul(text);
    if (parsed < low || parsed > high) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}
bool CountArg(const std::vector<std::string>& args, size_t index, unsigned fallback, unsigned low, unsigned high, const char* what, unsigned& value) {
    if (args.size() <= index) { value = fallback; return true; }
    if (ParseCount(args[index], low, high, value)) return true;
    Error(Format("%s must be a whole number from %u to %u, got '%s'", what, low, high, args[index].c_str()));
    return false;
}

void LogRow(App& app, const std::string& row) {
    if (app.log.is_open()) { app.log << row << '\n'; app.log.flush(); }
}

// ---- commands ----
void Help() {
    Print("commands:");
    Print("  help                         this list");
    Print("  config                       print every tunable value");
    Print("  set <name> <value>           change one value (world values restart the episode; learner values apply on reset)");
    Print("  pattern <straight|square|circle>   change the runner pattern (keeps the learned weights)");
    Print("  train <episodes>             learn while playing; a line every 10 episodes");
    Print("  eval [n=50]                  fixed starts, no learning: CURRENT, SNAPSHOT, GREEDY, RANDOM");
    Print("  snapshot                     keep the current policy for eval comparison");
    Print("  freeze | unfreeze            stop / resume learning (frozen: mean action, experiences ignored)");
    Print("  reset                        fresh weights, counters cleared (snapshot kept)");
    Print("  watch [episodes=1] [fps=20]  play episodes with the current policy as an ASCII board (no learning)");
    Print("  replay                       positions every 10 steps of the last training episode");
    Print("  log <file.csv> | log off     write every train and eval line to a CSV file");
    Print("  run <file.txt>               run a command script");
    Print("  quit");
}

void Config(App& app) {
    for (const ConfigEntry& e : ListConfig(app.session->Config(), app.session->LearnerConfig())) {
        Print(Format("  %-20s = %-8s # %s", e.name.c_str(), e.value.c_str(), e.comment.c_str()));
    }
}

void Set(App& app, const std::vector<std::string>& args) {
    if (args.size() != 3) { Error("usage: set <name> <value>"); return; }
    ChaseConfig world = app.session->Config();
    ChaseLearnerConfig learner = app.session->LearnerConfig();
    std::string error = SetConfigValue(world, learner, args[1], args[2]);
    if (error.empty()) error = IsLearnerKey(args[1]) ? app.session->SetLearnerConfig(learner) : app.session->SetConfig(world);
    if (!error.empty()) { Error(error + " (unchanged)"); return; }
    Print(Format("%s = %s%s", args[1].c_str(), args[2].c_str(), IsLearnerKey(args[1]) ? " (applies on reset)" : " (current episode restarted, weights kept)"));
}

void Pattern(App& app, const std::vector<std::string>& args) {
    RunnerPattern pattern;
    if (args.size() != 2 || !ParsePattern(args[1], pattern)) {
        Error(std::string("unknown pattern '") + (args.size() > 1 ? args[1] : "") + "' (use straight, square or circle)");
        return;
    }
    app.session->SetPattern(pattern);
    Print(Format("pattern = %s (weights kept, policy v%u)", PatternName(pattern), app.session->GetState().policyVersion));
}

void TrainLine(App& app) {
    const ChaseSessionState s = app.session->GetState();
    Print(Format("episode %5u | steps %7llu | policy v%-6u | catch rate (last %u) %.2f | avg steps to catch %6.1f | avg final dist %5.1f",
        s.episodesDone, static_cast<unsigned long long>(s.totalSteps), s.policyVersion, s.stats.episodes, s.stats.catchRate,
        s.stats.meanStepsToCatch, s.stats.meanFinalDistance));
    LogRow(app, Format("train,%s,%u,%llu,%u,current,%.4f,%.2f,%.3f", PatternName(s.pattern), s.episodesDone,
        static_cast<unsigned long long>(s.totalSteps), s.policyVersion, s.stats.catchRate, s.stats.meanStepsToCatch, s.stats.meanFinalDistance));
}

void Train(App& app, const std::vector<std::string>& args) {
    unsigned episodes;
    if (args.size() != 2) { Error("usage: train <episodes>"); return; }
    if (!CountArg(args, 1, 0, 1, 100000, "episodes", episodes)) return;
    const ChaseSessionState before = app.session->GetState();
    Print(Format("training %u episodes on %s%s", episodes, PatternName(before.pattern),
        before.frozen ? " -- FROZEN: playing without learning (use unfreeze)" : " (learning on)"));
    const auto start = std::chrono::steady_clock::now();
    unsigned done = 0;
    app.session->RunEpisodes(episodes, [&](const EpisodeSummary&) {
        ++done;
        if (done % 10 == 0 || done == episodes) TrainLine(app);
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    Print(Format("trained %u episodes in %.1f s (policy v%u, %llu SAC updates total)", episodes, seconds,
        app.session->GetState().policyVersion, static_cast<unsigned long long>(app.session->GetState().updates)));
}

void Eval(App& app, const std::vector<std::string>& args) {
    unsigned episodes;
    if (args.size() > 2) { Error("usage: eval [n=50]"); return; }
    if (!CountArg(args, 1, 50, 1, 10000, "n", episodes)) return;
    const ChaseEvaluation e = app.session->Evaluate(episodes);
    const ChaseSessionState s = app.session->GetState();
    Print(Format("eval on %s: %u fixed start states (eval_seed %u), deterministic, no learning", PatternName(e.pattern), episodes, app.session->Config().eval_seed));
    Print("  policy             catch rate   avg steps to catch   avg final dist");
    auto row = [&](const std::string& name, const ChaseEvalResult& r, unsigned version) {
        Print(Format("  %-17s  %10.2f   %18.1f   %14.1f", name.c_str(), r.catchRate, r.meanStepsToCatch, r.meanFinalDistance));
        LogRow(app, Format("eval,%s,%u,%llu,%u,%s,%.4f,%.2f,%.3f", PatternName(e.pattern), s.episodesDone,
            static_cast<unsigned long long>(s.totalSteps), version, name.substr(0, name.find(' ')).c_str(), r.catchRate, r.meanStepsToCatch, r.meanFinalDistance));
    };
    row(Format("CURRENT (v%u)", e.currentVersion), e.current, e.currentVersion);
    if (e.hasSnapshot) row(Format("SNAPSHOT (v%u)", e.snapshotVersion), e.snapshot, e.snapshotVersion);
    else Print("  SNAPSHOT           (none taken)");
    row("GREEDY", e.greedy, 0);
    row("RANDOM", e.random, 0);
}

std::vector<std::string> Board(const ChaseTrajectory& t, size_t frame, const ChaseConfig& config) {
    std::vector<std::string> rows(BoardRows + 2, std::string(BoardColumns + 2, ' '));
    for (int c = 0; c < BoardColumns + 2; ++c) rows[0][c] = rows[BoardRows + 1][c] = '-';
    for (int r = 0; r < BoardRows + 2; ++r) { rows[r][0] = r == 0 || r == BoardRows + 1 ? '+' : '|'; rows[r][BoardColumns + 1] = rows[r][0]; }
    auto cell = [&](const Vec2& p) -> char& {
        int c = static_cast<int>(p.x / config.board_size * BoardColumns), r = static_cast<int>((config.board_size - p.y) / config.board_size * BoardRows);
        c = c < 0 ? 0 : (c >= BoardColumns ? BoardColumns - 1 : c);
        r = r < 0 ? 0 : (r >= BoardRows ? BoardRows - 1 : r);
        return rows[r + 1][c + 1];
    };
    const size_t trail = frame > 40 ? frame - 40 : 0;
    for (size_t i = trail; i < frame; ++i) { cell(t.frames[i].runner.position) = '.'; cell(t.frames[i].chaser) = ','; }
    char& runner = cell(t.frames[frame].runner.position);
    runner = 'R';
    char& chaser = cell(t.frames[frame].chaser);
    chaser = &chaser == &runner ? 'X' : 'C';
    return rows;
}

void Watch(App& app, const std::vector<std::string>& args) {
    unsigned episodes, fps;
    if (args.size() > 3) { Error("usage: watch [episodes=1] [fps=20]"); return; }
    if (!CountArg(args, 1, 1, 1, 100, "episodes", episodes) || !CountArg(args, 2, 20, 1, 1000, "fps", fps)) return;
    // In a terminal every frame is redrawn in place; when output is redirected (scripts, transcripts) every 25th frame is printed.
    const unsigned every = app.interactiveTerminal ? 1 : 25;
    for (unsigned episode = 0; episode < episodes; ++episode) {
        const ChaseTrajectory t = app.session->PlayEpisode(app.watchIndex++);
        const ChaseConfig& config = app.session->Config();
        for (size_t frame = 0; frame < t.frames.size(); ++frame) {
            const bool lastFrame = frame + 1 == t.frames.size();
            if (frame % every != 0 && !lastFrame) continue;
            std::string text = app.interactiveTerminal ? "\x1b[H\x1b[2J" : "";
            for (const std::string& row : Board(t, frame, config)) text += row + "\n";
            text += Format("watch %u/%u | %s | step %3zu/%u | distance %5.1f | policy v%u | C chaser, R runner%s",
                episode + 1, episodes, PatternName(t.pattern), frame, config.max_episode_steps, t.frames[frame].distance, t.policyVersion,
                lastFrame ? (t.caught ? Format(" | CAUGHT at step %u", t.steps).c_str() : " | TIME-OUT") : "");
            Print(text);
            if (app.interactiveTerminal) std::this_thread::sleep_for(std::chrono::microseconds(1000000 / fps));
        }
    }
}

void PrintTrajectory(const ChaseTrajectory& t) {
    for (size_t i = 0; i < t.frames.size(); ++i) {
        if (i % 10 != 0 && i + 1 != t.frames.size()) continue;
        const ChaseWorldState& s = t.frames[i];
        Print(Format("  step %3zu  chaser (%5.1f, %5.1f)  runner (%5.1f, %5.1f)  distance %5.1f", i, s.chaser.x, s.chaser.y,
            s.runner.position.x, s.runner.position.y, s.distance));
    }
    if (t.caught) Print(Format("  caught at step %u", t.steps));
    else Print(Format("  not caught (time-out at step %u), final distance %.1f", t.steps, t.frames.back().distance));
}

void Replay(App& app) {
    const ChaseTrajectory& last = app.session->LastTrainingEpisode();
    if (last.frames.empty()) {
        Print("no training episode finished yet; playing one with the current policy (no learning):");
        PrintTrajectory(app.session->PlayEpisode(app.watchIndex++));
        return;
    }
    Print(Format("replay of the last training episode (%s, acted with policy up to v%u):", PatternName(last.pattern), last.policyVersion));
    PrintTrajectory(last);
}

void Log(App& app, const std::vector<std::string>& args) {
    if (args.size() != 2) { Error("usage: log <file.csv> | log off"); return; }
    if (app.log.is_open()) app.log.close();
    if (args[1] == "off") { Print("logging off"); return; }
    app.log.open(args[1], std::ios::out | std::ios::trunc);
    if (!app.log.is_open()) { Error("cannot open '" + args[1] + "' for writing"); return; }
    app.log << "kind,pattern,episode,total_steps,policy_version,policy,catch_rate,avg_steps_to_catch,avg_final_distance\n";
    app.logPath = args[1];
    Print("logging train and eval lines to " + args[1]);
}

bool RunScript(App& app, const std::string& path, unsigned depth);

void Execute(App& app, const std::string& line, unsigned depth) {
    std::istringstream stream(line);
    std::vector<std::string> args;
    for (std::string word; stream >> word;) args.push_back(word);
    if (args.empty()) return;
    const std::string& c = args[0];
    auto noArgs = [&] { if (args.size() == 1) return true; Error("'" + c + "' takes no arguments"); return false; };
    if (c == "help") Help();
    else if (c == "config") { if (noArgs()) Config(app); }
    else if (c == "set") Set(app, args);
    else if (c == "pattern") Pattern(app, args);
    else if (c == "train") Train(app, args);
    else if (c == "eval") Eval(app, args);
    else if (c == "snapshot") { if (noArgs()) { app.session->TakeSnapshot(); Print(Format("snapshot taken: policy v%u", app.session->GetState().snapshotVersion)); } }
    else if (c == "freeze") { if (noArgs()) { app.session->SetFrozen(true); Print(Format("frozen at policy v%u: no learning, mean actions", app.session->GetState().policyVersion)); } }
    else if (c == "unfreeze") { if (noArgs()) { app.session->SetFrozen(false); Print("learning on"); } }
    else if (c == "reset") { if (noArgs()) { app.session->Reset(); Print(Format("reset: fresh weights (seed %u), counters cleared", app.session->LearnerConfig().seed)); } }
    else if (c == "watch") Watch(app, args);
    else if (c == "replay") { if (noArgs()) Replay(app); }
    else if (c == "log") Log(app, args);
    else if (c == "run") {
        if (args.size() != 2) Error("usage: run <file.txt>");
        else if (depth >= MaxScriptDepth) Error("scripts nested too deep");
        else RunScript(app, args[1], depth + 1);
    }
    else if (c == "quit" || c == "exit") { if (noArgs()) app.quit = true; }
    else Error("unknown command '" + c + "' (type 'help')");
}

bool RunScript(App& app, const std::string& path, unsigned depth) {
    std::ifstream file(path);
    if (!file.is_open()) { Error("cannot open script '" + path + "'"); return false; }
    for (std::string line; !app.quit && std::getline(file, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.find_first_not_of(" \t") == std::string::npos) continue;
        Print("> " + line);
        if (line[line.find_first_not_of(" \t")] == '#') continue;
        Execute(app, line, depth);
    }
    return true;
}
}

int main(int argc, char** argv) {
    App app;
#ifdef _WIN32
    app.interactiveTerminal = _isatty(_fileno(stdout)) != 0;
    if (app.interactiveTerminal) {
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD mode = 0;
        if (GetConsoleMode(out, &mode)) SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#else
    app.interactiveTerminal = isatty(fileno(stdout)) != 0;
#endif
    ChaseConfig config;
    ChaseLearnerConfig learner;
    RunnerPattern pattern = RunnerPattern::Straight;
    std::string script;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i], name, value;
        if (arg.rfind("--", 0) != 0) { Error("unexpected argument '" + arg + "'"); return 2; }
        const size_t equals = arg.find('=');
        if (equals != std::string::npos) { name = arg.substr(2, equals - 2); value = arg.substr(equals + 1); }
        else if (i + 1 < argc) { name = arg.substr(2); value = argv[++i]; }
        else { Error("missing value for '" + arg + "'"); return 2; }
        if (name == "pattern") { if (!ParsePattern(value, pattern)) { Error("unknown pattern '" + value + "' (use straight, square or circle)"); return 2; } }
        else if (name == "script") script = value;
        else {
            const std::string error = SetConfigValue(config, learner, name, value);
            if (!error.empty()) { Error(error); return 2; }
        }
    }
    ChaseSession session(config, learner, pattern);
    app.session = &session;
    Print(Format("chase_sim: pattern %s, seed %u, board %g, runner speed %g, chaser max speed %g. Type 'help'.",
        PatternName(pattern), learner.seed, config.board_size, config.runner_speed, config.chaser_max_speed));
    if (!script.empty()) RunScript(app, script, 1);
    for (std::string line; !app.quit;) {
        if (app.interactiveTerminal) { std::fputs("chase> ", stdout); std::fflush(stdout); }
        if (!std::getline(std::cin, line)) break;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!app.interactiveTerminal && line.find_first_not_of(" \t") != std::string::npos) Print("> " + line);
        Execute(app, line, 0);
    }
    return 0;
}

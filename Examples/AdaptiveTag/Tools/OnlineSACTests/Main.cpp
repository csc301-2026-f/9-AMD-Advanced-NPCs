// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Native (no Unreal) harness for OnlineSACRuntime. Usage:
//   online_sac_tests baselines <outdir>
//   online_sac_tests chase <outdir> [trainSteps] [adaptSteps]
//   online_sac_tests push <outdir> [trainSteps] [adaptSteps]
//   online_sac_tests lifecycle <outdir>
//   online_sac_tests checkpoint <outdir>
//   online_sac_tests pendulum <outdir> [steps]
#include "ChasePushExperiment.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace online_sac;
namespace {
void Save(const std::string& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}
EvalResult RandomBaseline(ChasePattern pattern, unsigned episodes, unsigned seed) {
    std::mt19937 rng(seed + 99);
    std::uniform_real_distribution<float> uniform(-1.0f, 1.0f);
    return EvaluatePolicy([&](const float*, float* action) { action[0] = uniform(rng); action[1] = uniform(rng); }, pattern, episodes, seed);
}
EvalResult GreedyBaseline(ChasePattern pattern, unsigned episodes, unsigned seed) {
    return EvaluatePolicy(GreedyPursuitAction, pattern, episodes, seed);
}
std::string Describe(const char* name, const EvalResult& r) {
    char line[256];
    std::snprintf(line, sizeof(line), "%-28s catch_rate=%.3f (%u/%u) mean_steps_to_catch=%.1f mean_return=%.2f\n",
        name, r.catchRate, r.caught, r.episodes, r.meanStepsToCatch, r.meanReturn);
    return line;
}

int Baselines(const std::string& out) {
    const ChaseSACOptions eval;
    std::string report = "Chase baselines (deterministic start states, eval seed " + std::to_string(eval.evalSeed) + ")\n";
    for (ChasePattern pattern : {ChasePattern::Bounce, ChasePattern::Circle}) {
        const char* p = pattern == ChasePattern::Bounce ? "pattern0/bounce" : "pattern1/circle";
        report += std::string("[") + p + "]\n";
        report += Describe("random   (100 eps)", RandomBaseline(pattern, eval.evalEpisodes, eval.evalSeed));
        report += Describe("greedy   (100 eps)", GreedyBaseline(pattern, eval.evalEpisodes, eval.evalSeed));
        report += Describe("random   (1000 eps, seed 7)", RandomBaseline(pattern, 1000, 7));
        report += Describe("greedy   (1000 eps, seed 7)", GreedyBaseline(pattern, 1000, 7));
    }
    std::fputs(report.c_str(), stdout);
    Save(out + "/baselines.txt", report);
    return 0;
}

constexpr double PassCatchRate = 0.80;
constexpr double PassMaxStepsToCatch = 100.0;
constexpr unsigned Seeds[] = {1, 2, 3};

int Chase(const std::string& out, unsigned trainSteps, unsigned adaptSteps, bool push, unsigned updateEvery = OnlineSACConfig{}.updateEvery) {
    OnlineSACConfig learnerConfig;
    learnerConfig.updateEvery = updateEvery;
    std::vector<ChaseSACResult> results(std::size(Seeds));
    std::vector<std::thread> workers;
    for (size_t i = 0; i < std::size(Seeds); ++i) {
        workers.emplace_back([&, i] {
            ChaseSACOptions options;
            options.seed = Seeds[i];
            options.trainSteps = trainSteps;
            options.adaptSteps = adaptSteps;
            results[i] = push ? RunChasePush(options, learnerConfig) : RunChaseSAC(options);
        });
    }
    for (std::thread& worker : workers) worker.join();
    bool passed = true;
    const std::string kind = push ? "push" : "chase";
    std::string report = std::string(push ? "OnlineSACLearner (push, our own loop)" : "RLtools SAC (sac::loop::core)") + " on chase, trainSteps=" + std::to_string(trainSteps) + " adaptSteps=" + std::to_string(adaptSteps) + "\n";
    char line[400];
    for (const ChaseSACResult& r : results) {
        const std::string suffix = adaptSteps ? "-adapt" : "";
        Save(out + "/" + kind + "-seed" + std::to_string(r.seed) + suffix + ".csv", CurveToCsv(r));
        const CurvePoint* trained = r.AfterTraining();
        const bool ok = r.completed && trained && trained->bounce.catchRate >= PassCatchRate && trained->bounce.meanStepsToCatch <= PassMaxStepsToCatch;
        passed = passed && ok;
        std::snprintf(line, sizeof(line), "seed %u: after %u steps bounce catch_rate=%.3f steps_to_catch=%.1f | circle (zero-shot) catch_rate=%.3f | train %.1fs -> %s\n",
            r.seed, trained ? trained->step : 0, trained ? trained->bounce.catchRate : 0, trained ? trained->bounce.meanStepsToCatch : 0,
            trained ? trained->circle.catchRate : 0, r.trainSeconds, ok ? "PASS" : "FAIL");
        report += line;
        if (adaptSteps && !r.curve.empty()) {
            const CurvePoint& last = r.curve.back();
            std::snprintf(line, sizeof(line), "seed %u: after %u more steps on circle: circle catch_rate=%.3f steps_to_catch=%.1f, bounce catch_rate=%.3f | adapt %.1fs\n",
                r.seed, adaptSteps, last.circle.catchRate, last.circle.meanStepsToCatch, last.bounce.catchRate, r.adaptSeconds);
            report += line;
        }
    }
    std::snprintf(line, sizeof(line), "thresholds: catch_rate >= %.2f and steps_to_catch <= %.0f on every seed -> %s\n", PassCatchRate, PassMaxStepsToCatch, passed ? "PASS" : "FAIL");
    report += line;
    std::fputs(report.c_str(), stdout);
    Save(out + "/native-" + kind + std::string(adaptSteps ? "-adapt" : "") + ".txt", report);
    return passed ? 0 : 1;
}

int Lifecycle(const std::string& out) {
    std::string report = "OnlineSACLearner lifecycle checks\n";
    bool passed = true;
    for (const LifecycleCheck& c : {CheckInvalidExperiencesRejected(), CheckVersionIncreases(), CheckShutdownDuringRound(), CheckConcurrentActDuringPublishing()}) {
        report += std::string(c.passed ? "PASS " : "FAIL ") + c.name + ": " + c.detail + "\n";
        passed = passed && c.passed;
    }
    std::fputs(report.c_str(), stdout);
    Save(out + "/native-lifecycle.txt", report);
    return passed ? 0 : 1;
}

const char* Name(CheckpointResult r) {
    switch (r) {
    case CheckpointResult::Ok: return "Ok";
    case CheckpointResult::IoError: return "IoError";
    case CheckpointResult::BadFormat: return "BadFormat";
    case CheckpointResult::Mismatch: return "Mismatch";
    }
    return "?";
}
void PushRandom(OnlineSACLearner& learner, unsigned count, std::mt19937& rng) {
    std::uniform_real_distribution<float> u(-1.0f, 1.0f);
    for (unsigned n = 0; n < count; ++n) {
        float o[ObservationDim], a[ActionDim], next[ObservationDim];
        for (float& v : o) v = u(rng);
        for (float& v : next) v = u(rng);
        for (float& v : a) v = u(rng);
        learner.AddExperience(o, ObservationDim, a, ActionDim, -std::fabs(o[0]), next, ObservationDim, false, false);
    }
}
std::vector<float> Outputs(const OnlineSACLearner& learner) {
    std::vector<float> out;
    for (unsigned i = 0; i < 20; ++i) {
        float o[ObservationDim], a[ActionDim] = {};
        for (unsigned j = 0; j < ObservationDim; ++j) o[j] = std::sin(0.37f * (i + 1) + 1.3f * j);
        learner.Act(o, ObservationDim, a, ActionDim, true);
        out.insert(out.end(), a, a + ActionDim);
    }
    return out;
}
std::vector<char> ReadAll(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(file), {});
}
void WriteAll(const std::string& path, const std::vector<char>& bytes) {
    std::ofstream file(path, std::ios::binary);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

int Checkpoint(const std::string& out) {
    std::string report = "OnlineSACLearner checkpoint checks (FR-11, test 7)\n";
    bool passed = true;
    const auto check = [&](bool ok, const std::string& name, const std::string& detail) {
        report += std::string(ok ? "PASS " : "FAIL ") + name + ": " + detail + "\n";
        passed = passed && ok;
    };
    OnlineSACConfig config;
    config.updateEvery = 50;
    config.warmupExperiences = 100;
    std::mt19937 rng(5);
    OnlineSACLearner trained(config);
    for (int round = 0; round < 6; ++round) { PushRandom(trained, 50, rng); trained.WaitForLearner(0, 60000); }
    const std::vector<float> reference = Outputs(trained);
    const unsigned trainedVersion = trained.Version();
    const std::string path = out + "/native-checkpoint.osac";
    const CheckpointResult saved = trained.SaveCheckpoint(path.c_str());
    const std::vector<char> good = ReadAll(path);
    check(saved == CheckpointResult::Ok && !good.empty(), "save", std::string(Name(saved)) + ", " + std::to_string(good.size()) + " bytes, trained version " + std::to_string(trainedVersion) + ", updates " + std::to_string(trained.ReadStats().updates));

    OnlineSACConfig freshConfig = config;
    freshConfig.seed = 2;
    OnlineSACLearner fresh(freshConfig);
    const std::vector<float> before = Outputs(fresh);
    const CheckpointResult loaded = fresh.LoadCheckpoint(path.c_str());
    const std::vector<float> after = Outputs(fresh);
    const bool identical = after.size() == reference.size() && std::memcmp(after.data(), reference.data(), after.size() * sizeof(float)) == 0;
    check(loaded == CheckpointResult::Ok && identical && before != reference, "round trip", std::string(Name(loaded)) + ", deterministic outputs on 20 observations " + (identical ? "bit-identical" : "DIFFER") + " to the saved learner");
    check(fresh.Version() >= trainedVersion && fresh.Version() >= 1, "version after load", "fresh learner version 0 -> " + std::to_string(fresh.Version()) + " (saved " + std::to_string(trainedVersion) + ")");
    const unsigned trainedBefore = trained.Version();
    const CheckpointResult reload = trained.LoadCheckpoint(path.c_str());
    check(reload == CheckpointResult::Ok && trained.Version() == trainedBefore + 1, "version never goes backwards", "reloading into the saved learner: " + std::to_string(trainedBefore) + " -> " + std::to_string(trained.Version()));

    const auto refused = [&](const std::string& name, const std::vector<char>& bytes) {
        const std::string bad = out + "/native-checkpoint-bad.osac";
        WriteAll(bad, bytes);
        const unsigned v = fresh.Version();
        const CheckpointResult r = fresh.LoadCheckpoint(bad.c_str());
        const bool unchanged = Outputs(fresh) == after && fresh.Version() == v;
        check(r != CheckpointResult::Ok && unchanged, name, std::string("refused with ") + Name(r) + (unchanged ? ", model and version unchanged" : ", MODEL CHANGED"));
        std::remove(bad.c_str());
    };
    refused("truncated file", std::vector<char>(good.begin(), good.begin() + good.size() / 2));
    refused("header only", std::vector<char>(good.begin(), good.begin() + 48));
    std::vector<char> flipped = good;
    flipped[48 + flipped.size() / 2 - 24] ^= 0x01;
    refused("one flipped payload byte", flipped);
    std::vector<char> magic = good;
    magic[0] = 'X';
    refused("wrong magic", magic);
    // Valid header and checksum but a renamed tar entry: must be refused by the structural check, not abort inside RLtools.
    std::vector<char> renamed = good;
    renamed[48] = renamed[48] == 'z' ? 'y' : 'z';
    uint32_t hash = 2166136261u;
    for (size_t i = 48; i < renamed.size(); ++i) { hash ^= static_cast<unsigned char>(renamed[i]); hash *= 16777619u; }
    std::memcpy(renamed.data() + 40, &hash, sizeof(hash));
    refused("renamed entry with valid checksum", renamed);
    const CheckpointResult missing = fresh.LoadCheckpoint((out + "/does-not-exist.osac").c_str());
    check(missing == CheckpointResult::IoError, "missing file", Name(missing));
    std::remove(path.c_str());

    std::fputs(report.c_str(), stdout);
    Save(out + "/native-checkpoint.txt", report);
    return passed ? 0 : 1;
}

int Pendulum(const std::string& out, unsigned steps) {
    const PendulumSACResult r = RunPendulumSAC(1, steps);
    std::string report = "RLtools SAC on Pendulum (fallback diagnostic), mean return of last 10 training episodes every 1000 steps:\n";
    for (size_t i = 0; i < r.returnsEvery1000.size(); ++i) report += std::to_string((i + 1) * 1000) + "," + std::to_string(r.returnsEvery1000[i]) + "\n";
    report += "seconds," + std::to_string(r.seconds) + "\n";
    std::fputs(report.c_str(), stdout);
    Save(out + "/native-pendulum.txt", report);
    return 0;
}
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: online_sac_tests baselines|chase|pendulum <outdir> [...]\n"); return 2; }
    const std::string mode = argv[1], out = argv[2];
    const auto arg = [&](int i, unsigned fallback) { return argc > i ? static_cast<unsigned>(std::strtoul(argv[i], nullptr, 10)) : fallback; };
    if (mode == "baselines") return Baselines(out);
    if (mode == "chase") return Chase(out, arg(3, 20000), arg(4, 0), false);
    if (mode == "push") return Chase(out, arg(3, 20000), arg(4, 0), true, arg(5, OnlineSACConfig{}.updateEvery));
    if (mode == "lifecycle") return Lifecycle(out);
    if (mode == "checkpoint") return Checkpoint(out);
    if (mode == "pendulum") return Pendulum(out, arg(3, 10000));
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}

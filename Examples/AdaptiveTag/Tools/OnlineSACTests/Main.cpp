// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Native (no Unreal) harness for OnlineSACRuntime. Usage:
//   online_sac_tests baselines <outdir>
//   online_sac_tests chase <outdir> [trainSteps] [adaptSteps]
//   online_sac_tests push <outdir> [trainSteps] [adaptSteps]
//   online_sac_tests lifecycle <outdir>
//   online_sac_tests pendulum <outdir> [steps]
#include "ChasePushExperiment.h"
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
    if (mode == "pendulum") return Pendulum(out, arg(3, 10000));
    std::fprintf(stderr, "unknown mode %s\n", mode.c_str());
    return 2;
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChasePushExperiment.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace online_sac {
namespace {
using Clock = std::chrono::steady_clock;
double Seconds(Clock::time_point start) { return std::chrono::duration<double>(Clock::now() - start).count(); }
std::string Format(const char* format, double a = 0, double b = 0, double c = 0, double d = 0) {
    char text[256];
    std::snprintf(text, sizeof(text), format, a, b, c, d);
    return text;
}
EvalResult EvaluateLearner(const OnlineSACLearner& learner, ChasePattern pattern, const ChaseSACOptions& options) {
    return EvaluatePolicy([&](const float* observation, float* action) {
        learner.Act(observation, ObservationDim, action, ActionDim, true);
    }, pattern, options.evalEpisodes, options.evalSeed);
}
void PushSynthetic(OnlineSACLearner& learner, unsigned count) {
    ChaseTask task(99);
    float observation[ObservationDim], next[ObservationDim], action[ActionDim];
    for (unsigned i = 0; i < count; ++i) {
        task.Observe(observation);
        action[0] = std::sin(0.37f * i);
        action[1] = std::cos(0.91f * i);
        const ChaseStep step = task.Step(action);
        task.Observe(next);
        learner.AddExperience(observation, ObservationDim, action, ActionDim, step.reward, next, ObservationDim, step.terminated, step.truncated);
        if (step.truncated) task.Reset();
    }
}
}

ChaseSACResult RunChasePush(const ChaseSACOptions& options, const OnlineSACConfig& learnerConfig) {
    ChaseSACResult result;
    result.seed = options.seed;
    OnlineSACConfig config = learnerConfig;
    config.seed = options.seed;
    OnlineSACLearner learner(config);
    ChaseTask task(options.seed, ChasePattern::Bounce);
    std::vector<float> recentReturns;
    double episodeReturn = 0;
    const auto start = Clock::now();
    auto record = [&](unsigned step, ChasePattern training) {
        CurvePoint point;
        point.step = step;
        point.trainingPattern = static_cast<unsigned>(training);
        point.bounce = EvaluateLearner(learner, ChasePattern::Bounce, options);
        point.circle = EvaluateLearner(learner, ChasePattern::Circle, options);
        float sum = 0;
        const size_t n = recentReturns.size() < 10 ? recentReturns.size() : 10;
        for (size_t i = recentReturns.size() - n; i < recentReturns.size(); ++i) sum += recentReturns[i];
        point.trainReturn = n ? sum / n : 0.0f;
        point.seconds = Seconds(start);
        result.curve.push_back(point);
    };
    float observation[ObservationDim], next[ObservationDim], action[ActionDim];
    auto run = [&](unsigned steps, unsigned& step, ChasePattern pattern) {
        for (unsigned i = 0; i < steps; ++i) {
            task.Observe(observation);
            learner.Act(observation, ObservationDim, action, ActionDim);
            const ChaseStep outcome = task.Step(action);
            task.Observe(next);
            learner.AddExperience(observation, ObservationDim, action, ActionDim, outcome.reward, next, ObservationDim, outcome.terminated, outcome.truncated);
            episodeReturn += outcome.reward;
            if (outcome.truncated) { recentReturns.push_back(static_cast<float>(episodeReturn)); episodeReturn = 0; task.Reset(); }
            learner.WaitForLearner(config.updateEvery, 60000);
            if (++step % options.evalInterval == 0) record(step, pattern);
        }
    };
    record(0, ChasePattern::Bounce);
    unsigned step = 0;
    run(options.trainSteps, step, ChasePattern::Bounce);
    result.trainSeconds = Seconds(start);
    if (options.adaptSteps > 0) {
        task.SetPattern(ChasePattern::Circle);
        task.Reset();
        episodeReturn = 0;
        const auto adaptStart = Clock::now();
        run(options.adaptSteps, step, ChasePattern::Circle);
        result.adaptSeconds = Seconds(adaptStart);
    }
    const OnlineSACStats stats = learner.ReadStats();
    learner.Shutdown();
    result.completed = stats.dropped == 0 && stats.rejected == 0 && stats.version > 0;
    return result;
}

LifecycleCheck CheckInvalidExperiencesRejected() {
    LifecycleCheck check{"invalid experiences are rejected without changing state"};
    OnlineSACConfig config;
    config.updateEvery = 1;
    config.warmupExperiences = 1;
    OnlineSACLearner learner(config);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float good[ObservationDim] = {0.1f, -0.2f, 0, 0, 0.5f, 0.5f};
    float bad[ObservationDim] = {0.1f, nan, 0, 0, 0.5f, 0.5f};
    const float action[ActionDim] = {0.5f, -0.5f}, wide[ActionDim] = {1.5f, 0}, nanAction[ActionDim] = {nan, 0};
    const OnlineSACStats before = learner.ReadStats();
    const unsigned versionBefore = learner.Version();
    unsigned rejected = 0;
    rejected += learner.AddExperience(bad, ObservationDim, action, ActionDim, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim, action, ActionDim, 0, bad, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim - 1, action, ActionDim, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim, action, ActionDim + 1, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim, wide, ActionDim, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim, nanAction, ActionDim, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(good, ObservationDim, action, ActionDim, inf, good, ObservationDim, false, false) == AddResult::Rejected;
    rejected += learner.AddExperience(nullptr, ObservationDim, action, ActionDim, 0, good, ObservationDim, false, false) == AddResult::Rejected;
    float out[ActionDim] = {7, 7};
    const bool actRejected = !learner.Act(bad, ObservationDim, out, ActionDim) && !learner.Act(good, ObservationDim - 1, out, ActionDim) && out[0] == 7 && out[1] == 7;
    learner.WaitForLearner(0, 200);
    const OnlineSACStats after = learner.ReadStats();
    check.passed = rejected == 8 && actRejected && after.rejected == before.rejected + 8 && after.accepted == before.accepted
        && after.stored == before.stored && after.updates == before.updates && learner.Version() == versionBefore;
    check.detail = Format("rejected=%.0f/8 stats.rejected=%.0f accepted=%.0f stored=%.0f", rejected, double(after.rejected), double(after.accepted), double(after.stored))
        + Format(" updates=%.0f version=%.0f act_rejected=%.0f", double(after.updates), learner.Version(), actRejected);
    return check;
}

LifecycleCheck CheckVersionIncreases() {
    LifecycleCheck check{"published version increases"};
    OnlineSACConfig config;
    config.updateEvery = 50;
    config.warmupExperiences = 100;
    OnlineSACLearner learner(config);
    std::vector<unsigned> versions{learner.Version()};
    bool increasing = true;
    for (int round = 0; round < 6; ++round) {
        PushSynthetic(learner, 50);
        learner.WaitForLearner(0, 60000);
        const unsigned v = learner.Version();
        if (versions.size() > 2 && v <= versions.back()) increasing = false;
        versions.push_back(v);
    }
    const OnlineSACStats stats = learner.ReadStats();
    learner.SetFrozen(true);
    float observation[ObservationDim] = {0.3f, 0.1f, 0, 0, 0, 0}, action[ActionDim];
    const AddResult frozenAdd = learner.AddExperience(observation, ObservationDim, observation, ActionDim, 0, observation, ObservationDim, false, false);
    const bool frozenAct = learner.Act(observation, ObservationDim, action, ActionDim);
    float again[ActionDim];
    learner.Act(observation, ObservationDim, again, ActionDim);
    const bool deterministicWhenFrozen = frozenAct && action[0] == again[0] && action[1] == again[1];
    check.passed = increasing && versions.front() == 0 && versions.back() >= 4 && stats.updates > 0 && frozenAdd == AddResult::Frozen && deterministicWhenFrozen;
    check.detail = "versions:";
    for (unsigned v : versions) check.detail += " " + std::to_string(v);
    check.detail += Format(" updates=%.0f frozen_add_ignored=%.0f frozen_act_deterministic=%.0f", double(stats.updates), frozenAdd == AddResult::Frozen, deterministicWhenFrozen);
    return check;
}

LifecycleCheck CheckShutdownDuringRound() {
    LifecycleCheck check{"shutdown during a learning round returns promptly"};
    OnlineSACConfig config;
    config.updateEvery = 2000;
    config.warmupExperiences = 100;
    auto learner = std::make_unique<OnlineSACLearner>(config);
    PushSynthetic(*learner, 2000);
    const auto waitStart = Clock::now();
    while (!learner->ReadStats().training && Seconds(waitStart) < 30) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const bool inRound = learner->ReadStats().training;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    const auto start = Clock::now();
    learner->Shutdown();
    const double shutdownSeconds = Seconds(start);
    const OnlineSACStats stats = learner->ReadStats();
    float observation[ObservationDim] = {0.2f, 0.2f, 0, 0, 0, 0}, action[ActionDim];
    const bool actAfterShutdown = learner->Act(observation, ObservationDim, action, ActionDim, true);
    learner->Shutdown();
    learner.reset();
    check.passed = inRound && shutdownSeconds < 1.0 && !stats.running && stats.version == 0 && actAfterShutdown;
    check.detail = Format("in_round=%.0f shutdown=%.3fs running_after=%.0f version=%.0f", inRound, shutdownSeconds, stats.running, stats.version)
        + Format(" act_after_shutdown=%.0f", actAfterShutdown);
    return check;
}

LifecycleCheck CheckConcurrentActDuringPublishing() {
    LifecycleCheck check{"Act from other threads while snapshots are replaced"};
    OnlineSACConfig config;
    config.updateEvery = 5;
    config.warmupExperiences = 50;
    OnlineSACLearner learner(config);
    std::atomic<bool> done{false};
    std::atomic<uint64_t> calls{0}, invalid{0};
    std::vector<std::thread> readers;
    for (int r = 0; r < 4; ++r) {
        readers.emplace_back([&, r] {
            float observation[ObservationDim] = {0.1f * r, -0.2f, 0.1f, 0, 0.3f, -0.3f}, action[ActionDim];
            while (!done) {
                if (!learner.Act(observation, ObservationDim, action, ActionDim, (r & 1) != 0)
                    || !std::isfinite(action[0]) || !std::isfinite(action[1]) || std::fabs(action[0]) > 1 || std::fabs(action[1]) > 1) ++invalid;
                ++calls;
            }
        });
    }
    for (int round = 0; round < 200; ++round) {
        PushSynthetic(learner, config.updateEvery);
        learner.WaitForLearner(0, 60000);
    }
    done = true;
    for (std::thread& t : readers) t.join();
    const unsigned version = learner.Version();
    check.passed = invalid == 0 && calls > 1000 && version >= 150;
    check.detail = Format("act_calls=%.0f invalid=%.0f versions_published=%.0f", double(calls.load()), double(invalid.load()), version);
    return check;
}
}

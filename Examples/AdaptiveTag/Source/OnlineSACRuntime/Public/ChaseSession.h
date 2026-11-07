// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// A live chase: ChaseWorld + a ChaseLearner (SAC) that learns while it plays. All game and learning logic for
// any front end (console, Unreal actor): call Step() (e.g. once per frame) and draw GetState().
#pragma once
#include "ChaseWorld.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace online_sac {
struct EpisodeSummary {
    unsigned episode = 0;         // 1-based index of the finished training episode
    RunnerPattern pattern = RunnerPattern::Straight;
    bool caught = false;
    unsigned steps = 0;
    float finalDistance = 0, totalReward = 0;
};
// Over the last stats_window finished training episodes.
struct RollingStats {
    unsigned episodes = 0, caught = 0;
    double catchRate = 0;
    double meanStepsToCatch = 0;  // over caught episodes only; 0 when none
    double meanFinalDistance = 0;
};
struct ChaseSessionState {
    ChaseWorldState world;        // the episode in progress
    RunnerPattern pattern = RunnerPattern::Straight;
    uint64_t totalSteps = 0;      // training steps since the last Reset()
    unsigned episodesDone = 0;    // training episodes finished since the last Reset()
    unsigned policyVersion = 0;   // version of the policy Step() acts with
    uint64_t updates = 0;         // SAC gradient updates so far
    bool frozen = false, hasSnapshot = false;
    unsigned snapshotVersion = 0;
    RollingStats stats;
};
struct ChaseStepResult {
    ChaseStepOutcome outcome;
    bool episodeEnded = false;    // when true, `summary` is the finished episode and a new one has started
    EpisodeSummary summary;
};
struct ChaseEvalResult {
    unsigned episodes = 0, caught = 0;
    double catchRate = 0, meanStepsToCatch = 0, meanFinalDistance = 0;
};
struct ChaseEvaluation {
    RunnerPattern pattern = RunnerPattern::Straight;
    unsigned currentVersion = 0, snapshotVersion = 0;
    bool hasSnapshot = false;
    ChaseEvalResult current, snapshot, greedy, random;
};
struct ChaseTrajectory {
    RunnerPattern pattern = RunnerPattern::Straight;
    unsigned policyVersion = 0;
    std::vector<ChaseWorldState> frames;  // frames[0] is the start, frames[i] after step i
    bool caught = false;
    unsigned steps = 0;
};

class ONLINESACRUNTIME_API ChaseSession {
public:
    // Returns a non-empty error from ValidateConfig through `error` and uses defaults when the config is impossible.
    explicit ChaseSession(const ChaseConfig& config = {}, const ChaseLearnerConfig& learner = {},
        RunnerPattern pattern = RunnerPattern::Straight, std::string* error = nullptr);
    ~ChaseSession();
    ChaseSession(const ChaseSession&) = delete;
    ChaseSession& operator=(const ChaseSession&) = delete;

    // One training step: observe, Act, move both, reward, AddExperience; on catch or time-out records the
    // episode and starts the next one. When frozen it plays the mean action and learns nothing.
    ChaseStepResult Step();
    // Steps until `episodes` episodes have finished; `onEpisode` (optional) sees each one.
    void RunEpisodes(unsigned episodes, const std::function<void(const EpisodeSummary&)>& onEpisode = nullptr);
    // Keeps the learned weights; restarts the current episode with the new pattern.
    void SetPattern(RunnerPattern pattern);
    void SetFrozen(bool frozen);
    // Keeps the current policy for comparison in Evaluate(). Survives Reset().
    void TakeSnapshot();
    // Fresh weights (a new learner from the learner config), counters and stats cleared.
    void Reset();
    // Fixed start states (eval_seed), deterministic actions, no learning. Does not change training.
    ChaseEvaluation Evaluate(unsigned episodes) const;
    // Plays one episode with the current policy (mean action) in a separate world; no learning, does not
    // change training. `index` picks the start state (deterministic per index).
    ChaseTrajectory PlayEpisode(unsigned index) const;
    // The last finished training episode (empty frames before the first one).
    const ChaseTrajectory& LastTrainingEpisode() const;

    // Validates; on success applies and restarts the current episode (weights kept). Returns "" or the error.
    std::string SetConfig(const ChaseConfig& config);
    // Validates; on success stored and applied by the next Reset(). Returns "" or the error.
    std::string SetLearnerConfig(const ChaseLearnerConfig& learner);
    const ChaseConfig& Config() const;
    const ChaseLearnerConfig& LearnerConfig() const;
    ChaseSessionState GetState() const;
private:
    struct Impl;
    std::unique_ptr<Impl> data;
};
}

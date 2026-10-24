// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Chase v2 simulation: one chaser (the learner) and one runner on a walled square board. Plain C++, no RLtools.
#pragma once
#include "RunnerPatterns.h"

namespace online_sac {
struct ChaseWorldState {
    Vec2 chaser, chaserVelocity;  // chaserVelocity is what actually happened this step (zeroed by walls)
    RunnerState runner;
    unsigned step = 0;            // steps taken in this episode
    float distance = 0;           // chaser to runner
    bool caught = false;
};
struct ChaseStepOutcome {
    float reward = 0;             // distance closed this step + catch_bonus on the catching step
    bool terminated = false;      // caught
    bool truncated = false;       // max_episode_steps reached without a catch
};

class ONLINESACRUNTIME_API ChaseWorld {
public:
    // Starts an episode: the runner at a random point of `pattern`, the chaser at rest at least
    // min_start_distance away. Keeps a copy of `config` for the whole episode.
    void Reset(const ChaseConfig& config, RunnerPattern pattern, ChaseRng& rng);
    // ChaseObservationSize values, see ChaseConfig.h.
    void Observe(float* observation) const;
    // `action`: ChaseActionSize values in [-1, 1] (clamped), the desired velocity / chaser_max_speed.
    ChaseStepOutcome Step(const float* action);
    const ChaseWorldState& State() const { return state; }
    RunnerPattern Pattern() const { return pattern; }
    const ChaseConfig& Config() const { return config; }
private:
    ChaseConfig config;
    RunnerPattern pattern = RunnerPattern::Straight;
    ChaseWorldState state;
};

// Baseline: run straight at the runner's current position at full speed.
ONLINESACRUNTIME_API void GreedyChaseAction(const float* observation, float* action);
}

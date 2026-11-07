// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// How the runner moves. Plain C++: coordinates are board units with y pointing up, so "clockwise" is as seen
// on a map with north at the top. Straight, Square and Circle ignore the chaser; the habit patterns react to it.
// All patterns are deterministic given the start state.
#pragma once
#include "ChaseConfig.h"
#include <cstdint>

namespace online_sac {
struct Vec2 {
    float x = 0, y = 0;
};

// Small portable RNG (splitmix64) so a seed gives the same episodes on every compiler.
class ONLINESACRUNTIME_API ChaseRng {
public:
    explicit ChaseRng(uint64_t seed = 1) : state(seed) {}
    uint64_t Next();
    float Uniform(float low, float high);
private:
    uint64_t state;
};

struct RunnerState {
    Vec2 position, velocity;  // velocity is per step; its length is runner_speed
    float phase = 0;          // square: distance along the perimeter; circle: angle in radians; corner: target corner 0-3 (-1 = none yet); others: unused
};

// A random point of the pattern (straight: random position and random heading).
ONLINESACRUNTIME_API RunnerState StartRunner(RunnerPattern pattern, const ChaseConfig& config, ChaseRng& rng);
// Moves the runner by one step. `chaser` is where the chaser is now (only the habit patterns look at it).
ONLINESACRUNTIME_API void MoveRunner(RunnerPattern pattern, const ChaseConfig& config, RunnerState& runner, const Vec2& chaser);
}

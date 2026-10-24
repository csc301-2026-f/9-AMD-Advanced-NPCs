// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Every tunable value of the chase v2 simulation (ChaseWorld / ChaseSession) lives in this file.
#pragma once
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif
#include <string>
#include <vector>
#ifndef ONLINESACRUNTIME_API
#define ONLINESACRUNTIME_API
#endif

namespace online_sac {
// Compile-time sizes (RLtools needs them as template arguments).
// Observation: chaser x, y, runner x, y (/ board_size), chaser vx, vy (/ chaser_max_speed), runner vx, vy (/ runner_speed).
constexpr unsigned ChaseObservationSize = 8;
// Action: desired chaser velocity x, y in [-1, 1], scaled by chaser_max_speed.
constexpr unsigned ChaseActionSize = 2;

enum class RunnerPattern : unsigned { Straight = 0, Square = 1, Circle = 2 };
ONLINESACRUNTIME_API const char* PatternName(RunnerPattern pattern);
// Accepts "straight", "square", "circle". Returns false for anything else.
ONLINESACRUNTIME_API bool ParsePattern(const std::string& name, RunnerPattern& pattern);

// World and game rules. Changing these restarts the current episode; learned weights are kept.
struct ChaseConfig {
    float board_size = 100.0f;          // Board is [0, board_size]^2 units with walls at the edges.
    unsigned max_episode_steps = 300;   // Steps (1 step = 1 time unit) before an episode is truncated.
    float runner_speed = 1.0f;          // Runner distance per step. Same as chaser_max_speed on purpose.
    float chaser_max_speed = 1.0f;      // Chaser top speed per step; the action vector is capped to this length.
    float catch_distance = 3.0f;        // Caught when the distance is at most this (episode terminates).
    float min_start_distance = 30.0f;   // Chaser spawns at least this far from the runner.
    float square_side = 60.0f;          // Square pattern side, centred on the board (corners at 20 and 80 by default).
    float circle_radius = 30.0f;        // Circle pattern radius around the board centre.
    float catch_bonus = 50.0f;          // Reward added on the catching step (reward = distance closed + bonus).
    unsigned stats_window = 20;         // Episodes in the rolling training stats (catch rate, steps, final distance).
    unsigned eval_seed = 4242;          // Seed of the fixed evaluation start states (same for every policy).
};

// Learner settings. These take effect on Reset() (fresh weights).
struct ChaseLearnerConfig {
    unsigned seed = 1;                  // Seeds the network init, exploration and training start states.
    unsigned update_every = 1;          // SAC updates run in rounds every N experiences (50 caused snapshot lag).
    unsigned warmup_experiences = 1000; // No updates (and random actions) until this many experiences are stored.
    unsigned queue_capacity = 4096;     // Experiences waiting for the learner thread; extra ones are dropped.
    bool lockstep = true;               // Step() waits for the learner to finish its round (reproducible).
                                        // A real-time game sets false so a frame never blocks.
    // Fixed at compile time (RLtools template arguments), not settable here: replay buffer 60000
    // (Private/RLtoolsSetup.h, SACLoopParameters), and RLtools SAC/loop defaults: batch 32, gamma 0.99,
    // polyak 0.995, adaptive entropy (target -2), actor and critics 3 layers x 64 ReLU, Adam.
};

// One named, settable value (for consoles, config files, Unreal console variables...).
struct ConfigEntry {
    std::string name, value, comment;
};
ONLINESACRUNTIME_API std::vector<ConfigEntry> ListConfig(const ChaseConfig& world, const ChaseLearnerConfig& learner);
// Sets `name` from text. Returns an empty string on success, else a message; nothing changes on failure.
ONLINESACRUNTIME_API std::string SetConfigValue(ChaseConfig& world, ChaseLearnerConfig& learner, const std::string& name, const std::string& value);
// Empty string when every value is possible, else what is wrong.
ONLINESACRUNTIME_API std::string ValidateConfig(const ChaseConfig& world, const ChaseLearnerConfig& learner);
// True if `name` is a ChaseLearnerConfig key (applies on reset).
ONLINESACRUNTIME_API bool IsLearnerKey(const std::string& name);
}

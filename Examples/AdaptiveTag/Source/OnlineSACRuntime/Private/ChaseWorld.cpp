// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseWorld.h"
#include <cmath>

namespace online_sac {
namespace {
float Distance(const Vec2& a, const Vec2& b) { return std::hypot(a.x - b.x, a.y - b.y); }
float Clamp(float v, float low, float high) { return v < low ? low : (v > high ? high : v); }
float Unit(float v) { return std::isfinite(v) ? Clamp(v, -1, 1) : 0.0f; }
}

void ChaseWorld::Reset(const ChaseConfig& value, RunnerPattern runnerPattern, ChaseRng& rng) {
    config = value;
    pattern = runnerPattern;
    state = {};
    state.runner = StartRunner(pattern, config, rng);
    // Rejection sampling; if no spot is far enough (tiny board), keep the farthest one tried.
    float best = -1;
    for (int attempt = 0; attempt < 100; ++attempt) {
        const Vec2 candidate{rng.Uniform(0, config.board_size), rng.Uniform(0, config.board_size)};
        const float d = Distance(candidate, state.runner.position);
        if (d > best) { best = d; state.chaser = candidate; }
        if (d >= config.min_start_distance) break;
    }
    state.distance = Distance(state.chaser, state.runner.position);
}

void ChaseWorld::Observe(float* observation) const {
    const float b = config.board_size;
    observation[0] = Unit(state.chaser.x / b);
    observation[1] = Unit(state.chaser.y / b);
    observation[2] = Unit(state.runner.position.x / b);
    observation[3] = Unit(state.runner.position.y / b);
    observation[4] = Unit(state.chaserVelocity.x / config.chaser_max_speed);
    observation[5] = Unit(state.chaserVelocity.y / config.chaser_max_speed);
    observation[6] = Unit(state.runner.velocity.x / config.runner_speed);
    observation[7] = Unit(state.runner.velocity.y / config.runner_speed);
#ifndef ONLINE_SAC_NO_RELATIVE_OBS
    observation[8] = Unit((state.runner.position.x - state.chaser.x) / b);
    observation[9] = Unit((state.runner.position.y - state.chaser.y) / b);
#endif
}

ChaseStepOutcome ChaseWorld::Step(const float* action) {
    ChaseStepOutcome outcome;
    // Desired velocity, length capped at chaser_max_speed so diagonals are not faster; turns instantly.
    Vec2 v{Unit(action[0]) * config.chaser_max_speed, Unit(action[1]) * config.chaser_max_speed};
    const float speed = std::hypot(v.x, v.y);
    if (speed > config.chaser_max_speed) { v.x *= config.chaser_max_speed / speed; v.y *= config.chaser_max_speed / speed; }
    const Vec2 before = state.chaser;
    state.chaser = {Clamp(before.x + v.x, 0, config.board_size), Clamp(before.y + v.y, 0, config.board_size)};
    state.chaserVelocity = {state.chaser.x - before.x, state.chaser.y - before.y};
    MoveRunner(pattern, config, state.runner, state.chaser);

    const float previous = state.distance;
    state.distance = Distance(state.chaser, state.runner.position);
    ++state.step;
    outcome.reward = previous - state.distance;
    if (state.distance <= config.catch_distance) {
        state.caught = true;
        outcome.terminated = true;
        outcome.reward += config.catch_bonus;
    } else if (state.step >= config.max_episode_steps) {
        outcome.truncated = true;
    }
    return outcome;
}

void GreedyChaseAction(const float* observation, float* action) {
    const float dx = observation[2] - observation[0], dy = observation[3] - observation[1];
    const float length = std::hypot(dx, dy);
    action[0] = length > 1e-6f ? dx / length : 0.0f;
    action[1] = length > 1e-6f ? dy / length : 0.0f;
}

void InterceptChaseAction(const float* observation, const ChaseConfig& config, float* action) {
    const float b = config.board_size;
    const float dx = (observation[2] - observation[0]) * b, dy = (observation[3] - observation[1]) * b;
    const float vx = observation[6] * config.runner_speed, vy = observation[7] * config.runner_speed;
    // Smallest t > 0 with |d + v t| = chaser_max_speed * t:  (v.v - s^2) t^2 + 2 (d.v) t + d.d = 0.
    const float s = config.chaser_max_speed;
    const float a = vx * vx + vy * vy - s * s, h = dx * vx + dy * vy, c = dx * dx + dy * dy;
    float t = -1;
    if (std::fabs(a) < 1e-6f) {
        if (h < 0) t = -c / (2 * h);
    } else {
        const float disc = h * h - a * c;
        if (disc >= 0) {
            const float root = std::sqrt(disc), t1 = (-h - root) / a, t2 = (-h + root) / a;
            t = std::fmin(t1 > 0 ? t1 : 1e30f, t2 > 0 ? t2 : 1e30f);
            if (t > 1e29f) t = -1;
        }
    }
    float tx = dx, ty = dy;
    if (t > 0) { tx += vx * t; ty += vy * t; }
    const float length = std::hypot(tx, ty);
    action[0] = length > 1e-6f ? tx / length : 0.0f;
    action[1] = length > 1e-6f ? ty / length : 0.0f;
}
}

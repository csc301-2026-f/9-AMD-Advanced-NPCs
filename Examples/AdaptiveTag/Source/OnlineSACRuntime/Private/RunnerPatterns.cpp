// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RunnerPatterns.h"
#include <cmath>

namespace online_sac {
namespace {
constexpr float TwoPi = 6.28318530718f;
// Clockwise from the top-left corner: right along the top, down the right side, left along the bottom, up the left side.
void PlaceOnSquare(const ChaseConfig& config, RunnerState& runner) {
    const float side = config.square_side;
    const float low = (config.board_size - side) / 2, high = low + side;
    const unsigned edge = static_cast<unsigned>(runner.phase / side) % 4;
    const float t = runner.phase - edge * side;
    const float s = config.runner_speed;
    switch (edge) {
    case 0: runner.position = {low + t, high}; runner.velocity = {s, 0}; break;
    case 1: runner.position = {high, high - t}; runner.velocity = {0, -s}; break;
    case 2: runner.position = {high - t, low}; runner.velocity = {-s, 0}; break;
    default: runner.position = {low, low + t}; runner.velocity = {0, s}; break;
    }
}
// Clockwise means the angle decreases.
void PlaceOnCircle(const ChaseConfig& config, RunnerState& runner) {
    const float centre = config.board_size / 2, c = std::cos(runner.phase), s = std::sin(runner.phase);
    runner.position = {centre + config.circle_radius * c, centre + config.circle_radius * s};
    runner.velocity = {config.runner_speed * s, -config.runner_speed * c};
}
float Bounce(float p, float& v, float size) {
    if (p < 0) { p = -p; v = -v; }
    if (p > size) { p = 2 * size - p; v = -v; }
    return p < 0 ? 0 : (p > size ? size : p);
}
}

uint64_t ChaseRng::Next() {
    uint64_t z = (state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
float ChaseRng::Uniform(float low, float high) {
    const float u = static_cast<float>(Next() >> 40) * (1.0f / 16777216.0f);
    return low + (high - low) * u;
}

RunnerState StartRunner(RunnerPattern pattern, const ChaseConfig& config, ChaseRng& rng) {
    RunnerState runner;
    switch (pattern) {
    case RunnerPattern::Straight: {
        runner.position = {rng.Uniform(0, config.board_size), rng.Uniform(0, config.board_size)};
        const float heading = rng.Uniform(0, TwoPi);
        runner.velocity = {config.runner_speed * std::cos(heading), config.runner_speed * std::sin(heading)};
        break;
    }
    case RunnerPattern::Square:
        runner.phase = rng.Uniform(0, 4 * config.square_side);
        PlaceOnSquare(config, runner);
        break;
    case RunnerPattern::Circle:
        runner.phase = rng.Uniform(0, TwoPi);
        PlaceOnCircle(config, runner);
        break;
    }
    return runner;
}

void MoveRunner(RunnerPattern pattern, const ChaseConfig& config, RunnerState& runner) {
    switch (pattern) {
    case RunnerPattern::Straight:
        runner.position.x = Bounce(runner.position.x + runner.velocity.x, runner.velocity.x, config.board_size);
        runner.position.y = Bounce(runner.position.y + runner.velocity.y, runner.velocity.y, config.board_size);
        break;
    case RunnerPattern::Square:
        runner.phase = std::fmod(runner.phase + config.runner_speed, 4 * config.square_side);
        PlaceOnSquare(config, runner);
        break;
    case RunnerPattern::Circle:
        runner.phase -= config.runner_speed / config.circle_radius;
        if (runner.phase < 0) runner.phase += TwoPi;
        PlaceOnCircle(config, runner);
        break;
    }
}
}

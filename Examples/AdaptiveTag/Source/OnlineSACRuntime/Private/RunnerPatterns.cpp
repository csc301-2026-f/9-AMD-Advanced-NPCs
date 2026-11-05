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

Vec2 Unit(Vec2 v, Vec2 fallback) {
    const float length = std::hypot(v.x, v.y);
    return length > 1e-6f ? Vec2{v.x / length, v.y / length} : fallback;
}
// Clockwise rotation (the angle decreases), matching PlaceOnCircle.
Vec2 RotateClockwise(Vec2 v, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return {v.x * c + v.y * s, -v.x * s + v.y * c};
}
// Direction of travel that goes clockwise around the board along the wall nearest to `p`.
Vec2 ClockwiseAlongWall(const ChaseConfig& config, Vec2 p) {
    const float b = config.board_size;
    const float top = b - p.y, right = b - p.x, bottom = p.y, left = p.x;
    const float nearest = std::fmin(std::fmin(top, right), std::fmin(bottom, left));
    if (nearest == top) return {1, 0};
    if (nearest == right) return {0, -1};
    if (nearest == bottom) return {-1, 0};
    return {0, 1};
}
// Moves by `direction` * runner_speed. A move that would leave the board slides along the wall instead
// (clockwise if there is no sideways part), so the runner never loses speed to a wall.
void StepInside(const ChaseConfig& config, RunnerState& runner, Vec2 direction) {
    const float b = config.board_size, s = config.runner_speed;
    Vec2 d = Unit(direction, {1, 0});
    Vec2 next{runner.position.x + d.x * s, runner.position.y + d.y * s};
    if (next.x < 0 || next.x > b) d.x = 0;
    if (next.y < 0 || next.y > b) d.y = 0;
    if (d.x == 0 || d.y == 0) {
        d = Unit(d, ClockwiseAlongWall(config, runner.position));
        next = {runner.position.x + d.x * s, runner.position.y + d.y * s};
        // In a corner the wall tangent can still point out of the board: take the first clockwise direction that fits.
        const Vec2 tangents[] = {{1, 0}, {0, -1}, {-1, 0}, {0, 1}};
        for (const Vec2& t : tangents) {
            if (next.x >= 0 && next.x <= b && next.y >= 0 && next.y <= b) break;
            d = t;
            next = {runner.position.x + d.x * s, runner.position.y + d.y * s};
        }
    }
    runner.velocity = {d.x * s, d.y * s};
    runner.position = {next.x < 0 ? 0 : (next.x > b ? b : next.x), next.y < 0 ? 0 : (next.y > b ? b : next.y)};
}
Vec2 CornerPoint(const ChaseConfig& config, int corner) {
    const float b = config.board_size;
    return {(corner & 1) ? b : 0.0f, (corner & 2) ? b : 0.0f};
}
float DistanceTo(Vec2 a, Vec2 b) { return std::hypot(a.x - b.x, a.y - b.y); }

// Flees the chaser, but always veers to the right (clockwise). With the chaser far, curves right gently.
void MoveRightTurn(const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
    Vec2 heading = Unit(runner.velocity, {1, 0});
    if (DistanceTo(runner.position, chaser) < config.flee_radius) {
        const Vec2 away = Unit({runner.position.x - chaser.x, runner.position.y - chaser.y}, heading);
        heading = RotateClockwise(away, config.flee_veer);
    } else {
        heading = RotateClockwise(heading, config.habit_turn);
    }
    StepInside(config, runner, heading);
}
// Always goes clockwise along the walls (pulled to wall_margin from the wall); the chaser only nudges it away.
void MoveClockwise(const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
    const float b = config.board_size, m = config.wall_margin;
    const Vec2 p = runner.position;
    const Vec2 along = ClockwiseAlongWall(config, p);
    // Pull towards the line wall_margin inside the nearest wall (full strength 4 units or more away from it).
    Vec2 pull{0, 0};
    if (along.x != 0) { const float line = along.x > 0 ? b - m : m; pull.y = (line - p.y) / 4; }
    else { const float line = along.y > 0 ? m : b - m; pull.x = (line - p.x) / 4; }
    pull = {std::fmax(-1.0f, std::fmin(1.0f, pull.x)), std::fmax(-1.0f, std::fmin(1.0f, pull.y))};
    Vec2 heading{along.x + pull.x, along.y + pull.y};
    if (DistanceTo(p, chaser) < config.flee_radius) {
        const Vec2 away = Unit({p.x - chaser.x, p.y - chaser.y}, {0, 0});
        heading = {heading.x + 0.7f * away.x, heading.y + 0.7f * away.y};  // 0.7 < 1: the clockwise part always wins along the wall
    }
    StepInside(config, runner, heading);
}
// Runs to a corner and waits there; when the chaser comes close, runs to the corner farthest from the chaser.
void MoveCorner(const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
    auto farthest = [&](int skip) {
        int best = -1;
        float bestDistance = -1;
        for (int c = 0; c < 4; ++c) {
            const float d = DistanceTo(CornerPoint(config, c), chaser);
            if (c != skip && d > bestDistance) { bestDistance = d; best = c; }
        }
        return best;
    };
    int target = static_cast<int>(runner.phase);
    if (runner.phase < 0) target = farthest(-1);
    const Vec2 corner = CornerPoint(config, target);
    if (DistanceTo(runner.position, corner) <= config.runner_speed) {
        runner.position = corner;
        runner.velocity = {0, 0};
        if (DistanceTo(runner.position, chaser) < config.flee_radius) target = farthest(target);
    } else {
        StepInside(config, runner, {corner.x - runner.position.x, corner.y - runner.position.y});
    }
    runner.phase = static_cast<float>(target);
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
    case RunnerPattern::RightTurn:
    case RunnerPattern::Clockwise:
    case RunnerPattern::Corner: {
        runner.position = {rng.Uniform(0, config.board_size), rng.Uniform(0, config.board_size)};
        const float heading = rng.Uniform(0, TwoPi);
        runner.velocity = {config.runner_speed * std::cos(heading), config.runner_speed * std::sin(heading)};
        runner.phase = -1;
        break;
    }
    }
    return runner;
}

void MoveRunner(RunnerPattern pattern, const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
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
    case RunnerPattern::RightTurn: MoveRightTurn(config, runner, chaser); break;
    case RunnerPattern::Clockwise: MoveClockwise(config, runner, chaser); break;
    case RunnerPattern::Corner: MoveCorner(config, runner, chaser); break;
    }
}
}

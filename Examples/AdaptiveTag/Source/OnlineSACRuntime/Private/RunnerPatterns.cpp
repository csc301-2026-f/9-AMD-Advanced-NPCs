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

// Only ever turns right (clockwise). Calm: a gentle right-hand curve. Chaser close: turns right as fast as flee_turn
// allows until it faces away from the chaser; if "away" is to its left it keeps turning right, so it loops the long way round.
void MoveRightTurn(const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
    Vec2 heading = Unit(runner.velocity, {1, 0});
    if (DistanceTo(runner.position, chaser) < config.flee_radius) {
        const Vec2 away = Unit({runner.position.x - chaser.x, runner.position.y - chaser.y}, heading);
        // Clockwise angle (0 to 2 pi) from the current heading to "away".
        float needed = std::atan2(heading.x * away.y - heading.y * away.x, heading.x * away.x + heading.y * away.y);
        needed = needed <= 0 ? -needed : TwoPi - needed;
        heading = RotateClockwise(heading, std::fmin(needed, config.flee_turn));
    } else {
        heading = RotateClockwise(heading, config.habit_turn);
    }
    StepInside(config, runner, heading);
}
// Always goes clockwise round the walls (pulled to wall_margin from the wall). With the chaser close it swerves
// away from it (cutting inward round it) but never stops making clockwise progress.
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
        heading = {pull.x + along.x + 2.0f * away.x, pull.y + along.y + 2.0f * away.y};
        const float progress = heading.x * along.x + heading.y * along.y;
        const float length = std::hypot(heading.x, heading.y);
        // Keep at least 30% of the movement clockwise along the wall, whatever the chaser does.
        if (progress < 0.3f * length) { const float add = 0.3f * length - progress; heading = {heading.x + add * along.x, heading.y + add * along.y}; }
    }
    StepInside(config, runner, heading);
}
// Runs to a corner and waits there. When the chaser comes close it leaves at once (or switches mid-run) for the other corner it can
// reach with the biggest lead over the chaser (distance from the chaser minus distance from the runner).
void MoveCorner(const ChaseConfig& config, RunnerState& runner, const Vec2& chaser) {
    auto lead = [&](int corner) { return DistanceTo(CornerPoint(config, corner), chaser) - DistanceTo(CornerPoint(config, corner), runner.position); };
    auto best = [&](int skip) {
        int bestCorner = -1;
        for (int c = 0; c < 4; ++c) if (c != skip && (bestCorner < 0 || lead(c) > lead(bestCorner))) bestCorner = c;
        return bestCorner;
    };
    int target = static_cast<int>(runner.phase);
    if (runner.phase < 0) target = best(-1);
    else if (DistanceTo(runner.position, chaser) < config.flee_radius) {
        // Never consider the corner it is standing in: staying always looks safest (its lead is its whole distance to the chaser).
        int here = -1;
        for (int c = 0; c < 4; ++c) if (DistanceTo(runner.position, CornerPoint(config, c)) <= 3.0f) here = c;
        const int candidate = best(here);
        if (target == here || lead(candidate) > lead(target) + 25.0f) target = candidate;  // it commits to a corner: 25 units of hysteresis, no dithering
    }
    const Vec2 corner = CornerPoint(config, target);
    if (DistanceTo(runner.position, corner) <= config.runner_speed) {
        runner.position = corner;
        runner.velocity = {0, 0};
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

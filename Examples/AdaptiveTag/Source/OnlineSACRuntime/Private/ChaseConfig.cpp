// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseConfig.h"
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace online_sac {
namespace {
// One settable value; exactly one of the three pointers is set.
struct Field {
    const char* name;
    float* f;
    unsigned* u;
    bool* b;
    bool learner;
    const char* comment;
};
std::vector<Field> Fields(ChaseConfig& w, ChaseLearnerConfig& l) {
    return {
        {"board_size", &w.board_size, nullptr, nullptr, false, "board is [0, board_size]^2 units, walls at the edges"},
        {"max_episode_steps", nullptr, &w.max_episode_steps, nullptr, false, "steps before an episode is truncated (1 step = 1 time unit)"},
        {"runner_speed", &w.runner_speed, nullptr, nullptr, false, "runner distance per step"},
        {"chaser_max_speed", &w.chaser_max_speed, nullptr, nullptr, false, "chaser top speed per step (action vector capped to it)"},
        {"catch_distance", &w.catch_distance, nullptr, nullptr, false, "caught when distance <= this"},
        {"min_start_distance", &w.min_start_distance, nullptr, nullptr, false, "chaser spawns at least this far from the runner"},
        {"square_side", &w.square_side, nullptr, nullptr, false, "square pattern side, centred on the board"},
        {"circle_radius", &w.circle_radius, nullptr, nullptr, false, "circle pattern radius around the board centre"},
        {"catch_bonus", &w.catch_bonus, nullptr, nullptr, false, "reward on catch (reward = distance closed + bonus)"},
        {"stats_window", nullptr, &w.stats_window, nullptr, false, "episodes in the rolling training stats"},
        {"eval_seed", nullptr, &w.eval_seed, nullptr, false, "seed of the fixed evaluation start states"},
        {"seed", nullptr, &l.seed, nullptr, true, "learner + training start states seed (applies on reset)"},
        {"update_every", nullptr, &l.update_every, nullptr, true, "SAC update round every N experiences (applies on reset)"},
        {"warmup_experiences", nullptr, &l.warmup_experiences, nullptr, true, "random actions, no updates until this many (applies on reset)"},
        {"queue_capacity", nullptr, &l.queue_capacity, nullptr, true, "experiences waiting for the learner thread (applies on reset)"},
        {"lockstep", nullptr, nullptr, &l.lockstep, true, "Step() waits for the learner round, reproducible (applies on reset)"},
    };
}
std::string Text(const Field& field) {
    char text[64];
    if (field.f) std::snprintf(text, sizeof(text), "%g", *field.f);
    else if (field.u) std::snprintf(text, sizeof(text), "%u", *field.u);
    else std::snprintf(text, sizeof(text), "%s", *field.b ? "true" : "false");
    return text;
}
bool ParseFloat(const std::string& text, float& value) {
    if (text.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(text.c_str(), &end);
    if (errno != 0 || *end != '\0' || !std::isfinite(parsed) || std::fabs(parsed) > 1e9) return false;
    value = static_cast<float>(parsed);
    return true;
}
bool ParseUnsigned(const std::string& text, unsigned& value) {
    if (text.empty() || text.size() > 10) return false;
    for (char c : text) if (c < '0' || c > '9') return false;
    const unsigned long long parsed = std::strtoull(text.c_str(), nullptr, 10);
    if (parsed > 0xFFFFFFFFull) return false;
    value = static_cast<unsigned>(parsed);
    return true;
}
bool ParseBool(const std::string& text, bool& value) {
    if (text == "true" || text == "1" || text == "on") { value = true; return true; }
    if (text == "false" || text == "0" || text == "off") { value = false; return true; }
    return false;
}
std::string Join(const char* a, float v, const char* b) {
    char text[256];
    std::snprintf(text, sizeof(text), "%s%g%s", a, v, b);
    return text;
}
}

const char* PatternName(RunnerPattern pattern) {
    switch (pattern) {
    case RunnerPattern::Straight: return "straight";
    case RunnerPattern::Square: return "square";
    case RunnerPattern::Circle: return "circle";
    }
    return "?";
}
bool ParsePattern(const std::string& name, RunnerPattern& pattern) {
    for (RunnerPattern p : {RunnerPattern::Straight, RunnerPattern::Square, RunnerPattern::Circle}) {
        if (name == PatternName(p)) { pattern = p; return true; }
    }
    return false;
}

std::vector<ConfigEntry> ListConfig(const ChaseConfig& world, const ChaseLearnerConfig& learner) {
    ChaseConfig w = world;
    ChaseLearnerConfig l = learner;
    std::vector<ConfigEntry> entries;
    for (const Field& field : Fields(w, l)) entries.push_back({field.name, Text(field), field.comment});
    return entries;
}

bool IsLearnerKey(const std::string& name) {
    ChaseConfig w;
    ChaseLearnerConfig l;
    for (const Field& field : Fields(w, l)) if (name == field.name) return field.learner;
    return false;
}

std::string SetConfigValue(ChaseConfig& world, ChaseLearnerConfig& learner, const std::string& name, const std::string& value) {
    ChaseConfig w = world;
    ChaseLearnerConfig l = learner;
    for (const Field& field : Fields(w, l)) {
        if (name != field.name) continue;
        bool ok;
        if (field.f) ok = ParseFloat(value, *field.f);
        else if (field.u) ok = ParseUnsigned(value, *field.u);
        else ok = ParseBool(value, *field.b);
        if (!ok) {
            return "'" + value + "' is not a valid " + (field.f ? "number" : field.u ? "whole number >= 0" : "true/false") + " for " + name;
        }
        const std::string error = ValidateConfig(w, l);
        if (!error.empty()) return error;
        world = w;
        learner = l;
        return "";
    }
    return "unknown config key '" + name + "' (type 'config' to list them)";
}

std::string ValidateConfig(const ChaseConfig& w, const ChaseLearnerConfig& l) {
    if (!(w.board_size >= 10 && w.board_size <= 100000)) return "board_size must be between 10 and 100000";
    if (w.max_episode_steps < 1 || w.max_episode_steps > 1000000) return "max_episode_steps must be between 1 and 1000000";
    if (!(w.runner_speed > 0 && w.runner_speed < w.board_size / 2)) return Join("runner_speed must be > 0 and < board_size / 2 (", w.board_size / 2, ")");
    if (!(w.chaser_max_speed > 0 && w.chaser_max_speed < w.board_size / 2)) return Join("chaser_max_speed must be > 0 and < board_size / 2 (", w.board_size / 2, ")");
    if (!(w.catch_distance > 0 && w.catch_distance < w.board_size / 2)) return Join("catch_distance must be > 0 and < board_size / 2 (", w.board_size / 2, ")");
    if (!(w.min_start_distance > w.catch_distance && w.min_start_distance < w.board_size)) return "min_start_distance must be > catch_distance and < board_size";
    if (!(w.square_side > 0 && w.square_side <= w.board_size)) return Join("square_side must be > 0 and <= board_size (", w.board_size, ")");
    if (!(w.circle_radius > 0 && w.circle_radius <= w.board_size / 2)) return Join("circle_radius must be > 0 and <= board_size / 2 (", w.board_size / 2, ")");
    if (!(w.catch_bonus >= 0 && w.catch_bonus <= 1e6f)) return "catch_bonus must be between 0 and 1000000";
    if (w.stats_window < 1 || w.stats_window > 100000) return "stats_window must be between 1 and 100000";
    if (l.update_every < 1 || l.update_every > 100000) return "update_every must be between 1 and 100000";
    if (l.warmup_experiences < 32) return "warmup_experiences must be at least 32 (the SAC batch size)";
    if (l.queue_capacity < 1 || l.queue_capacity > 10000000) return "queue_capacity must be between 1 and 10000000";
    if (l.lockstep && l.queue_capacity < l.update_every) return "queue_capacity must be >= update_every in lockstep mode";
    return "";
}
}

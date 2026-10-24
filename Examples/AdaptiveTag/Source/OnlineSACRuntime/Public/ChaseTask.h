// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#ifdef UE_MODULE_NAME
#include "HAL/Platform.h"
#endif
#include <functional>
#include <memory>
#ifndef ONLINESACRUNTIME_API
#define ONLINESACRUNTIME_API
#endif

namespace online_sac {
constexpr unsigned ObservationDim = 6;
constexpr unsigned ActionDim = 2;

struct ChaseConstants {
    static constexpr float BoardSize = 10.0f;
    static constexpr float Dt = 0.02f;
    static constexpr unsigned EpisodeSteps = 200;
    static constexpr float AgentMaxSpeed = 5.0f;
    static constexpr float AgentMaxAcceleration = 20.0f;
    static constexpr float TargetMaxSpeed = 3.0f;
    static constexpr float CatchDistance = 0.5f;
};

enum class ChasePattern : unsigned { Bounce = 0, Circle = 1 };

struct ChaseStep {
    float reward = 0;
    bool terminated = false;
    bool truncated = false;
};

class ONLINESACRUNTIME_API ChaseTask {
public:
    explicit ChaseTask(unsigned seed, ChasePattern pattern = ChasePattern::Bounce);
    ~ChaseTask();
    ChaseTask(const ChaseTask&) = delete;
    ChaseTask& operator=(const ChaseTask&) = delete;
    void SetPattern(ChasePattern pattern);
    void Reset();
    void Observe(float* observation) const;
    ChaseStep Step(const float* action);
    unsigned StepIndex() const;
private:
    struct Impl;
    std::unique_ptr<Impl> data;
};

struct EvalResult {
    unsigned episodes = 0, caught = 0;
    double catchRate = 0, meanStepsToCatch = 0, meanReturn = 0;
};

using PolicyFn = std::function<void(const float* observation, float* action)>;
// Runs `episodes` episodes from initial states drawn from `seed`, so every policy sees the same starts.
ONLINESACRUNTIME_API EvalResult EvaluatePolicy(const PolicyFn& policy, ChasePattern pattern, unsigned episodes, unsigned seed);
ONLINESACRUNTIME_API void GreedyPursuitAction(const float* observation, float* action);
}

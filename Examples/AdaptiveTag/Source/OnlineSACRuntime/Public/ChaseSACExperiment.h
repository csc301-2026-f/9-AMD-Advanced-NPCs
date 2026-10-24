// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "ChaseTask.h"
#include <atomic>
#include <string>
#include <vector>

namespace online_sac {
struct CurvePoint {
    unsigned step = 0;
    unsigned trainingPattern = 0;
    EvalResult bounce, circle;
    float trainReturn = 0;
    double seconds = 0;
};
struct ChaseSACOptions {
    unsigned seed = 1;
    unsigned trainSteps = 20000;
    unsigned adaptSteps = 0;
    unsigned evalInterval = 1000;
    unsigned evalEpisodes = 100;
    unsigned evalSeed = 4242;
};
struct ChaseSACResult {
    unsigned seed = 0;
    bool completed = false;
    std::vector<CurvePoint> curve;
    double trainSeconds = 0, adaptSeconds = 0;
    const CurvePoint* AfterTraining() const;
};
ONLINESACRUNTIME_API ChaseSACResult RunChaseSAC(const ChaseSACOptions& options, const std::atomic<bool>* cancel = nullptr);
ONLINESACRUNTIME_API std::string CurveToCsv(const ChaseSACResult& result);

struct PendulumSACResult {
    std::vector<float> returnsEvery1000;
    float finalMeanReturn = 0;
    double seconds = 0;
};
ONLINESACRUNTIME_API PendulumSACResult RunPendulumSAC(unsigned seed, unsigned steps);
}

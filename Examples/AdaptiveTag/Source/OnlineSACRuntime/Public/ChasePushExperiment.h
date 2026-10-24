// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "ChaseSACExperiment.h"
#include "OnlineSACLearner.h"
#include <string>

namespace online_sac {
ONLINESACRUNTIME_API ChaseSACResult RunChasePush(const ChaseSACOptions& options, const OnlineSACConfig& learnerConfig = {});

struct LifecycleCheck {
    std::string name;
    bool passed = false;
    std::string detail;
};
ONLINESACRUNTIME_API LifecycleCheck CheckInvalidExperiencesRejected();
ONLINESACRUNTIME_API LifecycleCheck CheckVersionIncreases();
ONLINESACRUNTIME_API LifecycleCheck CheckShutdownDuringRound();
ONLINESACRUNTIME_API LifecycleCheck CheckConcurrentActDuringPublishing();
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "../RLToolsLearnerParameters.h"

// Project-local generated parameter types are declared here, before profile registration.
namespace Schola::RLTools
{
struct FShortHorizonParameters : FAlgorithmParameters
{
    static constexpr float Discount = 0.95f;
    static constexpr float PPOClip = 0.15f;
};
}

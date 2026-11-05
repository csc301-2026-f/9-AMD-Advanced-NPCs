// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "RLToolsPolicyTypes.h"
#include "Common/InteractionDefinition.h"
#include "RLToolsLearner.h"

namespace Schola::RLTools
{
FSettings ToNative(const FRLToolsRuntimeSettings& Settings);
FRLToolsConfiguration DescribeProfile(const FProfile& Profile);
bool ValidateConfiguration(ERLToolsAlgorithm Algorithm, const FString& Id, const FRLToolsRuntimeSettings& Settings,
    const FInteractionDefinition& Definition, FString& Error);
bool SameSettings(const FRLToolsRuntimeSettings& A, const FRLToolsRuntimeSettings& B);
bool SameDefinition(const FInteractionDefinition& A, const FInteractionDefinition& B);
bool ObservationVector(const TInstancedStruct<FPoint>& Point, const FInteractionDefinition& Definition, std::vector<float>& Out);
bool ActionPoint(const std::vector<float>& Normalized, const FInteractionDefinition& Definition, TInstancedStruct<FPoint>& Out);
std::vector<float> NormalizeAction(const TInstancedStruct<FPoint>& Point, const FInteractionDefinition& Definition);
}

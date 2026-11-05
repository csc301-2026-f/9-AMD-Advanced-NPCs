// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Policies/AdaptivePolicySnapshot.h"

bool UAdaptivePolicySnapshot::InitializeSnapshot(const FGuid& InSessionId, int64 InPolicyVersion, const FInteractionDefinition& InDefinition)
{
    check(IsInGameThread());
    if (bInitialized || !InSessionId.IsValid() || InPolicyVersion < 0
        || !InDefinition.ObsSpaceDefn.IsValid() || !InDefinition.ActionSpaceDefn.IsValid())
    {
        return false;
    }
    SessionId = InSessionId;
    PolicyVersion = InPolicyVersion;
    InteractionDefinition = InDefinition;
    bInitialized = true;
    return true;
}

bool UAdaptivePolicySnapshot::Evaluate(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action)
{
    check(IsInGameThread());
    TInstancedStruct<FPoint> Candidate;
    if (!bInitialized || !Observation.IsValid() || !DispatchEvaluation(Observation, Candidate) || !Candidate.IsValid())
    {
        return false;
    }
    Action = MoveTemp(Candidate);
    return true;
}

bool UAdaptivePolicySnapshot::GetInteractionDefinition(FInteractionDefinition& OutDefinition) const
{
    check(IsInGameThread());
    if (!bInitialized) return false;
    OutDefinition = InteractionDefinition;
    return true;
}

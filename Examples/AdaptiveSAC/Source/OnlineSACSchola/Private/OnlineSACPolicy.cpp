// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "OnlineSACPolicy.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"

DEFINE_LOG_CATEGORY_STATIC(LogOnlineSACPolicy, Log, All);

namespace
{
const FBoxSpace* AsBox(const TInstancedStruct<FSpace>& Definition)
{
    return Definition.IsValid() && Definition.GetScriptStruct() == FBoxSpace::StaticStruct() ? &Definition.Get<FBoxSpace>() : nullptr;
}
const TArray<float>* BoxValues(const TInstancedStruct<FPoint>& Point)
{
    return Point.IsValid() && Point.GetScriptStruct() == FBoxPoint::StaticStruct() ? &Point.Get<FBoxPoint>().Values : nullptr;
}
}

bool UOnlineSACPolicy::Init(const FInteractionDefinition& InDefinition)
{
    check(IsInGameThread());
    const FBoxSpace* Observation = AsBox(InDefinition.ObsSpaceDefn);
    const FBoxSpace* Action = AsBox(InDefinition.ActionSpaceDefn);
    if (Observation && Action && (Observation->Dimensions.Num() != ObservationSize || Action->Dimensions.Num() != ActionSize))
    {
        LearnerError = FString::Printf(TEXT("UOnlineSACPolicy is compiled for a %d-value Box observation and a %d-value Box action; the definition has %d and %d."),
            ObservationSize, ActionSize, Observation->Dimensions.Num(), Action->Dimensions.Num());
        UE_LOG(LogOnlineSACPolicy, Warning, TEXT("%s"), *LearnerError);
        return false;
    }
    if (!Super::Init(InDefinition))
    {
        LearnerError = GetLastError();
        return false;
    }
    for (int32 Index = 0; Index < ActionSize; ++Index)
    {
        ActionLow[Index] = Action->Dimensions[Index].Low;
        ActionHigh[Index] = Action->Dimensions[Index].High;
    }
    StopLearner();
    online_sac::OnlineSACConfig Config;
    Config.seed = static_cast<unsigned>(FMath::Max(Seed, 0));
    Config.updateEvery = static_cast<unsigned>(FMath::Max(UpdateEvery, 1));
    Config.warmupExperiences = static_cast<unsigned>(FMath::Max(WarmupExperiences, 1));
    Config.queueCapacity = static_cast<unsigned>(FMath::Max(QueueCapacity, 1));
    Learner = MakeUnique<online_sac::OnlineSACLearner>(Config);
    LocalSessionId = FGuid::NewGuid();
    if (!StartSession(LocalSessionId, 0, 0))
    {
        LearnerError = GetLastError();
        StopLearner();
        return false;
    }
    LearnerError.Reset();
    return true;
}

bool UOnlineSACPolicy::DispatchDecision(const FAdaptiveDecisionRequest& InRequest)
{
    const TArray<float>* Values = BoxValues(InRequest.Observation);
    float Normalized[ActionSize];
    unsigned Version = 0;
    if (!Learner || !Values || !Learner->Act(Values->GetData(), static_cast<unsigned>(Values->Num()), Normalized, ActionSize, false, &Version))
    {
        return false;
    }
    TArray<float> ActionValues;
    ActionValues.Reserve(ActionSize);
    for (int32 Index = 0; Index < ActionSize; ++Index)
    {
        const float Mapped = ActionLow[Index] + (Normalized[Index] + 1.0f) * 0.5f * (ActionHigh[Index] - ActionLow[Index]);
        ActionValues.Add(FMath::Clamp(Mapped, ActionLow[Index], ActionHigh[Index]));
    }
    FAdaptiveDecision Result;
    Result.SessionId = InRequest.SessionId;
    Result.AgentId = InRequest.AgentId;
    Result.EpisodeId = InRequest.EpisodeId;
    Result.DecisionId = InRequest.DecisionId;
    Result.PolicyVersion = Version;
    Result.Action.InitializeAs<FBoxPoint>(ActionValues);
    CompleteDecision(Result);
    return true;
}

bool UOnlineSACPolicy::ToLearnerAction(const TInstancedStruct<FPoint>& Action, float* OutNormalized) const
{
    const TArray<float>* Values = BoxValues(Action);
    if (!Values || Values->Num() != ActionSize)
    {
        return false;
    }
    for (int32 Index = 0; Index < ActionSize; ++Index)
    {
        const float Value = (*Values)[Index];
        if (!FMath::IsFinite(Value) || Value < ActionLow[Index] || Value > ActionHigh[Index])
        {
            return false;
        }
        OutNormalized[Index] = FMath::Clamp(2.0f * (Value - ActionLow[Index]) / (ActionHigh[Index] - ActionLow[Index]) - 1.0f, -1.0f, 1.0f);
    }
    return true;
}

EOnlineSACSubmitResult UOnlineSACPolicy::SubmitExperience(const FAdaptiveExperience& Experience)
{
    check(IsInGameThread());
    if (!Learner || GetSessionState() != EAdaptiveSessionState::Ready)
    {
        if (GetSessionState() == EAdaptiveSessionState::Stopped)
        {
            StopLearner();
        }
        LearnerError = TEXT("Experiences require a Ready session with a running learner.");
        return EOnlineSACSubmitResult::NotReady;
    }
    const TArray<float>* Observation = BoxValues(Experience.Observation);
    const TArray<float>* NextObservation = BoxValues(Experience.NextObservation);
    float Action[ActionSize];
    if (Experience.SessionId != LocalSessionId || Experience.PolicyVersion < 0 || !Observation || !NextObservation
        || !ToLearnerAction(Experience.ExecutedAction, Action))
    {
        LearnerError = TEXT("Experience rejected: wrong session, negative policy version, non-Box points or an executed action outside the action space.");
        return EOnlineSACSubmitResult::Rejected;
    }
    switch (Learner->AddExperience(Observation->GetData(), static_cast<unsigned>(Observation->Num()), Action, ActionSize, Experience.Reward,
        NextObservation->GetData(), static_cast<unsigned>(NextObservation->Num()), Experience.bTerminated, Experience.bTruncated))
    {
    case online_sac::AddResult::Accepted:
        return EOnlineSACSubmitResult::Accepted;
    case online_sac::AddResult::Dropped:
        LearnerError = TEXT("Experience dropped: the learner queue is full.");
        return EOnlineSACSubmitResult::Dropped;
    case online_sac::AddResult::Frozen:
        return EOnlineSACSubmitResult::Frozen;
    default:
        LearnerError = TEXT("Experience rejected by the learner: wrong sizes or nonfinite values.");
        return EOnlineSACSubmitResult::Rejected;
    }
}

void UOnlineSACPolicy::SetFrozen(bool bFrozen)
{
    if (Learner)
    {
        Learner->SetFrozen(bFrozen);
    }
}

int64 UOnlineSACPolicy::GetPolicyVersion() const
{
    return Learner ? static_cast<int64>(Learner->Version()) : INDEX_NONE;
}

online_sac::OnlineSACStats UOnlineSACPolicy::GetLearnerStats() const
{
    return Learner ? Learner->ReadStats() : online_sac::OnlineSACStats();
}

bool UOnlineSACPolicy::WaitForLearner(uint32 MaxBacklog, uint32 TimeoutMs)
{
    return Learner && Learner->WaitForLearner(MaxBacklog, TimeoutMs);
}

void UOnlineSACPolicy::StopLearner()
{
    if (Learner)
    {
        Learner->Shutdown();
    }
}

void UOnlineSACPolicy::ShutdownLearner()
{
    check(IsInGameThread());
    Shutdown();
    StopLearner();
}

void UOnlineSACPolicy::BeginDestroy()
{
    StopLearner();
    Super::BeginDestroy();
}

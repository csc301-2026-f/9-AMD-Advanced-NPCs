// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "Policies/AdaptiveNPCPolicy.h"
#include "Policies/AdaptivePolicySnapshot.h"
#include "AdaptiveDecisionTestPolicy.generated.h"

/** Test-only controlled responses and clock, without background tasks or sleeps. */
UCLASS()
class UAdaptiveDecisionTestPolicy : public UAdaptiveNPCPolicy
{
    GENERATED_BODY()

public:
    bool Start(const FGuid& Id, int64 Agent = 0, int64 Episode = 0) { return StartSession(Id, Agent, Episode); }
    bool Deliver(const FAdaptiveDecision& Result) { return CompleteDecision(Result); }
    bool Reject(const FAdaptiveDecisionRequest& Ticket, const FString& Error) { return FailDecision(Ticket, Error); }

    FAdaptiveDecisionRequest Sent;
    FAdaptiveDecision ImmediateResult;
    double Now = 100.0;
    int32 DispatchCount = 0;
    bool bAcceptDispatch = true;
    bool bCompleteImmediately = false;
    bool bHandleExperience = false;
    EAdaptiveSubmitResult ExperienceResult = EAdaptiveSubmitResult::Accepted;
    int32 ExperienceDispatchCount = 0;
    FAdaptiveExperience SubmittedExperience;
    int32 ReleaseCount = 0;
    int32 EpisodeResetCount = 0;
    bool bStoppedBeforeRelease = false;
    FAdaptiveSessionInfo ReleasedSession;
    FAdaptiveSessionInfo ResetSession;

protected:
    void ReleaseLearnerBinding(const FAdaptiveSessionInfo& PreviousSession) override
    {
        ++ReleaseCount;
        ReleasedSession = PreviousSession;
        bStoppedBeforeRelease = GetSessionState() == EAdaptiveSessionState::Stopped && !GetSessionInfo().SessionId.IsValid();
    }

    void OnEpisodeReset(const FAdaptiveSessionInfo& PreviousSession) override
    {
        ++EpisodeResetCount;
        ResetSession = PreviousSession;
    }

    EAdaptiveSubmitResult DispatchExperience(const FAdaptiveExperience& Experience) override
    {
        ++ExperienceDispatchCount;
        if (!bHandleExperience) return Super::DispatchExperience(Experience);
        SubmittedExperience = Experience;
        return ExperienceResult;
    }

    double GetTimeSeconds() const override { return Now; }
    bool DispatchDecision(const FAdaptiveDecisionRequest& Input) override
    {
        ++DispatchCount;
        Sent = Input;
        if (bCompleteImmediately && bAcceptDispatch)
        {
            auto Result = ImmediateResult;
            Result.SessionId = Input.SessionId;
            Result.AgentId = Input.AgentId;
            Result.EpisodeId = Input.EpisodeId;
            Result.DecisionId = Input.DecisionId;
            CompleteDecision(Result);
        }
        return bAcceptDispatch;
    }
};

/** Verifies that common tickets/identity can serve a backend with categorical spaces. */
UCLASS()
class UAdaptiveDiscreteTestPolicy : public UAdaptiveDecisionTestPolicy
{
    GENERATED_BODY()
protected:
    bool ValidateSpaceDefinition(const TInstancedStruct<FSpace>& Definition, bool bActionSpace, FString& Error) const override;
    bool ValidatePoint(const TInstancedStruct<FPoint>& Point, const TInstancedStruct<FSpace>& Definition, FString& Error) const override;
};

/** An immutable scalar actor used to verify the common snapshot contract without a learner. */
UCLASS()
class UAdaptiveTestSnapshot : public UAdaptivePolicySnapshot
{
    GENERATED_BODY()

public:
    bool Configure(const FGuid& Session, int64 Version, const FInteractionDefinition& Definition, float Action)
    {
        if (!InitializeSnapshot(Session, Version, Definition)) return false;
        FrozenAction = Action;
        return true;
    }

protected:
    bool DispatchEvaluation(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) const override;

private:
    float FrozenAction = 0.0f;
};

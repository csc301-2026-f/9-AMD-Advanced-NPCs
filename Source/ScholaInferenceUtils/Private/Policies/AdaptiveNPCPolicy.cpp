// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Policies/AdaptiveNPCPolicy.h"
#include "Spaces/BoxSpace.h"
#include "HAL/PlatformTime.h"

namespace
{
bool ValidateBoxPoint(const TInstancedStruct<FPoint>& Point, const TInstancedStruct<FSpace>& Definition, FString& Error)
{
    if (!Point.IsValid() || Point.GetScriptStruct() != FBoxPoint::StaticStruct())
    {
        Error = TEXT("Expected an FBoxPoint.");
        return false;
    }
    const FBoxPoint& Box = Point.Get<FBoxPoint>();
    for (float Value : Box.Values)
    {
        if (!FMath::IsFinite(Value))
        {
            Error = TEXT("Point contains a nonfinite value.");
            return false;
        }
    }
    const FBoxSpace& Space = Definition.Get<FBoxSpace>();
    const TArray<int> ExpectedShape = Space.Shape.IsEmpty() ? TArray<int>{Space.Dimensions.Num()} : Space.Shape;
    // FBoxSpace::Validate permits empty point shapes and does not check shapes for vector definitions.
    if ((!Box.Shape.IsEmpty() && Box.Shape != ExpectedShape)
        || Space.Validate(Point) != ESpaceValidationResult::Success)
    {
        Error = TEXT("Point type, dimensions, shape or bounds do not match the declared Box space.");
        return false;
    }
    return true;
}

// FSpace::Validate checks a point against a space; it does not validate the space definition itself.
bool ValidateBoxDefinition(const TInstancedStruct<FSpace>& Definition, bool bActionSpace, FString& Error)
{
    const TCHAR* Label = bActionSpace ? TEXT("Action") : TEXT("Observation");
    if (!Definition.IsValid() || Definition.GetScriptStruct() != FBoxSpace::StaticStruct())
    {
        Error = FString::Printf(TEXT("%s space must be a valid FBoxSpace; other space types are not supported yet."), Label);
        return false;
    }

    const FBoxSpace& Box = Definition.Get<FBoxSpace>();
    if (Box.IsEmpty())
    {
        Error = FString::Printf(TEXT("%s space must contain at least one dimension."), Label);
        return false;
    }

    // Empty Shape means a vector. A specified shape must match the number of stored bounds.
    if (!Box.Shape.IsEmpty())
    {
        int64 Size = 1;
        for (int Dimension : Box.Shape)
        {
            // Bound multiplication before doing it, including for hostile/overflowing Blueprint shapes.
            if (Dimension <= 0 || Size > Box.Dimensions.Num() / Dimension)
            {
                Error = FString::Printf(TEXT("%s space shape must have positive dimensions and match its number of bounds."), Label);
                return false;
            }
            Size *= Dimension;
        }
        if (Size != Box.Dimensions.Num())
        {
            Error = FString::Printf(TEXT("%s space shape must match its number of bounds."), Label);
            return false;
        }
    }

    for (int32 Index = 0; Index < Box.Dimensions.Num(); ++Index)
    {
        const FBoxSpaceDimension& Dimension = Box.Dimensions[Index];
        if ((Dimension.bHasLow && !FMath::IsFinite(Dimension.Low))
            || (Dimension.bHasHigh && !FMath::IsFinite(Dimension.High)))
        {
            Error = FString::Printf(TEXT("%s space dimension %d has a nonfinite enabled bound."), Label, Index);
            return false;
        }
        if (Dimension.IsFullyBounded() && Dimension.Low > Dimension.High)
        {
            Error = FString::Printf(TEXT("%s space dimension %d has its lower bound above its upper bound."), Label, Index);
            return false;
        }
        if (bActionSpace && (!Dimension.IsFullyBounded() || Dimension.Low == Dimension.High
            || !FMath::IsFinite(Dimension.High - Dimension.Low)))
        {
            Error = FString::Printf(TEXT("Action space dimension %d requires finite lower and upper bounds with a positive finite range."), Index);
            return false;
        }
    }
    return true;
}
}

bool UAdaptiveNPCPolicy::ValidateSpaceDefinition(const TInstancedStruct<FSpace>& Definition, bool bActionSpace, FString& Error) const
{
    return ValidateBoxDefinition(Definition, bActionSpace, Error);
}

bool UAdaptiveNPCPolicy::ValidatePoint(const TInstancedStruct<FPoint>& Point, const TInstancedStruct<FSpace>& Definition, FString& Error) const
{
    return ValidateBoxPoint(Point, Definition, Error);
}

bool UAdaptiveNPCPolicy::Init(const FInteractionDefinition& InDefinition)
{
    check(IsInGameThread());
    FString Error;
    if (!ValidateSpaceDefinition(InDefinition.ObsSpaceDefn, false, Error)
        || !ValidateSpaceDefinition(InDefinition.ActionSpaceDefn, true, Error))
    {
        LastError = MoveTemp(Error);
        if (!bInitialized)
        {
            SessionState = EAdaptiveSessionState::Failed;
        }
        return false;
    }

    EndSession(TEXT("Policy reinitialized."));
    InteractionDefinition = InDefinition;
    bInitialized = true;
    SessionState = EAdaptiveSessionState::Initialized;
    LastError.Reset();
    return true;
}

bool UAdaptiveNPCPolicy::GetInteractionDefinition(FInteractionDefinition& OutDefinition) const
{
    if (!bInitialized)
    {
        return false;
    }
    OutDefinition = InteractionDefinition;
    return true;
}

FAdaptiveSessionInfo UAdaptiveNPCPolicy::GetSessionInfo() const
{
    check(IsInGameThread());
    FAdaptiveSessionInfo Result;
    Result.State = SessionState;
    Result.SessionId = SessionId;
    Result.AgentId = AgentId;
    Result.EpisodeId = EpisodeId;
    return Result;
}

namespace
{
FAdaptiveOperationResult UnsupportedOperation(const TCHAR* Operation)
{
    FAdaptiveOperationResult Result;
    Result.Diagnostic = FString::Printf(TEXT("%s is not supported by this adaptive policy."), Operation);
    return Result;
}
}

FAdaptiveOperationResult UAdaptiveNPCPolicy::SetLearningEnabled(bool bEnabled)
{
    check(IsInGameThread());
    return UnsupportedOperation(TEXT("Learning control"));
}

FAdaptiveOperationResult UAdaptiveNPCPolicy::SetInferenceMode(EAdaptiveInferenceMode Mode)
{
    check(IsInGameThread());
    return UnsupportedOperation(TEXT("Inference mode selection"));
}

FAdaptiveOperationResult UAdaptiveNPCPolicy::ResetLearning(int32 Seed)
{
    check(IsInGameThread());
    return UnsupportedOperation(TEXT("Learning reset"));
}

FAdaptiveOperationResult UAdaptiveNPCPolicy::CaptureSnapshot(UAdaptivePolicySnapshot*& OutSnapshot)
{
    check(IsInGameThread());
    return UnsupportedOperation(TEXT("Policy snapshots"));
}

bool UAdaptiveNPCPolicy::Think(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action)
{
    check(IsInGameThread());
    LastError = TEXT("Immediate adaptive inference is unavailable. Use RequestDecision and TryGetDecision after the session is Ready.");
    return false;
}

bool UAdaptiveNPCPolicy::BatchedThink(const TArray<TInstancedStruct<FPoint>>& Observations,
                                    TArray<TInstancedStruct<FPoint>>& Actions)
{
    check(IsInGameThread());
    LastError = TEXT("Immediate adaptive inference is unavailable. Use RequestDecision and TryGetDecision after the session is Ready.");
    return false;
}

double UAdaptiveNPCPolicy::GetTimeSeconds() const
{
    return FPlatformTime::Seconds();
}

bool UAdaptiveNPCPolicy::StartSession(const FGuid& InSessionId, int64 InAgentId, int64 InEpisodeId)
{
    check(IsInGameThread());
    if (!bInitialized || SessionState != EAdaptiveSessionState::Initialized
        || !InSessionId.IsValid() || InAgentId < 0 || InEpisodeId < 0)
    {
        LastError = TEXT("Starting a session requires local initialization, a valid session GUID and nonnegative agent/episode IDs.");
        return false;
    }
    SessionId = InSessionId;
    AgentId = InAgentId;
    EpisodeId = InEpisodeId;
    SessionState = EAdaptiveSessionState::Ready;
    LastError.Reset();
    return true;
}

void UAdaptiveNPCPolicy::ExpireDecision()
{
    if ((RequestStatus == EAdaptiveRequestStatus::Pending || RequestStatus == EAdaptiveRequestStatus::Ready)
        && GetTimeSeconds() >= DeadlineSeconds)
    {
        RequestStatus = EAdaptiveRequestStatus::Expired;
        RequestError = TEXT("Decision expired before use. Request a fresh observation/action.");
        Decision = FAdaptiveDecision();
        LastError = RequestError;
    }
}

bool UAdaptiveNPCPolicy::IsInferenceBusy() const
{
    check(IsInGameThread());
    return RequestStatus == EAdaptiveRequestStatus::Pending && GetTimeSeconds() < DeadlineSeconds;
}

bool UAdaptiveNPCPolicy::RequestDecision(const TInstancedStruct<FPoint>& Observation, int64& OutDecisionId, float TimeoutSeconds)
{
    check(IsInGameThread());
    ExpireDecision();
    if (SessionState != EAdaptiveSessionState::Ready)
    {
        LastError = TEXT("Decision requests require a Ready session started by a native learner policy; base initialization only validates spaces.");
        return false;
    }
    if (RequestStatus == EAdaptiveRequestStatus::Pending)
    {
        LastError = TEXT("A decision is already pending. Poll or cancel it before requesting another.");
        return false;
    }
    if (!FMath::IsFinite(TimeoutSeconds) || TimeoutSeconds <= 0.0f)
    {
        LastError = TEXT("Decision timeout must be finite and greater than zero.");
        return false;
    }
    FString Error;
    if (!ValidatePoint(Observation, InteractionDefinition.ObsSpaceDefn, Error))
    {
        LastError = TEXT("Observation rejected: ") + Error;
        return false;
    }
    if (NextDecisionId == MAX_int64)
    {
        LastError = TEXT("Decision ID capacity exhausted. Create a new policy object.");
        return false;
    }

    // One bounded slot: a new accepted request retires the previous terminal/ready ticket.
    Request = FAdaptiveDecisionRequest();
    Request.SessionId = SessionId;
    Request.AgentId = AgentId;
    Request.EpisodeId = EpisodeId;
    Request.DecisionId = NextDecisionId++;
    Request.Observation = Observation;
    Decision = FAdaptiveDecision();
    RequestStatus = EAdaptiveRequestStatus::Pending;
    RequestError.Reset();
    DeadlineSeconds = GetTimeSeconds() + static_cast<double>(TimeoutSeconds);
    LastError.Reset();

    // Copy protects against synchronous completion/reset changing the retained request during dispatch.
    const FAdaptiveDecisionRequest DispatchedRequest = Request;
    if (!DispatchDecision(DispatchedRequest))
    {
        FailDecision(DispatchedRequest, TEXT("Decision dispatch unavailable or rejected. The base policy has no learner implementation."));
        return false;
    }
    OutDecisionId = DispatchedRequest.DecisionId;
    return true;
}

EAdaptiveRequestStatus UAdaptiveNPCPolicy::TryGetDecision(int64 DecisionId, FAdaptiveDecision& OutDecision)
{
    check(IsInGameThread());
    ExpireDecision();
    if (DecisionId < 0 || Request.DecisionId != DecisionId)
    {
        return EAdaptiveRequestStatus::Unknown;
    }
    if (RequestStatus == EAdaptiveRequestStatus::Ready)
    {
        OutDecision = Decision;
    }
    return RequestStatus;
}

FString UAdaptiveNPCPolicy::GetDecisionError(int64 DecisionId) const
{
    check(IsInGameThread());
    return DecisionId >= 0 && Request.DecisionId == DecisionId ? RequestError : FString();
}

EAdaptiveSubmitResult UAdaptiveNPCPolicy::SubmitExperience(const FAdaptiveExperience& Experience)
{
    check(IsInGameThread());
    if (SessionState != EAdaptiveSessionState::Ready)
    {
        LastError = TEXT("Experiences require a Ready learner session.");
        return EAdaptiveSubmitResult::NotReady;
    }
    if (Experience.SessionId != SessionId || Experience.AgentId != AgentId || Experience.EpisodeId != EpisodeId)
    {
        LastError = TEXT("Experience session, agent and episode must match the active policy binding.");
        return EAdaptiveSubmitResult::Rejected;
    }
    if (Experience.DecisionId < 0 || Experience.PolicyVersion < 0 || !FMath::IsFinite(Experience.Reward))
    {
        LastError = TEXT("Experience requires nonnegative decision/version IDs and a finite reward.");
        return EAdaptiveSubmitResult::Rejected;
    }
    FString Error;
    if (!ValidatePoint(Experience.Observation, InteractionDefinition.ObsSpaceDefn, Error))
    {
        LastError = TEXT("Experience observation rejected: ") + Error;
        return EAdaptiveSubmitResult::Rejected;
    }
    if (!ValidatePoint(Experience.NextObservation, InteractionDefinition.ObsSpaceDefn, Error))
    {
        LastError = TEXT("Experience next observation rejected: ") + Error;
        return EAdaptiveSubmitResult::Rejected;
    }
    if (!ValidatePoint(Experience.ExecutedAction, InteractionDefinition.ActionSpaceDefn, Error))
    {
        LastError = TEXT("Experience executed action rejected: ") + Error;
        return EAdaptiveSubmitResult::Rejected;
    }

    const EAdaptiveSubmitResult Result = DispatchExperience(Experience);
    switch (Result)
    {
    case EAdaptiveSubmitResult::Accepted:
    case EAdaptiveSubmitResult::Frozen:
        LastError.Reset();
        return Result;
    case EAdaptiveSubmitResult::Dropped:
        LastError = TEXT("Experience dropped: the learner queue is full.");
        return Result;
    case EAdaptiveSubmitResult::NotReady:
        LastError = TEXT("The native learner is not ready to accept experiences.");
        return Result;
    default:
        LastError = TEXT("Experience rejected by the native learner.");
        return EAdaptiveSubmitResult::Rejected;
    }
}

bool UAdaptiveNPCPolicy::MatchesPendingRequest(const FGuid& InSessionId, int64 InAgentId, int64 InEpisodeId, int64 InDecisionId) const
{
    return SessionState == EAdaptiveSessionState::Ready && RequestStatus == EAdaptiveRequestStatus::Pending
        && SessionId == InSessionId && AgentId == InAgentId && EpisodeId == InEpisodeId
        && Request.SessionId == InSessionId && Request.AgentId == InAgentId
        && Request.EpisodeId == InEpisodeId && Request.DecisionId == InDecisionId;
}

bool UAdaptiveNPCPolicy::CompleteDecision(const FAdaptiveDecision& InDecision)
{
    check(IsInGameThread());
    ExpireDecision();
    if (!MatchesPendingRequest(InDecision.SessionId, InDecision.AgentId, InDecision.EpisodeId, InDecision.DecisionId))
    {
        // Unrelated/late/duplicate callbacks cannot fail or replace the active request.
        return false;
    }
    FString Error;
    if (InDecision.PolicyVersion < 0 || !ValidatePoint(InDecision.Action, InteractionDefinition.ActionSpaceDefn, Error))
    {
        FailDecision(Request, InDecision.PolicyVersion < 0
            ? TEXT("Result policy version must be nonnegative.") : TEXT("Action rejected: ") + Error);
        return false;
    }
    Decision = InDecision;
    RequestStatus = EAdaptiveRequestStatus::Ready;
    RequestError.Reset();
    LastError.Reset();
    return true;
}

bool UAdaptiveNPCPolicy::FailDecision(const FAdaptiveDecisionRequest& InRequest, const FString& Error)
{
    check(IsInGameThread());
    ExpireDecision();
    if (!MatchesPendingRequest(InRequest.SessionId, InRequest.AgentId, InRequest.EpisodeId, InRequest.DecisionId))
    {
        return false;
    }
    RequestStatus = EAdaptiveRequestStatus::Failed;
    RequestError = Error.IsEmpty() ? TEXT("Decision request failed.") : Error;
    Decision = FAdaptiveDecision();
    LastError = RequestError;
    return true;
}

void UAdaptiveNPCPolicy::InvalidateDecision(const FString& Reason)
{
    if (RequestStatus == EAdaptiveRequestStatus::Pending || RequestStatus == EAdaptiveRequestStatus::Ready)
    {
        RequestStatus = EAdaptiveRequestStatus::Cancelled;
        RequestError = Reason;
    }
    Decision = FAdaptiveDecision();
}

bool UAdaptiveNPCPolicy::CancelDecision(int64 DecisionId)
{
    check(IsInGameThread());
    ExpireDecision();
    if (DecisionId < 0 || Request.DecisionId != DecisionId
        || (RequestStatus != EAdaptiveRequestStatus::Pending && RequestStatus != EAdaptiveRequestStatus::Ready))
    {
        LastError = TEXT("Only a retained pending or ready decision can be cancelled.");
        return false;
    }
    InvalidateDecision(TEXT("Decision cancelled by caller."));
    LastError.Reset();
    return true;
}

bool UAdaptiveNPCPolicy::ResetEpisode()
{
    check(IsInGameThread());
    if (SessionState != EAdaptiveSessionState::Ready || EpisodeId == MAX_int64)
    {
        LastError = TEXT("Encounter reset requires a Ready session and an available episode ID.");
        return false;
    }
    const FAdaptiveSessionInfo PreviousSession = GetSessionInfo();
    InvalidateDecision(TEXT("Encounter reset cancelled the previous action."));
    ++EpisodeId;
    OnEpisodeReset(PreviousSession);
    LastError.Reset();
    return true;
}

void UAdaptiveNPCPolicy::EndSession(const FString& Reason)
{
    const FAdaptiveSessionInfo PreviousSession = GetSessionInfo();
    InvalidateDecision(Reason);
    SessionId.Invalidate();
    AgentId = EpisodeId = INDEX_NONE;
    SessionState = EAdaptiveSessionState::Stopped;
    if (PreviousSession.State == EAdaptiveSessionState::Ready)
    {
        ReleaseLearnerBinding(PreviousSession);
    }
}

void UAdaptiveNPCPolicy::Shutdown()
{
    check(IsInGameThread());
    EndSession(TEXT("Policy shut down."));
    LastError.Reset();
}

void UAdaptiveNPCPolicy::BeginDestroy()
{
    EndSession(TEXT("Policy destroyed."));
    Super::BeginDestroy();
}

void UAdaptiveNPCPolicy::ResetRuntimeState()
{
    // Loaded/duplicated policy configuration never inherits an active learner session or its work.
    SessionId.Invalidate();
    AgentId = EpisodeId = INDEX_NONE;
    Request = FAdaptiveDecisionRequest();
    Decision = FAdaptiveDecision();
    RequestStatus = EAdaptiveRequestStatus::Unknown;
    RequestError.Reset();
    DeadlineSeconds = 0.0;
    SessionState = bInitialized ? EAdaptiveSessionState::Initialized : EAdaptiveSessionState::Uninitialized;
    LastError.Reset();
}

void UAdaptiveNPCPolicy::PostLoad()
{
    Super::PostLoad();
    ResetRuntimeState();
}

void UAdaptiveNPCPolicy::PostDuplicate(bool bDuplicateForPIE)
{
    Super::PostDuplicate(bDuplicateForPIE);
    ResetRuntimeState();
}

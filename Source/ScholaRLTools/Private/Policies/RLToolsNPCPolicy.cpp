// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Policies/RLToolsNPCPolicy.h"
#include "RLToolsWorldSubsystem.h"
#include "Learning/RLToolsPolicyConversion.h"
#include "Engine/World.h"
#include "RLToolsConfigurationAsset.h"

UWorld* URLToolsNPCPolicy::GetWorld() const
{
    return !HasAnyFlags(RF_ClassDefaultObject) && GetOuter() ? GetOuter()->GetWorld() : nullptr;
}
TArray<FRLToolsConfiguration> URLToolsNPCPolicy::GetAvailableConfigurations(ERLToolsAlgorithm Algorithm)
{
    TArray<FRLToolsConfiguration> Result;
    for (const auto& Profile : Schola::RLTools::FLearner::GetProfiles())
        if (static_cast<ERLToolsAlgorithm>(Profile.Algorithm) == Algorithm)
            Result.Add(Schola::RLTools::DescribeProfile(Profile));
    return Result;
}
bool URLToolsNPCPolicy::Init(const FInteractionDefinition& Definition)
{
    check(IsInGameThread());
    FString Error;
    if (!ValidateSpaceDefinition(Definition.ObsSpaceDefn, false, Error)
        || !ValidateSpaceDefinition(Definition.ActionSpaceDefn, true, Error))
        return RejectInitialization(Error);
    ERLToolsAlgorithm RequestedAlgorithm;
    FString RequestedId;
    if (!ResolveConfiguration(RequestedAlgorithm, RequestedId, Error)
        || !Schola::RLTools::ValidateConfiguration(RequestedAlgorithm, RequestedId, Settings, Definition, Error))
        return RejectInitialization(Error);
    if (DecisionCapacity < 1 || DecisionCapacity > 65536 || !FMath::IsFinite(ExperienceRetentionSeconds)
        || ExperienceRetentionSeconds <= 0
        || (InitialInferenceMode != EAdaptiveInferenceMode::Stochastic && InitialInferenceMode != EAdaptiveInferenceMode::Deterministic))
        return RejectInitialization(TEXT("Decision capacity must be 1..65536, retention finite and positive, and inference mode valid."));
    auto* World = GetWorld();
    auto* WorldService = World ? World->GetSubsystem<URLToolsWorldSubsystem>() : nullptr;
    if (!WorldService) return RejectInitialization(TEXT("Create the RLtools policy with an outer in a live game/PIE world."));
    if (!WorldService->ValidateBinding(*this, Definition, BindingId, Error)) return RejectInitialization(Error);
    if (!Super::Init(Definition)) return false;

    Service = WorldService;
    ActiveInferenceMode = InitialInferenceMode;
    InitializationOperation = RecordOperation(EAdaptiveOperationStatus::Pending, TEXT("Spaces/settings validated; native learner construction is pending on a worker.")).OperationId;
    if (!WorldService->Bind(*this, Definition, BindingId, Error))
    {
        CompleteOperation(InitializationOperation, EAdaptiveOperationStatus::Failed, Error);
        FailInitialization(Error);
        Service.Reset();
        return false;
    }
    return true;
}
bool URLToolsNPCPolicy::ResolveConfiguration(ERLToolsAlgorithm& OutAlgorithm, FString& OutId, FString& Error) const
{
    if (ConfigurationAsset)
    {
        FRLToolsConfiguration Compiled;
        if (!ConfigurationAsset->GetCompiledConfiguration(Compiled, Error)) return false;
        OutAlgorithm = Compiled.Algorithm;
        OutId = Compiled.Id;
    }
    else
    {
        OutAlgorithm = Algorithm;
        OutId = Configuration;
    }
    return true;
}
FAdaptiveCapabilities URLToolsNPCPolicy::GetCapabilities() const
{
    FAdaptiveCapabilities Result;
    Result.bLearningControl = Result.bInferenceMode = Result.bLearningReset = Result.bSnapshots = true;
    return Result;
}
void URLToolsNPCPolicy::BindingReady(FGuid Binding, FGuid Session, int64 Agent)
{
    if (Binding != BindingId || GetSessionState() != EAdaptiveSessionState::Initialized) return;
    if (StartSession(Session, Agent, 0))
        CompleteOperation(InitializationOperation, EAdaptiveOperationStatus::Succeeded);
}
void URLToolsNPCPolicy::BindingFailed(FGuid Binding, const FString& Error)
{
    if (Binding != BindingId) return;
    CompleteOperation(InitializationOperation, EAdaptiveOperationStatus::Failed, Error);
    FailInitialization(Error);
    OnSessionEnded(GetSessionInfo());
}
void URLToolsNPCPolicy::BindingResetting(FGuid Binding)
{
    if (Binding == BindingId) SuspendSession();
}
void URLToolsNPCPolicy::BindingOperationCompleted(FGuid Binding, FGuid Operation, const FString& Error)
{
    if (Binding == BindingId)
        CompleteOperation(Operation, Error.IsEmpty() ? EAdaptiveOperationStatus::Succeeded : EAdaptiveOperationStatus::Failed, Error);
}
void URLToolsNPCPolicy::OnSessionEnded(const FAdaptiveSessionInfo& Previous)
{
    const FGuid Old = BindingId;
    BindingId.Invalidate();
    if (auto* WorldService = Service.Get()) WorldService->Release(Old);
    Service.Reset();
}
void URLToolsNPCPolicy::OnEpisodeReset(const FAdaptiveSessionInfo& Previous)
{
    if (auto* WorldService = Service.Get()) WorldService->EpisodeReset(BindingId, GetSessionInfo().EpisodeId);
}
void URLToolsNPCPolicy::OnDecisionRetrieved(int64 Id)
{
    if (auto* WorldService = Service.Get()) WorldService->Retrieved(BindingId, Id);
}
void URLToolsNPCPolicy::OnDecisionRetired(int64 Id, bool bCancelled)
{
    if (auto* WorldService = Service.Get()) WorldService->Retire(BindingId, Id, bCancelled);
}
bool URLToolsNPCPolicy::CancelDecision(int64 Id)
{
    check(IsInGameThread());
    auto* WorldService = Service.Get();
    const bool Retired = WorldService && WorldService->Retire(BindingId, Id, true);
    const bool Cancelled = Super::CancelDecision(Id);
    if (Retired) SetBackendError(FString());
    return Retired || Cancelled;
}
bool URLToolsNPCPolicy::DispatchDecision(const FAdaptiveDecisionRequest& InRequest)
{
    FString Error;
    FAdaptiveDecision Result;
    auto* WorldService = Service.Get();
    if (!WorldService || !WorldService->Decide(BindingId, InRequest, ActiveInferenceMode, Result, Error))
    {
        FailDecision(InRequest, Error.IsEmpty() ? TEXT("Native learner binding is unavailable.") : Error);
        return false;
    }
    if (CompleteDecision(Result)) return true;
    WorldService->Retire(BindingId, InRequest.DecisionId, true);
    return false;
}
EAdaptiveSubmitResult URLToolsNPCPolicy::DispatchExperience(const FAdaptiveExperience& Experience)
{
    FString Error;
    auto* WorldService = Service.Get();
    const auto Result = WorldService ? WorldService->Submit(BindingId, Experience, Error) : EAdaptiveSubmitResult::NotReady;
    if (!Error.IsEmpty()) SetBackendError(Error);
    return Result;
}
FAdaptiveOperationResult URLToolsNPCPolicy::SetLearningEnabled(bool bEnabled)
{
    check(IsInGameThread());
    auto Operation = RecordOperation(EAdaptiveOperationStatus::Pending);
    FString Error;
    auto* WorldService = Service.Get();
    if (!WorldService || !WorldService->LearningControl(BindingId, bEnabled, Operation.OperationId, Error))
        CompleteOperation(Operation.OperationId, EAdaptiveOperationStatus::Failed, Error.IsEmpty() ? TEXT("No live learner binding.") : Error);
    return GetOperationResult(Operation.OperationId);
}
FAdaptiveOperationResult URLToolsNPCPolicy::SetInferenceMode(EAdaptiveInferenceMode Mode)
{
    check(IsInGameThread());
    if (GetSessionState() != EAdaptiveSessionState::Ready
        || (Mode != EAdaptiveInferenceMode::Stochastic && Mode != EAdaptiveInferenceMode::Deterministic))
        return RecordOperation(EAdaptiveOperationStatus::Failed, TEXT("Inference mode requires a Ready binding and a valid mode."));
    ActiveInferenceMode = Mode;
    return RecordOperation(EAdaptiveOperationStatus::Succeeded);
}
FAdaptiveOperationResult URLToolsNPCPolicy::ResetLearning(int32 Seed)
{
    check(IsInGameThread());
    auto Operation = RecordOperation(EAdaptiveOperationStatus::Pending);
    FString Error;
    auto* WorldService = Service.Get();
    if (!WorldService || !WorldService->LearningReset(BindingId, Seed, Operation.OperationId, Error))
        CompleteOperation(Operation.OperationId, EAdaptiveOperationStatus::Failed, Error.IsEmpty() ? TEXT("No live learner binding.") : Error);
    return GetOperationResult(Operation.OperationId);
}
FAdaptiveOperationResult URLToolsNPCPolicy::CaptureSnapshot(UAdaptivePolicySnapshot*& Snapshot)
{
    check(IsInGameThread());
    FString Error;
    auto* WorldService = Service.Get();
    if (!WorldService || !WorldService->Snapshot(BindingId, Snapshot, Error))
        return RecordOperation(EAdaptiveOperationStatus::Failed, Error.IsEmpty() ? TEXT("No live learner binding.") : Error);
    return RecordOperation(EAdaptiveOperationStatus::Succeeded);
}
bool URLToolsNPCPolicy::GetLearningStats(FRLToolsLearningStats& Out) const
{
    check(IsInGameThread());
    auto* WorldService = Service.Get();
    return WorldService && WorldService->Statistics(BindingId, Out);
}
bool URLToolsNPCPolicy::GetActiveSettings(FRLToolsRuntimeSettings& Out) const
{
    check(IsInGameThread());
    auto* WorldService = Service.Get();
    return WorldService && WorldService->ActiveSettings(BindingId, Out);
}

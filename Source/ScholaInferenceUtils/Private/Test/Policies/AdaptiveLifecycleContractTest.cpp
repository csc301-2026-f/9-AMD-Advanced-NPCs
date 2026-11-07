// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "AdaptiveDecisionTestPolicy.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"
#include "UObject/UnrealType.h"

namespace
{
FInteractionDefinition ContractDefinition()
{
    FInteractionDefinition Result;
    Result.ObsSpaceDefn.InitializeAs<FBoxSpace>(TArray<float>{-1, -1}, TArray<float>{1, 1});
    Result.ActionSpaceDefn.InitializeAs<FBoxSpace>(TArray<float>{-1}, TArray<float>{1});
    return Result;
}

TInstancedStruct<FPoint> ContractPoint(TArray<float> Values)
{
    TInstancedStruct<FPoint> Result;
    Result.InitializeAs<FBoxPoint>(Values);
    return Result;
}
}

bool UAdaptiveTestSnapshot::DispatchEvaluation(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) const
{
    FInteractionDefinition Definition;
    if (!GetInteractionDefinition(Definition) || Definition.ObsSpaceDefn.Get<FBoxSpace>().Validate(Observation) != ESpaceValidationResult::Success)
    {
        // Deliberately write a candidate on failure: the public wrapper must preserve caller output.
        Action = ContractPoint({-1});
        return false;
    }
    Action = ContractPoint({FrozenAction});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveBindingReleaseTest, "Schola.Inference.Adaptive.BindingRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveBindingReleaseTest::RunTest(const FString& Parameters)
{
    auto* Policy = NewObject<UAdaptiveDecisionTestPolicy>();
    UAdaptiveNPCPolicy* Base = Policy;
    Base->Shutdown();
    TestEqual(TEXT("No binding to release before readiness"), Policy->ReleaseCount, 0);
    Base->Init(ContractDefinition());
    const FGuid Session = FGuid::NewGuid();
    Policy->Start(Session, 4, 6);
    int64 Ticket = INDEX_NONE;
    Base->RequestDecision(ContractPoint({0, 0}), Ticket);
    TestFalse(TEXT("Invalid Init is rejected"), Base->Init(FInteractionDefinition()));
    TestEqual(TEXT("Invalid Init retains the live binding"), Policy->ReleaseCount, 0);
    FAdaptiveDecision Decision;
    TestEqual(TEXT("Invalid Init retains pending work"), Base->TryGetDecision(Ticket, Decision), EAdaptiveRequestStatus::Pending);
    TestTrue(TEXT("Valid Init replaces the binding"), Base->Init(ContractDefinition()));
    TestEqual(TEXT("Successful replacement releases once"), Policy->ReleaseCount, 1);
    TestTrue(TEXT("Release receives the previous identity"), Policy->ReleasedSession.SessionId == Session
        && Policy->ReleasedSession.AgentId == 4 && Policy->ReleasedSession.EpisodeId == 6);
    TestTrue(TEXT("Admission closes before backend release"), Policy->bStoppedBeforeRelease);
    Policy->Start(FGuid::NewGuid());
    Base->Shutdown();
    Base->Shutdown();
    TestEqual(TEXT("Base-pointer Shutdown releases once per binding"), Policy->ReleaseCount, 2);
    Policy->ConditionalBeginDestroy();
    TestEqual(TEXT("Destruction after Shutdown does not release twice"), Policy->ReleaseCount, 2);

    auto* Destroyed = NewObject<UAdaptiveDecisionTestPolicy>();
    Destroyed->Init(ContractDefinition());
    Destroyed->Start(FGuid::NewGuid());
    Destroyed->ConditionalBeginDestroy();
    TestEqual(TEXT("Destruction also releases an active binding"), Destroyed->ReleaseCount, 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveEpisodeContractTest, "Schola.Inference.Adaptive.EpisodeContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveEpisodeContractTest::RunTest(const FString& Parameters)
{
    const FGuid Session = FGuid::NewGuid();
    auto* First = NewObject<UAdaptiveDecisionTestPolicy>();
    auto* Other = NewObject<UAdaptiveDecisionTestPolicy>();
    First->Init(ContractDefinition()); Other->Init(ContractDefinition());
    First->Start(Session, 3, 5); Other->Start(Session, 4, 8);
    const FAdaptiveSessionInfo Before = First->GetSessionInfo();
    TestTrue(TEXT("Episode reset succeeds"), First->ResetEpisode());
    const auto After = First->GetSessionInfo();
    TestEqual(TEXT("One reset notification"), First->EpisodeResetCount, 1);
    TestTrue(TEXT("Backend receives old attribution"), First->ResetSession.SessionId == Before.SessionId
        && First->ResetSession.AgentId == 3 && First->ResetSession.EpisodeId == 5);
    TestTrue(TEXT("Reset advances only the encounter"), After.SessionId == Session && After.AgentId == 3 && After.EpisodeId == 6);
    TestEqual(TEXT("Reset never releases learning state"), First->ReleaseCount, 0);
    TestEqual(TEXT("Other agent's episode is unaffected"), Other->GetSessionInfo().EpisodeId, int64(8));
    First->Shutdown();
    TestEqual(TEXT("Other binding remains Ready"), Other->GetSessionState(), EAdaptiveSessionState::Ready);
    TestEqual(TEXT("Other binding was not released"), Other->ReleaseCount, 0);
    TestFalse(TEXT("Stopped binding cannot reset"), First->ResetEpisode());
    TestEqual(TEXT("Failed reset does not notify the backend"), First->EpisodeResetCount, 1);
    TestFalse(TEXT("Stopped identity is invalid"), First->GetSessionInfo().SessionId.IsValid());
    Other->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveOptionalOperationsTest, "Schola.Inference.Adaptive.OptionalOperations",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveOptionalOperationsTest::RunTest(const FString& Parameters)
{
    auto* Policy = NewObject<UAdaptiveDecisionTestPolicy>();
    Policy->Init(ContractDefinition()); Policy->Start(FGuid::NewGuid());
    int64 Ticket = INDEX_NONE;
    Policy->RequestDecision(ContractPoint({0, 0}), Ticket);
    const auto Capabilities = Policy->GetCapabilities();
    TestTrue(TEXT("Base does not advertise unimplemented operations"), !Capabilities.bLearningControl
        && !Capabilities.bInferenceMode && !Capabilities.bLearningReset && !Capabilities.bSnapshots);
    auto* Sentinel = NewObject<UAdaptiveTestSnapshot>();
    UAdaptivePolicySnapshot* Snapshot = Sentinel;
    for (const auto& Result : {Policy->SetLearningEnabled(false), Policy->SetInferenceMode(EAdaptiveInferenceMode::Deterministic),
        Policy->ResetLearning(42), Policy->CaptureSnapshot(Snapshot)})
    {
        TestEqual(TEXT("Unimplemented operation is explicitly Unsupported"), Result.Status, EAdaptiveOperationStatus::Unsupported);
        TestFalse(TEXT("Unsupported operation has no active ID"), Result.OperationId.IsValid());
        TestFalse(TEXT("Unsupported operation explains why"), Result.Diagnostic.IsEmpty());
    }
    TestTrue(TEXT("Unsupported capture preserves the output"), Snapshot == Sentinel);
    FAdaptiveDecision Decision;
    TestEqual(TEXT("Unsupported operations preserve pending work"), Policy->TryGetDecision(Ticket, Decision), EAdaptiveRequestStatus::Pending);
    for (const TCHAR* Name : {TEXT("GetSessionInfo"), TEXT("GetCapabilities"), TEXT("SetLearningEnabled"),
        TEXT("SetInferenceMode"), TEXT("ResetLearning"), TEXT("CaptureSnapshot")})
    {
        UFunction* Function = Policy->FindFunction(FName(Name));
        TestTrue(Name, Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
    }
    Policy->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveSnapshotContractTest, "Schola.Inference.Adaptive.SnapshotContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveSnapshotContractTest::RunTest(const FString& Parameters)
{
    auto* Snapshot = NewObject<UAdaptiveTestSnapshot>();
    auto Definition = ContractDefinition();
    const FGuid Session = FGuid::NewGuid();
    auto Action = ContractPoint({0.9f});
    const auto Observation = ContractPoint({0, 0});
    TestFalse(TEXT("Uninitialized snapshot cannot evaluate"), Snapshot->Evaluate(Observation, Action));
    TestFalse(TEXT("Invalid attribution cannot initialize a snapshot"), Snapshot->Configure(FGuid(), 0, Definition, 0));
    TestFalse(TEXT("Invalid version rejected"), Snapshot->Configure(Session, -1, Definition, 0));
    TestTrue(TEXT("Concrete snapshot initializes"), Snapshot->Configure(Session, 7, Definition, 0.25f));
    Definition.ObsSpaceDefn.GetMutable<FBoxSpace>().Dimensions[0].Low = -0.5f;
    FInteractionDefinition Captured;
    Snapshot->GetInteractionDefinition(Captured);
    TestEqual(TEXT("Snapshot owns its definition"), Captured.ObsSpaceDefn.Get<FBoxSpace>().Dimensions[0].Low, -1.0f);
    TestFalse(TEXT("Captured snapshot cannot be replaced in place"), Snapshot->Configure(FGuid::NewGuid(), 8, Definition, -0.5f));
    TestTrue(TEXT("Frozen actor evaluates"), Snapshot->Evaluate(Observation, Action));
    TestEqual(TEXT("Original actor is retained"), Action.Get<FBoxPoint>().Values[0], 0.25f);
    TestEqual(TEXT("Original version is retained"), Snapshot->GetPolicyVersion(), int64(7));
    TestTrue(TEXT("Original session is retained"), Snapshot->GetSessionId() == Session);
    TestFalse(TEXT("Incompatible observation fails"), Snapshot->Evaluate(ContractPoint({0}), Action));
    TestEqual(TEXT("Failed evaluation preserves caller output"), Action.Get<FBoxPoint>().Values[0], 0.25f);
    return true;
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RLToolsPolicyTestHelpers.h"
#include "Spaces/DiscreteSpace.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RLToolsPolicyTests;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicySharingTest, "Schola.RLTools.Policy.IndependentAndSharedOwnership", Flags)
bool FRLToolsPolicySharingTest::RunTest(const FString&)
{
    for (auto Algorithm : {ERLToolsAlgorithm::SAC, ERLToolsAlgorithm::TD3, ERLToolsAlgorithm::PPO})
    {
        const FString Profile = URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm)[0].Id;
        FWorld F, Other(EWorldType::PIE);
        auto* A = F.Policy(Algorithm, Profile);
        auto* B = F.Policy(Algorithm, Profile);
        auto* Independent = F.Policy(Algorithm, Profile);
        auto* Independent2 = F.Policy(Algorithm, Profile);
        auto* OtherWorld = Other.Policy(Algorithm, Profile);
        A->SharedGroup = B->SharedGroup = OtherWorld->SharedGroup = TEXT("Guards");
        B->InitialInferenceMode = EAdaptiveInferenceMode::Deterministic;
        B->DecisionCapacity = 64; // Binding-local controls need not agree.
        const auto Def = Definition();
        for (auto* P : {A, B, Independent, Independent2, OtherWorld})
        {
            if (!TestTrue(TEXT("Init"), P->Init(Def)) || !TestTrue(TEXT("Ready"), Ready(P))) return false;
        }
        TestTrue(TEXT("Explicit group shares session"), A->GetSessionInfo().SessionId == B->GetSessionInfo().SessionId);
        TestTrue(TEXT("Shared agents differ"), A->GetSessionInfo().AgentId != B->GetSessionInfo().AgentId);
        TestTrue(TEXT("Same settings alone do not share"), Independent->GetSessionInfo().SessionId != Independent2->GetSessionInfo().SessionId);
        TestTrue(TEXT("PIE worlds isolate same group"), A->GetSessionInfo().SessionId != OtherWorld->GetSessionInfo().SessionId);
        TestEqual(TEXT("One learner per explicit group plus two independent"), F.Service()->GetExecutionStats().Learners, 3);
        TestEqual(TEXT("Four bindings"), F.Service()->GetExecutionStats().Bindings, 4);
        FAdaptiveDecision D;
        Decision(A, Observation(), D);
        TestEqual(TEXT("Shared admission"), A->SubmitExperience(Outcome(D, Observation())), EAdaptiveSubmitResult::Accepted);
        TestEqual(TEXT("Group observes same learner counters"), Stats(B).Accepted, int64(1));
        TestEqual(TEXT("Independent weights/storage unaffected"), Stats(Independent).Accepted, int64(0));
        TestEqual(TEXT("Other world unaffected"), Stats(OtherWorld).Accepted, int64(0));
        const auto EpisodeB = B->GetSessionInfo().EpisodeId;
        A->ResetEpisode();
        TestEqual(TEXT("Episode reset stays agent-local"), B->GetSessionInfo().EpisodeId, EpisodeB);
        A->Shutdown();
        TestEqual(TEXT("Shared learner retained"), F.Service()->GetExecutionStats().Learners, 3);
        TestTrue(TEXT("Remaining shared agent still selects actions"), Decision(B, Observation(), D));
        TestEqual(TEXT("Remaining shared agent still submits"), B->SubmitExperience(Outcome(D, Observation())), EAdaptiveSubmitResult::Accepted);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyCompatibilityTest, "Schola.RLTools.Policy.SharedCompatibility", Flags)
bool FRLToolsPolicyCompatibilityTest::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Policy();
    A->SharedGroup = TEXT("Guards");
    auto Def = Definition();
    if (!TestTrue(TEXT("Init"), A->Init(Def)) || !TestTrue(TEXT("Ready"), Ready(A))) return false;
    auto* Bad = F.Policy();
    Bad->SharedGroup = A->SharedGroup;
    const auto Baseline = Bad->Settings;
    auto Reject = [&](const FInteractionDefinition& Candidate)
    {
        TestFalse(TEXT("Incompatible join fails"), Bad->Init(Candidate));
        TestTrue(TEXT("Useful sharing diagnostic"), Bad->GetLastError().Contains(TEXT("incompatible")));
        TestEqual(TEXT("No accidental extra binding"), F.Service()->GetExecutionStats().Bindings, 1);
        TestEqual(TEXT("Original learner remains usable"), A->GetSessionState(), EAdaptiveSessionState::Ready);
    };
    auto Different = Def;
    Different.ActionSpaceDefn.GetMutable<FBoxSpace>().Dimensions[0].High += 1; Reject(Different);
    Different = Def; Different.ObsSpaceDefn.GetMutable<FBoxSpace>().Dimensions[0].High += 1; Reject(Different);
    Different = Def; Different.ObsSpaceDefn.GetMutable<FBoxSpace>().Shape = {3}; Reject(Different);
    Different = Def; Different.AgentType = TEXT("OtherType"); Reject(Different);
    for (int32 FRLToolsRuntimeSettings::* Member : {&FRLToolsRuntimeSettings::Seed, &FRLToolsRuntimeSettings::QueueCapacity,
        &FRLToolsRuntimeSettings::MaxRecordsPerBurst, &FRLToolsRuntimeSettings::LearningStarts,
        &FRLToolsRuntimeSettings::InitialRandomSteps, &FRLToolsRuntimeSettings::TrainFrequency,
        &FRLToolsRuntimeSettings::GradientSteps, &FRLToolsRuntimeSettings::MaxUpdatesPerBurst,
        &FRLToolsRuntimeSettings::PublishEveryUpdates, &FRLToolsRuntimeSettings::PPOEpochs})
    {
        Bad->Settings = Baseline; ++(Bad->Settings.*Member); Reject(Def);
    }
    Bad->Settings = Baseline; Bad->Settings.LearningRate *= 2; Reject(Def);
    Bad->Settings = Baseline; Bad->Settings.ExplorationNoise *= 2; Reject(Def);
    Bad->Settings = Baseline; Bad->Settings.ObservationMean.Init(0, 3); Bad->Settings.ObservationStd.Init(1, 3); Reject(Def);
    Bad->Settings = Baseline; Bad->Algorithm = ERLToolsAlgorithm::TD3; Bad->Configuration = TEXT("td3/3x2-mlp32"); Reject(Def);
    Bad->Algorithm = ERLToolsAlgorithm::SAC; Bad->Configuration = TEXT("sac/5x1-mlp32"); Reject(Definition(5, 1));
    Bad->Configuration = A->Configuration;
    TestTrue(TEXT("Matching group finally joins"), Bad->Init(Def));
    TestTrue(TEXT("Ready after matching join"), Ready(Bad));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyControlsTest, "Schola.RLTools.Policy.ControlsResetAndSnapshot", Flags)
bool FRLToolsPolicyControlsTest::RunTest(const FString&)
{
    for (auto Algorithm : {ERLToolsAlgorithm::SAC, ERLToolsAlgorithm::TD3, ERLToolsAlgorithm::PPO})
    {
        FWorld F;
        const auto Profile = URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm)[0].Id;
        auto* A = F.Policy(Algorithm, Profile);
        auto* B = F.Policy(Algorithm, Profile);
        A->SharedGroup = B->SharedGroup = TEXT("Controls");
        A->Init(Definition()); B->Init(Definition());
        if (!TestTrue(TEXT("A ready"), Ready(A)) || !TestTrue(TEXT("B ready"), Ready(B))) return false;
        FAdaptiveDecision D;
        Decision(A, Observation(), D);
        const auto Experience = Outcome(D, Observation());
        const auto Pause = B->SetLearningEnabled(false);
        TestEqual(TEXT("Pause has operation identity"), Pause.Status, EAdaptiveOperationStatus::Pending);
        TestTrue(TEXT("Pause completion observable"), Completed(B, Pause));
        TestTrue(TEXT("Pause closes group admission after active burst"), !Stats(A).bLearningEnabled && !Stats(A).bTraining);
        TestEqual(TEXT("Frozen admission distinguished"), A->SubmitExperience(Experience), EAdaptiveSubmitResult::Frozen);
        TestEqual(TEXT("Deterministic selection while paused"), A->SetInferenceMode(EAdaptiveInferenceMode::Deterministic).Status, EAdaptiveOperationStatus::Succeeded);
        TestFalse(TEXT("Changing inference mode does not resume learning"), Stats(A).bLearningEnabled);
        FAdaptiveDecision Another;
        TestTrue(TEXT("Inference continues while paused"), Decision(A, Observation(), Another));
        A->CancelDecision(Another.DecisionId);
        const auto Resume = A->SetLearningEnabled(true);
        TestTrue(TEXT("Resume completion observable"), Completed(A, Resume));
        TestEqual(TEXT("Frozen decision can be retried"), A->SubmitExperience(Experience), EAdaptiveSubmitResult::Accepted);

        UAdaptivePolicySnapshot* Snapshot = nullptr;
        A->CaptureSnapshot(Snapshot);
        TStrongObjectPtr<UAdaptivePolicySnapshot> Keep(Snapshot);
        TInstancedStruct<FPoint> Before, After;
        TestTrue(TEXT("Snapshot evaluates before reset"), Snapshot && Snapshot->Evaluate(Observation(), Before));
        const auto OldSession = A->GetSessionInfo();
        Decision(B, Observation(), D);
        const auto OldOutcome = Outcome(D, Observation());
        const auto Reset = A->ResetLearning(17);
        TestEqual(TEXT("Reset is pending"), Reset.Status, EAdaptiveOperationStatus::Pending);
        TestEqual(TEXT("Both bindings close during reconstruction"), B->GetSessionState(), EAdaptiveSessionState::Initialized);
        TestTrue(TEXT("Reset completion observable"), Completed(A, Reset));
        TestEqual(TEXT("All affected bindings ready"), B->GetSessionState(), EAdaptiveSessionState::Ready);
        TestTrue(TEXT("New shared session prevents version-zero aliasing"), A->GetSessionInfo().SessionId != OldSession.SessionId
            && A->GetSessionInfo().SessionId == B->GetSessionInfo().SessionId);
        TestEqual(TEXT("Agent identity retained"), A->GetSessionInfo().AgentId, OldSession.AgentId);
        TestEqual(TEXT("Learning version restarted"), Stats(A).Version, int64(0));
        TestEqual(TEXT("Native generation advanced"), Stats(A).Generation, int64(2));
        FRLToolsRuntimeSettings Active;
        B->GetActiveSettings(Active);
        TestEqual(TEXT("Shared reset seed visible to all"), Active.Seed, 17);
        TestEqual(TEXT("Old decision cannot alias new generation"), B->SubmitExperience(OldOutcome), EAdaptiveSubmitResult::Rejected);
        TestTrue(TEXT("Snapshot survives reset"), Snapshot->Evaluate(Observation(), After));
        TestTrue(TEXT("Snapshot retains preprocessing and actor"), Before.Get<FBoxPoint>().Values == After.Get<FBoxPoint>().Values);
        A->Shutdown(); B->Shutdown();
        TestTrue(TEXT("Snapshot survives last live release"), Snapshot->Evaluate(Observation(), After));
        TestTrue(TEXT("Snapshot still unchanged"), Before.Get<FBoxPoint>().Values == After.Get<FBoxPoint>().Values);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyConfigurationTest, "Schola.RLTools.Policy.ConfigurationAndBlueprintAPI", Flags)
bool FRLToolsPolicyConfigurationTest::RunTest(const FString&)
{
    FWorld F;
    auto* P = F.Policy();
    TestEqual(TEXT("SAC discovered shapes"), P->GetAvailableConfigurations(ERLToolsAlgorithm::SAC).Num(), 2);
    TestEqual(TEXT("TD3 discovered shapes"), P->GetAvailableConfigurations(ERLToolsAlgorithm::TD3).Num(), 2);
    TestEqual(TEXT("PPO discovered shapes"), P->GetAvailableConfigurations(ERLToolsAlgorithm::PPO).Num(), 2);
    TestEqual(TEXT("Nondefault compiled discount visible"), P->GetAvailableConfigurations(ERLToolsAlgorithm::PPO)[1].Discount, 0.95f);
    auto Def = Definition();
    TestTrue(TEXT("Init"), P->Init(Def));
    if (!TestTrue(TEXT("Ready"), Ready(P))) return false;
    const FGuid Original = P->GetSessionInfo().SessionId;
    P->Configuration = TEXT("sac/uncompiled");
    TestFalse(TEXT("Unavailable configuration rejected"), P->Init(Def));
    TestTrue(TEXT("Build guidance"), P->GetLastError().Contains(TEXT("compiled")));
    TestTrue(TEXT("Invalid replacement preserves live learner"), P->GetSessionInfo().SessionId == Original);
    P->Configuration = TEXT("td3/3x2-mlp32");
    TestFalse(TEXT("Algorithm/config mismatch"), P->Init(Def));
    P->Configuration = TEXT("sac/3x2-mlp32");
    P->Settings.LearningStarts = 1; TestFalse(TEXT("Invalid warmup rejected locally"), P->Init(Def)); P->Settings.LearningStarts = 32;
    P->Settings.ObservationMean = {0}; P->Settings.ObservationStd = {1};
    TestFalse(TEXT("Invalid preprocessing rejected locally"), P->Init(Def));
    P->Settings.ObservationMean.Empty(); P->Settings.ObservationStd.Empty();
    auto Discrete = Def;
    Discrete.ObsSpaceDefn.InitializeAs<FDiscreteSpace>();
    TestFalse(TEXT("Discrete observations unsupported"), P->Init(Discrete));
    Discrete = Def; Discrete.ActionSpaceDefn.InitializeAs<FDiscreteSpace>();
    TestFalse(TEXT("Discrete actions unsupported"), P->Init(Discrete));
    auto Unbounded = Def;
    Unbounded.ActionSpaceDefn.GetMutable<FBoxSpace>().Dimensions[0].bHasHigh = false;
    TestFalse(TEXT("Unbounded actions unsupported"), P->Init(Unbounded));
    for (const TCHAR* Name : {TEXT("Init"), TEXT("RequestDecision"), TEXT("TryGetDecision"), TEXT("SubmitExperience"),
        TEXT("GetLearningStats"), TEXT("GetActiveSettings"), TEXT("GetAvailableConfigurations"), TEXT("GetInitializationOperation"),
        TEXT("GetOperationResult"), TEXT("ResetLearning"), TEXT("CaptureSnapshot")})
    {
        auto* Function = P->FindFunction(FName(Name));
        TestTrue(Name, Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
    }
    for (const TCHAR* Name : {TEXT("Algorithm"), TEXT("Configuration"), TEXT("Settings"), TEXT("SharedGroup")})
    {
        const auto* Property = P->GetClass()->FindPropertyByName(FName(Name));
        TestTrue(TEXT("Configuration is reflected and editable"), Property && Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible));
    }
    const auto* Batch = FRLToolsConfiguration::StaticStruct()->FindPropertyByName(TEXT("BatchSize"));
    TestTrue(TEXT("Compiled choices are read-only"), Batch && Batch->HasAllPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst));
    const auto Capabilities = P->GetCapabilities();
    TestTrue(TEXT("Capabilities implemented"), Capabilities.bLearningControl && Capabilities.bLearningReset && Capabilities.bInferenceMode && Capabilities.bSnapshots);
    return true;
}
#endif

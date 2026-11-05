// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RLToolsPolicyTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RLToolsPolicyTests;
namespace
{
bool CheckAlgorithm(FAutomationTestBase& Test, ERLToolsAlgorithm Algorithm)
{
    for (const auto& Profile : URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm))
    {
        Test.AddInfo(TEXT("Actual Schola policy/world workflow: ") + Profile.Id);
        FWorld Fixture;
        auto* P = Fixture.Policy(Algorithm, Profile.Id);
        P->Settings.PPOEpochs = 2;
        P->Settings.LearningStarts = Profile.BatchSize;
        P->Settings.MaxRecordsPerBurst = 8;
        P->Settings.ObservationMean.Init(0.1f, Profile.Observations);
        P->Settings.ObservationStd.Init(2.0f, Profile.Observations);
        const auto Def = Definition(Profile.Observations, Profile.Actions);
        const auto Obs = Observation(Profile.Observations);
        if (!Test.TestTrue(TEXT("Validated initialization accepted"), P->Init(Def))) return false;
        Test.TestEqual(TEXT("Construction is asynchronous"), P->GetSessionState(), EAdaptiveSessionState::Initialized);
        Test.TestEqual(TEXT("Initialization is observable"), P->GetInitializationOperation().Status, EAdaptiveOperationStatus::Pending);
        if (!Test.TestTrue(TEXT("Worker creates learner"), Ready(P))) return false;
        Test.TestEqual(TEXT("Initialization acknowledged"), P->GetInitializationOperation().Status, EAdaptiveOperationStatus::Succeeded);
        UAdaptivePolicySnapshot* Snapshot = nullptr;
        Test.TestEqual(TEXT("Capture"), P->CaptureSnapshot(Snapshot).Status, EAdaptiveOperationStatus::Succeeded);
        TStrongObjectPtr<UAdaptivePolicySnapshot> KeepSnapshot(Snapshot);
        TInstancedStruct<FPoint> Before, After;
        if (!Test.TestTrue(TEXT("Snapshot evaluates"), Snapshot && Snapshot->Evaluate(Obs, Before))) return false;
        Test.TestTrue(TEXT("Declared multidimensional shape retained"), Before.Get<FBoxPoint>().Shape == Def.ActionSpaceDefn.Get<FBoxSpace>().Shape);
        Test.TestEqual(TEXT("Deterministic mode"), P->SetInferenceMode(EAdaptiveInferenceMode::Deterministic).Status, EAdaptiveOperationStatus::Succeeded);
        FAdaptiveDecision D;
        if (!Test.TestTrue(TEXT("Deterministic action"), Decision(P, Obs, D))) return false;
        Test.TestTrue(TEXT("Snapshot/live preprocessing and scaling agree"), D.Action.Get<FBoxPoint>().Values == Before.Get<FBoxPoint>().Values);
        P->CancelDecision(D.DecisionId);
        P->SetInferenceMode(EAdaptiveInferenceMode::Stochastic);
        FAdaptiveDecision RetainedOldPolicy;
        if (!Test.TestTrue(TEXT("Retain an outcome across publication"), Decision(P, Obs, RetainedOldPolicy))) return false;

        const int32 Records = Algorithm == ERLToolsAlgorithm::PPO ? Profile.RolloutLength : FMath::Max(64, Profile.BatchSize);
        // Exercise a full PPO rollout or SAC/TD3 warm-up for every compiled configuration.
        for (int32 Step = 0; Step < Records; ++Step)
        {
            if (!Test.TestTrue(TEXT("Request/poll action"), Decision(P, Obs, D))) return false;
            const auto& Values = D.Action.Get<FBoxPoint>().Values;
            for (float Value : Values) Test.TestTrue(TEXT("Non-default finite bounds"), Value >= 10000.1f && Value <= 10000.2f);
            auto E = Outcome(D, Obs);
            E.bTerminated = Step + 1 == Records;
            if (!Test.TestEqual(TEXT("Unchanged executed action admitted (including PPO float round trip)"),
                P->SubmitExperience(E), EAdaptiveSubmitResult::Accepted)) return false;
        }
        if (!Test.TestTrue(TEXT("Background training and publication"),
            Wait([P, Records] { const auto S = Stats(P); return S.Stored == Records && S.Version > 0 && !S.bTraining; }))) return false;
        const auto S = Stats(P);
        Test.TestTrue(TEXT("Real actor/critic updates"), S.ActorUpdates > 0 && S.CriticUpdates > 0);
        Test.TestTrue(TEXT("Pending work is eventually drained"), Wait([P] { auto S = Stats(P); return S.PendingUpdates == 0 && S.Queued == 0 && !S.bTraining; }));
        Snapshot->Evaluate(Obs, After);
        Test.TestTrue(TEXT("Captured actor unchanged after training"), Before.Get<FBoxPoint>().Values == After.Get<FBoxPoint>().Values);
        UAdaptivePolicySnapshot* Updated = nullptr;
        P->CaptureSnapshot(Updated);
        TStrongObjectPtr<UAdaptivePolicySnapshot> KeepUpdated(Updated);
        Test.TestTrue(TEXT("New snapshot has newer version"), Updated && Updated->GetPolicyVersion() > Snapshot->GetPolicyVersion());
        if (Algorithm == ERLToolsAlgorithm::PPO)
        {
            Test.TestEqual(TEXT("PPO retires outcomes after on-policy publication"),
                P->SubmitExperience(Outcome(RetainedOldPolicy, Obs)), EAdaptiveSubmitResult::Rejected);
            Test.TestTrue(TEXT("PPO stale-version diagnostic"), P->GetLastError().Contains(TEXT("stale")));
        }
        P->Shutdown();
        Snapshot->Evaluate(Obs, After);
        Test.TestTrue(TEXT("Snapshot survives binding shutdown"), Before.Get<FBoxPoint>().Values == After.Get<FBoxPoint>().Values);
        Test.AddInfo(FString::Printf(TEXT("%s: admitted=%lld stored=%lld updates=%lld version=%lld"),
            *Profile.Id, S.Accepted, S.Stored, S.Updates, S.Version));
    }
    return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsSACPolicyTest, "Schola.RLTools.Policy.SAC", Flags)
bool FRLToolsSACPolicyTest::RunTest(const FString&) { return CheckAlgorithm(*this, ERLToolsAlgorithm::SAC); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsTD3PolicyTest, "Schola.RLTools.Policy.TD3", Flags)
bool FRLToolsTD3PolicyTest::RunTest(const FString&) { return CheckAlgorithm(*this, ERLToolsAlgorithm::TD3); }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPPOPolicyTest, "Schola.RLTools.Policy.PPO", Flags)
bool FRLToolsPPOPolicyTest::RunTest(const FString&) { return CheckAlgorithm(*this, ERLToolsAlgorithm::PPO); }

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPPOConversionTest, "Schola.RLTools.Policy.PPOConversionAndAttribution", Flags)
bool FRLToolsPPOConversionTest::RunTest(const FString&)
{
    FWorld F;
    auto* P = F.Policy(ERLToolsAlgorithm::PPO, TEXT("ppo/3x2-mlp32"));
    const auto Def = Definition();
    const auto Obs = Observation();
    TestTrue(TEXT("Init"), P->Init(Def));
    if (!TestTrue(TEXT("Ready"), Ready(P))) return false;
    // Prove these bounds actually lose normalized bits; passing admission must reuse the retained native action.
    TInstancedStruct<FPoint> Scaled;
    const std::vector<float> Original{0.1234567f, -0.7654321f};
    Schola::RLTools::ActionPoint(Original, Def, Scaled);
    TestTrue(TEXT("Test bounds cause round-trip drift"), Schola::RLTools::NormalizeAction(Scaled, Def) != Original);
    FAdaptiveDecision D;
    if (!TestTrue(TEXT("Decision"), Decision(P, Obs, D))) return false;
    const auto Valid = Outcome(D, Obs);
    auto Reject = [&](const FAdaptiveExperience& E, const TCHAR* Diagnostic)
    {
        TestEqual(TEXT("Altered payload rejected"), P->SubmitExperience(E), EAdaptiveSubmitResult::Rejected);
        TestTrue(TEXT("Actionable common diagnostic"), P->GetLastError().Contains(Diagnostic));
    };
    auto Bad = Valid; ++Bad.PolicyVersion; Reject(Bad, TEXT("attribution"));
    Bad = Valid; Bad.Observation.GetMutable<FBoxPoint>().Values[0] += 0.1f; Reject(Bad, TEXT("originated"));
    Bad = Valid; Bad.AgentId += 1; Reject(Bad, TEXT("agent"));
    Bad = Valid; Bad.SessionId = FGuid::NewGuid(); Reject(Bad, TEXT("session"));
    Bad = Valid;
    auto& Value = Bad.ExecutedAction.GetMutable<FBoxPoint>().Values[0];
    Value = Value == 10000.1f ? 10000.2f : 10000.1f;
    Reject(Bad, TEXT("PPO"));
    TestEqual(TEXT("Rejection preserves legitimate original decision"), P->SubmitExperience(Valid), EAdaptiveSubmitResult::Accepted);
    Reject(Valid, TEXT("already submitted"));
    FAdaptiveDecision Old;
    Decision(P, Obs, Old);
    P->ResetEpisode();
    TestEqual(TEXT("Old episode rejected"), P->SubmitExperience(Outcome(Old, Obs)), EAdaptiveSubmitResult::Rejected);
    FAdaptiveDecision Next;
    TestTrue(TEXT("New episode continues"), Decision(P, Obs, Next));
    TestEqual(TEXT("Episode advances"), Next.EpisodeId, Old.EpisodeId + 1);
    TestTrue(TEXT("Learned state preserved"), Next.SessionId == Old.SessionId);
    return true;
}
#endif

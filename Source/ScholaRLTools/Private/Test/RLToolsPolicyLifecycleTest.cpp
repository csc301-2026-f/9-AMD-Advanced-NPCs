// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "RLToolsPolicyTestHelpers.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS
using namespace RLToolsPolicyTests;
using Schola::RLTools::EWork;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyQueueRetryTest, "Schola.RLTools.Policy.QueueFullRetry", Flags)
bool FRLToolsPolicyQueueRetryTest::RunTest(const FString&)
{
    for (auto Algorithm : {ERLToolsAlgorithm::SAC, ERLToolsAlgorithm::TD3, ERLToolsAlgorithm::PPO})
    {
        FWorld F;
        auto* P = F.Policy(Algorithm, URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm)[0].Id);
        P->Settings.QueueCapacity = 1;
        P->Init(Definition());
        if (!TestTrue(TEXT("Ready"), Ready(P))) return false;
        auto Gate = std::make_shared<FGate>(EWork::Train);
        Gate->Install(F.Service());
        ON_SCOPE_EXIT { Gate->Open(); };
        FAdaptiveDecision A, B;
        Decision(P, Observation(), A);
        Decision(P, Observation(), B);
        TestEqual(TEXT("First record admitted"), P->SubmitExperience(Outcome(A, Observation())), EAdaptiveSubmitResult::Accepted);
        if (!TestTrue(TEXT("Training deliberately held on worker"), Wait([Gate] { return Gate->Entered > 0; }))) return false;
        const auto Retry = Outcome(B, Observation());
        TestEqual(TEXT("Queue-full admission distinguished"), P->SubmitExperience(Retry), EAdaptiveSubmitResult::Dropped);
        TestTrue(TEXT("Common diagnostic describes retry"), P->GetLastError().Contains(TEXT("retry")));
        TestEqual(TEXT("Retryable record retained"), Stats(P).RetainedDecisions, 1);
        Gate->Open();
        if (!TestTrue(TEXT("Queue drains"), Wait([P] { return Stats(P).Stored == 1 && Stats(P).Queued == 0; }))) return false;
        TestEqual(TEXT("Same native decision can retry"), P->SubmitExperience(Retry), EAdaptiveSubmitResult::Accepted);
        TestTrue(TEXT("Retried experience stored once"), Wait([P] { return Stats(P).Stored == 2; }));
        TestEqual(TEXT("No duplicate accepted records"), Stats(P).Accepted, int64(2));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyRetirementTest, "Schola.RLTools.Policy.DecisionRetirementAndLateRewards", Flags)
bool FRLToolsPolicyRetirementTest::RunTest(const FString&)
{
    FWorld F;
    auto* P = F.Policy();
    P->DecisionCapacity = 2;
    P->Init(Definition());
    if (!TestTrue(TEXT("Ready"), Ready(P))) return false;
    const auto Obs = Observation();
    FAdaptiveDecision Delivered;
    if (!TestTrue(TEXT("Retrieve within selection window"), Decision(P, Obs, Delivered, 0.05f))) return false;
    FPlatformProcess::SleepNoStats(0.06f);
    FAdaptiveDecision Output;
    TestEqual(TEXT("Selection window expires"), P->TryGetDecision(Delivered.DecisionId, Output), EAdaptiveRequestStatus::Expired);
    TestEqual(TEXT("Previously delivered action may receive a later reward"),
        P->SubmitExperience(Outcome(Delivered, Obs)), EAdaptiveSubmitResult::Accepted);
    int64 Undelivered = INDEX_NONE;
    TestTrue(TEXT("Unused request issued"), P->RequestDecision(Obs, Undelivered, 0.05f));
    const auto Info = P->GetSessionInfo();
    FPlatformProcess::SleepNoStats(0.06f);
    TestEqual(TEXT("Unused command expires"), P->TryGetDecision(Undelivered, Output), EAdaptiveRequestStatus::Expired);
    auto Fake = Outcome(Delivered, Obs);
    Fake.DecisionId = Undelivered;
    TestEqual(TEXT("Expired unobserved action cannot train"), P->SubmitExperience(Fake), EAdaptiveSubmitResult::Rejected);
    FAdaptiveDecision First, Second, Third;
    Decision(P, Obs, First); Decision(P, Obs, Second); Decision(P, Obs, Third);
    TestEqual(TEXT("Bounded retention"), Stats(P).RetainedDecisions, 2);
    TestEqual(TEXT("Oldest decision explicitly retired"), P->SubmitExperience(Outcome(First, Obs)), EAdaptiveSubmitResult::Rejected);
    TestTrue(TEXT("Cancel old delivered command after a newer request"), P->CancelDecision(Second.DecisionId));
    TestEqual(TEXT("Cancelled decision cannot train"), P->SubmitExperience(Outcome(Second, Obs)), EAdaptiveSubmitResult::Rejected);
    TestEqual(TEXT("Another command unaffected"), P->SubmitExperience(Outcome(Third, Obs)), EAdaptiveSubmitResult::Accepted);
    P->ExperienceRetentionSeconds = 0.05f;
    P->Init(Definition()); Ready(P);
    Decision(P, Obs, Delivered);
    FPlatformProcess::SleepNoStats(0.06f);
    TestEqual(TEXT("Reward retention expiry"), P->SubmitExperience(Outcome(Delivered, Obs)), EAdaptiveSubmitResult::Rejected);

    // Off-policy algorithms allow an actual changed command and consistently unscale it.
    for (auto Algorithm : {ERLToolsAlgorithm::SAC, ERLToolsAlgorithm::TD3})
    {
        auto* OffPolicy = F.Policy(Algorithm, URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm)[0].Id);
        OffPolicy->Init(Definition()); Ready(OffPolicy);
        Decision(OffPolicy, Obs, Delivered);
        auto E = Outcome(Delivered, Obs);
        E.ExecutedAction.GetMutable<FBoxPoint>().Values[0] = 10000.1f;
        TestEqual(TEXT("Off-policy adjusted action is admitted"), OffPolicy->SubmitExperience(E), EAdaptiveSubmitResult::Accepted);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyStaleCompletionTest, "Schola.RLTools.Policy.DelayedCreationCannotReviveBinding", Flags)
bool FRLToolsPolicyStaleCompletionTest::RunTest(const FString&)
{
    FWorld F;
    auto Gate = std::make_shared<FGate>(EWork::Create);
    Gate->Install(F.Service());
    ON_SCOPE_EXIT { Gate->Open(); };
    auto* Stopped = F.Policy();
    auto* Replaced = F.Policy();
    Stopped->Init(Definition()); Replaced->Init(Definition());
    const auto StoppedOperation = Stopped->GetInitializationOperation();
    const auto ReplacedOperation = Replaced->GetInitializationOperation();
    if (!TestTrue(TEXT("Both construction jobs in flight"), Wait([Gate] { return Gate->Entered == 2; }))) return false;
    const double Start = FPlatformTime::Seconds();
    UAdaptiveNPCPolicy* Base = Stopped;
    Base->Shutdown();
    TestTrue(TEXT("Shutdown never waits for construction"), FPlatformTime::Seconds() - Start < 0.25);
    TestEqual(TEXT("Abandoned initialization cancelled"), Stopped->GetOperationResult(StoppedOperation.OperationId).Status, EAdaptiveOperationStatus::Cancelled);
    TestTrue(TEXT("Reinitialize while previous create blocked"), Replaced->Init(Definition()));
    TestEqual(TEXT("Previous initialization cancelled"), Replaced->GetOperationResult(ReplacedOperation.OperationId).Status, EAdaptiveOperationStatus::Cancelled);
    const FGuid LatestOperation = Replaced->GetInitializationOperation().OperationId;
    Gate->Open();
    if (!TestTrue(TEXT("Replacement ready"), Ready(Replaced))) return false;
    TestEqual(TEXT("Stopped policy remains stopped"), Stopped->GetSessionState(), EAdaptiveSessionState::Stopped);
    TestEqual(TEXT("Only current initialization completed"), Replaced->GetOperationResult(LatestOperation).Status, EAdaptiveOperationStatus::Succeeded);
    TestEqual(TEXT("Only replacement remains bound"), F.Service()->GetExecutionStats().Bindings, 1);
    TestTrue(TEXT("Old numerical states reclaimed on workers"), Wait([&F] { return F.Service()->GetExecutionStats().DisposedLearners >= 2; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicySharedResetFlightTest, "Schola.RLTools.Policy.SharedResetWithReleaseInFlight", Flags)
bool FRLToolsPolicySharedResetFlightTest::RunTest(const FString&)
{
    FWorld F;
    auto* A = F.Policy();
    auto* B = F.Policy();
    A->SharedGroup = B->SharedGroup = TEXT("Group");
    A->Init(Definition()); B->Init(Definition());
    if (!Ready(A) || !Ready(B)) return false;
    const auto Old = B->GetSessionInfo();
    auto Gate = std::make_shared<FGate>(EWork::Reset);
    Gate->Install(F.Service());
    ON_SCOPE_EXIT { Gate->Open(); };
    const auto Reset = A->ResetLearning(9);
    if (!TestTrue(TEXT("Reset held on worker"), Wait([Gate] { return Gate->Entered > 0; }))) return false;
    A->Shutdown();
    TestEqual(TEXT("Initiator's pending operation cancelled on release"), A->GetOperationResult(Reset.OperationId).Status, EAdaptiveOperationStatus::Cancelled);
    Gate->Open();
    if (!TestTrue(TEXT("Other shared binding completes reset"), Ready(B))) return false;
    TestEqual(TEXT("Released binding not revived"), A->GetSessionState(), EAdaptiveSessionState::Stopped);
    TestTrue(TEXT("Remaining binding receives new session"), B->GetSessionInfo().SessionId != Old.SessionId);
    TestEqual(TEXT("One remaining binding"), F.Service()->GetExecutionStats().Bindings, 1);
    FAdaptiveDecision D;
    TestTrue(TEXT("Remaining binding works"), Decision(B, Observation(), D));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyWorldTeardownTest, "Schola.RLTools.Policy.WorldTeardownWithWorkInFlight", Flags)
bool FRLToolsPolicyWorldTeardownTest::RunTest(const FString&)
{
    for (auto Algorithm : {ERLToolsAlgorithm::SAC, ERLToolsAlgorithm::TD3, ERLToolsAlgorithm::PPO})
    {
        FWorld F;
        auto* P = F.Policy(Algorithm, URLToolsNPCPolicy::GetAvailableConfigurations(Algorithm)[0].Id);
        P->Init(Definition());
        if (!Ready(P)) return false;
        TStrongObjectPtr<URLToolsWorldSubsystem> Service(F.Service());
        auto Gate = std::make_shared<FGate>(EWork::Train);
        Gate->Install(Service.Get());
        ON_SCOPE_EXIT { Gate->Open(); };
        UAdaptivePolicySnapshot* Snapshot = nullptr;
        P->CaptureSnapshot(Snapshot);
        TStrongObjectPtr<UAdaptivePolicySnapshot> Keep(Snapshot);
        FAdaptiveDecision D;
        Decision(P, Observation(), D);
        P->SubmitExperience(Outcome(D, Observation()));
        if (!TestTrue(TEXT("Training work held"), Wait([Gate] { return Gate->Entered > 0; }))) return false;
        const double Start = FPlatformTime::Seconds();
        F.World->DestroyWorld(false);
        if (F.World->IsRooted()) F.World->RemoveFromRoot();
        F.World = nullptr;
        TestTrue(TEXT("World teardown does not wait for worker"), FPlatformTime::Seconds() - Start < 0.5);
        TestEqual(TEXT("Binding promptly stopped"), P->GetSessionState(), EAdaptiveSessionState::Stopped);
        TestEqual(TEXT("All bindings released"), Service->GetExecutionStats().Bindings, 0);
        Gate->Open();
        TestTrue(TEXT("Learner eventually destroyed on worker"), Wait([&Service]
        {
            const auto S = Service->GetExecutionStats();
            return S.ActiveWorkers == 0 && S.QueuedLearners == 0 && S.DisposedLearners == 1;
        }));
        TInstancedStruct<FPoint> Action;
        TestTrue(TEXT("Snapshot independent of destroyed world"), Snapshot->Evaluate(Observation(), Action));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyFairSchedulingTest, "Schola.RLTools.Policy.BoundedFairScheduling", Flags)
bool FRLToolsPolicyFairSchedulingTest::RunTest(const FString&)
{
    FWorld F;
    TArray<URLToolsNPCPolicy*> Learners;
    for (int32 I = 0; I < 5; ++I)
    {
        auto* P = F.Policy();
        P->Settings.MaxRecordsPerBurst = 1;
        P->Init(Definition());
        if (!Ready(P)) return false;
        Learners.Add(P);
    }
    auto Gate = std::make_shared<FGate>(EWork::Train);
    Gate->Install(F.Service());
    ON_SCOPE_EXIT { Gate->Open(); };
    for (auto* P : Learners)
    {
        for (int32 I = 0; I < 32; ++I)
        {
            FAdaptiveDecision D;
            if (!Decision(P, Observation(), D)) return false;
            P->SubmitExperience(Outcome(D, Observation()));
        }
    }
    if (!TestTrue(TEXT("Exactly two workers execute"), Wait([Gate] { return Gate->Entered == 2; }))) return false;
    auto S = F.Service()->GetExecutionStats();
    TestEqual(TEXT("Worker cap respected"), S.ActiveWorkers, 2);
    TestTrue(TEXT("Other learners queued fairly"), S.QueuedLearners >= 3);
    Gate->Open();
    TestTrue(TEXT("Every learner makes progress"), Wait([&Learners]
    {
        for (auto* P : Learners) if (Stats(P).Version == 0) return false;
        return true;
    }));
    TestTrue(TEXT("Peak never exceeds configured bound"), F.Service()->GetExecutionStats().PeakWorkers <= S.WorkerLimit);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPPOUnavailableTest, "Schola.RLTools.Policy.PPOTemporarilyUnavailableRetention", Flags)
bool FRLToolsPPOUnavailableTest::RunTest(const FString&)
{
    FWorld F;
    auto* P = F.Policy(ERLToolsAlgorithm::PPO, TEXT("ppo/3x2-mlp32"));
    P->Settings.PPOEpochs = 2;
    P->Settings.MaxUpdatesPerBurst = 1;
    P->Init(Definition());
    if (!Ready(P)) return false;
    auto First = std::make_shared<FGate>(EWork::Train);
    auto Later = std::make_shared<FGate>(EWork::Train);
    auto Count = std::make_shared<std::atomic<int32>>(0);
    FRLToolsWorldTestAccess::Gate(F.Service(), [First, Later, Count](EWork Kind)
    {
        if (Kind != EWork::Train) return;
        auto Gate = ++(*Count) == 1 ? First : Later;
        ++Gate->Entered;
        Gate->Event->Wait(15000);
    });
    ON_SCOPE_EXIT { First->Open(); Later->Open(); };
    for (int32 I = 0; I < 64; ++I)
    {
        FAdaptiveDecision D;
        if (!Decision(P, Observation(), D)) return false;
        TestEqual(TEXT("Rollout admission"), P->SubmitExperience(Outcome(D, Observation())), EAdaptiveSubmitResult::Accepted);
    }
    if (!TestTrue(TEXT("First storage/update burst held"), Wait([First] { return First->Entered > 0; }))) return false;
    First->Open();
    if (!TestTrue(TEXT("PPO epoch continuation held"), Wait([Later] { return Later->Entered > 0; }))) return false;
    TestEqual(TEXT("One epoch still pending"), Stats(P).PendingUpdates, int64(1));
    TestEqual(TEXT("Old actor still published"), Stats(P).Version, int64(0));
    FAdaptiveDecision D;
    TestTrue(TEXT("Inference remains available during PPO update"), Decision(P, Observation(), D));
    const auto E = Outcome(D, Observation());
    TestEqual(TEXT("Updating PPO closes admission temporarily"), P->SubmitExperience(E), EAdaptiveSubmitResult::NotReady);
    TestEqual(TEXT("NotReady retains original decision"), Stats(P).RetainedDecisions, 1);
    TestEqual(TEXT("Retry does not report a consumed decision"), P->SubmitExperience(E), EAdaptiveSubmitResult::NotReady);
    Later->Open();
    TestTrue(TEXT("Completed rollout publishes"), Wait([P] { return Stats(P).Version > 0; }));
    TestEqual(TEXT("On-policy publication then makes old outcome stale"), P->SubmitExperience(E), EAdaptiveSubmitResult::Rejected);
    TestTrue(TEXT("Stale policy explained"), P->GetLastError().Contains(TEXT("stale")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsPolicyGarbageCollectionTest, "Schola.RLTools.Policy.CollectedBindingSkipsDelayedCompletion", Flags)
bool FRLToolsPolicyGarbageCollectionTest::RunTest(const FString&)
{
    FWorld F;
    auto Gate = std::make_shared<FGate>(EWork::Create);
    Gate->Install(F.Service());
    ON_SCOPE_EXIT { Gate->Open(); };
    auto* P = F.Policy();
    P->Init(Definition());
    if (!TestTrue(TEXT("Creation in flight"), Wait([Gate] { return Gate->Entered > 0; }))) return false;
    TWeakObjectPtr<URLToolsNPCPolicy> Weak(P);
    F.Policies.Remove(P);
    P->RemoveFromRoot();
    P->MarkAsGarbage();
    CollectGarbage(RF_NoFlags);
    TestFalse(TEXT("Policy collected"), Weak.IsValid());
    TestEqual(TEXT("Collected policy released binding"), F.Service()->GetExecutionStats().Bindings, 0);
    Gate->Open();
    TestTrue(TEXT("Native construction eventually reclaimed without touching UObject"), Wait([&F]
    { return F.Service()->GetExecutionStats().DisposedLearners == 1; }));
    return true;
}
#endif

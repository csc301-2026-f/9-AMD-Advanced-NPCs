// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "OnlineSACPolicy.h"
#include "ChaseSACExperiment.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"
#include "Spaces/DiscreteSpace.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
constexpr EAutomationTestFlags TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

FInteractionDefinition SACDefinition(int32 ObservationSize = 6, int32 ActionSize = 2, float ActionLow = -1.0f, float ActionHigh = 1.0f)
{
    FInteractionDefinition Definition;
    Definition.AgentType = TEXT("OnlineSACTest");
    Definition.ObsSpaceDefn.InitializeAs<FBoxSpace>();
    Definition.ObsSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(FBoxSpaceDimension(-10.0f, 10.0f), ObservationSize);
    Definition.ActionSpaceDefn.InitializeAs<FBoxSpace>();
    Definition.ActionSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(FBoxSpaceDimension(ActionLow, ActionHigh), ActionSize);
    return Definition;
}

TInstancedStruct<FPoint> Box(const float* Values, int32 Count)
{
    TInstancedStruct<FPoint> Point;
    Point.InitializeAs<FBoxPoint>(TArray<float>(Values, Count));
    return Point;
}

UOnlineSACPolicy* NewPolicy(int32 UpdateEvery, int32 Warmup, int32 Seed = 1)
{
    UOnlineSACPolicy* Policy = NewObject<UOnlineSACPolicy>();
    Policy->UpdateEvery = UpdateEvery;
    Policy->WarmupExperiences = Warmup;
    Policy->Seed = Seed;
    return Policy;
}

struct FPolicyStep
{
    bool bDecided = false;
    EOnlineSACSubmitResult Submitted = EOnlineSACSubmitResult::NotReady;
    online_sac::ChaseStep Outcome;
    FAdaptiveDecision Decision;
};
FPolicyStep StepThroughPolicy(UOnlineSACPolicy& Policy, online_sac::ChaseTask& Task, bool bSubmit = true)
{
    FPolicyStep Step;
    float Observation[online_sac::ObservationDim], Next[online_sac::ObservationDim];
    Task.Observe(Observation);
    int64 Ticket = INDEX_NONE;
    if (!Policy.RequestDecision(Box(Observation, online_sac::ObservationDim), Ticket)
        || Policy.TryGetDecision(Ticket, Step.Decision) != EAdaptiveRequestStatus::Ready)
    {
        return Step;
    }
    Step.bDecided = true;
    const TArray<float>& Action = Step.Decision.Action.Get<FBoxPoint>().Values;
    Step.Outcome = Task.Step(Action.GetData());
    Task.Observe(Next);
    if (bSubmit)
    {
        FAdaptiveExperience Experience;
        Experience.SessionId = Step.Decision.SessionId;
        Experience.AgentId = Step.Decision.AgentId;
        Experience.EpisodeId = Step.Decision.EpisodeId;
        Experience.DecisionId = Step.Decision.DecisionId;
        Experience.PolicyVersion = Step.Decision.PolicyVersion;
        Experience.Observation = Box(Observation, online_sac::ObservationDim);
        Experience.ExecutedAction = Step.Decision.Action;
        Experience.Reward = Step.Outcome.reward;
        Experience.NextObservation = Box(Next, online_sac::ObservationDim);
        Experience.bTerminated = Step.Outcome.terminated;
        Experience.bTruncated = Step.Outcome.truncated && !Step.Outcome.terminated;
        Step.Submitted = Policy.SubmitExperience(Experience);
    }
    if (Step.Outcome.truncated)
    {
        Task.Reset();
        Policy.ResetEpisode();
    }
    return Step;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACPolicyTicketTest, "ScholaOnlineLearning.SAC.Policy.TicketCompletes", TestFlags)
bool FOnlineSACPolicyTicketTest::RunTest(const FString& Parameters)
{
    UOnlineSACPolicy* Policy = NewPolicy(50, 1000);
    TestTrue(TEXT("Init accepts 6-value observation and 2-value action Box spaces"), Policy->Init(SACDefinition(6, 2, -2.0f, 4.0f)));
    TestEqual(TEXT("Init starts a local Ready session (no Python)"), Policy->GetSessionState(), EAdaptiveSessionState::Ready);
    const float Observation[6] = {0.2f, -0.3f, 0.0f, 0.1f, 0.5f, -0.5f};
    int64 Ticket = INDEX_NONE;
    TestTrue(TEXT("RequestDecision accepted"), Policy->RequestDecision(Box(Observation, 6), Ticket));
    FAdaptiveDecision Decision;
    TestEqual(TEXT("Ticket completed synchronously during dispatch"), Policy->TryGetDecision(Ticket, Decision), EAdaptiveRequestStatus::Ready);
    TestEqual(TEXT("Decision matches its ticket"), Decision.DecisionId, Ticket);
    TestEqual(TEXT("Policy version is the initial snapshot"), Decision.PolicyVersion, int64(0));
    const TArray<float>* Values = Decision.Action.IsValid() && Decision.Action.GetScriptStruct() == FBoxPoint::StaticStruct()
        ? &Decision.Action.Get<FBoxPoint>().Values : nullptr;
    if (TestNotNull(TEXT("Action is an FBoxPoint"), Values) && TestEqual(TEXT("Action has 2 values"), Values->Num(), 2))
    {
        AddInfo(FString::Printf(TEXT("action = (%.3f, %.3f) in [-2, 4]"), (*Values)[0], (*Values)[1]));
        for (float Value : *Values)
        {
            TestTrue(TEXT("Action value finite and inside the declared bounds"), FMath::IsFinite(Value) && Value >= -2.0f && Value <= 4.0f);
        }
    }
    int64 Second = INDEX_NONE;
    TestTrue(TEXT("A second request is accepted after the first is Ready"), Policy->RequestDecision(Box(Observation, 6), Second) && Second == Ticket + 1);
    Policy->ShutdownLearner();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACPolicyWrongSizeTest, "ScholaOnlineLearning.SAC.Policy.WrongSizeRefused", TestFlags)
bool FOnlineSACPolicyWrongSizeTest::RunTest(const FString& Parameters)
{
    UOnlineSACPolicy* Policy = NewPolicy(50, 1000);
    TestFalse(TEXT("5-value observation refused"), Policy->Init(SACDefinition(5, 2)));
    TestTrue(TEXT("Error names the compiled sizes"), Policy->GetLearnerError().Contains(TEXT("6-value Box observation")));
    AddInfo(Policy->GetLearnerError());
    TestFalse(TEXT("Not initialized after refusal"), Policy->IsInitialized());
    TestEqual(TEXT("No learner after refusal"), Policy->GetPolicyVersion(), int64(INDEX_NONE));
    TestFalse(TEXT("3-value action refused"), Policy->Init(SACDefinition(6, 3)));
    FInteractionDefinition Discrete = SACDefinition();
    Discrete.ActionSpaceDefn.InitializeAs<FDiscreteSpace>();
    TestFalse(TEXT("Non-Box action refused (by the base class)"), Policy->Init(Discrete));
    AddInfo(Policy->GetLearnerError());
    int64 Ticket = INDEX_NONE;
    const float Observation[6] = {};
    TestFalse(TEXT("No decisions without a valid Init"), Policy->RequestDecision(Box(Observation, 6), Ticket));
    TestTrue(TEXT("A correct definition still initializes afterwards"), Policy->Init(SACDefinition()));
    Policy->ShutdownLearner();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACPolicyExperienceTest, "ScholaOnlineLearning.SAC.Policy.ExperiencesReachLearner", TestFlags)
bool FOnlineSACPolicyExperienceTest::RunTest(const FString& Parameters)
{
    UOnlineSACPolicy* Policy = NewPolicy(25, 50);
    TestTrue(TEXT("Init"), Policy->Init(SACDefinition()));
    online_sac::ChaseTask Task(7);
    int32 Accepted = 0;
    for (int32 Index = 0; Index < 200; ++Index)
    {
        const FPolicyStep Step = StepThroughPolicy(*Policy, Task);
        Accepted += Step.Submitted == EOnlineSACSubmitResult::Accepted;
        Policy->WaitForLearner(25, 30000);
    }
    TestTrue(TEXT("Learner caught up"), Policy->WaitForLearner(0, 60000));
    const online_sac::OnlineSACStats Stats = Policy->GetLearnerStats();
    AddInfo(FString::Printf(TEXT("accepted=%d stored=%llu updates=%llu version=%u rounds=%u"), Accepted, Stats.stored, Stats.updates, Stats.version, Stats.rounds));
    TestEqual(TEXT("All 200 submitted experiences accepted"), Accepted, 200);
    TestEqual(TEXT("All 200 reached the replay buffer"), Stats.stored, uint64(200));
    TestTrue(TEXT("SAC updates ran"), Stats.updates > 0);
    TestTrue(TEXT("Policy version increased"), Policy->GetPolicyVersion() > 0);
    const FPolicyStep Next = StepThroughPolicy(*Policy, Task, false);
    TestTrue(TEXT("New decisions carry the new policy version"), Next.bDecided && Next.Decision.PolicyVersion == Policy->GetPolicyVersion());
    FAdaptiveExperience Bad;
    Bad.SessionId = Next.Decision.SessionId;
    Bad.PolicyVersion = 0;
    float Observation[6] = {0, 0, 0, 0, 0, 0};
    Bad.Observation = Box(Observation, 6);
    Bad.NextObservation = Box(Observation, 6);
    const float OutOfRange[2] = {1.5f, 0.0f};
    Bad.ExecutedAction = Box(OutOfRange, 2);
    TestEqual(TEXT("Action outside the action space rejected"), Policy->SubmitExperience(Bad), EOnlineSACSubmitResult::Rejected);
    const float Fine[2] = {0.5f, 0.0f};
    Bad.ExecutedAction = Box(Fine, 2);
    Observation[1] = std::numeric_limits<float>::quiet_NaN();
    Bad.Observation = Box(Observation, 6);
    TestEqual(TEXT("NaN observation rejected"), Policy->SubmitExperience(Bad), EOnlineSACSubmitResult::Rejected);
    Bad.Observation = Box(Fine, 2);
    TestEqual(TEXT("Wrong-size observation rejected"), Policy->SubmitExperience(Bad), EOnlineSACSubmitResult::Rejected);
    Bad.Observation = Bad.NextObservation;
    Bad.SessionId = FGuid::NewGuid();
    TestEqual(TEXT("Experience from another session rejected"), Policy->SubmitExperience(Bad), EOnlineSACSubmitResult::Rejected);
    const online_sac::OnlineSACStats After = Policy->GetLearnerStats();
    TestEqual(TEXT("Rejections did not reach the learner queue"), After.accepted, Stats.accepted);
    Policy->ShutdownLearner();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACPolicyShutdownTest, "ScholaOnlineLearning.SAC.Policy.ShutdownWhileLearning", TestFlags)
bool FOnlineSACPolicyShutdownTest::RunTest(const FString& Parameters)
{
    for (int32 Variant = 0; Variant < 2; ++Variant)
    {
        const TCHAR* Name = Variant == 0 ? TEXT("ShutdownLearner") : TEXT("BeginDestroy");
        UOnlineSACPolicy* Policy = NewPolicy(2000, 100);
        TestTrue(TEXT("Init"), Policy->Init(SACDefinition()));
        online_sac::ChaseTask Task(11);
        for (int32 Index = 0; Index < 2000; ++Index)
        {
            StepThroughPolicy(*Policy, Task);
        }
        const double WaitStart = FPlatformTime::Seconds();
        while (!Policy->GetLearnerStats().training && FPlatformTime::Seconds() - WaitStart < 30.0)
        {
            FPlatformProcess::Sleep(0.001f);
        }
        TestTrue(FString::Printf(TEXT("%s: learner is mid-round"), Name), Policy->GetLearnerStats().training);
        FPlatformProcess::Sleep(0.2f);
        const double Start = FPlatformTime::Seconds();
        if (Variant == 0)
        {
            Policy->ShutdownLearner();
        }
        else
        {
            Policy->ConditionalBeginDestroy();
        }
        const double Seconds = FPlatformTime::Seconds() - Start;
        const online_sac::OnlineSACStats Stats = Policy->GetLearnerStats();
        AddInfo(FString::Printf(TEXT("%s during a round returned in %.3fs; running=%d version=%u"), Name, Seconds, Stats.running, Stats.version));
        TestTrue(FString::Printf(TEXT("%s returns within 1 s"), Name), Seconds < 1.0);
        TestFalse(FString::Printf(TEXT("%s stopped the learner thread"), Name), Stats.running);
        TestEqual(FString::Printf(TEXT("%s leaves the session Stopped"), Name), Policy->GetSessionState(), EAdaptiveSessionState::Stopped);
        if (Variant == 0)
        {
            int64 Ticket = INDEX_NONE;
            float Observation[6] = {};
            TestFalse(TEXT("No decisions after ShutdownLearner"), Policy->RequestDecision(Box(Observation, 6), Ticket));
            TestEqual(TEXT("No experiences after ShutdownLearner"), Policy->SubmitExperience(FAdaptiveExperience()), EOnlineSACSubmitResult::NotReady);
        }
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACPolicyChaseTest, "ScholaOnlineLearning.SAC.Policy.ChaseLearns", TestFlags)
bool FOnlineSACPolicyChaseTest::RunTest(const FString& Parameters)
{
    constexpr double PassCatchRate = 0.80;
    constexpr double PassMaxStepsToCatch = 100.0;
    constexpr int32 TrainSteps = 20000, EvalEvery = 2000;
    UOnlineSACPolicy* Policy = NewPolicy(50, 1000, 1);
    TestTrue(TEXT("Init"), Policy->Init(SACDefinition()));
    online_sac::ChaseTask Task(1);
    const online_sac::ChaseSACOptions Eval;
    auto Evaluate = [&]
    {
        Policy->SetFrozen(true);
        const online_sac::EvalResult Result = online_sac::EvaluatePolicy([&](const float* Observation, float* Action)
        {
            int64 Ticket = INDEX_NONE;
            FAdaptiveDecision Decision;
            if (Policy->RequestDecision(Box(Observation, 6), Ticket) && Policy->TryGetDecision(Ticket, Decision) == EAdaptiveRequestStatus::Ready)
            {
                Action[0] = Decision.Action.Get<FBoxPoint>().Values[0];
                Action[1] = Decision.Action.Get<FBoxPoint>().Values[1];
            }
            else
            {
                Action[0] = Action[1] = 0.0f;
            }
        }, online_sac::ChasePattern::Bounce, Eval.evalEpisodes, Eval.evalSeed);
        Policy->SetFrozen(false);
        return Result;
    };
    FString Csv = TEXT("step,policy_version,bounce_catch_rate,bounce_mean_steps_to_catch,seconds\n");
    const double Start = FPlatformTime::Seconds();
    online_sac::EvalResult Result = Evaluate();
    const double Untrained = Result.catchRate;
    Csv += FString::Printf(TEXT("0,%lld,%.3f,%.1f,0\n"), Policy->GetPolicyVersion(), Result.catchRate, Result.meanStepsToCatch);
    int32 Rejected = 0;
    for (int32 Step = 1; Step <= TrainSteps; ++Step)
    {
        const FPolicyStep Outcome = StepThroughPolicy(*Policy, Task);
        Rejected += Outcome.Submitted != EOnlineSACSubmitResult::Accepted;
        Policy->WaitForLearner(Policy->UpdateEvery, 60000);
        if (Step % EvalEvery == 0)
        {
            Result = Evaluate();
            Csv += FString::Printf(TEXT("%d,%lld,%.3f,%.1f,%.1f\n"), Step, Policy->GetPolicyVersion(), Result.catchRate, Result.meanStepsToCatch, FPlatformTime::Seconds() - Start);
        }
    }
    FFileHelper::SaveStringToFile(Csv, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSAC"), TEXT("policy-chase-seed1.csv")));
    AddInfo(FString::Printf(TEXT("seed 1 through UOnlineSACPolicy: catch_rate %.3f -> %.3f, steps_to_catch %.1f, version %lld, %.1fs"),
        Untrained, Result.catchRate, Result.meanStepsToCatch, Policy->GetPolicyVersion(), FPlatformTime::Seconds() - Start));
    TestEqual(TEXT("Every experience accepted"), Rejected, 0);
    TestTrue(FString::Printf(TEXT("catch rate %.3f >= %.2f"), Result.catchRate, PassCatchRate), Result.catchRate >= PassCatchRate);
    TestTrue(FString::Printf(TEXT("steps to catch %.1f <= %.0f"), Result.meanStepsToCatch, PassMaxStepsToCatch), Result.meanStepsToCatch <= PassMaxStepsToCatch);
    Policy->ShutdownLearner();
    return true;
}
#endif

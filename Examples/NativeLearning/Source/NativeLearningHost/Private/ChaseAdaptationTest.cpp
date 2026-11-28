// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// NativeLearning.ChaseAdaptation: the D3 chase adaptation test run through the shipped Unreal path,
// UAdaptiveStepper + URLToolsNPCPolicy (project profile sac/project/chase-sac, replay 10k), instead of the
// example-only OnlineSACLearner. The engine-free twin is Tools/ScholaRLToolsNative/ChaseAdaptation.cpp.
//
// Protocol: 150 episodes on rightturn, capture a snapshot, switch to corner and keep learning; evaluate the
// deterministic policy every 5 episodes on chase_sim's 100 fixed starts (eval_seed 4242).
// Criteria (deliverables/D3/adaptive-npc-evidence.md, never loosened):
//   1. >= 0.50 on rightturn at the switch     2. <= 0.80 on corner right after the switch
//   3. >= 0.80 on corner within 20,000 moves  4. snapshot > 0.20 below the learner at the first eval >= 20,000 moves
// Seed: -ChaseSeed=N on the command line (default 1). Run seeds 1-3 for criterion 5.
// Takes several minutes per seed; it is excluded from Test-NativeLearning.ps1's default filter.
#include "ChaseTestAgent.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/ScopeExit.h"
#include "Async/TaskGraphInterfaces.h"
#include "Engine/World.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"
#include "Policies/AdaptivePolicySnapshot.h"
#include "Policies/RLToolsNPCPolicy.h"
#include "Steppers/AdaptiveStepper.h"
#include "UObject/StrongObjectPtr.h"

using namespace online_sac;

namespace
{
constexpr uint32 ObservationCount = ChaseObservationSize, ActionCount = ChaseActionSize;
// Seed families from ChaseSession.cpp, so starts match chase_sim exactly.
constexpr uint64 TrainFamily = 1, EvalFamily = 2;
uint64 SeedFor(uint32 Seed, uint64 Family, uint32 Index)
{ return (static_cast<uint64>(Seed) << 32) ^ (Family * 0x9E3779B97F4A7C15ull) ^ Index; }

TInstancedStruct<FPoint> BoxPoint(const float* Values, int32 Count)
{
    TInstancedStruct<FPoint> Point;
    Point.InitializeAs<FBoxPoint>(TArray<float>(Values, Count), TArray<int>{1, Count});
    return Point;
}

TInstancedStruct<FSpace> UnitBox(int32 Count)
{
    TInstancedStruct<FSpace> Space;
    Space.InitializeAs<FBoxSpace>();
    Space.GetMutable<FBoxSpace>().Dimensions.Init(FBoxSpaceDimension(-1, 1), Count);
    Space.GetMutable<FBoxSpace>().Shape = {1, Count};
    return Space;
}

template <typename Predicate>
bool Wait(Predicate Ready, double Timeout = 60)
{
    const double Deadline = FPlatformTime::Seconds() + Timeout;
    do
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (Ready()) return true;
        FPlatformProcess::SleepNoStats(0.0005f);
    } while (FPlatformTime::Seconds() < Deadline);
    return false;
}

/** Lockstep like chase_sim: the next move waits until every submitted record has been trained on. */
bool Drained(URLToolsNPCPolicy* Policy)
{
    return Wait([Policy] {
        FRLToolsLearningStats S;
        return Policy->GetLearningStats(S) && S.Queued == 0 && S.PendingUpdates == 0 && !S.bTraining;
    });
}

double CatchRate(UAdaptivePolicySnapshot* Snapshot, const ChaseConfig& Config, RunnerPattern Pattern, uint32 Episodes)
{
    uint32 Caught = 0;
    float Observation[ObservationCount];
    for (uint32 I = 0; I < Episodes; ++I)
    {
        ChaseRng StartRng(SeedFor(Config.eval_seed, EvalFamily, I));
        ChaseWorld World;
        World.Reset(Config, Pattern, StartRng);
        for (;;)
        {
            World.Observe(Observation);
            TInstancedStruct<FPoint> Action;
            float Values[ActionCount] = {0, 0};
            if (Snapshot->Evaluate(BoxPoint(Observation, ObservationCount), Action))
            {
                const TArray<float>& Out = Action.Get<FBoxPoint>().Values;
                for (uint32 K = 0; K < ActionCount && K < uint32(Out.Num()); ++K) Values[K] = Out[K];
            }
            const ChaseStepOutcome Outcome = World.Step(Values);
            if (Outcome.terminated || Outcome.truncated) break;
        }
        Caught += World.State().caught ? 1 : 0;
    }
    return Episodes ? double(Caught) / Episodes : 0;
}
}

void UChaseTestAgent::Begin(uint64 TrainSeed, RunnerPattern InPattern)
{
    Rng = ChaseRng(TrainSeed);
    Pattern = InPattern;
    Moves = EpisodesStarted = 0;
    ResetEpisode_Implementation();
}

void UChaseTestAgent::Define_Implementation(FInteractionDefinition& OutDefinition)
{
    OutDefinition.AgentType = TEXT("Chaser");
    OutDefinition.ObsSpaceDefn = UnitBox(ObservationCount);
    OutDefinition.ActionSpaceDefn = UnitBox(ActionCount);
}

void UChaseTestAgent::Observe_Implementation(FInstancedStruct& OutObservations)
{
    float Values[ObservationCount];
    World.Observe(Values);
    OutObservations.InitializeAs<FBoxPoint>(TArray<float>(Values, ObservationCount), TArray<int>{1, int(ObservationCount)});
}

void UChaseTestAgent::Act_Implementation(const FInstancedStruct& InAction)
{
    float Values[ActionCount] = {0, 0};
    if (const FBoxPoint* Box = InAction.GetPtr<FBoxPoint>())
        for (uint32 K = 0; K < ActionCount && K < uint32(Box->Values.Num()); ++K) Values[K] = Box->Values[K];
    const ChaseStepOutcome Outcome = World.Step(Values);
    Reward += Outcome.reward;
    bTerminated = Outcome.terminated;
    bTruncated = Outcome.truncated;
    ++Moves;
}

float UChaseTestAgent::ConsumeReward_Implementation()
{
    const float Value = Reward;
    Reward = 0;
    return Value;
}

void UChaseTestAgent::GetEpisodeStatus_Implementation(bool& bOutTerminated, bool& bOutTruncated)
{
    bOutTerminated = bTerminated;
    bOutTruncated = bTruncated;
}

void UChaseTestAgent::ResetEpisode_Implementation()
{
    World.Reset(Config, Pattern, Rng);
    Reward = 0;
    bTerminated = bTruncated = false;
    ++EpisodesStarted;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNativeLearningChaseAdaptationTest, "NativeLearning.ChaseAdaptation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FNativeLearningChaseAdaptationTest::RunTest(const FString& Parameters)
{
    uint32 Seed = 1;
    FParse::Value(FCommandLine::Get(), TEXT("ChaseSeed="), Seed);
    constexpr int64 PretrainEpisodes = 150, ChunkEpisodes = 5, WindowMoves = 20000;
    constexpr uint32 EvalEpisodes = 100;
    AddInfo(FString::Printf(TEXT("Chase adaptation through UAdaptiveStepper + URLToolsNPCPolicy, seed %u"), Seed));

    const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    ON_SCOPE_EXIT { World->DestroyWorld(false); if (World->IsRooted()) World->RemoveFromRoot(); };

    TStrongObjectPtr<URLToolsNPCPolicy> Template(NewObject<URLToolsNPCPolicy>(World));
    Template->Algorithm = ERLToolsAlgorithm::SAC;
    Template->Configuration = TEXT("sac/project/chase-sac");
    // chase_sim's learner: lr 1e-3, 1000 random warm-up moves, 8 updates per move, one publication per move.
    Template->Settings.Seed = int32(Seed);
    Template->Settings.LearningRate = 0.001f;
    Template->Settings.LearningStarts = Template->Settings.InitialRandomSteps = 1000;
    Template->Settings.TrainFrequency = 1;
    Template->Settings.GradientSteps = 8;
    Template->Settings.MaxRecordsPerBurst = 64;
    Template->Settings.MaxUpdatesPerBurst = 64;
    Template->Settings.PublishEveryUpdates = 8;
    Template->Settings.QueueCapacity = 4096;

    TStrongObjectPtr<UChaseTestAgent> Agent(NewObject<UChaseTestAgent>());
    Agent->Begin(SeedFor(Seed, TrainFamily, 0), RunnerPattern::RightTurn);
    TStrongObjectPtr<UAdaptiveStepper> Stepper(NewObject<UAdaptiveStepper>());
    if (!TestTrue(TEXT("Stepper binds the chase agent"),
        Stepper->Init(TArray<TScriptInterface<IAgent>>{TScriptInterface<IAgent>(Agent.Get())}, TScriptInterface<IPolicy>(Template.Get()))))
        return false;
    ON_SCOPE_EXIT { Stepper->Shutdown(); };
    auto* Policy = Cast<URLToolsNPCPolicy>(Stepper->GetPolicy(0));
    if (!TestTrue(TEXT("Learner ready"), Policy && Wait([&] { return Stepper->AreAllReady(); })))
    {
        AddError(Policy ? Policy->GetLastError() : TEXT("no binding"));
        return false;
    }

    auto Capture = [&]() -> TStrongObjectPtr<UAdaptivePolicySnapshot> {
        UAdaptivePolicySnapshot* Snapshot = nullptr;
        Policy->CaptureSnapshot(Snapshot);
        return TStrongObjectPtr<UAdaptivePolicySnapshot>(Snapshot);
    };
    // Steps until the agent has started `Target` episodes (the episode in progress then is the next one).
    auto TrainUntilEpisode = [&](int64 Target) {
        while (Agent->EpisodesStarted <= Target)
        {
            Stepper->Step();
            if (!Drained(Policy)) return false;
        }
        return true;
    };

    // Pre-switch on rightturn. The pattern for the episode after the last pretrain one is set before it starts.
    if (!TestTrue(TEXT("Pretrain"), TrainUntilEpisode(PretrainEpisodes - 1))) return false;
    Agent->Pattern = RunnerPattern::Corner;
    if (!TestTrue(TEXT("Finish pretrain"), TrainUntilEpisode(PretrainEpisodes))) return false;

    // The stepper already applied the first corner move when episode 150 ended; it is counted after the switch.
    const TStrongObjectPtr<UAdaptivePolicySnapshot> Snapshot = Capture(); // immutable: later publications do not change it
    if (!TestTrue(TEXT("Snapshot captured"), Snapshot.IsValid())) return false;
    const double PreSwitch = CatchRate(Snapshot.Get(), Agent->Config, RunnerPattern::RightTurn, EvalEpisodes);
    const double AfterSwitch = CatchRate(Snapshot.Get(), Agent->Config, RunnerPattern::Corner, EvalEpisodes);
    const int64 SwitchMoves = Agent->Moves;
    AddInfo(FString::Printf(TEXT("switch at %lld moves: rightturn %.2f, corner %.2f"), SwitchMoves, PreSwitch, AfterSwitch));

    int64 RecoveredAt = 0, WindowAt = 0;
    double WindowLearner = 0, WindowSnapshot = 0;
    for (int64 Episode = PretrainEpisodes; !WindowAt; Episode += ChunkEpisodes)
    {
        if (!TestTrue(TEXT("Adapt"), TrainUntilEpisode(Episode + ChunkEpisodes))) return false;
        const int64 Since = Agent->Moves - SwitchMoves;
        auto Current = Capture();
        if (!Current.IsValid()) { AddError(TEXT("Capture failed")); return false; }
        const double Learned = CatchRate(Current.Get(), Agent->Config, RunnerPattern::Corner, EvalEpisodes);
        AddInfo(FString::Printf(TEXT("%lld moves after switch: learner %.2f"), Since, Learned));
        if (!RecoveredAt && Since <= WindowMoves && Learned >= 0.80) RecoveredAt = Since;
        if (Since >= WindowMoves)
        {
            WindowAt = Since;
            WindowLearner = Learned;
            WindowSnapshot = CatchRate(Snapshot.Get(), Agent->Config, RunnerPattern::Corner, EvalEpisodes);
        }
    }

    TestTrue(FString::Printf(TEXT("1. rightturn before switch %.2f >= 0.50"), PreSwitch), PreSwitch >= 0.50);
    TestTrue(FString::Printf(TEXT("2. corner right after switch %.2f <= 0.80"), AfterSwitch), AfterSwitch <= 0.80);
    TestTrue(FString::Printf(TEXT("3. recovered to >= 0.80 within %lld moves (at %lld)"), WindowMoves, RecoveredAt), RecoveredAt > 0);
    TestTrue(FString::Printf(TEXT("4. at %lld moves learner %.2f vs snapshot %.2f: gap > 0.20"), WindowAt, WindowLearner, WindowSnapshot),
        WindowLearner - WindowSnapshot > 0.20);
    const FAdaptiveStepperStats Stats = Stepper->GetStats();
    TestEqual(TEXT("Every outcome was admitted by the learner"), Stats.NotAccepted, int64(0));
    return true;
}
#endif

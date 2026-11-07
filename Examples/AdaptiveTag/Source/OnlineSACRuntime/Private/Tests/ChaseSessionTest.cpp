// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "ChaseSession.h"
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS
// Runs the engine-neutral ChaseSession headless inside Unreal: the same core chase_sim uses.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChaseSessionTest, "ScholaOnlineLearning.SAC.ChaseSession",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FChaseSessionTest::RunTest(const FString& Parameters)
{
    using namespace online_sac;
    ChaseLearnerConfig Learner;
    Learner.warmup_experiences = 200; // learning starts inside the first episode
    std::string Error;
    ChaseSession Session(ChaseConfig{}, Learner, RunnerPattern::Circle, &Error);
    TestTrue(TEXT("default config is valid"), Error.empty());

    // Episodes advance the state and the learner publishes new versions.
    std::vector<EpisodeSummary> Episodes;
    Session.RunEpisodes(3, [&Episodes](const EpisodeSummary& E) { Episodes.push_back(E); });
    ChaseSessionState State = Session.GetState();
    uint64_t StepSum = 0;
    for (const EpisodeSummary& E : Episodes) StepSum += E.steps;
    AddInfo(FString::Printf(TEXT("after 3 episodes: steps %llu, policy v%u, updates %llu, catch rate %.2f"),
        (unsigned long long)State.totalSteps, State.policyVersion, (unsigned long long)State.updates, State.stats.catchRate));
    TestEqual(TEXT("3 episodes reported"), (int32)Episodes.size(), 3);
    TestEqual(TEXT("episodesDone"), (int32)State.episodesDone, 3);
    TestEqual(TEXT("totalSteps equals the episode lengths"), (uint64)State.totalSteps, (uint64)StepSum);
    TestTrue(TEXT("policy version advanced"), State.policyVersion > 0);
    TestTrue(TEXT("SAC updates ran"), State.updates > 0);
    for (const EpisodeSummary& E : Episodes) TestTrue(TEXT("episode ends by catch or time-out"), E.caught || E.steps == ChaseConfig{}.max_episode_steps);

    // Pattern change keeps the weights; a frozen session does not learn.
    Session.TakeSnapshot();
    Session.SetPattern(RunnerPattern::Square);
    TestEqual(TEXT("pattern change keeps the version"), (int32)Session.GetState().policyVersion, (int32)State.policyVersion);
    Session.SetFrozen(true);
    const unsigned FrozenVersion = Session.GetState().policyVersion;
    Session.RunEpisodes(1);
    TestEqual(TEXT("frozen: version unchanged"), (int32)Session.GetState().policyVersion, (int32)FrozenVersion);
    TestEqual(TEXT("frozen: episodes still advance"), (int32)Session.GetState().episodesDone, 4);
    Session.SetFrozen(false);

    // Evaluation covers all four policies on fixed starts.
    const ChaseEvaluation Eval = Session.Evaluate(5);
    TestTrue(TEXT("snapshot evaluated"), Eval.hasSnapshot && Eval.snapshot.episodes == 5);
    TestTrue(TEXT("current, greedy, random evaluated"), Eval.current.episodes == 5 && Eval.greedy.episodes == 5 && Eval.random.episodes == 5);
    AddInfo(FString::Printf(TEXT("eval square (5 starts): current %.2f snapshot %.2f greedy %.2f random %.2f"),
        Eval.current.catchRate, Eval.snapshot.catchRate, Eval.greedy.catchRate, Eval.random.catchRate));

    // Invalid config is refused and changes nothing.
    ChaseConfig Bad = Session.Config();
    Bad.circle_radius = 80;
    TestFalse(TEXT("impossible circle_radius refused"), Session.SetConfig(Bad).empty());
    TestEqual(TEXT("config unchanged"), Session.Config().circle_radius, ChaseConfig{}.circle_radius);

    // Playback (watch) does not change training: same seed with and without a PlayEpisode in between.
    ChaseSession A(ChaseConfig{}, Learner, RunnerPattern::Circle), B(ChaseConfig{}, Learner, RunnerPattern::Circle);
    A.RunEpisodes(1);
    const ChaseTrajectory Played = A.PlayEpisode(0);
    A.Evaluate(2);
    A.RunEpisodes(1);
    B.RunEpisodes(2);
    TestTrue(TEXT("playback produced frames"), Played.frames.size() > 1);
    TestEqual(TEXT("playback/eval do not change training: steps"), (uint64)A.GetState().totalSteps, (uint64)B.GetState().totalSteps);
    TestEqual(TEXT("playback/eval do not change training: version"), (int32)A.GetState().policyVersion, (int32)B.GetState().policyVersion);
    TestEqual(TEXT("playback/eval do not change training: final distance"), A.GetState().stats.meanFinalDistance, B.GetState().stats.meanFinalDistance);

    // Reset gives fresh weights and clears counters.
    Session.Reset();
    State = Session.GetState();
    TestTrue(TEXT("reset clears counters and version"), State.episodesDone == 0 && State.totalSteps == 0 && State.policyVersion == 0);
    TestTrue(TEXT("reset keeps the snapshot"), State.hasSnapshot);
    return true;
}
#endif

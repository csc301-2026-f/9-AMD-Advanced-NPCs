// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Learning/RLToolsLearner.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <atomic>

using namespace Schola::RLTools;
static void Require(bool Condition, const char* Message)
{
    if (!Condition) { std::cerr << "FAIL: " << Message << std::endl; std::abort(); }
}
static std::vector<float> Observation(unsigned Count, unsigned Step)
{
    std::vector<float> Values(Count);
    for (unsigned Index = 0; Index < Count; ++Index) Values[Index] = std::sin((Step + Index) * 0.1f);
    return Values;
}
static void Drain(FLearner& Learner)
{
    unsigned Bursts = 0;
    while (Learner.Train()) Require(++Bursts < 10000, "training must make bounded progress");
}
static void AlgorithmTest(const FProfile& Profile)
{
    std::string Error;
    FSettings Settings;
    Settings.PublishEveryUpdates = 1;
    auto Learner = FLearner::Create(Profile.Id, Settings, Error);
    Require(Learner != nullptr, Error.c_str());
    const auto Frozen = Learner->CapturePolicy();
    std::vector<float> Original, CapturedAgain;
    const auto Probe = Observation(Profile.Observations, 7);
    Require(Frozen->Evaluate(Probe, Original), "initial inference");
    for (unsigned Round = 0; Round < 3; ++Round)
    {
        for (unsigned Index = 0; Index < 64; ++Index)
        {
            FDecision Decision;
            Require(Learner->Decide(Observation(Profile.Observations, Index), 0, Round, false, Decision), "decision");
            float Reward = 1;
            for (float Action : Decision.GetAction()) Reward -= (Action - 0.5f) * (Action - 0.5f);
            Require(Learner->Submit(Decision, Decision.GetAction(), Reward,
                Observation(Profile.Observations, Index + 1), Index == 63, false) == ESubmitResult::Accepted, "admission");
        }
        Drain(*Learner);
    }
    const auto Stats = Learner->ReadStats();
    Require(Stats.Stored == 192 && Stats.Updates > 0 && Stats.Version > 0, "real data and updates");
    Require(Frozen->Evaluate(Probe, CapturedAgain) && Original == CapturedAgain, "snapshot remains immutable");
    std::vector<float> Updated;
    Require(Learner->CapturePolicy()->Evaluate(Probe, Updated) && Updated != Original, "updated actor changes inference");
    if (Profile.Algorithm == EAlgorithm::TD3)
        Require(Stats.ActorUpdates == (Stats.Updates + 1) / 2, "TD3 policy delay");
    if (Profile.Algorithm == EAlgorithm::PPO)
        Require(Stats.Updates == 12 && Stats.RolloutSize == 0, "PPO epochs and consumed rollouts");
    else Require(Stats.ReplaySize == 192, "replay occupancy");

    FDecision Old;
    Require(Learner->Decide(Probe, 0, 4, false, Old), "old decision");
    Learner->SetLearningEnabled(false);
    Require(Learner->Submit(Old, Old.GetAction(), 1, Probe, false, false) == ESubmitResult::Frozen, "pause admission");
    std::vector<float> Paused;
    Require(Learner->CapturePolicy()->Evaluate(Probe, Paused), "inference continues while paused");
    Require(!Learner->Train() && Learner->ReadStats().Updates == Stats.Updates, "pause stops updates");
    Learner->SetLearningEnabled(true);
    Learner->Reset(2);
    Require(Learner->Submit(Old, Old.GetAction(), 1, Probe, false, false) == ESubmitResult::Rejected, "reset retires old decisions");
    Require(Frozen->Evaluate(Probe, CapturedAgain) && Original == CapturedAgain, "snapshot survives reset");
    Learner->Stop();
    Require(!Learner->Decide(Probe, 0, 0, false, Old), "stop refuses decisions");
    Require(Learner->Submit(Old, Old.GetAction(), 1, Probe, false, false) == ESubmitResult::NotReady, "stop closes admission");
    std::cout << Profile.Id << ": data=" << Stats.Stored << " updates=" << Stats.Updates
        << " actor=" << Stats.ActorUpdates << " versions=" << Stats.Version << std::endl;
}
static void AdmissionTest()
{
    std::string Error;
    FSettings Settings;
    Settings.QueueCapacity = 1;
    auto Learner = FLearner::Create("sac/3x2-mlp32", Settings, Error);
    auto Other = FLearner::Create("sac/3x2-mlp32", Settings, Error);
    const std::vector<float> Obs(3, 0);
    FDecision First, Second;
    Require(Learner->Decide(Obs, 3, 0, false, First), "first decision");
    Require(Other->Submit(First, First.GetAction(), 1, Obs, false, false) == ESubmitResult::Rejected, "cross-learner isolation");
    Require(Learner->Submit(First, First.GetAction(), 1, Obs, false, false) == ESubmitResult::Accepted, "first admission");
    Require(Learner->Decide(Obs, 3, 0, false, Second), "second decision");
    Require(Learner->Submit(Second, Second.GetAction(), 1, Obs, false, false) == ESubmitResult::Dropped, "queue overflow");
    Drain(*Learner);
    Require(Learner->Submit(First, First.GetAction(), 1, Obs, false, false) == ESubmitResult::Rejected, "duplicate outcome");
    Require(Learner->Submit(Second, Second.GetAction(), 1, Obs, false, true) == ESubmitResult::Accepted, "overflow preserves unsubmitted decision");
    FSettings Bad;
    Bad.LearningRate = -1;
    Require(!FLearner::Create("sac/3x2-mlp32", Bad, Error) && !Error.empty(), "invalid settings");
    Require(!FLearner::Create("ppo/missing", Settings, Error) && !Error.empty(), "missing configuration");
    auto PPO = FLearner::Create("ppo/3x2-mlp32", Settings, Error);
    FDecision Adjusted;
    Require(PPO->Decide(Obs, 0, 0, false, Adjusted), "PPO decision");
    auto Action = Adjusted.GetAction();
    Action[0] = Action[0] > 0 ? -0.5f : 0.5f;
    Require(PPO->Submit(Adjusted, Action, 1, Obs, false, false) == ESubmitResult::Rejected, "PPO adjusted actions require valid likelihood");
}
static void ConcurrentTest()
{
    std::string Error;
    FSettings Settings;
    Settings.MaxUpdatesPerBurst = 1;
    auto Learner = FLearner::Create("sac/3x2-mlp32", Settings, Error);
    std::atomic<bool> Done{false};
    std::thread Worker([&] { while (!Done) { Learner->Train(); std::this_thread::yield(); } Drain(*Learner); });
    for (unsigned Step = 0; Step < 256; ++Step)
    {
        FDecision Decision;
        const auto Obs = Observation(3, Step);
        Require(Learner->Decide(Obs, Step % 4, 0, false, Decision), "concurrent inference");
        const auto Result = Learner->Submit(Decision, Decision.GetAction(), 1, Observation(3, Step + 1), false, false);
        Require(Result == ESubmitResult::Accepted, "concurrent admission");
    }
    Done = true;
    Worker.join();
    Require(Learner->ReadStats().Stored == 256 && Learner->ReadStats().Updates > 0, "concurrent worker progress");
}
static void PreprocessingAndIdentityTest()
{
    std::string Error;
    FSettings Settings;
    Settings.ObservationMean = {1, 2, 3};
    Settings.ObservationStd = {2, 4, 8};
    auto Normalized = FLearner::Create("sac/3x2-mlp32", Settings, Error);
    auto Plain = FLearner::Create("sac/3x2-mlp32", {}, Error);
    std::vector<float> Left, Right;
    Require(Normalized->CapturePolicy()->Evaluate({3, 6, 11}, Left), "fixed preprocessing");
    Require(Plain->CapturePolicy()->Evaluate({1, 1, 1}, Right) && Left == Right, "preprocessing preserves declared feature order");
    Settings.ObservationStd[1] = 0;
    Require(!FLearner::Create("sac/3x2-mlp32", Settings, Error), "zero normalization deviation");
    FSettings NoNoise;
    NoNoise.ExplorationNoise = 0;
    NoNoise.InitialRandomSteps = 0;
    auto TD3 = FLearner::Create("td3/3x2-mlp32", NoNoise, Error);
    FDecision TD3Decision;
    Require(TD3->Decide({0.2f, 0.3f, 0.4f}, 0, 0, false, TD3Decision), "TD3 zero-noise decision");
    TD3->CapturePolicy()->Evaluate({0.2f, 0.3f, 0.4f}, Right);
    Require(TD3Decision.GetAction() == Right, "TD3 exploration is applied once and honors zero noise");
    auto A = FLearner::Create("sac/3x2-mlp32", {}, Error);
    auto B = FLearner::Create("sac/3x2-mlp32", {}, Error);
    for (unsigned Step = 0; Step < 4; ++Step)
    {
        FDecision One, Two, Other;
        Require(A->Decide({0, 0, 0}, 7, 0, false, One), "agent RNG A");
        Require(B->Decide({0, 0, 0}, 9, 0, false, Other), "unrelated agent RNG");
        Require(B->Decide({0, 0, 0}, 7, 0, false, Two), "agent RNG B");
        Require(One.GetAction() == Two.GetAction(), "agent sampling is independent of other agent calls");
    }
}
static void PPOBoundaryTest()
{
    std::string Error;
    FSettings Settings;
    Settings.MaxUpdatesPerBurst = 1;
    Settings.PPOEpochs = 3;
    auto Learner = FLearner::Create("ppo/3x2-mlp32", Settings, Error);
    const std::vector<float> Obs = {0.3f, -0.4f, 0.5f};
    FDecision Stale;
    for (unsigned Index = 0; Index < 65; ++Index)
    {
        FDecision Decision;
        Learner->Decide(Obs, 1, 0, false, Decision);
        if (Index == 64) Stale = Decision;
        Require(Learner->Submit(Decision, Decision.GetAction(), 1, Obs, false, Index == 63) == ESubmitResult::Accepted, "PPO rollout admission");
    }
    Require(Learner->Train(), "PPO remaining epochs");
    Require(Learner->ReadStats().Version == 0 && Learner->ReadStats().Updates == 1, "PPO bounds one epoch and delays publication");
    FDecision DuringUpdate;
    Require(Learner->Decide(Obs, 1, 0, false, DuringUpdate), "PPO inference during updates");
    Require(Learner->Submit(DuringUpdate, DuringUpdate.GetAction(), 1, Obs, false, false) == ESubmitResult::NotReady, "PPO collection closes during updates");
    Learner->SetLearningEnabled(false);
    Require(!Learner->Train(), "PPO pause between epochs");
    Learner->SetLearningEnabled(true);
    Drain(*Learner);
    const auto Stats = Learner->ReadStats();
    Require(Stats.Version == 1 && Stats.Updates == 3 && Stats.Stale == 1, "PPO publication and retired old queue records");
    Require(Learner->Submit(DuringUpdate, DuringUpdate.GetAction(), 1, Obs, false, false) == ESubmitResult::Rejected, "stale unsubmitted policy outcome");
    auto Terminal = FLearner::Create("ppo/3x2-mlp32", Settings, Error);
    auto Truncated = FLearner::Create("ppo/3x2-mlp32", Settings, Error);
    for (unsigned Index = 0; Index < 64; ++Index)
    {
        FDecision T, U;
        Terminal->Decide(Obs, 0, Index, false, T);
        Truncated->Decide(Obs, 0, Index, false, U);
        Terminal->Submit(T, T.GetAction(), 0, Obs, true, false);
        Truncated->Submit(U, U.GetAction(), 0, Obs, false, true);
    }
    Drain(*Terminal);
    Drain(*Truncated);
    float TerminalValue = 0, TruncatedValue = 0;
    Require(Terminal->CapturePolicy()->EvaluatePrepared(Obs, TerminalValue), "terminal value inference");
    Require(Truncated->CapturePolicy()->EvaluatePrepared(Obs, TruncatedValue), "truncated value inference");
    Require(TerminalValue != TruncatedValue, "PPO terminal and time-limit targets differ");
}
int main()
{
    AdmissionTest();
    PreprocessingAndIdentityTest();
    PPOBoundaryTest();
    for (const auto& Profile : FLearner::GetProfiles()) AlgorithmTest(Profile);
    ConcurrentTest();
    std::cout << "All native learning checks passed." << std::endl;
}

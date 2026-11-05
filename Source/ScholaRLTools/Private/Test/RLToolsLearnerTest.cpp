// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "Learning/RLToolsLearner.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsLearnerTest, "Schola.RLTools.Learner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsLearnerTest::RunTest(const FString& Parameters)
{
    using namespace Schola::RLTools;
    for (const auto& Profile : FLearner::GetProfiles())
    {
        AddInfo(FString::Printf(TEXT("Algorithm/configuration: %s"), UTF8_TO_TCHAR(Profile.Id.c_str())));
        FSettings Settings;
        Settings.PublishEveryUpdates = 1;
        std::string Error;
        auto Learner = FLearner::Create(Profile.Id, Settings, Error);
        if (!TestTrue(TEXT("Create"), Learner != nullptr)) continue;
        const std::vector<float> Observation(Profile.Observations, 0.25f);
        const auto Snapshot = Learner->CapturePolicy();
        std::vector<float> Before, After;
        Snapshot->Evaluate(Observation, Before);
        FDecision Retained;
        for (unsigned Step = 0; Step < 64; ++Step)
        {
            FDecision Decision;
            TestTrue(TEXT("Inference"), Learner->Decide(Observation, 7, 0, false, Decision));
            Retained = Decision;
            TestTrue(TEXT("Admission"), Learner->Submit(Decision, Decision.GetAction(), 1, Observation, Step == 63, false) == ESubmitResult::Accepted);
        }
        unsigned Bursts = 0;
        while (Learner->Train() && ++Bursts < 1000) {}
        TestTrue(TEXT("Bounded worker makes progress"), Bursts < 1000);
        const auto Stats = Learner->ReadStats();
        TestEqual(TEXT("Experiences stored"), static_cast<int64>(Stats.Stored), static_cast<int64>(64));
        TestTrue(TEXT("Real actor and critic updates"), Stats.ActorUpdates > 0 && Stats.CriticUpdates > 0);
        TestTrue(TEXT("Updated policy published"), Stats.Version > 0);
        Snapshot->Evaluate(Observation, After);
        TestTrue(TEXT("Captured snapshot remains unchanged"), Before == After);
        Learner->CapturePolicy()->Evaluate(Observation, After);
        TestTrue(TEXT("New weights affect inference"), Before != After);
        Learner->SetLearningEnabled(false);
        FDecision Paused;
        TestTrue(TEXT("Inference continues during pause"), Learner->Decide(Observation, 7, 0, false, Paused));
        TestTrue(TEXT("Pause closes admission"), Learner->Submit(Paused, Paused.GetAction(), 1, Observation, false, false) == ESubmitResult::Frozen);
        TestFalse(TEXT("Paused learner performs no work"), Learner->Train());
        Learner->Reset(3);
        TestTrue(TEXT("Reset retires outstanding work"), Learner->Submit(Paused, Paused.GetAction(), 1, Observation, false, false) == ESubmitResult::Rejected);
        Learner->Stop();
        TestFalse(TEXT("Stop closes inference"), Learner->Decide(Observation, 7, 0, false, Paused));
    }
    return true;
}

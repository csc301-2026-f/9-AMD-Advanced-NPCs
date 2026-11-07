// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Learning/RLToolsLearner.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ScholaRLTools.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsNativeBuildTest, "Schola.RLTools.NativeBuild",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsNativeBuildTest::RunTest(const FString& Parameters)
{
    auto& Module = FModuleManager::LoadModuleChecked<FScholaRLToolsModule>(TEXT("ScholaRLTools"));
    TestEqual(TEXT("Pinned dependency revision"), FString(Module.GetRLToolsRevision()),
        FString(TEXT("b32d9985c65a5e098a6bbf190fd994962d288b99")));
    for (const auto& Profile : Schola::RLTools::FLearner::GetProfiles())
    {
        std::string Error;
        Schola::RLTools::FSettings Settings;
        Settings.LearningStarts = Profile.BatchSize;
        auto Learner = Schola::RLTools::FLearner::Create(Profile.Id, Settings, Error);
        if (!TestTrue(UTF8_TO_TCHAR(Profile.Id.c_str()), Learner != nullptr)) continue;
        std::vector<float> Action;
        TestTrue(TEXT("Compiled algorithm initializes and evaluates"),
            Learner->CapturePolicy()->Evaluate(std::vector<float>(Profile.Observations, 0), Action));
        TestEqual(TEXT("Output dimensions"), static_cast<int32>(Action.size()), static_cast<int32>(Profile.Actions));
    }
    return true;
}
#endif

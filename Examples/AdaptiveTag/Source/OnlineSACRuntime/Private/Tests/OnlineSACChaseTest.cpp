// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ChaseSACExperiment.h"
#include <iterator>
#include <thread>
#include <vector>

#if WITH_DEV_AUTOMATION_TESTS
namespace {
constexpr double PassCatchRate = 0.80;
constexpr double PassMaxStepsToCatch = 100.0;
constexpr unsigned Seeds[] = {1, 2, 3};
constexpr unsigned TrainSteps = 20000;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOnlineSACChaseTest, "ScholaOnlineLearning.SAC.Chase",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FOnlineSACChaseTest::RunTest(const FString& Parameters)
{
    using namespace online_sac;
    const FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("OnlineSAC"), TEXT("Unreal"));
    const double Start = FPlatformTime::Seconds();
    std::vector<ChaseSACResult> Results(std::size(Seeds));
    std::vector<std::thread> Workers;
    for (size_t Index = 0; Index < std::size(Seeds); ++Index)
    {
        Workers.emplace_back([&Results, Index]
        {
            ChaseSACOptions Options;
            Options.seed = Seeds[Index];
            Options.trainSteps = TrainSteps;
            Results[Index] = RunChaseSAC(Options);
        });
    }
    for (std::thread& Worker : Workers) Worker.join();

    for (const ChaseSACResult& Result : Results)
    {
        FFileHelper::SaveStringToFile(FString(UTF8_TO_TCHAR(CurveToCsv(Result).c_str())),
            *FPaths::Combine(OutDir, FString::Printf(TEXT("chase-seed%u.csv"), Result.seed)));
        const CurvePoint* Untrained = Result.curve.empty() ? nullptr : &Result.curve.front();
        const CurvePoint* Trained = Result.AfterTraining();
        if (!TestTrue(FString::Printf(TEXT("seed %u completed"), Result.seed), Result.completed && Trained && Untrained)) continue;
        AddInfo(FString::Printf(TEXT("seed %u: bounce catch_rate %.3f -> %.3f, steps_to_catch %.1f, circle (zero-shot) %.3f, train %.1fs"),
            Result.seed, Untrained->bounce.catchRate, Trained->bounce.catchRate, Trained->bounce.meanStepsToCatch, Trained->circle.catchRate, Result.trainSeconds));
        TestTrue(FString::Printf(TEXT("seed %u catch rate %.3f >= %.2f"), Result.seed, Trained->bounce.catchRate, PassCatchRate),
            Trained->bounce.catchRate >= PassCatchRate);
        TestTrue(FString::Printf(TEXT("seed %u steps to catch %.1f <= %.0f"), Result.seed, Trained->bounce.meanStepsToCatch, PassMaxStepsToCatch),
            Trained->bounce.meanStepsToCatch <= PassMaxStepsToCatch);
    }
    AddInfo(FString::Printf(TEXT("total %.1fs, curves in %s"), FPlatformTime::Seconds() - Start, *OutDir));
    return true;
}
#endif

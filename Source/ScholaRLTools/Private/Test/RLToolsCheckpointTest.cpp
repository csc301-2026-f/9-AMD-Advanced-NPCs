// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Learning/RLToolsLearner.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include <algorithm>
#include <cstring>

namespace RLToolsCheckpointTests
{
using namespace Schola::RLTools;

inline FSettings MakeSettings(const FProfile& Profile, unsigned Seed)
{
    FSettings Settings;
    Settings.Seed = Seed;
    Settings.PublishEveryUpdates = 1;
    Settings.LearningStarts = Profile.BatchSize;
    return Settings;
}
inline FString Directory()
{
    const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Schola/Learning/Tests"));
    IFileManager::Get().MakeDirectory(*Dir, true);
    return Dir;
}
inline std::string PathFor(const FProfile& Profile, const TCHAR* Name)
{
    const FString Id = FString(UTF8_TO_TCHAR(Profile.Id.c_str())).Replace(TEXT("/"), TEXT("_"));
    return std::string(TCHAR_TO_UTF8(*(Directory() / FString::Printf(TEXT("%s-%s.ckpt"), *Id, Name))));
}
inline float Value(unsigned Index, unsigned Dim)
{
    const unsigned H = (Index * 2654435761u) ^ (Dim * 40503u + 0x9E3779B9u);
    return static_cast<float>((H >> 8) % 2001u) / 1000.0f - 1.0f;
}
inline std::vector<float> Obs(const FProfile& Profile, unsigned Index)
{
    std::vector<float> O(Profile.Observations);
    for (unsigned D = 0; D < Profile.Observations; ++D) O[D] = Value(Index, D);
    return O;
}
inline bool Outputs(const FLearner& Learner, const FProfile& Profile, std::vector<float>& Out)
{
    Out.clear();
    const auto Policy = Learner.CapturePolicy();
    for (unsigned K = 0; K < 20; ++K)
    {
        std::vector<float> Action;
        if (!Policy || !Policy->Evaluate(Obs(Profile, 100000 + K), Action) || Action.size() != Profile.Actions) return false;
        Out.insert(Out.end(), Action.begin(), Action.end());
    }
    return true;
}
inline bool BitEqual(const std::vector<float>& A, const std::vector<float>& B)
{
    return A.size() == B.size() && (A.empty() || std::memcmp(A.data(), B.data(), A.size() * sizeof(float)) == 0);
}
inline void Drain(FLearner& Learner)
{
    for (unsigned Burst = 0; Burst < 1000 && Learner.Train(); ++Burst) {}
}
/** Decide -> Submit -> Train until the published Version reaches Target. */
inline bool TrainUntil(FLearner& Learner, const FProfile& Profile, std::uint64_t Target, unsigned& Step)
{
    for (unsigned Iteration = 0; Iteration < 20000; ++Iteration)
    {
        if (Learner.ReadStats().Version >= Target) return true;
        const auto Observation = Obs(Profile, Step);
        FDecision Decision;
        if (Learner.Decide(Observation, 1, Step / 16, false, Decision))
        {
            ++Step;
            Learner.Submit(Decision, Decision.GetAction(), 0.1f * Observation[0], Obs(Profile, Step), Step % 16 == 0, false);
        }
        Drain(Learner);
    }
    return Learner.ReadStats().Version >= Target;
}
inline const TCHAR* ModeName(ECheckpointMode Mode)
{
    return Mode == ECheckpointMode::PolicyExport ? TEXT("PolicyExport") : Mode == ECheckpointMode::TrainingState ? TEXT("TrainingState") : TEXT("FullResume");
}
inline constexpr ECheckpointMode Modes[] = { ECheckpointMode::PolicyExport, ECheckpointMode::TrainingState, ECheckpointMode::FullResume };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsCheckpointRoundTripTest, "Schola.RLTools.Checkpoint.RoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsCheckpointRoundTripTest::RunTest(const FString& Parameters)
{
    using namespace RLToolsCheckpointTests;
    for (const auto& Profile : FLearner::GetProfiles())
    {
        AddInfo(FString::Printf(TEXT("Algorithm/configuration: %s"), UTF8_TO_TCHAR(Profile.Id.c_str())));
        const bool OffPolicy = Profile.Algorithm != EAlgorithm::PPO;
        std::string Error;
        auto Saver = FLearner::Create(Profile.Id, MakeSettings(Profile, 11), Error);
        if (!TestTrue(TEXT("Create saver"), Saver != nullptr)) continue;
        unsigned Step = 0;
        if (!TestTrue(TEXT("Saver trains to version 2"), TrainUntil(*Saver, Profile, 2, Step))) continue;
        Drain(*Saver);
        const FStats Saved = Saver->ReadStats();
        std::vector<float> SavedOut;
        TestTrue(TEXT("Saver evaluates"), Outputs(*Saver, Profile, SavedOut));
        for (const ECheckpointMode Mode : Modes)
        {
            const std::string Path = PathFor(Profile, ModeName(Mode));
            const FString Label = FString::Printf(TEXT("%s %s"), UTF8_TO_TCHAR(Profile.Id.c_str()), ModeName(Mode));
            if (!TestTrue(*(Label + TEXT(": save")), Saver->SaveCheckpoint(Path, Mode, Error) == ECheckpointResult::Ok)) continue;
            TestFalse(*(Label + TEXT(": no temporary file remains")), FPaths::FileExists(UTF8_TO_TCHAR((Path + ".tmp").c_str())));
            auto Fresh = FLearner::Create(Profile.Id, MakeSettings(Profile, 99), Error);
            if (!TestTrue(*(Label + TEXT(": create fresh learner")), Fresh != nullptr)) continue;
            const FStats Before = Fresh->ReadStats();
            if (!TestTrue(*(Label + TEXT(": load")), Fresh->LoadCheckpoint(Path, Error) == ECheckpointResult::Ok))
            {
                AddError(UTF8_TO_TCHAR(Error.c_str()));
                continue;
            }
            std::vector<float> Out;
            TestTrue(*(Label + TEXT(": deterministic outputs bit-identical")), Outputs(*Fresh, Profile, Out) && BitEqual(Out, SavedOut));
            const FStats After = Fresh->ReadStats();
            TestEqual(*(Label + TEXT(": version is max(current + 1, saved)")), static_cast<int64>(After.Version),
                static_cast<int64>(std::max<std::uint64_t>(Before.Version + 1, Saved.Version)));
            TestEqual(*(Label + TEXT(": generation increments")), static_cast<int64>(After.Generation), static_cast<int64>(Before.Generation + 1));
            if (Mode == ECheckpointMode::PolicyExport)
            {
                TestEqual(*(Label + TEXT(": no updates restored")), static_cast<int64>(After.Updates), int64(0));
                TestEqual(*(Label + TEXT(": empty replay")), static_cast<int32>(After.ReplaySize), 0);
            }
            else if (Mode == ECheckpointMode::FullResume && OffPolicy)
                TestEqual(*(Label + TEXT(": replay restored")), static_cast<int32>(After.ReplaySize), static_cast<int32>(Saved.ReplaySize));
            else
                TestEqual(*(Label + TEXT(": replay not restored")), static_cast<int32>(After.ReplaySize), 0);
            unsigned FreshStep = 5000;
            TestTrue(*(Label + TEXT(": loaded learner keeps training")), TrainUntil(*Fresh, Profile, After.Version + 1, FreshStep));
        }
        // Version never goes backwards when loading an older checkpoint into the saver itself.
        TrainUntil(*Saver, Profile, Saver->ReadStats().Version + 2, Step);
        Drain(*Saver);
        const FStats Before = Saver->ReadStats();
        TestTrue(TEXT("Self load"), Saver->LoadCheckpoint(PathFor(Profile, TEXT("TrainingState")), Error) == ECheckpointResult::Ok);
        const FStats After = Saver->ReadStats();
        TestEqual(TEXT("Self load version is current + 1"), static_cast<int64>(After.Version), static_cast<int64>(Before.Version + 1));
        TestEqual(TEXT("Self load generation increments"), static_cast<int64>(After.Generation), static_cast<int64>(Before.Generation + 1));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsCheckpointRetireTest, "Schola.RLTools.Checkpoint.RetiresDecisions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsCheckpointRetireTest::RunTest(const FString& Parameters)
{
    using namespace RLToolsCheckpointTests;
    for (const auto& Profile : FLearner::GetProfiles())
    {
        AddInfo(FString::Printf(TEXT("Algorithm/configuration: %s"), UTF8_TO_TCHAR(Profile.Id.c_str())));
        std::string Error;
        auto Learner = FLearner::Create(Profile.Id, MakeSettings(Profile, 11), Error);
        if (!TestTrue(TEXT("Create"), Learner != nullptr)) continue;
        unsigned Step = 0;
        TrainUntil(*Learner, Profile, 1, Step);
        Drain(*Learner);
        const std::string Path = PathFor(Profile, TEXT("retire"));
        if (!TestTrue(TEXT("Save"), Learner->SaveCheckpoint(Path, ECheckpointMode::TrainingState, Error) == ECheckpointResult::Ok)) continue;
        FDecision Old;
        TestTrue(TEXT("Decide before load"), Learner->Decide(Obs(Profile, 3), 1, 0, false, Old));
        TestTrue(TEXT("Load"), Learner->LoadCheckpoint(Path, Error) == ECheckpointResult::Ok);
        const ESubmitResult Result = Learner->Submit(Old, Old.GetAction(), 0, Obs(Profile, 4), false, false);
        TestTrue(TEXT("Pre-load decision is retired"), Result == ESubmitResult::Rejected || Result == ESubmitResult::NotReady);
        FDecision New;
        TestTrue(TEXT("Decide after load"), Learner->Decide(Obs(Profile, 3), 1, 0, false, New));
        TestTrue(TEXT("New decision is accepted"), Learner->Submit(New, New.GetAction(), 0, Obs(Profile, 4), false, false) == ESubmitResult::Accepted);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRLToolsCheckpointRefusalTest, "Schola.RLTools.Checkpoint.Refusals",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FRLToolsCheckpointRefusalTest::RunTest(const FString& Parameters)
{
    using namespace RLToolsCheckpointTests;
    const auto Profiles = FLearner::GetProfiles();
    for (const auto& Profile : Profiles)
    {
        AddInfo(FString::Printf(TEXT("Algorithm/configuration: %s"), UTF8_TO_TCHAR(Profile.Id.c_str())));
        std::string Error;
        auto Live = FLearner::Create(Profile.Id, MakeSettings(Profile, 11), Error);
        if (!TestTrue(TEXT("Create"), Live != nullptr)) continue;
        unsigned Step = 0;
        TrainUntil(*Live, Profile, 2, Step);
        Drain(*Live);
        const std::string Source = PathFor(Profile, TEXT("refusal-source"));
        if (!TestTrue(TEXT("Save"), Live->SaveCheckpoint(Source, ECheckpointMode::TrainingState, Error) == ECheckpointResult::Ok)) continue;
        FCheckpointInfo Info;
        if (!TestTrue(TEXT("Read info"), FLearner::ReadCheckpointInfo(Source, Info, Error) == ECheckpointResult::Ok)) continue;
        TestEqual(TEXT("Info profile"), FString(UTF8_TO_TCHAR(Info.ProfileId.c_str())), FString(UTF8_TO_TCHAR(Profile.Id.c_str())));
        TestTrue(TEXT("Info mode"), Info.Mode == ECheckpointMode::TrainingState);
        TArray<uint8> Bytes;
        TestTrue(TEXT("Read checkpoint bytes"), FFileHelper::LoadFileToArray(Bytes, UTF8_TO_TCHAR(Source.c_str())));
        const int64 Header = Bytes.Num() - static_cast<int64>(Info.PayloadSize);
        if (!TestTrue(TEXT("Header precedes payload"), Header > 0)) continue;

        auto Expect = [&](const TCHAR* Name, const std::string& Path, ECheckpointResult Expected)
        {
            const FStats Before = Live->ReadStats();
            std::vector<float> BeforeOut, AfterOut;
            Outputs(*Live, Profile, BeforeOut);
            std::string LoadError;
            const ECheckpointResult Result = Live->LoadCheckpoint(Path, LoadError);
            TestTrue(FString::Printf(TEXT("%s: %s (got %s)"), Name, UTF8_TO_TCHAR(ToString(Expected)), UTF8_TO_TCHAR(ToString(Result))), Result == Expected);
            const FStats After = Live->ReadStats();
            Outputs(*Live, Profile, AfterOut);
            TestTrue(FString::Printf(TEXT("%s: live outputs unchanged"), Name), BitEqual(BeforeOut, AfterOut));
            TestEqual(FString::Printf(TEXT("%s: generation unchanged"), Name), static_cast<int64>(After.Generation), static_cast<int64>(Before.Generation));
            TestEqual(FString::Printf(TEXT("%s: version unchanged"), Name), static_cast<int64>(After.Version), static_cast<int64>(Before.Version));
        };
        auto Write = [&](const TCHAR* Name, const TArray<uint8>& Data)
        {
            const std::string Path = PathFor(Profile, Name);
            FFileHelper::SaveArrayToFile(Data, UTF8_TO_TCHAR(Path.c_str()));
            return Path;
        };

        Expect(TEXT("Missing file"), PathFor(Profile, TEXT("does-not-exist")), ECheckpointResult::IoError);
        Expect(TEXT("Empty file"), Write(TEXT("empty"), TArray<uint8>()), ECheckpointResult::BadFormat);
        Expect(TEXT("Header only"), Write(TEXT("header-only"), TArray<uint8>(Bytes.GetData(), static_cast<int32>(Header))), ECheckpointResult::BadFormat);
        Expect(TEXT("Truncated payload"), Write(TEXT("truncated"), TArray<uint8>(Bytes.GetData(), static_cast<int32>(Header + Info.PayloadSize / 2))), ECheckpointResult::BadFormat);
        TArray<uint8> Flipped = Bytes;
        Flipped[static_cast<int32>(Header + Info.PayloadSize / 2)] ^= 0x01;
        Expect(TEXT("Flipped payload byte"), Write(TEXT("flipped"), Flipped), ECheckpointResult::BadFormat);
        TArray<uint8> Magic = Bytes;
        Magic[0] ^= 0xFF;
        Expect(TEXT("Wrong magic"), Write(TEXT("magic"), Magic), ECheckpointResult::BadFormat);

        // Same algorithm with another shape, and another algorithm with the same shape.
        for (const auto& Other : Profiles)
        {
            const bool OtherShape = Other.Algorithm == Profile.Algorithm && Other.Id != Profile.Id;
            const bool OtherAlgorithm = Other.Algorithm != Profile.Algorithm && Other.Observations == Profile.Observations && Other.Actions == Profile.Actions;
            if (!OtherShape && !OtherAlgorithm) continue;
            auto Donor = FLearner::Create(Other.Id, MakeSettings(Other, 5), Error);
            if (!TestTrue(TEXT("Create donor"), Donor != nullptr)) continue;
            const std::string Path = PathFor(Profile, TEXT("donor"));
            if (!TestTrue(TEXT("Save donor"), Donor->SaveCheckpoint(Path, ECheckpointMode::TrainingState, Error) == ECheckpointResult::Ok)) continue;
            Expect(*FString::Printf(TEXT("Checkpoint from %s"), UTF8_TO_TCHAR(Other.Id.c_str())), Path, ECheckpointResult::Mismatch);
        }

        // Different fixed preprocessing.
        FSettings Preprocessed = MakeSettings(Profile, 11);
        Preprocessed.ObservationMean.assign(Profile.Observations, 0.1f);
        Preprocessed.ObservationStd.assign(Profile.Observations, 2.0f);
        auto Donor = FLearner::Create(Profile.Id, Preprocessed, Error);
        if (TestTrue(TEXT("Create preprocessed donor"), Donor != nullptr))
        {
            const std::string Path = PathFor(Profile, TEXT("preprocessed"));
            if (TestTrue(TEXT("Save preprocessed donor"), Donor->SaveCheckpoint(Path, ECheckpointMode::TrainingState, Error) == ECheckpointResult::Ok))
                Expect(TEXT("Different preprocessing"), Path, ECheckpointResult::Mismatch);
        }

        const std::uint64_t Version = Live->ReadStats().Version;
        TestTrue(TEXT("Live learner still trains after refusals"), TrainUntil(*Live, Profile, Version + 1, Step));
    }
    return true;
}
#endif

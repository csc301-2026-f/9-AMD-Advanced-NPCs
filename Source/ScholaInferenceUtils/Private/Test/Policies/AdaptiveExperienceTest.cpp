// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "AdaptiveDecisionTestPolicy.h"
#include "Common/InstancedStructUtils.h"
#include "Points/BoxPoint.h"
#include "Spaces/BoxSpace.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace AdaptiveExperienceTest
{
FInteractionDefinition Definition()
{
    FInteractionDefinition Result;
    Result.ObsSpaceDefn.InitializeAs<FBoxSpace>(TArray<float>{-1, -1}, TArray<float>{1, 1});
    Result.ActionSpaceDefn.InitializeAs<FBoxSpace>(TArray<float>{-1}, TArray<float>{1});
    return Result;
}
TInstancedStruct<FPoint> Point(TArray<float> Values)
{
    TInstancedStruct<FPoint> Result;
    Result.InitializeAs<FBoxPoint>(Values);
    return Result;
}
FAdaptiveExperience Record(const FGuid& Session)
{
    FAdaptiveExperience Result;
    Result.SessionId = Session;
    Result.AgentId = Result.EpisodeId = 0;
    Result.DecisionId = 7;
    Result.PolicyVersion = 3;
    Result.Observation = Point({0.1f, 0.2f});
    Result.NextObservation = Point({0.2f, 0.3f});
    Result.ExecutedAction = Point({0.5f});
    Result.Reward = -0.25f;
    return Result;
}
struct FFixture
{
    const FGuid Session = FGuid::NewGuid();
    UAdaptiveDecisionTestPolicy* Policy = NewObject<UAdaptiveDecisionTestPolicy>();
    FFixture()
    {
        Policy->Init(Definition());
        Policy->Start(Session);
        Policy->bHandleExperience = true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveExperienceSubmissionTest, "Schola.Inference.Adaptive.ExperienceSubmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveExperienceSubmissionTest::RunTest(const FString& Parameters)
{
    using namespace AdaptiveExperienceTest;
    auto* Base = NewObject<UAdaptiveNPCPolicy>();
    TestEqual(TEXT("Uninitialized base cannot accept experience"), Base->SubmitExperience(Record(FGuid::NewGuid())), EAdaptiveSubmitResult::NotReady);
    Base->Init(Definition());
    TestEqual(TEXT("Local definitions alone cannot accept experience"), Base->SubmitExperience(Record(FGuid::NewGuid())), EAdaptiveSubmitResult::NotReady);

    FFixture Fixture;
    UAdaptiveNPCPolicy* Policy = Fixture.Policy;
    auto Experience = Record(Fixture.Session);
    Fixture.Policy->bHandleExperience = false;
    TestEqual(TEXT("Base dispatch never claims admission without a learner"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::NotReady);
    Fixture.Policy->bHandleExperience = true;
    for (const EAdaptiveSubmitResult Result : {EAdaptiveSubmitResult::Rejected, EAdaptiveSubmitResult::Accepted,
        EAdaptiveSubmitResult::Dropped, EAdaptiveSubmitResult::Frozen, EAdaptiveSubmitResult::NotReady})
    {
        Fixture.Policy->ExperienceResult = Result;
        const int32 Before = Fixture.Policy->ExperienceDispatchCount;
        TestEqual(TEXT("Base API forwards the learner's admission result"), Policy->SubmitExperience(Experience), Result);
        TestEqual(TEXT("One dispatch per submission"), Fixture.Policy->ExperienceDispatchCount, Before + 1);
        TestEqual(TEXT("Failure diagnostics clear on Accepted/Frozen"), Policy->GetLastError().IsEmpty(),
            Result == EAdaptiveSubmitResult::Accepted || Result == EAdaptiveSubmitResult::Frozen);
    }
    Experience.Observation.GetMutable<FBoxPoint>().Values[0] = 0.9f;
    TestEqual(TEXT("Retained experience owns its point data"), Fixture.Policy->SubmittedExperience.Observation.Get<FBoxPoint>().Values[0], 0.1f);
    TestEqual(TEXT("Negative reward is forwarded unchanged"), Fixture.Policy->SubmittedExperience.Reward, -0.25f);
    TestEqual(TEXT("Decision attribution is preserved"), Fixture.Policy->SubmittedExperience.DecisionId, int64(7));
    TestEqual(TEXT("Original policy version is preserved"), Fixture.Policy->SubmittedExperience.PolicyVersion, int64(3));

    Fixture.Policy->ExperienceResult = EAdaptiveSubmitResult::Accepted;
    TestTrue(TEXT("Encounter reset retains the session"), Policy->ResetEpisode());
    Experience.EpisodeId = 1;
    Experience.bTerminated = true;
    TestEqual(TEXT("Terminal experience accepted in the same learning session"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::Accepted);
    TestTrue(TEXT("Termination flag preserved"), Fixture.Policy->SubmittedExperience.bTerminated);
    Policy->Init(Definition());
    TestEqual(TEXT("Reinitialization requires a new ready session"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::NotReady);
    const FGuid NewSession = FGuid::NewGuid();
    Fixture.Policy->Start(NewSession);
    TestEqual(TEXT("Experience from the old session rejected"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::Rejected);
    Experience.SessionId = NewSession;
    Experience.EpisodeId = 0;
    Experience.bTerminated = false;
    Experience.bTruncated = true;
    TestEqual(TEXT("New session accepts experience"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::Accepted);
    TestTrue(TEXT("Truncation flag preserved"), Fixture.Policy->SubmittedExperience.bTruncated);
    const int32 BeforeStop = Fixture.Policy->ExperienceDispatchCount;
    Policy->Shutdown();
    TestEqual(TEXT("Stopped session cannot dispatch experience"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::NotReady);
    TestEqual(TEXT("Stopped submission never reaches the learner"), Fixture.Policy->ExperienceDispatchCount, BeforeStop);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveExperienceValidationTest, "Schola.Inference.Adaptive.ExperienceValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveExperienceValidationTest::RunTest(const FString& Parameters)
{
    using namespace AdaptiveExperienceTest;
    FFixture Fixture;
    UAdaptiveNPCPolicy* Policy = Fixture.Policy;
    const auto Valid = Record(Fixture.Session);
    const int32 Before = Fixture.Policy->ExperienceDispatchCount;
    auto Reject = [&](const FAdaptiveExperience& Experience)
    {
        TestEqual(TEXT("Malformed experience rejected"), Policy->SubmitExperience(Experience), EAdaptiveSubmitResult::Rejected);
        TestEqual(TEXT("Invalid data never reaches the learner"), Fixture.Policy->ExperienceDispatchCount, Before);
        TestFalse(TEXT("Rejection explains the failure"), Policy->GetLastError().IsEmpty());
    };
    auto Bad = Valid;
    Bad.SessionId = FGuid::NewGuid(); Reject(Bad);
    Bad.SessionId.Invalidate(); Reject(Bad);
    Bad = Valid; Bad.PolicyVersion = INDEX_NONE; Reject(Bad);
    Bad = Valid; Bad.AgentId = 1; Reject(Bad);
    Bad = Valid; Bad.EpisodeId = 1; Reject(Bad);
    Bad = Valid; Bad.DecisionId = INDEX_NONE; Reject(Bad);
    const float NaN = std::numeric_limits<float>::quiet_NaN();
    const float Infinity = std::numeric_limits<float>::infinity();
    for (float Value : {NaN, Infinity})
    {
        Bad = Valid; Bad.Reward = Value; Reject(Bad);
    }
    auto WrongShape = Point({0, 0});
    WrongShape.GetMutable<FBoxPoint>().Shape = {1, 2};
    TInstancedStruct<FPoint> WrongType;
    ToUntypedInstancedStruct(WrongType).InitializeAs<FInteractionDefinition>();
    for (const auto& Invalid : TArray<TInstancedStruct<FPoint>>{{}, WrongType, Point({0}), Point({2, 0}),
        Point({NaN, 0}), Point({Infinity, 0}), WrongShape})
    {
        Bad = Valid; Bad.Observation = Invalid; Reject(Bad);
        Bad = Valid; Bad.NextObservation = Invalid; Reject(Bad);
    }
    for (const auto& Invalid : TArray<TInstancedStruct<FPoint>>{{}, WrongType, Point({0, 0}), Point({2}), Point({NaN}), Point({Infinity})})
    {
        Bad = Valid; Bad.ExecutedAction = Invalid; Reject(Bad);
    }
    Fixture.Policy->ExperienceResult = EAdaptiveSubmitResult::Frozen;
    Bad = Valid; Bad.Reward = NaN; Reject(Bad);
    Fixture.Policy->ExperienceResult = EAdaptiveSubmitResult::Accepted;
    int64 Ticket = INDEX_NONE;
    Policy->RequestDecision(Valid.Observation, Ticket);
    TestEqual(TEXT("Valid data recovers after rejections"), Policy->SubmitExperience(Valid), EAdaptiveSubmitResult::Accepted);
    FAdaptiveDecision Decision;
    TestEqual(TEXT("Experience submission does not modify pending decisions"), Policy->TryGetDecision(Ticket, Decision), EAdaptiveRequestStatus::Pending);
    Policy->ResetEpisode();
    const int32 AfterReset = Fixture.Policy->ExperienceDispatchCount;
    TestEqual(TEXT("Previous episode's late outcome rejected"), Policy->SubmitExperience(Valid), EAdaptiveSubmitResult::Rejected);
    TestEqual(TEXT("Late outcome does not reach the learner"), Fixture.Policy->ExperienceDispatchCount, AfterReset);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveExperienceBlueprintTest, "Schola.Inference.Adaptive.ExperienceBlueprintAPI",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveExperienceBlueprintTest::RunTest(const FString& Parameters)
{
    using namespace AdaptiveExperienceTest;
    FFixture Fixture;
    UFunction* Function = Fixture.Policy->FindFunction(TEXT("SubmitExperience"));
    if (!TestNotNull(TEXT("Shared submission function is reflected"), Function)) return false;
    TestTrue(TEXT("Shared submission is Blueprint callable"), Function->HasAnyFunctionFlags(FUNC_BlueprintCallable));
    const FEnumProperty* Result = CastField<FEnumProperty>(Function->GetReturnProperty());
    TestTrue(TEXT("Return type belongs to the common adaptive API"), Result && Result->GetEnum() == StaticEnum<EAdaptiveSubmitResult>());
    struct FParameters
    {
        FAdaptiveExperience Experience;
        EAdaptiveSubmitResult ReturnValue = EAdaptiveSubmitResult::NotReady;
    } Args;
    Args.Experience = Record(Fixture.Session);
    Fixture.Policy->ProcessEvent(Function, &Args);
    TestEqual(TEXT("Blueprint dispatch reaches the learner hook"), Args.ReturnValue, EAdaptiveSubmitResult::Accepted);
    const int32 Before = Fixture.Policy->ExperienceDispatchCount;
    Args.Experience.Reward = std::numeric_limits<float>::quiet_NaN();
    Fixture.Policy->ProcessEvent(Function, &Args);
    TestEqual(TEXT("Blueprint dispatch validates experience"), Args.ReturnValue, EAdaptiveSubmitResult::Rejected);
    TestEqual(TEXT("Invalid Blueprint data is not dispatched"), Fixture.Policy->ExperienceDispatchCount, Before);
    return true;
}

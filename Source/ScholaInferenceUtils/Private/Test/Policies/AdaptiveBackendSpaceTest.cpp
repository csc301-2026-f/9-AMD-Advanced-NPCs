// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "Misc/AutomationTest.h"
#include "AdaptiveDecisionTestPolicy.h"
#include "Points/DiscretePoint.h"
#include "Spaces/DiscreteSpace.h"

bool UAdaptiveDiscreteTestPolicy::ValidateSpaceDefinition(const TInstancedStruct<FSpace>& Definition, bool bActionSpace, FString& Error) const
{
    if (Definition.IsValid() && Definition.GetScriptStruct() == FDiscreteSpace::StaticStruct())
    {
        if (Definition.Get<FDiscreteSpace>().High > 0) return true;
        Error = TEXT("A categorical space needs at least one choice.");
        return false;
    }
    return Super::ValidateSpaceDefinition(Definition, bActionSpace, Error);
}
bool UAdaptiveDiscreteTestPolicy::ValidatePoint(const TInstancedStruct<FPoint>& Point, const TInstancedStruct<FSpace>& Definition, FString& Error) const
{
    if (Definition.GetScriptStruct() == FDiscreteSpace::StaticStruct())
    {
        if (Definition.Get<FDiscreteSpace>().Validate(Point) == ESpaceValidationResult::Success) return true;
        Error = TEXT("Invalid categorical point.");
        return false;
    }
    return Super::ValidatePoint(Point, Definition, Error);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAdaptiveBackendSpaceTest, "Schola.Inference.Adaptive.BackendSpaces",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FAdaptiveBackendSpaceTest::RunTest(const FString& Parameters)
{
    FInteractionDefinition Definition;
    Definition.ObsSpaceDefn.InitializeAs<FDiscreteSpace>(3);
    Definition.ActionSpaceDefn.InitializeAs<FDiscreteSpace>(2);
    auto* Policy = NewObject<UAdaptiveDiscreteTestPolicy>();
    TestTrue(TEXT("A backend may support non-Box definitions"), Policy->Init(Definition));
    const FGuid Session = FGuid::NewGuid();
    Policy->Start(Session, 1, 2);
    TInstancedStruct<FPoint> Observation;
    Observation.InitializeAs<FDiscretePoint>(1);
    int64 Ticket = INDEX_NONE;
    TestTrue(TEXT("Request uses backend validation"), Policy->RequestDecision(Observation, Ticket));
    FAdaptiveDecision Decision;
    Decision.SessionId = Session;
    Decision.AgentId = 1; Decision.EpisodeId = 2; Decision.DecisionId = Ticket; Decision.PolicyVersion = 0;
    Decision.Action.InitializeAs<FDiscretePoint>(1);
    TestTrue(TEXT("Completion uses backend validation"), Policy->Deliver(Decision));
    FAdaptiveExperience Experience;
    Experience.SessionId = Session;
    Experience.AgentId = 1; Experience.EpisodeId = 2; Experience.DecisionId = Ticket; Experience.PolicyVersion = 0;
    Experience.Observation = Observation;
    Experience.NextObservation = Observation;
    Experience.ExecutedAction = Decision.Action;
    Experience.Reward = 1;
    Policy->bHandleExperience = true;
    UAdaptiveNPCPolicy* Base = Policy;
    TestEqual(TEXT("Common submission supports the backend"), Base->SubmitExperience(Experience), EAdaptiveSubmitResult::Accepted);
    Experience.ExecutedAction.InitializeAs<FDiscretePoint>(2);
    TestEqual(TEXT("Backend bounds still apply"), Base->SubmitExperience(Experience), EAdaptiveSubmitResult::Rejected);
    Experience.ExecutedAction = Decision.Action;
    Experience.EpisodeId = 3;
    TestEqual(TEXT("Common identity still applies"), Base->SubmitExperience(Experience), EAdaptiveSubmitResult::Rejected);
    auto* DefaultPolicy = NewObject<UAdaptiveDecisionTestPolicy>();
    TestFalse(TEXT("Unmodified default validation still refuses unsupported spaces"), DefaultPolicy->Init(Definition));
    return true;
}

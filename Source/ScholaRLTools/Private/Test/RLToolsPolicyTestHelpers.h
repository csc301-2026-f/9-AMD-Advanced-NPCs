// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "Misc/AutomationTest.h"
#include "Policies/RLToolsNPCPolicy.h"
#include "Policies/RLToolsPolicySnapshot.h"
#include "RLToolsWorldSubsystem.h"
#include "Learning/RLToolsWorldRuntime.h"
#include "Learning/RLToolsPolicyConversion.h"
#include "Engine/World.h"
#include "Spaces/BoxSpace.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/Event.h"
#include "UObject/StrongObjectPtr.h"
#include <atomic>

#if WITH_DEV_AUTOMATION_TESTS
struct FRLToolsWorldTestAccess
{
    static void Gate(URLToolsWorldSubsystem* Service, std::function<void(Schola::RLTools::EWork)> Function)
    { Service->Runtime->Executor->SetWorkGateForTests(std::move(Function)); }
};
namespace RLToolsPolicyTests
{
inline constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

template<typename Predicate>
bool Wait(Predicate Ready, double Timeout = 15)
{
    const double Deadline = FPlatformTime::Seconds() + Timeout;
    do
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        if (Ready()) return true;
        FPlatformProcess::SleepNoStats(0.001f);
    } while (FPlatformTime::Seconds() < Deadline);
    return false;
}
inline FInteractionDefinition Definition(int32 Observations = 3, int32 Actions = 2, float Low = 10000.1f, float High = 10000.2f)
{
    FInteractionDefinition Def;
    Def.AgentType = TEXT("TestNPC");
    Def.ObsSpaceDefn.InitializeAs<FBoxSpace>();
    Def.ObsSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(FBoxSpaceDimension(-10, 10), Observations);
    Def.ObsSpaceDefn.GetMutable<FBoxSpace>().Shape = {1, Observations};
    Def.ActionSpaceDefn.InitializeAs<FBoxSpace>();
    Def.ActionSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(FBoxSpaceDimension(Low, High), Actions);
    Def.ActionSpaceDefn.GetMutable<FBoxSpace>().Shape = {1, Actions};
    return Def;
}
inline TInstancedStruct<FPoint> Observation(int32 Dimensions = 3)
{
    TInstancedStruct<FPoint> Point;
    TArray<float> Values;
    Values.Init(0.25f, Dimensions);
    Point.InitializeAs<FBoxPoint>(Values, TArray<int>{1, Dimensions});
    return Point;
}
inline FAdaptiveExperience Outcome(const FAdaptiveDecision& Decision, const TInstancedStruct<FPoint>& Observation)
{
    FAdaptiveExperience E;
    E.SessionId = Decision.SessionId; E.AgentId = Decision.AgentId; E.EpisodeId = Decision.EpisodeId;
    E.DecisionId = Decision.DecisionId; E.PolicyVersion = Decision.PolicyVersion;
    E.Observation = E.NextObservation = Observation;
    E.ExecutedAction = Decision.Action; E.Reward = 1;
    return E;
}
inline bool Decision(URLToolsNPCPolicy* Policy, const TInstancedStruct<FPoint>& Observation, FAdaptiveDecision& Out, float Timeout = 1)
{
    int64 Id = INDEX_NONE;
    return Policy->RequestDecision(Observation, Id, Timeout)
        && Policy->TryGetDecision(Id, Out) == EAdaptiveRequestStatus::Ready;
}
inline FRLToolsLearningStats Stats(URLToolsNPCPolicy* Policy)
{
    FRLToolsLearningStats S;
    Policy->GetLearningStats(S);
    return S;
}
inline bool Ready(URLToolsNPCPolicy* Policy)
{
    return Wait([Policy] { return Policy->GetSessionState() == EAdaptiveSessionState::Ready; });
}
inline bool Completed(URLToolsNPCPolicy* Policy, const FAdaptiveOperationResult& Operation)
{
    return Wait([Policy, Operation] { return Policy->GetOperationResult(Operation.OperationId).Status != EAdaptiveOperationStatus::Pending; })
        && Policy->GetOperationResult(Operation.OperationId).Status == EAdaptiveOperationStatus::Succeeded;
}
struct FWorld
{
    UWorld* World;
    TArray<URLToolsNPCPolicy*> Policies;
    explicit FWorld(EWorldType::Type Type = EWorldType::Game)
    {
        const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).RequiresHitProxies(false)
            .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
        World = UWorld::CreateWorld(Type, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
    }
    URLToolsWorldSubsystem* Service() const { return World->GetSubsystem<URLToolsWorldSubsystem>(); }
    URLToolsNPCPolicy* Policy(ERLToolsAlgorithm Algorithm = ERLToolsAlgorithm::SAC, const FString& Id = TEXT("sac/3x2-mlp32"))
    {
        auto* P = NewObject<URLToolsNPCPolicy>(World);
        P->AddToRoot();
        P->Algorithm = Algorithm; P->Configuration = Id;
        P->Settings.PublishEveryUpdates = 1;
        P->Settings.MaxUpdatesPerBurst = 1;
        Policies.Add(P);
        return P;
    }
    ~FWorld()
    {
        for (auto* P : Policies)
        {
            if (!P->HasAnyFlags(RF_BeginDestroyed)) P->Shutdown();
            P->RemoveFromRoot();
        }
        if (World)
        {
            World->DestroyWorld(false);
            if (World->IsRooted()) World->RemoveFromRoot();
        }
    }
};
struct FGate : std::enable_shared_from_this<FGate>
{
    const Schola::RLTools::EWork Kind;
    FEvent* Event = FPlatformProcess::GetSynchEventFromPool(true);
    std::atomic<int32> Entered{0};
    explicit FGate(Schola::RLTools::EWork InKind) : Kind(InKind) {}
    ~FGate() { Open(); FPlatformProcess::ReturnSynchEventToPool(Event); }
    void Open() { Event->Trigger(); }
    void Install(URLToolsWorldSubsystem* Service)
    {
        auto Self = shared_from_this();
        FRLToolsWorldTestAccess::Gate(Service, [Self](Schola::RLTools::EWork Work)
        {
            if (Work == Self->Kind)
            {
                ++Self->Entered;
                Self->Event->Wait(15000); // Bounded even if the test fails before opening the gate.
            }
        });
    }
};
}
#endif

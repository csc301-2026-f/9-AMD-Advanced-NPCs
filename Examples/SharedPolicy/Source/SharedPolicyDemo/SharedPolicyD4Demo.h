// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "Agent/AgentInterface.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "SharedPolicyD4Demo.generated.h"

class ACameraActor;
class UNNEPerHeadSharedPolicy;
class USharedPolicyStepper;
class UStaticMeshComponent;

UCLASS()
class ASharedPolicyD4Agent : public AActor, public IAgent
{
    GENERATED_BODY()

public:
    ASharedPolicyD4Agent();
    void Configure(AActor& InTarget, bool bInToward, float Lane);
    FVector GetSteeringPoint() const;
    void Define_Implementation(FInteractionDefinition& OutDefinition) override;
    void Observe_Implementation(FInstancedStruct& OutObservations) override;
    void Act_Implementation(const FInstancedStruct& InAction) override;
    bool AppliedAction() const { return bAppliedAction; }
    void ClearAppliedAction() { bAppliedAction = false; }

private:
    UPROPERTY()
    TObjectPtr<UStaticMeshComponent> Body;

    UPROPERTY()
    TObjectPtr<AActor> Target;

    FVector ArenaCenter = FVector::ZeroVector;
    FVector TargetOffset = FVector::ZeroVector;
    bool bAppliedAction = false;
};

UCLASS()
class ASharedPolicyD4Demo : public AActor
{
    GENERATED_BODY()

public:
    ASharedPolicyD4Demo();
    bool Setup(AActor& InTarget);
    void Tick(float DeltaSeconds) override;

private:
    void MoveTarget(float DeltaSeconds);
    bool RunDecision();

    UPROPERTY()
    TObjectPtr<AActor> Target;

    UPROPERTY()
    TObjectPtr<ACameraActor> Camera;

    UPROPERTY()
    TObjectPtr<UNNEPerHeadSharedPolicy> Policy;

    UPROPERTY()
    TObjectPtr<USharedPolicyStepper> Stepper;

    UPROPERTY()
    TArray<TObjectPtr<ASharedPolicyD4Agent>> Agents;

    FVector TargetStart = FVector::ZeroVector;
    float DecisionElapsed = 0.f;
    int32 DecisionNumber = 0;
    bool bSmokeTest = false;
};

/** Starts the demo only when the D4 map is played. */
UCLASS()
class USharedPolicyD4WorldSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()

public:
    void OnWorldBeginPlay(UWorld& InWorld) override;
};

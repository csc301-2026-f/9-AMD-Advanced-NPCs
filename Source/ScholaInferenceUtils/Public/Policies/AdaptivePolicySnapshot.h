// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Policies/PolicyInterface.h"
#include "AdaptivePolicySnapshot.generated.h"

/** Immutable, backend-independent inference handle. Evaluation never submits training data. */
UCLASS(Abstract, BlueprintType)
class SCHOLAINFERENCEUTILS_API UAdaptivePolicySnapshot : public UObject
{
    GENERATED_BODY()

public:
    /** Failure preserves Action. A concrete backend validates compatible observations and actions. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool Evaluate(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action);

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    bool IsInitialized() const { return bInitialized; }

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    FGuid GetSessionId() const { return SessionId; }

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    int64 GetPolicyVersion() const { return PolicyVersion; }

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    bool GetInteractionDefinition(FInteractionDefinition& OutDefinition) const;

protected:
    /** Initialize once, after capturing the actor and all preprocessing required for evaluation. */
    bool InitializeSnapshot(const FGuid& InSessionId, int64 InPolicyVersion, const FInteractionDefinition& InDefinition);

    /** Evaluate only the captured state. Never read mutable weights from the live learner. */
    virtual bool DispatchEvaluation(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) const { return false; }

private:
    UPROPERTY(Transient, DuplicateTransient)
    bool bInitialized = false;

    UPROPERTY(Transient, DuplicateTransient)
    FGuid SessionId;

    UPROPERTY(Transient, DuplicateTransient)
    int64 PolicyVersion = INDEX_NONE;

    UPROPERTY(Transient, DuplicateTransient)
    FInteractionDefinition InteractionDefinition;
};

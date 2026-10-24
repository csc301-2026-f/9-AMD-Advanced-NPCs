// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Policies/AdaptiveNPCPolicy.h"
#include "OnlineSACLearner.h"
#include "OnlineSACPolicy.generated.h"

UENUM(BlueprintType)
enum class EOnlineSACSubmitResult : uint8
{
    Accepted,
    Rejected,
    Dropped,
    Frozen,
    NotReady
};

/**
 * UAdaptiveNPCPolicy backed by an in-process RLtools SAC learner (OnlineSACLearner). No Python.
 * Init starts a local session; decisions complete synchronously inside DispatchDecision.
 * Call on the game thread, like the base class. The learner trains on its own thread.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class ONLINESACSCHOLA_API UOnlineSACPolicy : public UAdaptiveNPCPolicy
{
    GENERATED_BODY()

public:
    static constexpr int32 ObservationSize = online_sac::OnlineSACLearner::ObservationSize;
    static constexpr int32 ActionSize = online_sac::OnlineSACLearner::ActionSize;

    /** Requires Box spaces of exactly ObservationSize and ActionSize values, then creates the learner and a local Ready session. */
    bool Init(const FInteractionDefinition& InDefinition) override;

    /**
     * Team entry point not agreed yet: this is our own. Converts the experience (ExecutedAction is mapped
     * from the action space bounds to [-1, 1]) and pushes it to the learner. Never blocks on training.
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive|SAC")
    EOnlineSACSubmitResult SubmitExperience(const FAdaptiveExperience& Experience);

    /** Base Shutdown is not virtual, so use this to stop both the session and the learner thread. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive|SAC")
    void ShutdownLearner();

    /** Frozen: learning off and decisions use the actor mean. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive|SAC")
    void SetFrozen(bool bFrozen);

    /** Latest published actor snapshot version, or INDEX_NONE without a learner. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive|SAC")
    int64 GetPolicyVersion() const;

    /** Why Init or SubmitExperience refused input (the base LastError has no protected setter). */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive|SAC")
    FString GetLearnerError() const { return LearnerError; }

    online_sac::OnlineSACStats GetLearnerStats() const;
    /** Test helper: see OnlineSACLearner::WaitForLearner. */
    bool WaitForLearner(uint32 MaxBacklog, uint32 TimeoutMs);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive|SAC")
    int32 Seed = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive|SAC", meta = (ClampMin = "1"))
    int32 UpdateEvery = 50;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive|SAC", meta = (ClampMin = "1"))
    int32 WarmupExperiences = 1000;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive|SAC", meta = (ClampMin = "1"))
    int32 QueueCapacity = 4096;

protected:
    bool DispatchDecision(const FAdaptiveDecisionRequest& InRequest) override;
    void BeginDestroy() override;

private:
    void StopLearner();
    bool ToLearnerAction(const TInstancedStruct<FPoint>& Action, float* OutNormalized) const;

    TUniquePtr<online_sac::OnlineSACLearner> Learner;
    FGuid LocalSessionId;
    float ActionLow[ActionSize] = {};
    float ActionHigh[ActionSize] = {};
    FString LearnerError;
};

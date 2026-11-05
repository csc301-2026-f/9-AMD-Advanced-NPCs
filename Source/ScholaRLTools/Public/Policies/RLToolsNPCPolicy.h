// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Policies/AdaptiveNPCPolicy.h"
#include "RLToolsPolicyTypes.h"
#include "RLToolsNPCPolicy.generated.h"

class URLToolsWorldSubsystem;

/** Schola adapter for native SAC, TD3 and PPO. Create with a world/actor/component outer.
 * Configuration is copied on Init; readiness is asynchronous. All policy calls belong on the game thread.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class SCHOLARLTOOLS_API URLToolsNPCPolicy : public UAdaptiveNPCPolicy
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    ERLToolsAlgorithm Algorithm = ERLToolsAlgorithm::SAC;

    /** Full compiled identifier. Must agree with Algorithm. Query GetAvailableConfigurations. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    FString Configuration = TEXT("sac/3x2-mlp32");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    FRLToolsRuntimeSettings Settings;

    /** None creates an independent learner. A named group shares compatible learning within this world. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Ownership")
    FName SharedGroup;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    EAdaptiveInferenceMode InitialInferenceMode = EAdaptiveInferenceMode::Stochastic;

    /** Per binding, not a shared learner setting. Oldest records are retired when capacity is reached. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Retention", meta = (ClampMin = "1", ClampMax = "65536"))
    int32 DecisionCapacity = 256;

    /** Reward retention after action retrieval; independent of the RequestDecision selection timeout. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Retention", meta = (ClampMin = "0.001"))
    float ExperienceRetentionSeconds = 60.0f;

    bool Init(const FInteractionDefinition& InDefinition) override;
    UWorld* GetWorld() const override;
    FAdaptiveCapabilities GetCapabilities() const override;
    FAdaptiveOperationResult SetLearningEnabled(bool bEnabled) override;
    FAdaptiveOperationResult SetInferenceMode(EAdaptiveInferenceMode Mode) override;
    FAdaptiveOperationResult ResetLearning(int32 Seed) override;
    FAdaptiveOperationResult CaptureSnapshot(UAdaptivePolicySnapshot*& OutSnapshot) override;
    bool CancelDecision(int64 DecisionId) override;

    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    static TArray<FRLToolsConfiguration> GetAvailableConfigurations(ERLToolsAlgorithm ForAlgorithm);

    /** False preserves OutStats while constructing, resetting or stopped. */
    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    bool GetLearningStats(FRLToolsLearningStats& OutStats) const;

    /** Includes a shared reset's current seed; editing Settings only affects the next Init. */
    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    bool GetActiveSettings(FRLToolsRuntimeSettings& OutSettings) const;

    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    FAdaptiveOperationResult GetInitializationOperation() const { return GetOperationResult(InitializationOperation); }

protected:
    bool DispatchDecision(const FAdaptiveDecisionRequest& Request) override;
    EAdaptiveSubmitResult DispatchExperience(const FAdaptiveExperience& Experience) override;
    void OnSessionEnded(const FAdaptiveSessionInfo& PreviousSession) override;
    void OnEpisodeReset(const FAdaptiveSessionInfo& PreviousSession) override;
    void OnDecisionRetrieved(int64 DecisionId) override;
    void OnDecisionRetired(int64 DecisionId, bool bCancelled) override;

private:
    friend class URLToolsWorldSubsystem;
    void BindingReady(FGuid Binding, FGuid Session, int64 Agent);
    void BindingFailed(FGuid Binding, const FString& Error);
    void BindingResetting(FGuid Binding);
    void BindingOperationCompleted(FGuid Binding, FGuid Operation, const FString& Error);

    FGuid BindingId;
    FGuid InitializationOperation;
    EAdaptiveInferenceMode ActiveInferenceMode = EAdaptiveInferenceMode::Stochastic;
    TWeakObjectPtr<URLToolsWorldSubsystem> Service;
};

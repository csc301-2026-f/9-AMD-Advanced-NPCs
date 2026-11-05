// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Common/InteractionDefinition.h"
#include "RLToolsPolicyTypes.h"
#include "RLToolsConfigurationAsset.generated.h"

/** Choices compiled into a CPU feedforward learner. Runtime controls remain on the policy Settings. */
USTRUCT(BlueprintType)
struct SCHOLARLTOOLS_API FRLToolsBuildSettings
{
    GENERATED_BODY()

    /** Project-local name; lowercase ASCII letters/digits/hyphens, starting with a letter (max 48). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    FString Name = TEXT("npc");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Configuration")
    ERLToolsAlgorithm Algorithm = ERLToolsAlgorithm::SAC;

    /** Uniform width of the actor's hidden layers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "1"))
    int32 ActorHiddenWidth = 32;

    /** Uniform width of the critic's hidden layers. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "1"))
    int32 CriticHiddenWidth = 32;

    /** Total dense layers, including output (3..16). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "3", ClampMax = "16"))
    int32 ActorLayers = 3;

    /** Total dense layers, including output (3..16). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "3", ClampMax = "16"))
    int32 CriticLayers = 3;

    /** Compiled training batch size (at least two). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "2"))
    int32 BatchSize = 32;

    /** SAC/TD3 replay capacity, at least BatchSize. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "1", EditCondition = "Algorithm != ERLToolsAlgorithm::PPO", EditConditionHides))
    int32 ReplayCapacity = 4096;

    /** PPO rollout length, at least BatchSize and divisible by it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure", meta = (ClampMin = "1", EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    int32 RolloutLength = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
    ERLToolsActivation ActorActivation = ERLToolsActivation::ReLU;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Structure")
    ERLToolsActivation CriticActivation = ERLToolsActivation::ReLU;

    /** Discount in (0,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters")
    float Discount = 0.99f;

    /** SAC/TD3 target update rate in (0,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm != ERLToolsAlgorithm::PPO", EditConditionHides))
    float TargetTau = 0.005f;

    /** 0 selects the algorithm default: SAC=1, TD3=2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm != ERLToolsAlgorithm::PPO", EditConditionHides))
    int32 ActorInterval = 0;

    /** 0 selects the algorithm default: SAC=1, TD3=2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm != ERLToolsAlgorithm::PPO", EditConditionHides))
    int32 TargetInterval = 0;

    /** Positive SAC initial entropy coefficient. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::SAC", EditConditionHides))
    float EntropyCoefficient = 0.5f;

    /** SAC entropy-temperature optimization. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::SAC", EditConditionHides))
    bool bAdaptiveEntropy = true;

    /** Nonnegative TD3 target smoothing noise. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::TD3", EditConditionHides))
    float TargetActionNoise = 0.2f;

    /** Nonnegative TD3 target noise clip. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::TD3", EditConditionHides))
    float TargetActionNoiseClip = 0.5f;

    /** Positive PPO clipping ratio. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    float PPOClip = 0.2f;

    /** PPO advantage-estimation factor in [0,1]. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    float GAE = 0.95f;

    /** Nonnegative PPO entropy coefficient. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    float PPOEntropyCoefficient = 0.01f;

    /** Positive PPO initial action standard deviation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    float InitialActionStd = 0.5f;

    /** PPO advantage normalization. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    bool bNormalizeAdvantage = true;

    /** PPO learns action standard deviation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Algorithm parameters", meta = (EditCondition = "Algorithm == ERLToolsAlgorithm::PPO", EditConditionHides))
    bool bLearnActionStd = true;

};

/** Author a project configuration without writing RLtools templates.
 * Save the asset, Generate Project Configuration, close the editor and rebuild.
 * UBT reads Config/ScholaRLTools.json and generates project-local template registrations.
 */
UCLASS(BlueprintType)
class SCHOLARLTOOLS_API URLToolsConfigurationAsset : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Existing Schola spaces. Bounds/shape remain runtime values; their flattened sizes select template dimensions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    FInteractionDefinition InteractionDefinition;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools")
    FRLToolsBuildSettings BuildSettings;

    /** Writes this asset's validated request to the current project's Config directory. Editor only. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Schola|RLtools")
    void GenerateProjectConfiguration();

    /** Removes this asset's owned request. Rebuild to remove it from the executable. */
    UFUNCTION(CallInEditor, BlueprintCallable, Category = "Schola|RLtools")
    void RemoveProjectConfiguration();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Schola|RLtools")
    FString GenerationStatus;

    /** Missing or stale compiled data explains Generate/Rebuild; packaged games never compile types. */
    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    bool GetCompiledConfiguration(FRLToolsConfiguration& OutConfiguration, FString& Diagnostic) const;

    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    FString GetConfigurationId() const;

    UFUNCTION(BlueprintPure, Category = "Schola|RLtools")
    bool ValidateConfiguration(FString& Diagnostic) const;

private:
    friend struct FRLToolsConfigurationTestAccess;
    bool DescribeRequested(FRLToolsConfiguration& OutConfiguration, FString& Diagnostic) const;
    bool WriteRequest(const FString& Manifest, const FString& Source, bool bRemove, FString& Diagnostic) const;
};

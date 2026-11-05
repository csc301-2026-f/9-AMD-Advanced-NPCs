// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "RLToolsPolicyTypes.generated.h"

UENUM(BlueprintType)
enum class ERLToolsAlgorithm : uint8 { SAC, TD3, PPO };

/** Runtime choices copied on Init. Structural and algorithm-template parameters come from the profile. */
USTRUCT(BlueprintType)
struct SCHOLARLTOOLS_API FRLToolsRuntimeSettings
{
    GENERATED_BODY()

    /** Nonnegative initialization and exploration seed. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Common", meta = (ClampMin = "0"))
    int32 Seed = 1;

    /** Learner-wide pending experience capacity. Full queues return Dropped and may be retried. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Common", meta = (ClampMin = "1"))
    int32 QueueCapacity = 1024;

    /** Maximum experiences stored by one worker burst. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Scheduling", meta = (ClampMin = "1"))
    int32 MaxRecordsPerBurst = 64;

    /** SAC/TD3 only: stored experiences before training; at least the compiled batch size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|OffPolicy", meta = (ClampMin = "0"))
    int32 LearningStarts = 32;

    /** SAC/TD3 only: random exploration decisions per agent before policy exploration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|OffPolicy", meta = (ClampMin = "0"))
    int32 InitialRandomSteps = 32;

    /** SAC/TD3 only: collect this many experiences per set of gradient steps. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|OffPolicy", meta = (ClampMin = "1"))
    int32 TrainFrequency = 1;

    /** SAC/TD3 only: training cycles per collection interval. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|OffPolicy", meta = (ClampMin = "1"))
    int32 GradientSteps = 1;

    /** Maximum SAC/TD3 cycles or PPO epochs in one cooperative worker burst. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Scheduling", meta = (ClampMin = "1"))
    int32 MaxUpdatesPerBurst = 4;

    /** SAC/TD3 only: publish every N cycles. PPO publishes at rollout completion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|OffPolicy", meta = (ClampMin = "1"))
    int32 PublishEveryUpdates = 4;

    /** PPO only: total bounded epochs for a complete rollout. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|PPO", meta = (ClampMin = "1"))
    int32 PPOEpochs = 4;

    /** Optimizer learning rate for every algorithm. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Common", meta = (ClampMin = "0.00000001"))
    float LearningRate = 0.0003f;

    /** TD3 only: action exploration standard deviation. SAC and PPO use their learned distributions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|TD3", meta = (ClampMin = "0"))
    float ExplorationNoise = 0.1f;

    /** Empty with ObservationStd means identity. Otherwise one finite mean per input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Preprocessing")
    TArray<float> ObservationMean;

    /** One strictly positive finite standard deviation per input. Applied once by the native learner. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|RLtools|Preprocessing")
    TArray<float> ObservationStd;
};

/** Read-only description of an actually compiled algorithm/network configuration. */
USTRUCT(BlueprintType)
struct SCHOLARLTOOLS_API FRLToolsConfiguration
{
    GENERATED_BODY()

    /** Full identifier, including sac/, td3/ or ppo/. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    FString Id;

    /** Algorithm instantiated by this configuration. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    ERLToolsAlgorithm Algorithm = ERLToolsAlgorithm::SAC;

    /** Input vector size. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 Observations = 0;

    /** Continuous command vector size. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 Actions = 0;

    /** Hidden width; fixed by compilation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 HiddenWidth = 0;

    /** Actor layers, including output; ReLU hidden activations. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 ActorLayers = 0;

    /** Critic layers; fixed by compilation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 CriticLayers = 0;

    /** Fixed training batch size. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 BatchSize = 0;

    /** SAC/TD3 replay capacity; zero for PPO. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 ReplayCapacity = 0;

    /** PPO rollout length; zero for SAC/TD3. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 RolloutLength = 0;

    /** Fixed discount factor. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float Discount = 0;

    /** SAC/TD3 fixed target update rate. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float TargetTau = 0;

    /** SAC/TD3 fixed actor update interval. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 ActorInterval = 0;

    /** SAC/TD3 fixed target interval. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    int32 TargetInterval = 0;

    /** SAC fixed initial entropy coefficient. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float EntropyCoefficient = 0;

    /** SAC entropy-temperature optimization. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    bool bAdaptiveEntropy = false;

    /** TD3 fixed target smoothing noise. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float TargetActionNoise = 0;

    /** TD3 fixed target noise clip. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float TargetActionNoiseClip = 0;

    /** PPO fixed clipping ratio. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float PPOClip = 0;

    /** PPO fixed advantage-estimation factor. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float GAE = 0;

    /** PPO fixed entropy coefficient. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float PPOEntropyCoefficient = 0;

    /** PPO fixed initial standard deviation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    float InitialActionStd = 0;

    /** PPO advantage normalization. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    bool bNormalizeAdvantage = false;

    /** PPO learns action standard deviation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    bool bLearnActionStd = false;

    /** Whether the compiled model is recurrent. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    bool bRecurrent = false;

    /** Compiled numerical device. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    FString Device;

    /** Pinned RLtools revision. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Configuration")
    FString Revision;

};

/** Learner-wide counters. Accepted experiences are not completed gradient updates. */
USTRUCT(BlueprintType)
struct SCHOLARLTOOLS_API FRLToolsLearningStats
{
    GENERATED_BODY()

    /** Accepted from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Accepted = 0;

    /** Rejected from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Rejected = 0;

    /** Dropped from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Dropped = 0;

    /** Frozen from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Frozen = 0;

    /** Stored from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Stored = 0;

    /** Updates from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Updates = 0;

    /** ActorUpdates from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 ActorUpdates = 0;

    /** CriticUpdates from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 CriticUpdates = 0;

    /** TargetUpdates from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 TargetUpdates = 0;

    /** Stale from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Stale = 0;

    /** Version from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Version = 0;

    /** Generation from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 Generation = 0;

    /** PendingUpdates from the native learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int64 PendingUpdates = 0;

    /** Queued occupancy. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int32 Queued = 0;

    /** ReplaySize occupancy. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int32 ReplaySize = 0;

    /** RolloutSize occupancy. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int32 RolloutSize = 0;

    /** Outstanding native decisions for this binding. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    int32 RetainedDecisions = 0;

    /** Duration of the last completed cooperative burst. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    double LastBurstMilliseconds = 0;

    /** LearningEnabled state of the shared learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    bool bLearningEnabled = false;

    /** Running state of the shared learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    bool bRunning = false;

    /** Training state of the shared learner. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|Statistics")
    bool bTraining = false;

};

/** Bounded execution and ownership diagnostics for one world. */
USTRUCT(BlueprintType)
struct SCHOLARLTOOLS_API FRLToolsWorldStats
{
    GENERATED_BODY()

    /** Learners in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 Learners = 0;

    /** Bindings in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 Bindings = 0;

    /** ActiveWorkers in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 ActiveWorkers = 0;

    /** QueuedLearners in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 QueuedLearners = 0;

    /** WorkerLimit in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 WorkerLimit = 0;

    /** PeakWorkers in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 PeakWorkers = 0;

    /** CompletedBursts in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 CompletedBursts = 0;

    /** DisposedLearners in this world's service. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|RLtools|World")
    int32 DisposedLearners = 0;

};

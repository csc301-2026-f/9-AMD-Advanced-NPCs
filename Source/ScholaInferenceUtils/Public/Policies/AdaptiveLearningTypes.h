// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Points/Point.h"
#include "StructUtils/InstancedStruct.h"
#include "AdaptiveLearningTypes.generated.h"

/** Local definition validation and native learner readiness are separate states. */
UENUM(BlueprintType)
enum class EAdaptiveSessionState : uint8
{
    Uninitialized,
    Initialized UMETA(DisplayName = "Locally Initialized"),
    // Preserve existing reflected values after removing the unused connection state (2).
    Ready = 3,
    Failed,
    Stopped
};

/** Only Ready can release an action. Expired means the local action age limit was reached. */
UENUM(BlueprintType)
enum class EAdaptiveRequestStatus : uint8
{
    Unknown,
    Pending,
    Ready,
    Failed,
    Cancelled,
    Expired
};

/** Outcome of submitting experience. Accepted means learner admission, not a completed training update. */
UENUM(BlueprintType)
enum class EAdaptiveSubmitResult : uint8
{
    Accepted,
    Rejected,
    Dropped,
    Frozen,
    NotReady
};

/** Copied identity of one policy binding. Only Ready carries an active session identity. */
USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveSessionInfo
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    EAdaptiveSessionState State = EAdaptiveSessionState::Uninitialized;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    FGuid SessionId;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    int64 AgentId = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    int64 EpisodeId = INDEX_NONE;
};

/** Action selection and parameter updates are independent controls. */
UENUM(BlueprintType)
enum class EAdaptiveInferenceMode : uint8
{
    Stochastic,
    Deterministic
};

/** Optional operations must report Unsupported rather than pretend to have changed a learner. */
UENUM(BlueprintType)
enum class EAdaptiveOperationStatus : uint8
{
    Unsupported,
    Pending,
    Succeeded,
    Failed,
    Cancelled
};

USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveOperationResult
{
    GENERATED_BODY()

    /** A backend assigns an ID to an operation it supports; unsupported requests have no ID. */
    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    FGuid OperationId;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    EAdaptiveOperationStatus Status = EAdaptiveOperationStatus::Unsupported;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    FString Diagnostic;
};

/** Supported API operations, independent of whether a particular session is currently Ready. */
USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveCapabilities
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    bool bLearningControl = false;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    bool bInferenceMode = false;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    bool bLearningReset = false;

    UPROPERTY(BlueprintReadOnly, Category = "Schola|Adaptive")
    bool bSnapshots = false;
};

/** Copied input for one decision. The policy assigns its session and ticket identity. */
USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveDecisionRequest
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    FGuid SessionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 AgentId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 EpisodeId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 DecisionId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    TInstancedStruct<FPoint> Observation;
};

/**
 * A returned action and the identity of the decision that produced it.
 * Invalid GUIDs and negative IDs mean unassigned; zero is a valid numeric ID/version.
 * This is a data record, not proof that a result has been validated or executed.
 */
USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveDecision
{
    GENERATED_BODY()

    /** Identifies one local learner session; assigned by the native policy subclass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    FGuid SessionId;

    /** NPC identity within the session. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 AgentId = INDEX_NONE;

    /** Encounter identity; resetting an encounter does not restart learning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 EpisodeId = INDEX_NONE;

    /** Matches this result to its originating request. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 DecisionId = INDEX_NONE;

    /** Inference snapshot that chose the action, which may be older than the latest snapshot. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 PolicyVersion = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    TInstancedStruct<FPoint> Action;
};

/**
 * A completed gameplay outcome for learning, attributed to its original decision.
 * Records are passive containers; the consuming native policy validates and submits their contents.
 */
USTRUCT(BlueprintType)
struct SCHOLAINFERENCEUTILS_API FAdaptiveExperience
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    FGuid SessionId;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 AgentId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 EpisodeId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 DecisionId = INDEX_NONE;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    int64 PolicyVersion = INDEX_NONE;

    /** Observation that originated the decision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    TInstancedStruct<FPoint> Observation;

    /** Command actually applied by gameplay, rather than an unrelated fallback or preferred action. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    TInstancedStruct<FPoint> ExecutedAction;

    /** Accumulated reward for the action interval. Finite negative rewards are valid. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    float Reward = 0.0f;

    /** Outcome observation, captured before resetting an ended encounter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    TInstancedStruct<FPoint> NextObservation;

    /** The task reached a terminal condition. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    bool bTerminated = false;

    /** A time limit or other external interruption ended the encounter. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Adaptive")
    bool bTruncated = false;
};

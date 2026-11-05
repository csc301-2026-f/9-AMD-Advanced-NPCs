// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Policies/AdaptiveLearningTypes.h"
#include "Policies/PolicyInterface.h"
#include "AdaptiveNPCPolicy.generated.h"

class UAdaptivePolicySnapshot;

/**
 * Nonblocking adaptive decision lifecycle. Call all methods on the game thread.
 * Currently accepts Box observations and finite, bounded Box actions of configurable size.
 * Base Init validates and copies definitions only; a native learner subclass starts the Ready session.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew, DefaultToInstanced)
class SCHOLAINFERENCEUTILS_API UAdaptiveNPCPolicy : public UObject, public IPolicy
{
    GENERATED_BODY()

public:
    /** Reject invalid definitions without replacing an existing valid definition. See GetLastError. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool Init(const FInteractionDefinition& InDefinition) override;

    /** Immediate adaptive inference is unavailable; returns false without modifying Action. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool Think(const TInstancedStruct<FPoint>& Observation, TInstancedStruct<FPoint>& Action) override;

    /** Also preserves the whole output array on failure, unlike the default IPolicy batch fallback. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool BatchedThink(const TArray<TInstancedStruct<FPoint>>& Observations,
                      TArray<TInstancedStruct<FPoint>>& Actions) override;

    /** Outstanding, unexpired inference only. A false value does not imply session readiness. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    bool IsInferenceBusy() const override;

    /** Issue a ticket without waiting. Rejection preserves OutDecisionId. One pending ticket at a time. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool RequestDecision(const TInstancedStruct<FPoint>& Observation, int64& OutDecisionId, float TimeoutSeconds = 1.0f);

    /** Poll without waiting. Only Ready modifies OutDecision. Reads do not consume the result. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    EAdaptiveRequestStatus TryGetDecision(int64 DecisionId, FAdaptiveDecision& OutDecision);

    /** Validate the active session and numerical data, then submit without waiting for training.
     * Accepted means the native learner admitted the record. Read GetLastError for a failure reason.
     */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    virtual EAdaptiveSubmitResult SubmitExperience(const FAdaptiveExperience& Experience);

    /** Invalidate a pending or ready action without stopping the learner or resetting its weights. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool CancelDecision(int64 DecisionId);

    /** Advance the encounter, cancelling its actions while preserving the ready learning session. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    bool ResetEpisode();

    /** Invalidate the session and stop accepting work. Init is required before starting another session. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    void Shutdown();

    /** Diagnostic for the retained ticket; empty for an unknown ticket. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    FString GetDecisionError(int64 DecisionId) const;

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    bool IsInitialized() const { return bInitialized; }

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    EAdaptiveSessionState GetSessionState() const { return SessionState; }

    /** Error from the latest failed operation; successful mutating operations clear it. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    FString GetLastError() const { return LastError; }

    /** Copies the accepted definition. Before initialization returns false and preserves the output. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    bool GetInteractionDefinition(FInteractionDefinition& OutDefinition) const;

    /** Copy this NPC's attribution state. This does not expose mutable learner state. */
    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    FAdaptiveSessionInfo GetSessionInfo() const;

    UFUNCTION(BlueprintPure, Category = "Schola|Adaptive")
    virtual FAdaptiveCapabilities GetCapabilities() const { return {}; }

    /** Pause/resume training independently of action selection. Backends define acknowledgement. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    virtual FAdaptiveOperationResult SetLearningEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    virtual FAdaptiveOperationResult SetInferenceMode(EAdaptiveInferenceMode Mode);

    /** Reset learned state, not merely this NPC's encounter. May affect a shared learner group. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    virtual FAdaptiveOperationResult ResetLearning(int32 Seed);

    /** Capture immutable inference state. Failure preserves OutSnapshot for native callers. */
    UFUNCTION(BlueprintCallable, Category = "Schola|Adaptive")
    virtual FAdaptiveOperationResult CaptureSnapshot(UAdaptivePolicySnapshot*& OutSnapshot);

protected:
    /** Native learner integration hooks, not Blueprint readiness switches.
     * A subclass calls StartSession after its learner is ready. Decision completions belong on the
     * game thread; deferred completions retain a weak policy pointer and the original request identity.
     */
    bool StartSession(const FGuid& InSessionId, int64 InAgentId, int64 InEpisodeId);
    bool CompleteDecision(const FAdaptiveDecision& InDecision);
    bool FailDecision(const FAdaptiveDecisionRequest& InRequest, const FString& Error);

    /** Must return promptly and copy input before retaining it. False means dispatch was rejected.
     * May complete synchronously from a learner snapshot. The base implementation rejects dispatch.
     */
    virtual bool DispatchDecision(const FAdaptiveDecisionRequest& InRequest) { return false; }

    /** Admit a validated record to the native learner without waiting for training.
     * Copy any retained data. The base has no learner and returns NotReady.
     */
    virtual EAdaptiveSubmitResult DispatchExperience(const FAdaptiveExperience& Experience) { return EAdaptiveSubmitResult::NotReady; }

    /** Called once when a Ready binding ends: Shutdown, successful Init, or destruction.
     * Common identity and tickets are invalidated before this call. Release only this binding;
     * a shared runtime stops only after its last binding is released. Failed Init never calls it.
     * Partial initialization failures remain the subclass's responsibility.
     */
    virtual void ReleaseLearnerBinding(const FAdaptiveSessionInfo& PreviousSession) {}

    /** Agent-local notification after advancing the episode. Preserve weights and replay. */
    virtual void OnEpisodeReset(const FAdaptiveSessionInfo& PreviousSession) {}

    /** Monotonic local elapsed time. Overridden only by deterministic tests. */
    virtual double GetTimeSeconds() const;

    void BeginDestroy() override;
    void PostLoad() override;
    void PostDuplicate(bool bDuplicateForPIE) override;

private:
    void EndSession(const FString& Reason);
    void ResetRuntimeState();
    void ExpireDecision();
    void InvalidateDecision(const FString& Reason);
    bool MatchesPendingRequest(const FGuid& InSessionId, int64 InAgentId, int64 InEpisodeId, int64 InDecisionId) const;

    UPROPERTY()
    FInteractionDefinition InteractionDefinition;

    UPROPERTY(Transient)
    EAdaptiveSessionState SessionState = EAdaptiveSessionState::Uninitialized;

    UPROPERTY()
    FString LastError;

    UPROPERTY()
    bool bInitialized = false;

    UPROPERTY(Transient, DuplicateTransient)
    FAdaptiveDecisionRequest Request;

    UPROPERTY(Transient, DuplicateTransient)
    FAdaptiveDecision Decision;

    FGuid SessionId;
    int64 AgentId = INDEX_NONE;
    int64 EpisodeId = INDEX_NONE;
    // Never reset within this object's lifetime, including reinitialization/new sessions.
    int64 NextDecisionId = 0;
    EAdaptiveRequestStatus RequestStatus = EAdaptiveRequestStatus::Unknown;
    FString RequestError;
    double DeadlineSeconds = 0.0;
};

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mass/EntityHandle.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "Common/InteractionDefinition.h"
#include "Inference/BackgroundInferenceTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ScholaMassPolicy.generated.h"

class UNNEModelData;
class UNPCBackgroundBackendProvider;
class UScholaMassInferenceSubsystem;
struct FMassEntityManager;
struct FMassEntityView;

/**
 * @brief Everything needed to run one model for a Mass crowd.
 *
 * Only Model (or Provider) is required. Observation and action sizes come from the model,
 * and queue limits come from ExpectedAgents x DecisionHz.
 */
USTRUCT(BlueprintType)
struct SCHOLAMASS_API FScholaMassPolicyConfig
{
	GENERATED_BODY()

	/** @brief Model run on the NNE CPU runtime named by RuntimeName. Ignored when Provider is set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass")
	TObjectPtr<UNNEModelData> Model;

	/** @brief NNE CPU runtime that runs Model. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass")
	FString RuntimeName = TEXT("NNERuntimeORTCpu");

	/** @brief Decisions per agent per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 0.01))
	float DecisionHz = 10.f;

	/** @brief Crowd size used to size the queue. 0 uses the scheduler default (4,096 pending rows). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 0))
	int32 ExpectedAgents = 0;

	/** @brief Floats each agent writes per observation. When nonzero, CreatePolicy fails if the model disagrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 0))
	int32 ExpectedObservationSize = 0;

	/** @brief Floats each agent reads per action. When nonzero, CreatePolicy fails if the model disagrees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 0))
	int32 ExpectedActionSize = 0;

	/** @brief Inference worker threads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 1, ClampMax = 64))
	int32 WorkerCount = 2;

	/** @brief Largest number of rows per model call. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 1))
	int32 MaxBatchSize = 64;

	/** @brief Actions older than this are discarded instead of applied. Also sizes the queue. 0 disables the limit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass", meta = (ClampMin = 0))
	float MaxActionAgeMs = 100.f;

	/** @brief Queue limit override. 0 derives it from half of MaxActionAgeMs of target work. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass|Advanced", meta = (ClampMin = 0))
	int32 MaxPendingRows = 0;

	/** @brief Per-frame submission override. 0 admits up to half the queue each frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass|Advanced", meta = (ClampMin = 0))
	int32 MaxRowsPerFrame = 0;

	/** @brief Any background backend provider. Replaces Model and RuntimeName when set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Instanced, Category = "Schola|Mass|Advanced")
	TObjectPtr<UNPCBackgroundBackendProvider> Provider;

	/** @brief Observation and action spaces. Leave both empty for one flat Box each, sized from the model's tensors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Schola|Mass|Advanced")
	FInteractionDefinition Definition;
};

/** @brief Rates and losses for one policy over a window. Produced by FScholaMassPolicyStats::Since. */
USTRUCT(BlueprintType)
struct SCHOLAMASS_API FScholaMassPolicyWindow
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double Seconds = 0;

	/** @brief Gather passes per second (normally the frame rate). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double FramesPerSecond = 0;

	/** @brief Actions delivered to ApplyActions per second. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double DecisionsPerSecond = 0;

	/** @brief Decisions per second requested by the bound agents. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double TargetDecisionsPerSecond = 0;

	/** @brief Actions delivered in the window. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Decisions = 0;

	/** @brief Delivered / target; 1 when nothing is requested. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double TargetFraction = 1;

	/** @brief Mean observation-to-apply latency. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double LatencyMs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double RowsPerModelCall = 0;

	/** @brief Model time summed across workers, per second of wall time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double ModelMsPerSecond = 0;

	/** @brief ModelMsPerSecond as a fraction of all workers' time. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double WorkerUtilization = 0;

	/** @brief Due rows not submitted this window; they are retried next frame. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Deferred = 0;

	/** @brief Submissions refused because the queue was full. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Rejected = 0;

	/** @brief Submissions that could never fit the queue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Invalid = 0;

	/** @brief Rows that expired in the scheduler. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Expired = 0;

	/** @brief Rows the model failed to produce. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Failed = 0;

	/** @brief Completed rows discarded on apply (too old, superseded, or entity gone). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Stale = 0;

	/** @brief Configuration in effect, echoed for reports. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 WorkerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxBatchSize = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxPendingRows = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxRowsPerFrame = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	float MaxActionAgeMs = 0;

	/** @brief Rows that reached the scheduler but never produced an action. */
	int64 Lost() const { return Expired + Failed + Stale; }

	/** @brief Converts a row count from this window into a rate. */
	double PerSecond(int64 Rows) const { return Seconds > 0 ? Rows / Seconds : 0; }

	/** @brief True when at least Tolerance of the requested decisions were delivered. */
	bool MeetsTarget(double Tolerance = .95) const { return TargetFraction >= Tolerance; }

	/** @brief Every limiting cause seen in the window, most direct first. Use when !MeetsTarget(). */
	FString DescribeShortfall() const;
};

/** @brief Cumulative counters for one policy. Take two snapshots and call Now.Since(Before) for rates. */
USTRUCT(BlueprintType)
struct SCHOLAMASS_API FScholaMassPolicyStats
{
	GENERATED_BODY()

	/** @brief FPlatformTime::Seconds() when taken. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double Time = 0;

	/** @brief Bound entities. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 Agents = 0;

	/** @brief Decisions per second requested by the bound entities. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double TargetDecisionsPerSecond = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Frames = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Decisions = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	double LatencySumMs = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Deferred = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int64 Stale = 0;

	/** @brief Scheduler counters (rejections, expiry, failures, model calls and time). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	FNPCBackgroundStats Scheduler;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 WorkerCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxBatchSize = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxPendingRows = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	int32 MaxRowsPerFrame = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Schola|Mass")
	float MaxActionAgeMs = 0;

	/** @brief Rates and losses between an earlier snapshot and this one. */
	FScholaMassPolicyWindow Since(const FScholaMassPolicyStats& Before) const;
};

/**
 * @brief One model shared by a Mass crowd, created by UScholaMassInferenceSubsystem::CreatePolicy.
 *
 * Copyable. The inference subsystem owns the underlying bucket and releases it when the world ends.
 * Blueprints read it through UScholaMassPolicyLibrary.
 */
USTRUCT(BlueprintType)
struct SCHOLAMASS_API FScholaMassPolicy
{
	GENERATED_BODY()

public:
	bool IsValid() const { return BucketId != INDEX_NONE && Subsystem.IsValid(); }

	/**
	 * @brief Creates Count entities bound to this policy. Game thread.
	 *
	 * The entities get Fragments plus the ScholaMass schedule, state and bucket fragments.
	 * Initialize runs for each entity before ScholaMass binds it, so fragments it fills are
	 * visible to the first gather.
	 *
	 * @param[in] Manager The entity manager to create the entities in.
	 * @param[in] Fragments The game's own fragment types.
	 * @param[in] Count Number of entities to create.
	 * @param[in,out] OutEntities The new entity handles are appended.
	 * @param[in] Initialize Per-entity setup, called with each new entity and its index from 0 to Count - 1.
	 */
	void BatchCreateEntities(FMassEntityManager& Manager, TConstArrayView<const UScriptStruct*> Fragments, int32 Count,
		TArray<FMassEntityHandle>& OutEntities, TFunctionRef<void(const FMassEntityView&, int32)> Initialize) const;

	/** @brief Bucket id stored in the entities' FScholaInferenceBucketFragment. */
	int32 GetBucketId() const { return BucketId; }

	/** @brief Decision period stored in each entity's FScholaInferenceScheduleFragment. */
	float GetPeriodSeconds() const { return PeriodSeconds; }

	int32 GetObservationSize() const { return ObservationSize; }
	int32 GetActionSize() const { return ActionSize; }
	int32 GetMaxRowsPerFrame() const { return MaxRowsPerFrame; }
	float GetMaxActionAgeMs() const { return MaxActionAgeMs; }

	/** @brief Settings the scheduler actually uses (a shared bucket keeps its first settings). */
	const FNPCBackgroundSchedulerSettings& GetSettings() const { return Settings; }

	/** @brief Cumulative snapshot. Call on the game thread, or anywhere outside Mass processing. */
	FScholaMassPolicyStats GetStats() const;

private:
	friend class UScholaMassInferenceSubsystem;
	TWeakObjectPtr<UScholaMassInferenceSubsystem> Subsystem;
	int32										  BucketId = INDEX_NONE;
	float										  PeriodSeconds = 0;
	float										  MaxActionAgeMs = 0;
	int32										  ObservationSize = 0;
	int32										  ActionSize = 0;
	int32										  MaxRowsPerFrame = 0;
	FNPCBackgroundSchedulerSettings				  Settings;
};

/** @brief Blueprint access to ScholaMass policies and their statistics. */
UCLASS()
class SCHOLAMASS_API UScholaMassPolicyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** @brief True for a policy returned by a successful CreatePolicy whose world is still alive. */
	UFUNCTION(BlueprintPure, Category = "Schola|Mass", meta = (DisplayName = "Is Valid (Policy)"))
	static bool IsPolicyValid(const FScholaMassPolicy& Policy) { return Policy.IsValid(); }

	/** @brief Decisions per agent per second. */
	UFUNCTION(BlueprintPure, Category = "Schola|Mass")
	static float GetDecisionHz(const FScholaMassPolicy& Policy);

	/** @brief Cumulative counters; subtract two snapshots with StatsSince. */
	UFUNCTION(BlueprintCallable, Category = "Schola|Mass")
	static FScholaMassPolicyStats GetPolicyStats(const FScholaMassPolicy& Policy) { return Policy.GetStats(); }

	/** @brief Rates and losses between two snapshots. */
	UFUNCTION(BlueprintPure, Category = "Schola|Mass")
	static FScholaMassPolicyWindow StatsSince(const FScholaMassPolicyStats& Now, const FScholaMassPolicyStats& Before) { return Now.Since(Before); }

	/** @brief True when at least Tolerance of the requested decisions were delivered. */
	UFUNCTION(BlueprintPure, Category = "Schola|Mass")
	static bool MeetsTarget(const FScholaMassPolicyWindow& Window, double Tolerance = .95) { return Window.MeetsTarget(Tolerance); }

	/** @brief Every limiting cause seen in the window, most direct first. */
	UFUNCTION(BlueprintPure, Category = "Schola|Mass")
	static FString DescribeShortfall(const FScholaMassPolicyWindow& Window) { return Window.DescribeShortfall(); }
};

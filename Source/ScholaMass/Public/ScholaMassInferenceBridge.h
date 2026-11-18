// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "ScholaInferenceFragments.h"
#include "Misc/Optional.h"
#include "Inference/BackgroundInferenceScheduler.h"
#include "Inference/CohortScheduler.h"
#include "Mass/EntityHandle.h"

struct FMassEntityManager;

/** Cumulative per-bucket counters kept by the bridge (game-thread transport side). */
struct FScholaMassBucketCounters
{
	/** Due rows not submitted: over the per-frame cap, or in a submission the scheduler refused. */
	int64 DeferredRows = 0;
	/** Completed rows discarded before or at apply (too old, superseded, or entity gone). */
	int64 DroppedRows = 0;
	/** Rows passed to ApplyActions. */
	int64 AppliedRows = 0;
	/** Sum of observation-to-apply latency over AppliedRows, in milliseconds. */
	double LatencySumMs = 0;
	/** Entities currently bound to the bucket, and the decisions per second they request. */
	int32  Agents = 0;
	double RequestedRowsPerSecond = 0;
};

/** Standalone-manager-testable, world-local transport and dense held-action table.
 * Call from ordered Mass processors; never accesses Actors or UObjects.
 */
class SCHOLAMASS_API FScholaMassInferenceBridge
{
public:
	~FScholaMassInferenceBridge();
	void				 AddBucket(int32 Id, uint32 Generation, TSharedRef<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler,
		TSharedRef<FNPCCohortScheduler> Cohorts);
	bool				 InitializeSlot(FMassEntityHandle Entity, int32 BucketId, FScholaInferenceScheduleFragment& Schedule,
		FScholaInferenceStateFragment& State, double Now, TOptional<ENPCPhaseLayout> Layout = {});
	void				 ReleaseSlot(FScholaInferenceStateFragment& State);
	ENPCBackgroundStatus Submit(int32 BucketId, FMassEntityManager& Manager, TConstArrayView<FMassEntityHandle> Entities,
		TFunctionRef<void(TArrayView<float>)> Fill, double Now);
	/** Collects completed actions; results older than their bucket's GetMaxActionAgeMs are dropped. */
	void Drain(FMassEntityManager& Manager, double Now, TArray<FMassEntityHandle>& Ready);
	/** Ready entities remain available until the matching apply processor acknowledges them. */
	/** True only after a matching apply processor acknowledged the newest row. */
	bool IsActionDelivered(const FScholaInferenceStateFragment& State) const;
	/** Pass Now (> 0) to record observation-to-apply latency in the bucket counters. */
	void													 MarkDelivered(const FScholaInferenceStateFragment& State, double Now = 0.0);
	void													 DiscardReady(const FScholaInferenceStateFragment& State);
	TConstArrayView<float>									 GetHeldAction(const FScholaInferenceStateFragment& State) const;
	TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> GetScheduler(int32 Id) const;
	double													 GetHeldObservationTime(const FScholaInferenceStateFragment& State) const;
	void													 Shutdown();
	/** Per-frame admission cap used by gather processors in automatic mode (MaxRowsPerFrame <= 0). */
	void SetRowsPerFrame(int32 Id, int32 Rows);
	/** Cap set by a policy (normally half its queue), else the scheduler's pending-row limit (largest submission that can fit). */
	int32 GetRowsPerFrame(int32 Id) const;
	void  AddDeferred(int32 Id, int64 Rows);
	/** Action age limit for a bucket: its scheduler's DefaultMaxAgeMs. 0 = no limit. */
	float					  GetMaxActionAgeMs(int32 Id) const;
	FScholaMassBucketCounters GetBucketCounters(int32 Id) const;
	/** Totals across buckets; per-bucket values are in GetBucketCounters. */
	int64 DeferredRows = 0, DroppedRows = 0, AppliedRows = 0;
	/** Gather passes; one per frame when a gather processor runs every frame. */
	int64 GatherPasses = 0;

private:
	struct FSlot
	{
		FMassEntityHandle Entity;
		uint32			  Serial = 1, DeliveredSerial = 0;
		int32			  BucketId = INDEX_NONE, Phase = 0, ReadyIndex = INDEX_NONE;
		float			  Period = 1;
		ENPCPhaseLayout	  Layout = ENPCPhaseLayout::ModelAware;
		TArray<float>	  Action;
		double			  ActionObsTime = 0;
	};
	struct FRequest
	{
		FNPCBackgroundRequest	  Handle;
		TArray<FMassEntityHandle> Entities;
		TArray<uint32>			  SlotSerials;
		uint32					  Serial = 0;
		double					  ObsTime = 0;
	};
	struct FBucket
	{
		TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler;
		TSharedPtr<FNPCCohortScheduler>							 Cohorts;
		uint32													 Generation = 0;
		TArray<FRequest>										 Pending;
		FScholaMassBucketCounters								 Counters;
		int32													 RowsPerFrame = 0;
	};
	TMap<int32, FBucket> Buckets;
	TArray<FSlot>		 Slots;
	TArray<int32>		 FreeSlots, ReadySlots;
	void				 ClearReady(FSlot& Slot);
	void				 CountDropped(int32 BucketId);
	void				 Bind(FSlot& Slot, int32 Delta);
	FNPCBackgroundResult Result;
};

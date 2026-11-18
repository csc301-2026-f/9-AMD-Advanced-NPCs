// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "ScholaMassPolicy.h"
#include "ScholaMassInferenceSubsystem.h"
#include "LogScholaMass.h"
#include "Inference/NNEBatchBackendProvider.h"
#include "MassEntityManager.h"
#include "MassEntityView.h"
#include "Misc/ScopeLock.h"
#include "Spaces/Space.h"

namespace
{
	int32 FlatSize(const TInstancedStruct<FSpace>& Space)
	{
		const FSpace* Ptr = Space.GetPtr<FSpace>();
		return Ptr ? Ptr->GetFlattenedSize() : 0;
	}
} // namespace

FScholaMassPolicy UScholaMassInferenceSubsystem::CreatePolicy(const FScholaMassPolicyConfig& Config, FString& OutError)
{
	check(IsInGameThread());
	OutError.Reset();
	if (!FMath::IsFinite(Config.DecisionHz) || Config.DecisionHz <= 0)
	{
		OutError = TEXT("DecisionHz must be positive");
		return {};
	}
	if (Config.ExpectedAgents < 0 || Config.MaxPendingRows < 0 || Config.MaxRowsPerFrame < 0 || Config.MaxActionAgeMs < 0)
	{
		OutError = TEXT("ExpectedAgents, MaxPendingRows, MaxRowsPerFrame and MaxActionAgeMs must not be negative");
		return {};
	}

	UNPCBackgroundBackendProvider* Provider = Config.Provider;
	if (!Provider)
	{
		if (!Config.Model)
		{
			OutError = TEXT("Set FScholaMassPolicyConfig::Model (or Provider)");
			return {};
		}
		UNNEBatchBackendProvider* NNEProvider = NewObject<UNNEBatchBackendProvider>(this);
		NNEProvider->ModelData = Config.Model;
		NNEProvider->RuntimeName = Config.RuntimeName;
		Provider = NNEProvider;
	}

	FInteractionDefinition Definition = Config.Definition;
	if (!Definition.ObsSpaceDefn.IsValid() && !Definition.ActionSpaceDefn.IsValid()
		&& !Provider->DescribeDefaultDefinition(Definition, OutError))
	{
		return {};
	}
	const int32 ObservationSize = FlatSize(Definition.ObsSpaceDefn);
	const int32 ActionSize = FlatSize(Definition.ActionSpaceDefn);
	if (ObservationSize <= 0 || ActionSize <= 0)
	{
		OutError = TEXT("Observation and action spaces must not be empty");
		return {};
	}
	if ((Config.ExpectedObservationSize > 0 && Config.ExpectedObservationSize != ObservationSize)
		|| (Config.ExpectedActionSize > 0 && Config.ExpectedActionSize != ActionSize))
	{
		OutError = FString::Printf(TEXT("The model maps %d observation floats to %d action floats, but the crowd expects %d to %d"),
			ObservationSize, ActionSize, Config.ExpectedObservationSize, Config.ExpectedActionSize);
		return {};
	}

	// Queue sizing. A row older than MaxActionAgeMs is discarded on apply, so holding more
	// than about half that age of target work only produces stale results. One frame's
	// submission is a single request that must fit the queue whole, so admit at most half
	// the queue per frame and leave room for the next. Overload then shows up as deferrals.
	FNPCBackgroundSchedulerSettings Settings;
	Settings.WorkerCount = FMath::Clamp(Config.WorkerCount, 1, 64);
	Settings.MaxBatchSize = FMath::Max(1, Config.MaxBatchSize);
	Settings.DefaultMaxAgeMs = Config.MaxActionAgeMs;
	if (Config.MaxPendingRows > 0)
	{
		Settings.MaxPendingRows = Config.MaxPendingRows;
	}
	else if (Config.ExpectedAgents > 0)
	{
		const double TargetRowsPerSecond = double(Config.ExpectedAgents) * Config.DecisionHz;
		const double HeldSeconds = Config.MaxActionAgeMs > 0 ? Config.MaxActionAgeMs * .5 / 1000.0 : .05;
		Settings.MaxPendingRows = FMath::Clamp(FMath::CeilToInt32(TargetRowsPerSecond * HeldSeconds),
			FMath::Min(4096, Config.ExpectedAgents), Config.ExpectedAgents);
	}
	const int64 RowBytes = (int64(ObservationSize) + ActionSize) * int64(sizeof(float));
	Settings.MaxPendingBytes = FMath::Max(Settings.MaxPendingBytes, int64(Settings.MaxPendingRows) * RowBytes * 2);
	int32 RowsPerFrame = Config.MaxRowsPerFrame > 0 ? Config.MaxRowsPerFrame : FMath::Max(1, Settings.MaxPendingRows / 2);
	if (RowsPerFrame > Settings.MaxPendingRows)
	{
		UE_LOG(LogScholaMass, Warning, TEXT("MaxRowsPerFrame=%d exceeds MaxPendingRows=%d; clamping, since larger submissions are always rejected"),
			RowsPerFrame, Settings.MaxPendingRows);
		RowsPerFrame = Settings.MaxPendingRows;
	}

	const int32 Bucket = AcquireBucket(*Provider, Definition, Settings, OutError);
	if (Bucket == INDEX_NONE)
	{
		return {};
	}
	if (!Config.Provider)
	{
		PolicyProviders.Add(Provider);
	}

	FScholaMassPolicy Policy;
	Policy.Subsystem = this;
	Policy.BucketId = Bucket;
	Policy.PeriodSeconds = 1.f / Config.DecisionHz;
	Policy.ObservationSize = ObservationSize;
	Policy.ActionSize = ActionSize;
	{
		FScopeLock													   Lock(&Mutex);
		const TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler = Bridge.GetScheduler(Bucket);
		// A shared bucket keeps the settings of the policy that created it.
		Policy.Settings = Scheduler ? Scheduler->GetSettings() : Settings;
		Policy.MaxActionAgeMs = Policy.Settings.DefaultMaxAgeMs;
		Bridge.SetRowsPerFrame(Bucket, FMath::Min(RowsPerFrame, Policy.Settings.MaxPendingRows));
		Policy.MaxRowsPerFrame = Bridge.GetRowsPerFrame(Bucket);
	}
	return Policy;
}

FScholaMassPolicyStats UScholaMassInferenceSubsystem::GetPolicyStats(const FScholaMassPolicy& Policy)
{
	FScholaMassPolicyStats Stats;
	Stats.Time = FPlatformTime::Seconds();
	Stats.WorkerCount = Policy.Settings.WorkerCount;
	Stats.MaxBatchSize = Policy.Settings.MaxBatchSize;
	Stats.MaxPendingRows = Policy.Settings.MaxPendingRows;
	Stats.MaxRowsPerFrame = Policy.MaxRowsPerFrame;
	Stats.MaxActionAgeMs = Policy.MaxActionAgeMs;
	TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler;
	{
		FScopeLock						Lock(&Mutex);
		const FScholaMassBucketCounters Counters = Bridge.GetBucketCounters(Policy.BucketId);
		Stats.Agents = Counters.Agents;
		Stats.TargetDecisionsPerSecond = Counters.RequestedRowsPerSecond;
		Stats.Frames = Bridge.GatherPasses;
		Stats.Decisions = Counters.AppliedRows;
		Stats.LatencySumMs = Counters.LatencySumMs;
		Stats.Deferred = Counters.DeferredRows;
		Stats.Stale = Counters.DroppedRows;
		Scheduler = Bridge.GetScheduler(Policy.BucketId);
	}
	if (Scheduler)
	{
		Stats.Scheduler = Scheduler->GetStats();
	}
	return Stats;
}

void FScholaMassPolicy::BatchCreateEntities(FMassEntityManager& Manager, TConstArrayView<const UScriptStruct*> Fragments, int32 Count,
	TArray<FMassEntityHandle>& OutEntities, TFunctionRef<void(const FMassEntityView&, int32)> Initialize) const
{
	check(IsInGameThread());
	if (!ensureMsgf(IsValid(), TEXT("BatchCreateEntities needs a policy from CreatePolicy")) || Count <= 0)
	{
		return;
	}
	TArray<const UScriptStruct*> Types(Fragments);
	Types.AddUnique(FScholaInferenceScheduleFragment::StaticStruct());
	Types.AddUnique(FScholaInferenceStateFragment::StaticStruct());
	const FMassArchetypeHandle Archetype = Manager.CreateArchetype(Types);

	FScholaInferenceBucketFragment Bucket;
	Bucket.BucketId = BucketId;
	FMassArchetypeSharedFragmentValues Shared;
	Shared.Add(Manager.GetOrCreateConstSharedFragment(Bucket));
	Shared.Sort();

	const int32 First = OutEntities.Num();
	// The schedule observer binds entities when the creation context ends, so the
	// period and the game's own fragments must be filled inside this scope.
	TSharedRef<FMassEntityManager::FEntityCreationContext> Creation = Manager.BatchCreateEntities(Archetype, Shared, Count, OutEntities);
	for (int32 I = 0; I < Count; ++I)
	{
		const FMassEntityView Entity(Manager, OutEntities[First + I]);
		Entity.GetFragmentData<FScholaInferenceScheduleFragment>().PeriodSeconds = PeriodSeconds;
		Initialize(Entity, I);
	}
}

float UScholaMassPolicyLibrary::GetDecisionHz(const FScholaMassPolicy& Policy)
{
	return Policy.GetPeriodSeconds() > 0 ? 1.f / Policy.GetPeriodSeconds() : 0.f;
}

FScholaMassPolicyStats FScholaMassPolicy::GetStats() const
{
	UScholaMassInferenceSubsystem* Owner = Subsystem.Get();
	return Owner && BucketId != INDEX_NONE ? Owner->GetPolicyStats(*this) : FScholaMassPolicyStats();
}

FScholaMassPolicyWindow FScholaMassPolicyStats::Since(const FScholaMassPolicyStats& Before) const
{
	FScholaMassPolicyWindow Window;
	Window.Seconds = FMath::Max(Time - Before.Time, 0.0);
	Window.WorkerCount = WorkerCount;
	Window.MaxBatchSize = MaxBatchSize;
	Window.MaxPendingRows = MaxPendingRows;
	Window.MaxRowsPerFrame = MaxRowsPerFrame;
	Window.MaxActionAgeMs = MaxActionAgeMs;
	const FNPCBackgroundStats& Now = Scheduler;
	const FNPCBackgroundStats& Then = Before.Scheduler;
	Window.Deferred = Deferred - Before.Deferred;
	Window.Stale = Stale - Before.Stale;
	Window.Rejected = Now.BusyRejections - Then.BusyRejections;
	Window.Invalid = Now.InvalidSubmissions - Then.InvalidSubmissions;
	Window.Expired = Now.ExpiredRows - Then.ExpiredRows;
	Window.Failed = Now.FailedRows - Then.FailedRows;
	const int64 Calls = Now.BackendCalls - Then.BackendCalls;
	Window.RowsPerModelCall = Calls > 0 ? double(Now.BackendRows - Then.BackendRows) / Calls : 0;
	Window.Decisions = Decisions - Before.Decisions;
	Window.LatencyMs = Window.Decisions > 0 ? (LatencySumMs - Before.LatencySumMs) / Window.Decisions : 0;
	Window.TargetDecisionsPerSecond = TargetDecisionsPerSecond;
	if (Window.Seconds > 0)
	{
		Window.FramesPerSecond = (Frames - Before.Frames) / Window.Seconds;
		Window.DecisionsPerSecond = Window.Decisions / Window.Seconds;
		Window.ModelMsPerSecond = (Now.BackendServiceSeconds - Then.BackendServiceSeconds) * 1000 / Window.Seconds;
		Window.WorkerUtilization = WorkerCount > 0 ? Window.ModelMsPerSecond / (1000.0 * WorkerCount) : 0;
		Window.TargetFraction = TargetDecisionsPerSecond > 0 ? Window.DecisionsPerSecond / TargetDecisionsPerSecond : 1;
	}
	return Window;
}

FString FScholaMassPolicyWindow::DescribeShortfall() const
{
	const auto		Count = [](int64 Value) { return FText::AsNumber(Value).ToString(); };
	TArray<FString> Causes;
	if (Invalid > 0)
	{
		Causes.Add(FString::Printf(TEXT("%s submissions invalid (MaxRowsPerFrame=%d must not exceed MaxPendingRows=%d)"),
			*Count(Invalid), MaxRowsPerFrame, MaxPendingRows));
	}
	if (Failed + Expired > 0)
	{
		Causes.Add(FString::Printf(TEXT("%s rows failed or expired"), *Count(Failed + Expired)));
	}
	if (Rejected > 0)
	{
		Causes.Add(FString::Printf(TEXT("queue full: %s submissions rejected (limit %d rows)"), *Count(Rejected), MaxPendingRows));
	}
	// A rejected submission defers at most MaxRowsPerFrame rows; anything beyond that hit the cap.
	if (Deferred > Rejected * MaxRowsPerFrame)
	{
		Causes.Add(FString::Printf(TEXT("per-frame cap: %d rows x %.0f FPS = %s decisions/s max"),
			MaxRowsPerFrame, FramesPerSecond, *Count(FMath::RoundToInt64(MaxRowsPerFrame * FramesPerSecond))));
	}
	if (Stale > 0)
	{
		Causes.Add(FString::Printf(TEXT("%s results older than %.0f ms discarded"), *Count(Stale), MaxActionAgeMs));
	}
	if (WorkerUtilization > .8)
	{
		Causes.Add(FString::Printf(TEXT("workers %.0f%% busy"), WorkerUtilization * 100));
	}
	if (Causes.IsEmpty())
	{
		Causes.Add(FString::Printf(TEXT("frame rate %.0f FPS too low for the requested decision rate"), FramesPerSecond));
	}
	return FString::Join(Causes, TEXT("; "));
}

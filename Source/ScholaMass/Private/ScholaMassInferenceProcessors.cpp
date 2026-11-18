// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "ScholaMassInferenceProcessors.h"
#include "ScholaMassInferenceSubsystem.h"
#include "MassExecutionContext.h"
#include "MassEntityManager.h"
#include "MassEntityUtils.h"
#include "MassSignalSubsystem.h"
#include "Misc/ScopeLock.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

UScholaMassGatherProcessor::UScholaMassGatherProcessor()
	: Query(*this)
{
	bRequiresGameThreadExecution = false;
	ExecutionOrder.ExecuteInGroup = TEXT("ScholaInference.Gather");
	ExecutionOrder.ExecuteAfter.Add(TEXT("ScholaInference.Apply"));
}

void UScholaMassGatherProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FScholaInferenceScheduleFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FScholaInferenceStateFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddConstSharedRequirement<FScholaInferenceBucketFragment>();
	Query.AddSubsystemRequirement<UScholaMassInferenceSubsystem>(EMassFragmentAccess::ReadWrite);
	ConfigureObservationRequirements(Query);
}

void UScholaMassGatherProcessor::Execute(FMassEntityManager& Manager, FMassExecutionContext& Context)
{
	UScholaMassInferenceSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UScholaMassInferenceSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	FScopeLock					Lock(&Subsystem->Mutex);
	FScholaMassInferenceBridge& Bridge = Subsystem->Bridge;
	++Bridge.GatherPasses;
	const double Now = FPlatformTime::Seconds();

	struct FDue
	{
		FMassEntityHandle Entity;
		double			  Time;
		int32			  Bucket;
	};
	TArray<FDue> AllDue;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola.Bg.Mass.CountDue");
		Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk) {
			const int32									 BucketId = Chunk.GetConstSharedFragment<FScholaInferenceBucketFragment>().BucketId;
			TArrayView<FScholaInferenceScheduleFragment> Schedules = Chunk.GetMutableFragmentView<FScholaInferenceScheduleFragment>();
			TArrayView<FScholaInferenceStateFragment>	 States = Chunk.GetMutableFragmentView<FScholaInferenceStateFragment>();
			for (int32 I = 0; I < Chunk.GetNumEntities(); ++I)
			{
				const bool bBound = Bridge.InitializeSlot(Chunk.GetEntity(I), BucketId, Schedules[I], States[I], Now, PhaseLayout);
				if (bBound && Now + 1e-6 >= Schedules[I].NextDueTime)
				{
					AllDue.Add({ Chunk.GetEntity(I), Schedules[I].NextDueTime, BucketId });
				}
			}
		});
	}

	// The frame budget is shared across buckets: select the globally oldest rows
	// before regrouping compatible models into their separate submissions.
	AllDue.Sort([](const FDue& A, const FDue& B) { return A.Time < B.Time; });
	TMap<int32, TArray<FDue>> Buckets;
	if (GlobalMaxRowsPerFrame > 0)
	{
		const int32 AdmittedCount = FMath::Min(GlobalMaxRowsPerFrame, AllDue.Num());
		for (int32 I = 0; I < AllDue.Num(); ++I)
		{
			if (I < AdmittedCount)
			{
				Buckets.FindOrAdd(AllDue[I].Bucket).Add(AllDue[I]);
			}
			else
			{
				Bridge.AddDeferred(AllDue[I].Bucket, 1);
			}
		}
	}
	else
	{
		// Each bucket admits its oldest rows up to its own cap, so one frame's
		// submission always fits that bucket's queue.
		TMap<int32, int32> Caps;
		for (const FDue& Due : AllDue)
		{
			const int32* Cap = Caps.Find(Due.Bucket);
			if (!Cap)
			{
				Cap = &Caps.Add(Due.Bucket, Bridge.GetRowsPerFrame(Due.Bucket));
			}
			TArray<FDue>& Admitted = Buckets.FindOrAdd(Due.Bucket);
			if (Admitted.Num() < *Cap)
			{
				Admitted.Add(Due);
			}
			else
			{
				Bridge.AddDeferred(Due.Bucket, 1);
			}
		}
	}

	for (TPair<int32, TArray<FDue>>& Pair : Buckets)
	{
		TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler = Bridge.GetScheduler(Pair.Key);
		if (!Scheduler)
		{
			continue;
		}
		const int32				RowSize = Scheduler->GetObservationSize();
		const TArray<FDue>&		Due = Pair.Value;
		TSet<FMassEntityHandle> Admitted;
		for (const FDue& Row : Due)
		{
			Admitted.Add(Row.Entity);
		}

		// Rows are laid out chunk by chunk so each chunk writes one contiguous block.
		struct FChunkRows
		{
			TArray<int32> Indices;
			int32		  Offset = 0;
		};
		TArray<FMassEntityHandle>			Entities;
		TMap<FMassEntityHandle, FChunkRows> Chunks;
		Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk) {
			if (Chunk.GetConstSharedFragment<FScholaInferenceBucketFragment>().BucketId != Pair.Key)
			{
				return;
			}
			FChunkRows Block;
			Block.Offset = Entities.Num();
			for (int32 I = 0; I < Chunk.GetNumEntities(); ++I)
			{
				if (Admitted.Contains(Chunk.GetEntity(I)))
				{
					Block.Indices.Add(I);
					Entities.Add(Chunk.GetEntity(I));
				}
			}
			if (!Block.Indices.IsEmpty())
			{
				Chunks.Add(Chunk.GetEntity(0), MoveTemp(Block));
			}
		});

		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola.Bg.Mass.Submit");
		const auto Fill = [&](TArrayView<float> Rows) {
			const auto FillChunk = [&](FMassExecutionContext& Chunk) {
				const FChunkRows* Block = Chunks.Find(Chunk.GetEntity(0));
				if (!Block)
				{
					return;
				}
				const FScholaMassObservationRows ChunkRows(Block->Indices,
					Rows.Slice(Block->Offset * RowSize, Block->Indices.Num() * RowSize), RowSize);
				WriteObservations(Chunk, ChunkRows);
			};
			if (Entities.Num() >= ParallelGatherMinEntities)
			{
				Query.ParallelForEachEntityChunk(Context, FillChunk, FMassEntityQuery::EParallelExecutionFlags::Force);
			}
			else
			{
				Query.ForEachEntityChunk(Context, FillChunk);
			}
		};
		Bridge.Submit(Pair.Key, Manager, Entities, Fill, Now);
	}
}

UScholaMassApplyProcessor::UScholaMassApplyProcessor()
	: Query(*this)
{
	bRequiresGameThreadExecution = false;
	ExecutionOrder.ExecuteInGroup = TEXT("ScholaInference.Apply");
}

void UScholaMassApplyProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FScholaInferenceStateFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddConstSharedRequirement<FScholaInferenceBucketFragment>();
	Query.AddSubsystemRequirement<UScholaMassInferenceSubsystem>(EMassFragmentAccess::ReadWrite);
	Query.AddSubsystemRequirement<UMassSignalSubsystem>(EMassFragmentAccess::ReadWrite);
	ConfigureActionRequirements(Query);
}

void UScholaMassApplyProcessor::Execute(FMassEntityManager& Manager, FMassExecutionContext& Context)
{
	UScholaMassInferenceSubsystem* Subsystem = Context.GetWorld()->GetSubsystem<UScholaMassInferenceSubsystem>();
	if (!Subsystem)
	{
		return;
	}
	FScopeLock Lock(&Subsystem->Mutex);
	TRACE_CPUPROFILER_EVENT_SCOPE_STR("Schola.Bg.Mass.Apply");
	FScholaMassInferenceBridge& Bridge = Subsystem->Bridge;
	TArray<FMassEntityHandle>	Ready;
	Bridge.Drain(Manager, FPlatformTime::Seconds(), Ready);
	TArray<FMassArchetypeEntityCollection> Collections;
	UE::Mass::Utils::CreateEntityCollections(Manager, Ready, FMassArchetypeEntityCollection::NoDuplicates, Collections);

	TArray<int32> Indices;
	TArray<float> Rows;
	for (const FMassArchetypeEntityCollection& Collection : Collections)
	{
		Query.ForEachEntityChunk(Collection, Context, [&](FMassExecutionContext& Chunk) {
			Indices.Reset();
			Rows.Reset();
			const float											 MaxActionAgeMs = Bridge.GetMaxActionAgeMs(Chunk.GetConstSharedFragment<FScholaInferenceBucketFragment>().BucketId);
			const double										 Now = FPlatformTime::Seconds();
			const TConstArrayView<FScholaInferenceStateFragment> States = Chunk.GetFragmentView<FScholaInferenceStateFragment>();
			int32												 RowSize = 0;
			for (int32 I = 0; I < Chunk.GetNumEntities(); ++I)
			{
				const TConstArrayView<float> Action = Bridge.GetHeldAction(States[I]);
				const bool					 bFresh = MaxActionAgeMs <= 0 || (Now - Bridge.GetHeldObservationTime(States[I])) * 1000.0 <= MaxActionAgeMs;
				if (!Action.IsEmpty() && bFresh)
				{
					Indices.Add(I);
					Rows.Append(Action.GetData(), Action.Num());
					RowSize = Action.Num();
				}
				else
				{
					Bridge.DiscardReady(States[I]);
				}
			}
			if (Indices.IsEmpty())
			{
				return;
			}

			ApplyActions(Chunk, FScholaMassActionRows(Indices, Rows, RowSize));
			const double Delivered = FPlatformTime::Seconds();
			for (int32 I : Indices)
			{
				Bridge.MarkDelivered(States[I], Delivered);
			}
			if (!ActionReadySignal.IsNone())
			{
				TArray<FMassEntityHandle> Signals;
				for (int32 I : Indices)
				{
					Signals.Add(Chunk.GetEntity(I));
				}
				Chunk.GetMutableSubsystemChecked<UMassSignalSubsystem>().SignalEntitiesDeferred(Chunk, ActionReadySignal, Signals);
			}
		});
	}
}

UScholaMassScheduleObserver::UScholaMassScheduleObserver()
	: Query(*this)
{
	ObservedTypes.Add(FScholaInferenceStateFragment::StaticStruct());
	ObservedOperations = EMassObservedOperationFlags::All;
	bRequiresGameThreadExecution = false;
}

void UScholaMassScheduleObserver::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	Query.AddRequirement<FScholaInferenceStateFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddRequirement<FScholaInferenceScheduleFragment>(EMassFragmentAccess::ReadWrite);
	Query.AddConstSharedRequirement<FScholaInferenceBucketFragment>();
	Query.AddSubsystemRequirement<UScholaMassInferenceSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UScholaMassScheduleObserver::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
	UScholaMassInferenceSubsystem* Subsystem = Context.GetWorld() ? Context.GetWorld()->GetSubsystem<UScholaMassInferenceSubsystem>() : nullptr;
	if (!Subsystem)
	{
		return; // Standalone managers initialize through the bridge explicitly.
	}
	FScopeLock					 Lock(&Subsystem->Mutex);
	const EMassObservedOperation Operation = Context.GetAuxData().Get<FMassObserverExecutionContext>().GetOperationType();
	const bool					 bRemove = Operation == EMassObservedOperation::RemoveElement || Operation == EMassObservedOperation::DestroyEntity;
	Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk) {
		TArrayView<FScholaInferenceStateFragment>	 States = Chunk.GetMutableFragmentView<FScholaInferenceStateFragment>();
		TArrayView<FScholaInferenceScheduleFragment> Schedules = Chunk.GetMutableFragmentView<FScholaInferenceScheduleFragment>();
		const int32									 BucketId = Chunk.GetConstSharedFragment<FScholaInferenceBucketFragment>().BucketId;
		for (int32 I = 0; I < Chunk.GetNumEntities(); ++I)
		{
			if (bRemove)
			{
				Subsystem->Bridge.ReleaseSlot(States[I]);
			}
			else
			{
				Subsystem->Bridge.InitializeSlot(Chunk.GetEntity(I), BucketId, Schedules[I], States[I], FPlatformTime::Seconds());
			}
		}
	});
}

UScholaMassDecideNowProcessor::UScholaMassDecideNowProcessor()
{
	bRequiresGameThreadExecution = false;
	ExecutionOrder.ExecuteBefore.Add(TEXT("ScholaInference.Gather"));
}

void UScholaMassDecideNowProcessor::InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& Manager)
{
	Super::InitializeInternal(Owner, Manager);
	if (UWorld* World = Owner.GetWorld())
	{
		if (UMassSignalSubsystem* Signals = World->GetSubsystem<UMassSignalSubsystem>())
		{
			SubscribeToSignal(*Signals, TEXT("ScholaDecideNow"));
		}
	}
}

void UScholaMassDecideNowProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
	EntityQuery.AddRequirement<FScholaInferenceScheduleFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddRequirement<FScholaInferenceStateFragment>(EMassFragmentAccess::ReadWrite);
	EntityQuery.AddConstSharedRequirement<FScholaInferenceBucketFragment>();
	EntityQuery.AddSubsystemRequirement<UScholaMassInferenceSubsystem>(EMassFragmentAccess::ReadWrite);
}

void UScholaMassDecideNowProcessor::SignalEntities(FMassEntityManager&, FMassExecutionContext& Context, FMassSignalNameLookup&)
{
	EntityQuery.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk) {
		UScholaMassInferenceSubsystem&				 Subsystem = Chunk.GetMutableSubsystemChecked<UScholaMassInferenceSubsystem>();
		FScopeLock									 Lock(&Subsystem.Mutex);
		const double								 Now = FPlatformTime::Seconds();
		TArrayView<FScholaInferenceScheduleFragment> Schedules = Chunk.GetMutableFragmentView<FScholaInferenceScheduleFragment>();
		TArrayView<FScholaInferenceStateFragment>	 States = Chunk.GetMutableFragmentView<FScholaInferenceStateFragment>();
		const int32									 BucketId = Chunk.GetConstSharedFragment<FScholaInferenceBucketFragment>().BucketId;
		for (int32 I = 0; I < Chunk.GetNumEntities(); ++I)
		{
			if (Subsystem.Bridge.InitializeSlot(Chunk.GetEntity(I), BucketId, Schedules[I], States[I], Now))
			{
				Schedules[I].bUrgent = true;
				Schedules[I].NextDueTime = FMath::Min(Schedules[I].NextDueTime, Now);
			}
		}
	});
}

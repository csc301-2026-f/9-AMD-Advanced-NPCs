// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ScholaMassInferenceBridge.h"
#include "MassEntityManager.h"

FScholaMassInferenceBridge::~FScholaMassInferenceBridge()
{
	Shutdown();
}
void FScholaMassInferenceBridge::AddBucket(int32 Id, uint32 Generation,
	TSharedRef<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler, TSharedRef<FNPCCohortScheduler> Cohorts)
{
	check(!Buckets.Contains(Id));
	FBucket B;
	B.Generation = Generation;
	B.Scheduler = Scheduler;
	B.Cohorts = Cohorts;
	Buckets.Add(Id, MoveTemp(B));
}
TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> FScholaMassInferenceBridge::GetScheduler(int32 Id) const
{
	const auto* B = Buckets.Find(Id);
	return B ? B->Scheduler : nullptr;
}
bool FScholaMassInferenceBridge::InitializeSlot(FMassEntityHandle Entity, int32 BucketId,
	FScholaInferenceScheduleFragment& Schedule, FScholaInferenceStateFragment& State, double Now, TOptional<ENPCPhaseLayout> RequestedLayout)
{
	auto* B = Buckets.Find(BucketId);
	if (!B || !FMath::IsFinite(Schedule.PeriodSeconds) || Schedule.PeriodSeconds <= 0)
	{
		return false;
	}
	const bool			  bSameBinding = Slots.IsValidIndex(State.Slot) && Slots[State.Slot].Serial == State.SlotSerial && Slots[State.Slot].Entity == Entity && Slots[State.Slot].BucketId == BucketId;
	const ENPCPhaseLayout Layout = RequestedLayout.IsSet() ? RequestedLayout.GetValue() : bSameBinding ? Slots[State.Slot].Layout
																									   : ENPCPhaseLayout::ModelAware;
	auto				  Rephase = [&](FSlot& S) {
		Bind(S, -1);
		S.Period = Schedule.PeriodSeconds;
		S.Layout = Layout;
		S.Phase = B->Cohorts->Acquire(S.Period, Layout, GetTypeHash(Entity.AsNumber()));
		Schedule.PhaseSlot = S.Phase;
		Schedule.NextDueTime = B->Cohorts->NextDue(S.Period, S.Phase, Now);
		Bind(S, +1);
	};
	if (bSameBinding)
	{
		auto& S = Slots[State.Slot];
		if (S.Period != Schedule.PeriodSeconds || S.Layout != Layout)
		{
			// Scheduling changes preserve entity lifetime, pending observations,
			// and held/ready actions; only the phase allocation changes.
			B->Cohorts->Release(S.Period, S.Phase);
			Rephase(S);
		}
		if (Schedule.bUrgent)
		{
			Schedule.NextDueTime = FMath::Min(Schedule.NextDueTime, Now);
		}
		return true;
	}
	ReleaseSlot(State);
	State.Slot = FreeSlots.IsEmpty() ? Slots.AddDefaulted() : FreeSlots.Pop(EAllowShrinking::No);
	ReadySlots.Reserve(Slots.Num());
	auto& S = Slots[State.Slot];
	S.Entity = Entity;
	S.BucketId = BucketId;
	S.Period = 0;
	Rephase(S);
	S.DeliveredSerial = 0;
	S.Action.Reset();
	State.SlotSerial = S.Serial;
	State.SubmittedSerial = State.AppliedSerial = 0;
	if (Schedule.bUrgent)
	{
		Schedule.NextDueTime = FMath::Min(Schedule.NextDueTime, Now);
	}
	return true;
}
void FScholaMassInferenceBridge::ReleaseSlot(FScholaInferenceStateFragment& State)
{
	if (Slots.IsValidIndex(State.Slot))
	{
		auto& S = Slots[State.Slot];
		if (S.Serial == State.SlotSerial && S.BucketId != INDEX_NONE)
		{
			if (auto* B = Buckets.Find(S.BucketId))
			{
				B->Cohorts->Release(S.Period, S.Phase);
			}
			Bind(S, -1);
			ClearReady(S);
			++S.Serial;
			S.BucketId = INDEX_NONE;
			S.Entity = {};
			S.Action.Reset();
			FreeSlots.Add(State.Slot);
		}
	}
	State = {};
}
ENPCBackgroundStatus FScholaMassInferenceBridge::Submit(int32 Id, FMassEntityManager& Manager,
	TConstArrayView<FMassEntityHandle> Entities, TFunctionRef<void(TArrayView<float>)> Fill, double Now)
{
	auto* B = Buckets.Find(Id);
	if (!B || Entities.IsEmpty())
	{
		return ENPCBackgroundStatus::Invalid;
	}
	FRequest R;
	R.Serial = B->Cohorts->NextRequestSerial();
	R.ObsTime = Now;
	R.Entities.Append(Entities.GetData(), Entities.Num());
	TArray<int64> Keys;
	Keys.Reserve(Entities.Num());
	for (auto Entity : Entities)
	{
		if (!Manager.IsEntityValid(Entity))
		{
			return ENPCBackgroundStatus::Invalid;
		}
		auto* State = Manager.GetFragmentDataPtr<FScholaInferenceStateFragment>(Entity);
		auto* Bucket = Manager.GetConstSharedFragmentDataPtr<FScholaInferenceBucketFragment>(Entity);
		if (!State || !Bucket || Bucket->BucketId != Id || !Slots.IsValidIndex(State->Slot) || Slots[State->Slot].Serial != State->SlotSerial || Slots[State->Slot].Entity != Entity)
		{
			return ENPCBackgroundStatus::Invalid;
		}
		R.SlotSerials.Add(State->SlotSerial);
		Keys.Add(int64(Entity.AsNumber()));
	}
	FNPCBackgroundSubmitOptions Options;
	Options.Keys = Keys;
	Options.UserTag = (uint64(B->Generation) << 32) | R.Serial;
	const auto Status = B->Scheduler->SubmitWith(Entities.Num(), Fill, Options, R.Handle);
	if (Status != ENPCBackgroundStatus::Pending)
	{
		AddDeferred(Id, Entities.Num());
		return Status;
	}
	for (auto Entity : Entities)
	{
		auto& State = Manager.GetFragmentDataChecked<FScholaInferenceStateFragment>(Entity);
		auto& Schedule = Manager.GetFragmentDataChecked<FScholaInferenceScheduleFragment>(Entity);
		State.SubmittedSerial = R.Serial;
		State.ObsTime = Now;
		if (Schedule.bUrgent)
		{
			const auto& S = Slots[State.Slot];
			Schedule.NextDueTime = B->Cohorts->NextDue(S.Period, S.Phase, Now + 2e-6);
			Schedule.bUrgent = false;
		}
		else
		{
			Schedule.NextDueTime += (FMath::FloorToDouble(FMath::Max(0.0, Now - Schedule.NextDueTime) / Schedule.PeriodSeconds) + 1.0) * Schedule.PeriodSeconds;
		}
	}
	B->Pending.Add(MoveTemp(R));
	return Status;
}
void FScholaMassInferenceBridge::Drain(FMassEntityManager& Manager, double Now, TArray<FMassEntityHandle>& Ready)
{
	Ready.Reset();
	for (auto& Pair : Buckets)
	{
		auto&		B = Pair.Value;
		const float MaxAgeMs = B.Scheduler->GetSettings().DefaultMaxAgeMs;
		for (int32 P = B.Pending.Num() - 1; P >= 0; --P)
		{
			auto& R = B.Pending[P];
			if (B.Scheduler->GetStatus(R.Handle) == ENPCBackgroundStatus::Pending)
			{
				continue;
			}
			const auto Status = B.Scheduler->Consume(R.Handle, Result);
			for (int32 I = 0; I < R.Entities.Num(); ++I)
			{
				const auto Entity = R.Entities[I];
				if (Status != ENPCBackgroundStatus::Completed || B.Scheduler->IsShuttingDown() || !Manager.IsEntityValid(Entity) || uint32(Result.UserTag >> 32) != B.Generation || uint32(Result.UserTag) != R.Serial || (MaxAgeMs > 0 && (Now - R.ObsTime) * 1000.0 > MaxAgeMs))
				{
					CountDropped(Pair.Key);
					continue;
				}
				auto*		State = Manager.GetFragmentDataPtr<FScholaInferenceStateFragment>(Entity);
				const auto* Bucket = Manager.GetConstSharedFragmentDataPtr<FScholaInferenceBucketFragment>(Entity);
				if (!State || !Bucket || Bucket->BucketId != Pair.Key || State->SlotSerial != R.SlotSerials[I] || State->SubmittedSerial != R.Serial || R.Serial <= State->AppliedSerial || !Slots.IsValidIndex(State->Slot))
				{
					CountDropped(Pair.Key);
					continue;
				}
				auto& S = Slots[State->Slot];
				if (S.Serial != State->SlotSerial || S.Entity != Entity || S.BucketId != Pair.Key)
				{
					CountDropped(Pair.Key);
					continue;
				}
				S.Action = MakeArrayView(Result.Actions).Slice(I * Result.ActionSize, Result.ActionSize);
				S.ActionObsTime = R.ObsTime;
				State->AppliedSerial = R.Serial;
				if (S.ReadyIndex == INDEX_NONE)
				{
					S.ReadyIndex = ReadySlots.Add(State->Slot);
				}
			}
			B.Pending.RemoveAtSwap(P, 1, EAllowShrinking::No);
		}
	}
	for (int32 Slot : ReadySlots)
	{
		Ready.Add(Slots[Slot].Entity);
	}
}
TConstArrayView<float> FScholaMassInferenceBridge::GetHeldAction(const FScholaInferenceStateFragment& State) const
{
	if (!Slots.IsValidIndex(State.Slot) || Slots[State.Slot].Serial != State.SlotSerial || State.AppliedSerial == 0)
	{
		return {};
	}
	return Slots[State.Slot].Action;
}
void FScholaMassInferenceBridge::Shutdown()
{
	for (auto& Pair : Buckets)
	{
		for (const auto& R : Pair.Value.Pending)
		{
			Pair.Value.Scheduler->Cancel(R.Handle);
		}
	}
	for (auto& S : Slots)
	{
		if (auto* B = Buckets.Find(S.BucketId))
		{
			B->Cohorts->Release(S.Period, S.Phase);
		}
	}
	Buckets.Empty();
	Slots.Empty();
	FreeSlots.Empty();
	ReadySlots.Empty();
}

double FScholaMassInferenceBridge::GetHeldObservationTime(const FScholaInferenceStateFragment& State) const
{
	return Slots.IsValidIndex(State.Slot) && Slots[State.Slot].Serial == State.SlotSerial ? Slots[State.Slot].ActionObsTime : 0;
}

void FScholaMassInferenceBridge::ClearReady(FSlot& Slot)
{
	if (Slot.ReadyIndex == INDEX_NONE)
	{
		return;
	}
	const int32 Index = Slot.ReadyIndex;
	ReadySlots.RemoveAtSwap(Index, 1, EAllowShrinking::No);
	if (ReadySlots.IsValidIndex(Index))
	{
		Slots[ReadySlots[Index]].ReadyIndex = Index;
	}
	Slot.ReadyIndex = INDEX_NONE;
}
void FScholaMassInferenceBridge::MarkDelivered(const FScholaInferenceStateFragment& State, double Now)
{
	if (!Slots.IsValidIndex(State.Slot) || Slots[State.Slot].Serial != State.SlotSerial || Slots[State.Slot].ReadyIndex == INDEX_NONE)
	{
		return;
	}
	auto& S = Slots[State.Slot];
	S.DeliveredSerial = State.AppliedSerial;
	ClearReady(S);
	++AppliedRows;
	if (auto* B = Buckets.Find(S.BucketId))
	{
		++B->Counters.AppliedRows;
		if (Now > 0 && S.ActionObsTime > 0)
		{
			B->Counters.LatencySumMs += (Now - S.ActionObsTime) * 1000.0;
		}
	}
}
void FScholaMassInferenceBridge::DiscardReady(const FScholaInferenceStateFragment& State)
{
	if (Slots.IsValidIndex(State.Slot) && Slots[State.Slot].Serial == State.SlotSerial && Slots[State.Slot].ReadyIndex != INDEX_NONE)
	{
		ClearReady(Slots[State.Slot]);
		CountDropped(Slots[State.Slot].BucketId);
	}
}

bool FScholaMassInferenceBridge::IsActionDelivered(const FScholaInferenceStateFragment& State) const
{
	return Slots.IsValidIndex(State.Slot) && Slots[State.Slot].Serial == State.SlotSerial && State.AppliedSerial > 0 && Slots[State.Slot].DeliveredSerial == State.AppliedSerial && Slots[State.Slot].ReadyIndex == INDEX_NONE && !Slots[State.Slot].Action.IsEmpty();
}

void FScholaMassInferenceBridge::CountDropped(int32 BucketId)
{
	++DroppedRows;
	if (auto* B = Buckets.Find(BucketId))
	{
		++B->Counters.DroppedRows;
	}
}
void FScholaMassInferenceBridge::Bind(FSlot& Slot, int32 Delta)
{
	auto* B = Buckets.Find(Slot.BucketId);
	if (!B || Slot.Period <= 0)
	{
		return;
	}
	B->Counters.Agents += Delta;
	B->Counters.RequestedRowsPerSecond = B->Counters.Agents > 0 ? FMath::Max(0.0, B->Counters.RequestedRowsPerSecond + Delta / double(Slot.Period)) : 0.0;
}
void FScholaMassInferenceBridge::AddDeferred(int32 Id, int64 Rows)
{
	DeferredRows += Rows;
	if (auto* B = Buckets.Find(Id))
	{
		B->Counters.DeferredRows += Rows;
	}
}
void FScholaMassInferenceBridge::SetRowsPerFrame(int32 Id, int32 Rows)
{
	if (auto* B = Buckets.Find(Id))
	{
		B->RowsPerFrame = FMath::Max(0, Rows);
	}
}
int32 FScholaMassInferenceBridge::GetRowsPerFrame(int32 Id) const
{
	const auto* B = Buckets.Find(Id);
	if (!B || !B->Scheduler)
	{
		return 1;
	}
	const int32 QueueLimit = B->Scheduler->GetSettings().MaxPendingRows;
	return FMath::Clamp(B->RowsPerFrame > 0 ? B->RowsPerFrame : QueueLimit, 1, FMath::Max(1, QueueLimit));
}
float FScholaMassInferenceBridge::GetMaxActionAgeMs(int32 Id) const
{
	const auto* B = Buckets.Find(Id);
	return B && B->Scheduler ? B->Scheduler->GetSettings().DefaultMaxAgeMs : 0.f;
}
FScholaMassBucketCounters FScholaMassInferenceBridge::GetBucketCounters(int32 Id) const
{
	const auto* B = Buckets.Find(Id);
	return B ? B->Counters : FScholaMassBucketCounters();
}

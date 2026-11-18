// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassObserverProcessor.h"
#include "MassSignalProcessorBase.h"
#include "MassEntityQuery.h"
#include "Inference/CohortScheduler.h"
#include "ScholaMassInferenceProcessors.generated.h"

/** @brief One agent's row in a gather or apply pass. */
template <typename T>
struct TScholaMassRow
{
	/** @brief Index of the agent in the chunk; use it with the chunk's fragment views. */
	int32 EntityIndex;

	/** @brief The agent's observation (gather) or action (apply) floats. */
	TArrayView<T> Values;
};

/**
 * @brief The rows of one chunk, one per agent. Supports range-for and indexing.
 *
 * @code
 * auto Agents = Context.GetMutableFragmentView<FMyAgentFragment>();
 * for (const auto& Row : Rows)
 * {
 * 	WriteMyObservation(Agents[Row.EntityIndex], Row.Values);
 * }
 * @endcode
 */
template <typename T>
class TScholaMassRows
{
public:
	TScholaMassRows(TConstArrayView<int32> InEntityIndices, TArrayView<T> InData, int32 InRowSize)
		: EntityIndices(InEntityIndices), Data(InData), RowSize(InRowSize)
	{
		check(Data.Num() == EntityIndices.Num() * RowSize);
	}

	int32 Num() const { return EntityIndices.Num(); }
	int32 GetRowSize() const { return RowSize; }

	TScholaMassRow<T> operator[](int32 Row) const
	{
		return { EntityIndices[Row], Data.Slice(Row * RowSize, RowSize) };
	}

	struct FIterator
	{
		const TScholaMassRows* Rows;
		int32				   Row;
		TScholaMassRow<T>	   operator*() const { return (*Rows)[Row]; }
		FIterator&			   operator++()
		{
			++Row;
			return *this;
		}
		bool operator!=(const FIterator& Other) const { return Row != Other.Row; }
	};

	FIterator begin() const { return { this, 0 }; }
	FIterator end() const { return { this, Num() }; }

private:
	TConstArrayView<int32> EntityIndices;
	TArrayView<T>		   Data;
	int32				   RowSize;
};

/** @brief Observation rows written by UScholaMassGatherProcessor::WriteObservations. */
using FScholaMassObservationRows = TScholaMassRows<float>;

/** @brief Action rows read by UScholaMassApplyProcessor::ApplyActions. */
using FScholaMassActionRows = TScholaMassRows<const float>;

/**
 * @brief Submits observations for agents whose next decision is due.
 *
 * Subclass it, add the fragments you read in ConfigureObservationRequirements, and fill each
 * row in WriteObservations. Queue limits and the action age limit come from the agents' policy.
 */
UCLASS(Abstract)
class SCHOLAMASS_API UScholaMassGatherProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UScholaMassGatherProcessor();

	/** @brief How agents' decision phases are spread across frames. */
	UPROPERTY(EditAnywhere, Config, Category = "Schola|Mass")
	ENPCPhaseLayout PhaseLayout = ENPCPhaseLayout::ModelAware;

	/** @brief Submissions at least this large write observations in parallel across chunks. */
	UPROPERTY(EditAnywhere, Config, Category = "Schola|Mass")
	int32 ParallelGatherMinEntities = 2048;

	/**
	 * @brief Rows submitted per frame across all policies, oldest first.
	 *
	 * 0 (default) lets each policy admit up to its own MaxRowsPerFrame, so a submission always fits its queue.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Schola|Mass", meta = (ClampMin = "0"))
	int32 GlobalMaxRowsPerFrame = 0;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override;

	/** @brief Add requirements for the fragments WriteObservations reads. */
	virtual void ConfigureObservationRequirements(FMassEntityQuery& InQuery) {}

	/**
	 * @brief Writes one observation row per due agent in a chunk.
	 *
	 * May run on several worker threads at once for different chunks. Touch only this chunk's fragments.
	 *
	 * @param[in] Context The chunk being gathered.
	 * @param[out] Rows One row per due agent, GetRowSize() floats each.
	 */
	virtual void WriteObservations(FMassExecutionContext& Context, const FScholaMassObservationRows& Rows)
		PURE_VIRTUAL(UScholaMassGatherProcessor::WriteObservations, );

	FMassEntityQuery Query;
};

/**
 * @brief Delivers completed actions to their agents.
 *
 * Subclass it, add the fragments you write in ConfigureActionRequirements, and read each row in ApplyActions.
 * Run it before the gather processor each frame; actions older than the policy's MaxActionAgeMs are dropped.
 */
UCLASS(Abstract)
class SCHOLAMASS_API UScholaMassApplyProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UScholaMassApplyProcessor();

	/** @brief Signal raised for each entity that received an action. None disables it. */
	UPROPERTY(EditAnywhere, Config, Category = "Schola|Mass")
	FName ActionReadySignal = NAME_None;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override;

	/** @brief Add requirements for the fragments ApplyActions writes. */
	virtual void ConfigureActionRequirements(FMassEntityQuery& InQuery) {}

	/**
	 * @brief Applies one action row per agent in a chunk.
	 *
	 * @param[in] Context The chunk receiving actions.
	 * @param[in] Rows One row per agent with a new action, GetRowSize() floats each.
	 */
	virtual void ApplyActions(FMassExecutionContext& Context, const FScholaMassActionRows& Rows)
		PURE_VIRTUAL(UScholaMassApplyProcessor::ApplyActions, );

	FMassEntityQuery Query;
};

/** @brief Binds entities to their policy when they gain ScholaMass fragments and releases them when they lose them. */
UCLASS()
class SCHOLAMASS_API UScholaMassScheduleObserver : public UMassObserverProcessor
{
	GENERATED_BODY()

public:
	UScholaMassScheduleObserver();

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override;

	FMassEntityQuery Query;
};

/** @brief Makes signalled entities due on the next gather. Raise the ScholaDecideNow signal to use it. */
UCLASS()
class SCHOLAMASS_API UScholaMassDecideNowProcessor : public UMassSignalProcessorBase
{
	GENERATED_BODY()

public:
	UScholaMassDecideNowProcessor();

protected:
	virtual void InitializeInternal(UObject& Owner, const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void SignalEntities(FMassEntityManager& Manager, FMassExecutionContext& Context, FMassSignalNameLookup& Signals) override;
};

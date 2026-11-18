// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "MassProcessor.h"
#include "MassEntityQuery.h"
#include "ScholaMassInferenceProcessors.h"
#include "CrowdTypes.h"
#include "CrowdProcessors.generated.h"

class ACampusScene;

/** @brief Writes an observation row for each student the ScholaMass scheduler marks due. */
UCLASS()
class UCrowdGatherProcessor : public UScholaMassGatherProcessor
{
	GENERATED_BODY()

public:
	UCrowdGatherProcessor();

	float HalfSize = 0;

protected:
	virtual void ConfigureObservationRequirements(FMassEntityQuery& InQuery) override;
	virtual void WriteObservations(FMassExecutionContext& Context, const FScholaMassObservationRows& Rows) override;
};

/** @brief Applies completed background actions on the game thread. */
UCLASS()
class UCrowdApplyProcessor : public UScholaMassApplyProcessor
{
	GENERATED_BODY()

public:
	UCrowdApplyProcessor();

protected:
	virtual void ConfigureActionRequirements(FMassEntityQuery& InQuery) override;
	virtual void ApplyActions(FMassExecutionContext& Context, const FScholaMassActionRows& Rows) override;
};

/** @brief Steers each student toward its latest action and writes instance transforms. */
UCLASS()
class UCrowdMoveProcessor : public UMassProcessor
{
	GENERATED_BODY()

public:
	UCrowdMoveProcessor();

	const ACampusScene* Campus = nullptr;
	TArray<FTransform>* Transforms = nullptr;
	float				HalfSize = 0;

protected:
	virtual void ConfigureQueries(const TSharedRef<FMassEntityManager>& Manager) override;
	virtual void Execute(FMassEntityManager& Manager, FMassExecutionContext& Context) override;

	FMassEntityQuery Query;

	// Separation grid, rebuilt each frame by counting sort and reused to avoid per-frame allocation.
	TArray<FVector2f> Positions;
	TArray<int32>	  CellOf;
	TArray<int32>	  CellStart;
	TArray<int32>	  CellFill;
	TArray<int32>	  Sorted;
};

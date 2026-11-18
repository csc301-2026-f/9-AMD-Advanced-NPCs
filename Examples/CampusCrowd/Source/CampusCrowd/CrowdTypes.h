// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "MassEntityTypes.h"
#include "CrowdTypes.generated.h"

CAMPUSCROWD_API DECLARE_LOG_CATEGORY_EXTERN(LogCampusCrowd, Log, All);

/** @brief Per-student simulation state. */
USTRUCT()
struct FCrowdAgentFragment : public FMassFragment
{
	GENERATED_BODY()

	FVector2f Position = FVector2f::ZeroVector;
	FVector2f Velocity = FVector2f::ZeroVector;
	/** @brief Latest steering direction returned by the policy. */
	FVector2f Steering = FVector2f(1, 0);
	FVector2f Goal = FVector2f::ZeroVector;
	int32	  PreviousNode = INDEX_NONE;
	int32	  CurrentNode = 0;
	int32	  NextNode = 0;
	float	  Lane = 0;
	float	  WalkSpeed = 140;
	float	  WaitSeconds = 0;
	int32	  Instance = 0;
	uint32	  Random = 1;
};

namespace CrowdSim
{
	constexpr int32 ObservationSize = 64;
	constexpr int32 ActionSize = 2;
	constexpr float Speed = 260.f;

	uint32	  NextRandom(uint32& State);
	FVector2f RandomPoint(uint32& State, float HalfSize);
	void	  WriteObservation(const FCrowdAgentFragment& Agent, float HalfSize, TArrayView<float> Row);
	void	  ApplyAction(FCrowdAgentFragment& Agent, TConstArrayView<float> Row);
} // namespace CrowdSim

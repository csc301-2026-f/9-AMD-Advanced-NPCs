// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "MassEntityTypes.h"
#include "CrowdTypes.generated.h"

UENUM()
enum class ECrowdInferenceMode : uint8
{
    /** Without the API: every due agent runs its own batch-1 model call on the game thread. */
    Synchronous,
    /** With the API: ScholaMass gathers due rows; batched inference runs on background workers. */
    Background,
};

/** Per-agent simulation state. Movement and observations are identical in both modes. */
USTRUCT()
struct FCrowdAgentFragment : public FMassFragment
{
    GENERATED_BODY()
    FVector2f Position = FVector2f::ZeroVector;
    FVector2f Velocity = FVector2f::ZeroVector;
    /** Latest steering direction returned by the policy. */
    FVector2f Steering = FVector2f(1, 0);
    FVector2f Goal = FVector2f::ZeroVector;
    int32 Instance = 0;
    uint32 Random = 1;
    double ObservationTime = 0;
    double SyncNextDue = 0;
};

/** Counters filled by the inference processors and read once per frame by the host. */
struct FCrowdFrameCounters
{
    int64 Decisions = 0;
    double LatencySumMs = 0;
};

namespace CrowdSim
{
constexpr int32 ObservationSize = 64;
constexpr int32 ActionSize = 2;
constexpr float Speed = 260.f;

uint32 NextRandom(uint32& State);
FVector2f RandomPoint(uint32& State, float HalfSize);
void WriteObservation(const FCrowdAgentFragment& Agent, float HalfSize, TArrayView<float> Row);
void ApplyAction(FCrowdAgentFragment& Agent, TConstArrayView<float> Row, FCrowdFrameCounters& Counters);
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdProcessors.h"
#include "MassExecutionContext.h"
#include "NNERuntimeCPU.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

uint32 CrowdSim::NextRandom(uint32& State)
{
    State ^= State << 13; State ^= State >> 17; State ^= State << 5;
    return State;
}

FVector2f CrowdSim::RandomPoint(uint32& State, float HalfSize)
{
    const float X = NextRandom(State) / float(MAX_uint32), Y = NextRandom(State) / float(MAX_uint32);
    return FVector2f((X * 2 - 1) * HalfSize * .95f, (Y * 2 - 1) * HalfSize * .95f);
}

void CrowdSim::WriteObservation(const FCrowdAgentFragment& Agent, float HalfSize, TArrayView<float> Row)
{
    check(Row.Num() == ObservationSize);
    const FVector2f ToGoal = Agent.Goal - Agent.Position;
    const FVector2f Direction = ToGoal.GetSafeNormal();
    Row[0] = Direction.X;
    Row[1] = Direction.Y;
    Row[2] = FMath::Min(ToGoal.Size() / 1000.f, 4.f);
    Row[3] = Agent.Velocity.X / Speed;
    Row[4] = Agent.Velocity.Y / Speed;
    // Smooth positional features: the policy's wander term varies across the arena.
    const FVector2f P = Agent.Position / FMath::Max(HalfSize, 1.f);
    for (int32 I = 5; I < ObservationSize; ++I)
    {
        const float Frequency = 1.f + (I - 5) / 2 * .5f;
        Row[I] = (I & 1) ? FMath::Sin(P.X * Frequency + P.Y) : FMath::Cos(P.Y * Frequency - P.X);
    }
}

void CrowdSim::ApplyAction(FCrowdAgentFragment& Agent, TConstArrayView<float> Row, FCrowdFrameCounters& Counters)
{
    const FVector2f Action(Row[0], Row[1]);
    if (Action.SizeSquared() > 1e-6f) Agent.Steering = Action.GetSafeNormal();
    ++Counters.Decisions;
    Counters.LatencySumMs += (FPlatformTime::Seconds() - Agent.ObservationTime) * 1000.0;
}

UCrowdGatherProcessor::UCrowdGatherProcessor() { bAutoRegisterWithProcessingPhases = false; }

void UCrowdGatherProcessor::ConfigureObservationRequirements(FMassEntityQuery& InQuery)
{
    InQuery.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UCrowdGatherProcessor::WriteObservations(FMassExecutionContext& Context, TConstArrayView<int32> DueIndices, TArrayView<float> Rows)
{
    // May run on several worker threads at once; only this chunk's rows and fragments are touched.
    auto Agents = Context.GetMutableFragmentView<FCrowdAgentFragment>();
    const double Now = FPlatformTime::Seconds();
    for (int32 R = 0; R < DueIndices.Num(); ++R)
    {
        auto& Agent = Agents[DueIndices[R]];
        Agent.ObservationTime = Now;
        CrowdSim::WriteObservation(Agent, HalfSize, Rows.Slice(R * CrowdSim::ObservationSize, CrowdSim::ObservationSize));
    }
}

UCrowdApplyProcessor::UCrowdApplyProcessor() { bAutoRegisterWithProcessingPhases = false; }

void UCrowdApplyProcessor::ConfigureActionRequirements(FMassEntityQuery& InQuery)
{
    InQuery.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UCrowdApplyProcessor::ApplyActions(FMassExecutionContext& Context, TConstArrayView<int32> EntityIndices, TConstArrayView<float> Rows)
{
    auto Agents = Context.GetMutableFragmentView<FCrowdAgentFragment>();
    for (int32 R = 0; R < EntityIndices.Num(); ++R)
        CrowdSim::ApplyAction(Agents[EntityIndices[R]], Rows.Slice(R * CrowdSim::ActionSize, CrowdSim::ActionSize), *Counters);
}

UCrowdSyncInferenceProcessor::UCrowdSyncInferenceProcessor() : Query(*this)
{
    bAutoRegisterWithProcessingPhases = false;
    bRequiresGameThreadExecution = true;
}

void UCrowdSyncInferenceProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
    Query.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UCrowdSyncInferenceProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
    if (!Instance || bFailed) return;
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("Crowd.SyncInference");
    float Observation[CrowdSim::ObservationSize];
    float Action[CrowdSim::ActionSize];
    const UE::NNE::FTensorBindingCPU Input{Observation, sizeof(Observation)};
    const UE::NNE::FTensorBindingCPU Output{Action, sizeof(Action)};
    Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
    {
        auto Agents = Chunk.GetMutableFragmentView<FCrowdAgentFragment>();
        for (auto& Agent : Agents)
        {
            const double Now = FPlatformTime::Seconds();
            if (bFailed || Now < Agent.SyncNextDue) continue;
            Agent.ObservationTime = Now;
            CrowdSim::WriteObservation(Agent, HalfSize, Observation);
            if (Instance->RunSync(MakeArrayView(&Input, 1), MakeArrayView(&Output, 1)) != UE::NNE::EResultStatus::Ok)
            {
                bFailed = true;
                return;
            }
            CrowdSim::ApplyAction(Agent, Action, *Counters);
            // Skip missed periods rather than queueing a backlog of stale decisions.
            Agent.SyncNextDue += (FMath::FloorToDouble((Now - Agent.SyncNextDue) / PeriodSeconds) + 1) * PeriodSeconds;
        }
    });
}

UCrowdMoveProcessor::UCrowdMoveProcessor() : Query(*this)
{
    bAutoRegisterWithProcessingPhases = false;
}

void UCrowdMoveProcessor::ConfigureQueries(const TSharedRef<FMassEntityManager>&)
{
    Query.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UCrowdMoveProcessor::Execute(FMassEntityManager&, FMassExecutionContext& Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE_STR("Crowd.Move");
    const float DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), .1f);
    const float Blend = 1.f - FMath::Exp(-3.f * DeltaTime);
    Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
    {
        for (auto& Agent : Chunk.GetMutableFragmentView<FCrowdAgentFragment>())
        {
            if (FVector2f::DistSquared(Agent.Position, Agent.Goal) < FMath::Square(120.f))
                Agent.Goal = CrowdSim::RandomPoint(Agent.Random, HalfSize);
            Agent.Velocity += (Agent.Steering * CrowdSim::Speed - Agent.Velocity) * Blend;
            Agent.Position += Agent.Velocity * DeltaTime;
            Agent.Position.X = FMath::Clamp(Agent.Position.X, -HalfSize, HalfSize);
            Agent.Position.Y = FMath::Clamp(Agent.Position.Y, -HalfSize, HalfSize);
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Agent.Velocity.Y, Agent.Velocity.X));
            (*Transforms)[Agent.Instance] = FTransform(FRotator(0, Yaw, 0),
                FVector(Agent.Position.X, Agent.Position.Y, 40), FVector(.7f, .3f, .8f));
        }
    });
}

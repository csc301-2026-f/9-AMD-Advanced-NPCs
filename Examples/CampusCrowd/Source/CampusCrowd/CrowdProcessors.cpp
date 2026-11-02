// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdProcessors.h"
#include "CampusScene.h"
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
    struct FNeighbour { FVector2f Position; int32 Instance; };
    TArray<FNeighbour> Neighbours;
    TMap<FIntPoint, TArray<int32>> Grid;
    constexpr float CellSize = 150.f;
    const auto Cell = [](FVector2f P) { return FIntPoint(FMath::FloorToInt(P.X / CellSize), FMath::FloorToInt(P.Y / CellSize)); };
    Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
    {
        for (const auto& Agent : Chunk.GetMutableFragmentView<FCrowdAgentFragment>())
        {
            Grid.FindOrAdd(Cell(Agent.Position)).Add(Neighbours.Num());
            Neighbours.Add({Agent.Position, Agent.Instance});
        }
    });
    Query.ForEachEntityChunk(Context, [&](FMassExecutionContext& Chunk)
    {
        for (auto& Agent : Chunk.GetMutableFragmentView<FCrowdAgentFragment>())
        {
            if (!Campus || Campus->Nodes.IsEmpty()) continue;
            if (Agent.WaitSeconds > 0)
            {
                Agent.WaitSeconds -= DeltaTime;
                Agent.Velocity *= .8f;
            }
            else
            {
                if (FVector2f::DistSquared(Agent.Position, Agent.Goal) < FMath::Square(110.f))
                {
                    Agent.PreviousNode = Agent.CurrentNode;
                    Agent.CurrentNode = Agent.NextNode;
                    const auto& Links = Campus->Nodes[Agent.CurrentNode].Links;
                    int32 Choice = CrowdSim::NextRandom(Agent.Random) % Links.Num();
                    if (Links.Num() > 1 && Links[Choice] == Agent.PreviousNode) Choice = (Choice + 1) % Links.Num();
                    Agent.NextNode = Links[Choice];
                    Agent.Goal = Campus->Nodes[Agent.NextNode].Position;
                    if (CrowdSim::NextRandom(Agent.Random) % 12 == 0) Agent.WaitSeconds = 1.f + CrowdSim::NextRandom(Agent.Random) % 4;
                }
                const FVector2f Start = Campus->Nodes[Agent.CurrentNode].Position;
                const FVector2f End = Campus->Nodes[Agent.NextNode].Position;
                const FVector2f Along = (End - Start).GetSafeNormal();
                const FVector2f Side(-Along.Y, Along.X);
                const FVector2f LaneTarget = End + Side * Agent.Lane;
                const FVector2f Desired = (LaneTarget - Agent.Position).GetSafeNormal();
                // ONNX steering is bounded by the route corridor, which keeps students outside CAD footprints.
                FVector2f Separation = FVector2f::ZeroVector;
                const FIntPoint Here = Cell(Agent.Position);
                for (int32 DX=-1; DX<=1; ++DX) for (int32 DY=-1; DY<=1; ++DY)
                {
                    const auto* Nearby = Grid.Find(Here + FIntPoint(DX,DY));
                    if (!Nearby) continue;
                    for (int32 Index : *Nearby)
                    {
                        const auto& Other = Neighbours[Index];
                        if (Other.Instance == Agent.Instance) continue;
                        const FVector2f Away = Agent.Position - Other.Position;
                        const float Distance = Away.Size();
                        if (Distance > .1f && Distance < 100.f) Separation += Away / Distance * (1.f - Distance/100.f);
                    }
                }
                Separation = Separation.GetClampedToMaxSize(1.f);
                const FVector2f Direction = (Desired * .85f + Agent.Steering * .15f + Separation * .9f).GetSafeNormal();
                Agent.Velocity += (Direction * Agent.WalkSpeed - Agent.Velocity) * Blend;
                Agent.Position += Agent.Velocity * DeltaTime;
                const float CrossTrack = FVector2f::DotProduct(Agent.Position - Start, Side);
                Agent.Position -= Side * (CrossTrack - FMath::Clamp(CrossTrack, -160.f, 160.f));
            }
            const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Agent.Velocity.Y, Agent.Velocity.X));
            (*Transforms)[Agent.Instance] = FTransform(FRotator(0, Yaw, 0),
                FVector(Agent.Position.X, Agent.Position.Y, 94), FVector(.6f, .6f, 1.8f));
        }
    });
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdProcessors.h"
#include "CampusScene.h"
#include "MassExecutionContext.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

uint32 CrowdSim::NextRandom(uint32& State)
{
	State ^= State << 13;
	State ^= State >> 17;
	State ^= State << 5;
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

void CrowdSim::ApplyAction(FCrowdAgentFragment& Agent, TConstArrayView<float> Row)
{
	const FVector2f Action(Row[0], Row[1]);
	if (Action.SizeSquared() > 1e-6f)
	{
		Agent.Steering = Action.GetSafeNormal();
	}
}

UCrowdGatherProcessor::UCrowdGatherProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
}

void UCrowdGatherProcessor::ConfigureObservationRequirements(FMassEntityQuery& InQuery)
{
	InQuery.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadOnly);
}

void UCrowdGatherProcessor::WriteObservations(FMassExecutionContext& Context, const FScholaMassObservationRows& Rows)
{
	// May run on several worker threads at once; only this chunk's rows and fragments are touched.
	const auto Agents = Context.GetFragmentView<FCrowdAgentFragment>();
	for (const auto& Row : Rows)
	{
		CrowdSim::WriteObservation(Agents[Row.EntityIndex], HalfSize, Row.Values);
	}
}

UCrowdApplyProcessor::UCrowdApplyProcessor()
{
	bAutoRegisterWithProcessingPhases = false;
}

void UCrowdApplyProcessor::ConfigureActionRequirements(FMassEntityQuery& InQuery)
{
	InQuery.AddRequirement<FCrowdAgentFragment>(EMassFragmentAccess::ReadWrite);
}

void UCrowdApplyProcessor::ApplyActions(FMassExecutionContext& Context, const FScholaMassActionRows& Rows)
{
	auto Agents = Context.GetMutableFragmentView<FCrowdAgentFragment>();
	for (const auto& Row : Rows)
	{
		CrowdSim::ApplyAction(Agents[Row.EntityIndex], Row.Values);
	}
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
	if (!Campus || Campus->Nodes.IsEmpty() || !Transforms)
	{
		return;
	}
	const float		DeltaTime = FMath::Min(Context.GetDeltaTimeSeconds(), .1f);
	const float		Blend = 1.f - FMath::Exp(-3.f * DeltaTime);
	constexpr float CellSize = 150.f, SeparationRadius = 100.f;
	static_assert(SeparationRadius <= CellSize, "Separation only searches neighbouring cells");

	// Snapshot positions by instance so every agent reads last frame's neighbours.
	const int32 Count = Transforms->Num();
	Positions.SetNumUninitialized(Count, EAllowShrinking::No);
	const auto Snapshot = [this](FMassExecutionContext& Chunk) {
		for (const FCrowdAgentFragment& Agent : Chunk.GetFragmentView<FCrowdAgentFragment>())
		{
			Positions[Agent.Instance] = Agent.Position;
		}
	};
	Query.ParallelForEachEntityChunk(Context, Snapshot, FMassEntityQuery::EParallelExecutionFlags::Force);

	// Dense grid over the campus, filled by counting sort; positions outside it clamp to the border cells.
	const float Half = FMath::Max(HalfSize, CellSize);
	const int32 Side = FMath::CeilToInt32(2 * Half / CellSize) + 1;
	const auto	CellXY = [Half, Side](FVector2f P) {
		return FIntPoint(FMath::Clamp(FMath::FloorToInt32((P.X + Half) / CellSize), 0, Side - 1),
			FMath::Clamp(FMath::FloorToInt32((P.Y + Half) / CellSize), 0, Side - 1));
	};
	{
		TRACE_CPUPROFILER_EVENT_SCOPE_STR("Crowd.Move.Grid");
		CellOf.SetNumUninitialized(Count, EAllowShrinking::No);
		CellStart.SetNumUninitialized(Side * Side + 1, EAllowShrinking::No);
		FMemory::Memzero(CellStart.GetData(), CellStart.Num() * sizeof(int32));
		Sorted.SetNumUninitialized(Count, EAllowShrinking::No);
		for (int32 I = 0; I < Count; ++I)
		{
			const FIntPoint C = CellXY(Positions[I]);
			CellOf[I] = C.Y * Side + C.X;
			++CellStart[CellOf[I] + 1];
		}
		for (int32 C = 1; C < CellStart.Num(); ++C)
		{
			CellStart[C] += CellStart[C - 1];
		}
		CellFill = CellStart;
		for (int32 I = 0; I < Count; ++I)
		{
			Sorted[CellFill[CellOf[I]]++] = I;
		}
	}

	const auto Step = [&](FMassExecutionContext& Chunk) {
		// Each agent writes only its own fragment and transform; neighbours come from the snapshot.
		for (auto& Agent : Chunk.GetMutableFragmentView<FCrowdAgentFragment>())
		{
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
					int32		Choice = CrowdSim::NextRandom(Agent.Random) % Links.Num();
					if (Links.Num() > 1 && Links[Choice] == Agent.PreviousNode)
					{
						Choice = (Choice + 1) % Links.Num();
					}
					Agent.NextNode = Links[Choice];
					Agent.Goal = Campus->Nodes[Agent.NextNode].Position;
					if (CrowdSim::NextRandom(Agent.Random) % 12 == 0)
					{
						Agent.WaitSeconds = 1.f + CrowdSim::NextRandom(Agent.Random) % 4;
					}
				}
				const FVector2f Start = Campus->Nodes[Agent.CurrentNode].Position;
				const FVector2f End = Campus->Nodes[Agent.NextNode].Position;
				const FVector2f Along = (End - Start).GetSafeNormal();
				const FVector2f Side2(-Along.Y, Along.X);
				const FVector2f LaneTarget = End + Side2 * Agent.Lane;
				const FVector2f Desired = (LaneTarget - Agent.Position).GetSafeNormal();
				// ONNX steering is bounded by the route corridor, which keeps students outside CAD footprints.
				FVector2f		Separation = FVector2f::ZeroVector;
				const FIntPoint Here = CellXY(Agent.Position);
				for (int32 CY = FMath::Max(Here.Y - 1, 0); CY <= FMath::Min(Here.Y + 1, Side - 1); ++CY)
				{
					for (int32 CX = FMath::Max(Here.X - 1, 0); CX <= FMath::Min(Here.X + 1, Side - 1); ++CX)
					{
						const int32 C = CY * Side + CX;
						for (int32 S = CellStart[C]; S < CellStart[C + 1]; ++S)
						{
							const int32 Other = Sorted[S];
							if (Other == Agent.Instance)
							{
								continue;
							}
							const FVector2f Away = Agent.Position - Positions[Other];
							const float		Distance = Away.Size();
							if (Distance > .1f && Distance < SeparationRadius)
							{
								Separation += Away / Distance * (1.f - Distance / SeparationRadius);
							}
						}
					}
				}
				Separation = Separation.GetClampedToMaxSize(1.f);
				const FVector2f Direction = (Desired * .85f + Agent.Steering * .15f + Separation * .9f).GetSafeNormal();
				Agent.Velocity += (Direction * Agent.WalkSpeed - Agent.Velocity) * Blend;
				Agent.Position += Agent.Velocity * DeltaTime;
				const float CrossTrack = FVector2f::DotProduct(Agent.Position - Start, Side2);
				Agent.Position -= Side2 * (CrossTrack - FMath::Clamp(CrossTrack, -160.f, 160.f));
			}
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Agent.Velocity.Y, Agent.Velocity.X));
			(*Transforms)[Agent.Instance] = FTransform(FRotator(0, Yaw, 0),
				FVector(Agent.Position.X, Agent.Position.Y, 94), FVector(.6f, .6f, 1.8f));
		}
	};
	Query.ParallelForEachEntityChunk(Context, Step, FMassEntityQuery::EParallelExecutionFlags::Force);
}

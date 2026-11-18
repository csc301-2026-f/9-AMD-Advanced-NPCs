// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ScholaMassBridgeTestTypes.h"
#if WITH_DEV_AUTOMATION_TESTS
	#include "Misc/AutomationTest.h"
	#include "ScholaMassInferenceSubsystem.h"
	#include "ScholaMassPolicy.h"
	#include "MassEntitySubsystem.h"
	#include "MassEntityView.h"
	#include "MassExecutor.h"
	#include "MassProcessingContext.h"
	#include "Spaces/BoxSpace.h"

namespace
{
	FScholaMassPolicyConfig TestConfig(UNPCBackgroundBackendProvider* Provider)
	{
		FScholaMassPolicyConfig Config;
		Config.Provider = Provider;
		const auto Space = TInstancedStruct<FSpace>::Make<FBoxSpace>(TArray<float>{ -100000 }, TArray<float>{ 100000 });
		Config.Definition = FInteractionDefinition(Space, Space);
		return Config;
	}
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScholaMassPolicySizingTest, "Schola.Mass.Policy.CreateAndSize",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FScholaMassPolicySizingTest::RunTest(const FString&)
{
	auto*	World = UWorld::CreateWorld(EWorldType::Game, false);
	auto*	Service = World->GetSubsystem<UScholaMassInferenceSubsystem>();
	FString Error;

	TestFalse(TEXT("A model or provider is required"), Service->CreatePolicy(FScholaMassPolicyConfig(), Error).IsValid());
	TestTrue(TEXT("Missing model is explained: ") + Error, Error.Contains(TEXT("Model")));

	auto*					Provider = NewObject<UScholaMassTestRowProvider>(World);
	FScholaMassPolicyConfig NoDefinition = TestConfig(Provider);
	NoDefinition.Definition = FInteractionDefinition();
	TestFalse(TEXT("Providers that cannot describe themselves need a definition"), Service->CreatePolicy(NoDefinition, Error).IsValid());
	TestTrue(TEXT("Missing definition is explained: ") + Error, Error.Contains(TEXT("FInteractionDefinition")));

	FScholaMassPolicyConfig BadRate = TestConfig(Provider);
	BadRate.DecisionHz = 0;
	TestFalse(TEXT("Zero decision rate is rejected"), Service->CreatePolicy(BadRate, Error).IsValid());

	// 20,000 agents at 10 Hz: half of the 100 ms age limit is 10,000 rows; half of that per frame.
	FScholaMassPolicyConfig Config = TestConfig(Provider);
	Config.ExpectedAgents = 20000;
	Config.WorkerCount = 3;
	const FScholaMassPolicy Policy = Service->CreatePolicy(Config, Error);
	if (!TestTrue(TEXT("Policy created: ") + Error, Policy.IsValid()))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("Pending rows sized from crowd and age limit"), Policy.GetSettings().MaxPendingRows, 10000);
	TestEqual(TEXT("Half the queue admitted per frame"), Policy.GetMaxRowsPerFrame(), 5000);
	TestEqual(TEXT("Workers forwarded"), Policy.GetSettings().WorkerCount, 3);
	TestEqual(TEXT("Period from decision rate"), Policy.GetPeriodSeconds(), .1f);
	TestEqual(TEXT("Sizes from the definition"), Policy.GetObservationSize() * 10 + Policy.GetActionSize(), 11);
	TestTrue(TEXT("Byte limit fits the row limit"),
		Policy.GetSettings().MaxPendingBytes >= int64(Policy.GetSettings().MaxPendingRows) * 2 * sizeof(float));

	FScholaMassPolicyConfig WrongSize = TestConfig(NewObject<UScholaMassTestRowProvider>(World));
	WrongSize.ExpectedObservationSize = 64;
	TestFalse(TEXT("Size mismatch with the crowd is rejected"), Service->CreatePolicy(WrongSize, Error).IsValid());
	TestTrue(TEXT("Size mismatch is explained: ") + Error, Error.Contains(TEXT("expects 64")));

	// Small crowds keep at least 4,096 rows (or the whole crowd) of queue.
	auto*					SmallProvider = NewObject<UScholaMassTestRowProvider>(World);
	FScholaMassPolicyConfig Small = TestConfig(SmallProvider);
	Small.ExpectedAgents = 300;
	TestEqual(TEXT("Small crowd queue holds the crowd"), Service->CreatePolicy(Small, Error).GetSettings().MaxPendingRows, 300);

	auto*					OverrideProvider = NewObject<UScholaMassTestRowProvider>(World);
	FScholaMassPolicyConfig Override = TestConfig(OverrideProvider);
	Override.MaxPendingRows = 100;
	Override.MaxRowsPerFrame = 500;
	AddExpectedMessagePlain(TEXT("exceeds MaxPendingRows"), ELogVerbosity::Warning);
	const FScholaMassPolicy Clamped = Service->CreatePolicy(Override, Error);
	TestEqual(TEXT("Explicit queue limit kept"), Clamped.GetSettings().MaxPendingRows, 100);
	TestEqual(TEXT("Per-frame cap clamped to the queue"), Clamped.GetMaxRowsPerFrame(), 100);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FScholaMassPolicyStatsTest, "Schola.Mass.Policy.StatsAndShortfall",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FScholaMassPolicyStatsTest::RunTest(const FString&)
{
	auto*					World = UWorld::CreateWorld(EWorldType::Game, false);
	auto*					Service = World->GetSubsystem<UScholaMassInferenceSubsystem>();
	FString					Error;
	FScholaMassPolicyConfig Config = TestConfig(NewObject<UScholaMassTestRowProvider>(World));
	Config.ExpectedAgents = 3000;
	// Rows wait for the test's polling loop; keep them all.
	Config.MaxActionAgeMs = 0;
	const FScholaMassPolicy Policy = Service->CreatePolicy(Config, Error);
	if (!TestTrue(TEXT("Policy created: ") + Error, Policy.IsValid()))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestEqual(TEXT("3,000 agents x 10 Hz: 1,500 rows per frame"), Policy.GetMaxRowsPerFrame(), 1500);

	auto&					  Manager = World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	TArray<FMassEntityHandle> Entities;
	int32					  Initialized = 0;
	Policy.BatchCreateEntities(Manager, {}, 3000, Entities, [&](const FMassEntityView& Entity, int32 Index) {
		Initialized += Entity.GetFragmentData<FScholaInferenceScheduleFragment>().PeriodSeconds == Policy.GetPeriodSeconds() ? 1 : 0;
	});
	TestEqual(TEXT("Every entity created with the policy's period"), Initialized, 3000);
	TestEqual(TEXT("Entities carry the policy's bucket"),
		Manager.GetConstSharedFragmentDataPtr<FScholaInferenceBucketFragment>(Entities[0])->BucketId, Policy.GetBucketId());
	auto* Gather = NewObject<UScholaMassTestGather>(World);
	auto* Apply = NewObject<UScholaMassTestApply>(World);
	TestEqual(TEXT("Gather defaults to the automatic per-bucket cap"), Gather->GlobalMaxRowsPerFrame, 0);
	Gather->CallInitialize(World, Manager.AsShared());
	Apply->CallInitialize(World, Manager.AsShared());

	const FScholaMassPolicyStats Before = Policy.GetStats();
	{
		// Bind every entity, then make all of them due at once: twice the per-frame cap.
		UE::Mass::FProcessingContext Context(Manager, 1.f / 60);
		UE::Mass::Executor::Run(*Gather, Context);
	}
	const FScholaMassPolicyStats Bound = Policy.GetStats();
	TestEqual(TEXT("Bound agents counted"), Bound.Agents, 3000);
	TestTrue(TEXT("Target is agents x rate"), FMath::IsNearlyEqual(Bound.TargetDecisionsPerSecond, 30000.0, 1.0));
	// Flush whatever the binding pass already submitted, so the window below starts empty.
	double Deadline = FPlatformTime::Seconds() + 5;
	while (Policy.GetStats().Scheduler.CompletedRows < Bound.Scheduler.AcceptedRows && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::SleepNoStats(.0001f);
	}
	{
		UE::Mass::FProcessingContext Context(Manager, 1.f / 60);
		UE::Mass::Executor::Run(*Apply, Context);
	}
	Apply->Applied = 0;
	for (auto Entity : Entities)
	{
		Manager.GetFragmentDataChecked<FScholaInferenceScheduleFragment>(Entity).NextDueTime = 0;
	}
	const FScholaMassPolicyStats Start = Policy.GetStats();
	{
		UE::Mass::FProcessingContext Context(Manager, 1.f / 60);
		UE::Mass::Executor::Run(*Gather, Context);
	}
	Deadline = FPlatformTime::Seconds() + 5;
	while (Policy.GetStats().Scheduler.CompletedRows < Start.Scheduler.CompletedRows + 1500 && FPlatformTime::Seconds() < Deadline)
	{
		FPlatformProcess::SleepNoStats(.0001f);
	}
	{
		UE::Mass::FProcessingContext Context(Manager, 1.f / 60);
		UE::Mass::Executor::Run(*Apply, Context);
	}
	const FScholaMassPolicyWindow Window = Policy.GetStats().Since(Start);
	TestEqual(TEXT("Rows over the cap are deferred"), Window.Deferred, int64(1500));
	TestEqual(TEXT("Admitted rows are delivered"), Window.Decisions, int64(1500));
	TestEqual(TEXT("Apply saw the same rows"), Apply->Applied, 1500);
	TestTrue(TEXT("Latency recorded"), Window.LatencyMs > 0);
	TestEqual(TEXT("Nothing rejected"), Window.Rejected + Window.Invalid, int64(0));
	TestTrue(TEXT("Shortfall names the per-frame cap: ") + Window.DescribeShortfall(),
		Window.DescribeShortfall().Contains(TEXT("per-frame cap")));
	TestEqual(TEXT("Window echoes the configuration"), Window.MaxRowsPerFrame, 1500);
	TestTrue(TEXT("Window covers elapsed time"), Window.Seconds > 0 && Bound.Time >= Before.Time);
	World->DestroyWorld(false);
	return true;
}
#endif

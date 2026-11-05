// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SharedPolicyRoutingTestHelpers.h"

namespace SharedPolicyRoutingMeasurementTest
{
	using namespace SharedPolicyRouting;

	TArray<float> MakeMixedObservations()
	{
		const int32 InputValues[] = {1, 2, 2, 1};
		TArray<float> Observations;
		Observations.SetNumZeroed(UE_ARRAY_COUNT(InputValues) * ObservationWidth);
		for (int32 Row = 0; Row < UE_ARRAY_COUNT(InputValues); ++Row)
		{
			Observations[Row * ObservationWidth] = float(InputValues[Row]);
		}
		return Observations;
	}

	FString SamplesText(TConstArrayView<double> Samples)
	{
		TArray<FString> Values;
		Values.Reserve(Samples.Num());
		for (const double Sample : Samples)
		{
			Values.Add(FString::Printf(TEXT("%.6f"), Sample));
		}
		return FString::Join(Values, TEXT(","));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSharedPolicyRoutingMeasurementTest,
	"Schola.Inference.SharedPolicy.Routing.CDMeasurementSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyRoutingMeasurementTest::RunTest(const FString&)
{
	using namespace SharedPolicyRouting;
	using namespace SharedPolicyRoutingMeasurementTest;

	FNneExpertBackend ArchitectureC(EArchitecture::C);
	FNneExpertBackend ArchitectureD(EArchitecture::D);
	FString Error;
	if (!TestTrue(TEXT("Initialize Architecture C backend"), ArchitectureC.Initialize(Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("Initialize Architecture D backend"), ArchitectureD.Initialize(Error)))
	{
		AddError(Error);
		return false;
	}

	const TArray<float> Observations = MakeMixedObservations();
	const TArray<int32> Experts = {1, 0, 1, 0};
	const FMeasurementConfig Config{2, 10};
	FMeasurementResult ResultC;
	FMeasurementResult ResultD;
	if (!TestTrue(TEXT("Measure Architecture C routed requests"),
		MeasureRequests(ArchitectureC, Observations, Experts, Config, ResultC, Error)))
	{
		AddError(Error);
		return false;
	}
	if (!TestTrue(TEXT("Measure Architecture D routed requests"),
		MeasureRequests(ArchitectureD, Observations, Experts, Config, ResultD, Error)))
	{
		AddError(Error);
		return false;
	}

	TestEqual(TEXT("Architecture C measured sample count"), ResultC.RequestMilliseconds.Num(), Config.MeasuredRequests);
	TestEqual(TEXT("Architecture D measured sample count"), ResultD.RequestMilliseconds.Num(), Config.MeasuredRequests);
	TestEqual(TEXT("Architecture C model calls per request"), ResultC.RequestReport.Calls.Num(), 2);
	TestEqual(TEXT("Architecture D model calls per request"), ResultD.RequestReport.Calls.Num(), 2);
	for (int32 CallIndex = 0; CallIndex < 2; ++CallIndex)
	{
		TestEqual(FString::Printf(TEXT("C rows in call %d"), CallIndex), ResultC.RequestReport.Calls[CallIndex].RowCount, 2);
		TestEqual(FString::Printf(TEXT("D rows in call %d"), CallIndex), ResultD.RequestReport.Calls[CallIndex].RowCount, 2);
		TestEqual(FString::Printf(TEXT("C/D expert in call %d"), CallIndex),
			ResultC.RequestReport.Calls[CallIndex].Expert, ResultD.RequestReport.Calls[CallIndex].Expert);
	}
	for (const double Sample : ResultC.RequestMilliseconds)
	{
		TestTrue(TEXT("Architecture C duration is finite and non-negative"), FMath::IsFinite(Sample) && Sample >= 0.0);
	}
	for (const double Sample : ResultD.RequestMilliseconds)
	{
		TestTrue(TEXT("Architecture D duration is finite and non-negative"), FMath::IsFinite(Sample) && Sample >= 0.0);
	}

	TestEqual(TEXT("C/D restored action count"), ResultC.LastActions.Num(), ResultD.LastActions.Num());
	bool bOutputsMatch = ResultC.LastActions.Num() == ResultD.LastActions.Num();
	for (int32 Index = 0; Index < ResultC.LastActions.Num() && bOutputsMatch; ++Index)
	{
		bOutputsMatch = FMath::IsNearlyEqual(ResultC.LastActions[Index], ResultD.LastActions[Index], 1e-5f);
	}
	TestTrue(TEXT("C/D measured requests preserve equivalent restored actions"), bOutputsMatch);

	AddInfo(FString::Printf(
		TEXT("Architecture C complete-request ms raw=[%s] p50=%.6f p95=%.6f"),
		*SamplesText(ResultC.RequestMilliseconds),
		PercentileMilliseconds(ResultC.RequestMilliseconds, 0.50),
		PercentileMilliseconds(ResultC.RequestMilliseconds, 0.95)));
	AddInfo(FString::Printf(
		TEXT("Architecture D complete-request ms raw=[%s] p50=%.6f p95=%.6f"),
		*SamplesText(ResultD.RequestMilliseconds),
		PercentileMilliseconds(ResultD.RequestMilliseconds, 0.50),
		PercentileMilliseconds(ResultD.RequestMilliseconds, 0.95)));
	AddInfo(TEXT("Smoke timings are structural evidence only; no performance winner is asserted."));

	FMeasurementResult InvalidResult;
	TestFalse(TEXT("Reject zero measured requests"),
		MeasureRequests(ArchitectureC, Observations, Experts, {0, 0}, InvalidResult, Error));
	TestTrue(TEXT("Invalid measurement returns no samples"), InvalidResult.RequestMilliseconds.IsEmpty());
	return bOutputsMatch;
}

#endif

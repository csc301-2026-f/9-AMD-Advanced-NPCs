// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "SharedPolicyRoutingTestHelpers.h"

namespace SharedPolicyRoutedInferenceTest
{
	using namespace SharedPolicyRouting;

	constexpr double AbsoluteTolerance = 1e-5;
	constexpr double RelativeTolerance = 1e-4;
	const double ExpectedByExpertAndInput[ExpertCount][2][ActionWidth] = {
		{
			{-0.1623554528, -0.1630928367},
			{-0.1648618877, -0.1687339097},
		},
		{
			{0.0694475845, -0.0019764062},
			{0.0638729781, -0.0013816468},
		},
	};

	struct FCase
	{
		const TCHAR* Name;
		TArray<int32> Experts;
		TArray<int32> InputValues;
		TArray<int32> ExpectedRowsPerCall;
	};

	TArray<float> MakeObservations(TConstArrayView<int32> InputValues)
	{
		TArray<float> Observations;
		Observations.SetNumZeroed(InputValues.Num() * ObservationWidth);
		for (int32 Row = 0; Row < InputValues.Num(); ++Row)
		{
			Observations[Row * ObservationWidth] = float(InputValues[Row]);
		}
		return Observations;
	}

	class FFailingBackend final : public IExpertBackend
	{
	public:
		virtual bool Run(
			int32 Expert,
			TConstArrayView<float>,
			int32 RowCount,
			TArray<float>& OutGroupedActions,
			FString& OutError) override
		{
			if (Expert == 1)
			{
				OutError = TEXT("Injected backend failure.");
				return false;
			}
			OutGroupedActions.SetNumZeroed(RowCount * ActionWidth);
			return true;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSharedPolicyRoutedInferenceTest,
	"Schola.Inference.SharedPolicy.Routing.CDContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyRoutedInferenceTest::RunTest(const FString&)
{
	using namespace SharedPolicyRoutedInferenceTest;
	using namespace SharedPolicyRouting;

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

	const FCase Cases[] = {
		{TEXT("mixed shuffled"), {1, 0, 1, 0}, {1, 2, 2, 1}, {2, 2}},
		{TEXT("homogeneous expert 0"), {0, 0}, {1, 2}, {2}},
		{TEXT("homogeneous expert 1"), {1, 1}, {1, 2}, {2}},
		{TEXT("singleton"), {1}, {1}, {1}},
	};

	bool bAllValuesMatch = true;
	for (const FCase& Case : Cases)
	{
		const TArray<float> Observations = MakeObservations(Case.InputValues);
		TArray<float> ActionsC;
		TArray<float> ActionsD;
		FRequestReport ReportC;
		FRequestReport ReportD;
		if (!TestTrue(FString::Printf(TEXT("Architecture C routes %s"), Case.Name),
			RouteRequest(ArchitectureC, Observations, Case.Experts, ActionsC, ReportC, Error)))
		{
			AddError(Error);
			return false;
		}
		if (!TestTrue(FString::Printf(TEXT("Architecture D routes %s"), Case.Name),
			RouteRequest(ArchitectureD, Observations, Case.Experts, ActionsD, ReportD, Error)))
		{
			AddError(Error);
			return false;
		}

		TestEqual(FString::Printf(TEXT("C call count for %s"), Case.Name), ReportC.Calls.Num(), Case.ExpectedRowsPerCall.Num());
		TestEqual(FString::Printf(TEXT("D call count for %s"), Case.Name), ReportD.Calls.Num(), Case.ExpectedRowsPerCall.Num());
		for (int32 CallIndex = 0; CallIndex < Case.ExpectedRowsPerCall.Num(); ++CallIndex)
		{
			TestEqual(FString::Printf(TEXT("C rows in call %d for %s"), CallIndex, Case.Name),
				ReportC.Calls[CallIndex].RowCount, Case.ExpectedRowsPerCall[CallIndex]);
			TestEqual(FString::Printf(TEXT("D rows in call %d for %s"), CallIndex, Case.Name),
				ReportD.Calls[CallIndex].RowCount, Case.ExpectedRowsPerCall[CallIndex]);
			TestEqual(FString::Printf(TEXT("C/D expert in call %d for %s"), CallIndex, Case.Name),
				ReportC.Calls[CallIndex].Expert, ReportD.Calls[CallIndex].Expert);
		}

		TestEqual(FString::Printf(TEXT("C action value count for %s"), Case.Name), ActionsC.Num(), Case.Experts.Num() * ActionWidth);
		TestEqual(FString::Printf(TEXT("D action value count for %s"), Case.Name), ActionsD.Num(), Case.Experts.Num() * ActionWidth);
		for (int32 Row = 0; Row < Case.Experts.Num(); ++Row)
		{
			const int32 InputIndex = Case.InputValues[Row] - 1;
			for (int32 Column = 0; Column < ActionWidth; ++Column)
			{
				const int32 FlatIndex = Row * ActionWidth + Column;
				const double Reference = ExpectedByExpertAndInput[Case.Experts[Row]][InputIndex][Column];
				const double Tolerance = AbsoluteTolerance + RelativeTolerance * FMath::Abs(Reference);
				const bool bCMatches = FMath::Abs(ActionsC[FlatIndex] - Reference) <= Tolerance;
				const bool bDMatches = FMath::Abs(ActionsD[FlatIndex] - Reference) <= Tolerance;
				const bool bArchitecturesMatch = FMath::Abs(ActionsC[FlatIndex] - ActionsD[FlatIndex]) <= Tolerance;
				if (!bCMatches || !bDMatches || !bArchitecturesMatch)
				{
					AddError(FString::Printf(
						TEXT("%s row %d column %d: C=%.10f D=%.10f expected=%.10f tolerance=%.10f."),
						Case.Name, Row, Column, ActionsC[FlatIndex], ActionsD[FlatIndex], Reference, Tolerance));
					bAllValuesMatch = false;
				}
			}
		}
	}

	TArray<float> Actions = {99.f};
	FRequestReport Report;
	TestFalse(TEXT("Reject empty request"), RouteRequest(ArchitectureC, {}, {}, Actions, Report, Error));
	TestTrue(TEXT("Empty request returns no actions"), Actions.IsEmpty());
	TestFalse(TEXT("Reject row/observation mismatch"),
		RouteRequest(ArchitectureC, MakeObservations({1}), TArray<int32>{0, 1}, Actions, Report, Error));
	TestFalse(TEXT("Reject unknown expert"),
		RouteRequest(ArchitectureC, MakeObservations({1}), TArray<int32>{2}, Actions, Report, Error));

	FFailingBackend FailingBackend;
	TestFalse(TEXT("Backend failure rejects the entire request"),
		RouteRequest(FailingBackend, MakeObservations({1, 2}), TArray<int32>{0, 1}, Actions, Report, Error));
	TestTrue(TEXT("Backend failure returns no partial actions"), Actions.IsEmpty());
	TestTrue(TEXT("Backend failure returns no successful report"), Report.Calls.IsEmpty());
	TestTrue(TEXT("Backend failure preserves its diagnostic"), Error.Contains(TEXT("Injected backend failure")));

	TestTrue(TEXT("C and D preserve equivalent actions in original row order"), bAllValuesMatch);
	return bAllValuesMatch;
}

#endif

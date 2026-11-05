// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Dom/JsonObject.h"
#include "GenericPlatform/GenericPlatformProperties.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SharedPolicyRoutingTestHelpers.h"

namespace SharedPolicyRoutingMatrixTest
{
	using namespace SharedPolicyRouting;

	constexpr int32 WarmupRequests = 3;
	constexpr int32 MeasuredRequests = 20;
	const int32 BatchSizes[] = {1, 16, 64, 256};
	const int32 Expert0Percents[] = {100, 50, 10, 0};

	struct FWorkload
	{
		int32 BatchSize = 0;
		int32 Expert0Percent = 0;
		int32 Expert0Rows = 0;
		int32 Expert1Rows = 0;
		TArray<float> Observations;
		TArray<int32> Experts;
	};

	FWorkload MakeWorkload(int32 BatchSize, int32 Expert0Percent)
	{
		FWorkload Workload;
		Workload.BatchSize = BatchSize;
		Workload.Expert0Percent = Expert0Percent;
		Workload.Expert0Rows = FMath::RoundToInt(double(BatchSize) * double(Expert0Percent) / 100.0);
		Workload.Expert0Rows = FMath::Clamp(Workload.Expert0Rows, 0, BatchSize);
		Workload.Expert1Rows = BatchSize - Workload.Expert0Rows;
		Workload.Experts.Reserve(BatchSize);
		for (int32 Row = 0; Row < Workload.Expert0Rows; ++Row)
		{
			Workload.Experts.Add(0);
		}
		for (int32 Row = 0; Row < Workload.Expert1Rows; ++Row)
		{
			Workload.Experts.Add(1);
		}

		// Deterministically shuffle role rows so mixed workloads exercise scatter,
		// while C and D still receive byte-identical inputs.
		FRandomStream Random(BatchSize * 1000 + Expert0Percent);
		for (int32 Index = Workload.Experts.Num() - 1; Index > 0; --Index)
		{
			Workload.Experts.Swap(Index, Random.RandRange(0, Index));
		}

		Workload.Observations.SetNumZeroed(BatchSize * ObservationWidth);
		for (int32 Row = 0; Row < BatchSize; ++Row)
		{
			Workload.Observations[Row * ObservationWidth] = float(1 + Row % 2);
		}
		return Workload;
	}

	TArray<TSharedPtr<FJsonValue>> NumberArray(TConstArrayView<double> Values)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Values.Num());
		for (const double Value : Values)
		{
			Result.Add(MakeShared<FJsonValueNumber>(Value));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> CallsArray(const FRequestReport& Report)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		Result.Reserve(Report.Calls.Num());
		for (const FModelCall& Call : Report.Calls)
		{
			TSharedRef<FJsonObject> JsonCall = MakeShared<FJsonObject>();
			JsonCall->SetNumberField(TEXT("expert"), Call.Expert);
			JsonCall->SetNumberField(TEXT("rows"), Call.RowCount);
			Result.Add(MakeShared<FJsonValueObject>(JsonCall));
		}
		return Result;
	}

	TSharedRef<FJsonObject> ResultObject(
		const TCHAR* Architecture,
		const FWorkload& Workload,
		const FMeasurementResult& Result,
		bool bCorrect)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("architecture"), Architecture);
		Json->SetNumberField(TEXT("batch_size"), Workload.BatchSize);
		Json->SetNumberField(TEXT("requested_expert_0_percent"), Workload.Expert0Percent);
		Json->SetNumberField(TEXT("expert_0_rows"), Workload.Expert0Rows);
		Json->SetNumberField(TEXT("expert_1_rows"), Workload.Expert1Rows);
		Json->SetNumberField(TEXT("model_calls"), Result.RequestReport.Calls.Num());
		Json->SetArrayField(TEXT("calls"), CallsArray(Result.RequestReport));
		Json->SetArrayField(TEXT("request_ms_raw"), NumberArray(Result.RequestMilliseconds));
		Json->SetNumberField(TEXT("request_ms_p50"), PercentileMilliseconds(Result.RequestMilliseconds, 0.50));
		Json->SetNumberField(TEXT("request_ms_p95"), PercentileMilliseconds(Result.RequestMilliseconds, 0.95));
		Json->SetBoolField(TEXT("correct"), bCorrect);
		return Json;
	}

	bool ActionsMatch(TConstArrayView<float> Left, TConstArrayView<float> Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!FMath::IsNearlyEqual(Left[Index], Right[Index], 1e-5f))
			{
				return false;
			}
		}
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSharedPolicyRoutingMatrixTest,
	"Schola.Inference.SharedPolicy.Routing.CDWorkloadMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyRoutingMatrixTest::RunTest(const FString&)
{
	using namespace SharedPolicyRouting;
	using namespace SharedPolicyRoutingMatrixTest;

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

	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("generated_utc"), FDateTime::UtcNow().ToIso8601());
	Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Root->SetStringField(TEXT("platform"), FPlatformProperties::PlatformName());
	Root->SetStringField(TEXT("os_version"), FPlatformMisc::GetOSVersion());
	Root->SetStringField(TEXT("runtime"), TEXT("NNERuntimeORTCpu"));
	Root->SetNumberField(TEXT("warmup_requests"), WarmupRequests);
	Root->SetNumberField(TEXT("measured_requests"), MeasuredRequests);
	Root->SetStringField(
		TEXT("interpretation"),
		TEXT("Same-process workload matrix for instrumentation validation; not a final performance conclusion."));

	TArray<TSharedPtr<FJsonValue>> Results;
	bool bAllCorrect = true;
	int32 WorkloadIndex = 0;
	const FMeasurementConfig Config{WarmupRequests, MeasuredRequests};
	for (const int32 BatchSize : BatchSizes)
	{
		for (const int32 Expert0Percent : Expert0Percents)
		{
			const FWorkload Workload = MakeWorkload(BatchSize, Expert0Percent);
			FMeasurementResult ResultC;
			FMeasurementResult ResultD;
			const bool bCRunsFirst = WorkloadIndex % 2 == 0;
			bool bMeasured = true;
			if (bCRunsFirst)
			{
				bMeasured = MeasureRequests(ArchitectureC, Workload.Observations, Workload.Experts, Config, ResultC, Error)
					&& MeasureRequests(ArchitectureD, Workload.Observations, Workload.Experts, Config, ResultD, Error);
			}
			else
			{
				bMeasured = MeasureRequests(ArchitectureD, Workload.Observations, Workload.Experts, Config, ResultD, Error)
					&& MeasureRequests(ArchitectureC, Workload.Observations, Workload.Experts, Config, ResultC, Error);
			}
			if (!TestTrue(FString::Printf(
				TEXT("Measure batch=%d expert_0=%d%%"), BatchSize, Expert0Percent), bMeasured))
			{
				AddError(Error);
				return false;
			}

			const bool bCorrect = ActionsMatch(ResultC.LastActions, ResultD.LastActions);
			TestTrue(FString::Printf(
				TEXT("C/D outputs match for batch=%d expert_0=%d%%"), BatchSize, Expert0Percent), bCorrect);
			bAllCorrect &= bCorrect;

			const int32 ExpectedCalls = (Workload.Expert0Rows > 0 ? 1 : 0) + (Workload.Expert1Rows > 0 ? 1 : 0);
			TestEqual(TEXT("Architecture C active-model calls"), ResultC.RequestReport.Calls.Num(), ExpectedCalls);
			TestEqual(TEXT("Architecture D active-model calls"), ResultD.RequestReport.Calls.Num(), ExpectedCalls);

			TSharedRef<FJsonObject> WorkloadJson = MakeShared<FJsonObject>();
			WorkloadJson->SetNumberField(TEXT("batch_size"), BatchSize);
			WorkloadJson->SetNumberField(TEXT("requested_expert_0_percent"), Expert0Percent);
			WorkloadJson->SetStringField(TEXT("execution_order"), bCRunsFirst ? TEXT("C,D") : TEXT("D,C"));
			WorkloadJson->SetObjectField(TEXT("C"), ResultObject(TEXT("C"), Workload, ResultC, bCorrect));
			WorkloadJson->SetObjectField(TEXT("D"), ResultObject(TEXT("D"), Workload, ResultD, bCorrect));
			Results.Add(MakeShared<FJsonValueObject>(WorkloadJson));
			++WorkloadIndex;
		}
	}
	Root->SetArrayField(TEXT("workloads"), Results);
	Root->SetBoolField(TEXT("all_correct"), bAllCorrect);

	FString JsonText;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
	if (!TestTrue(TEXT("Serialize C/D matrix report"), FJsonSerializer::Serialize(Root, Writer)))
	{
		return false;
	}
	const FString ReportPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("Automation"), TEXT("SharedPolicy"), TEXT("cd-matrix.json"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	if (!TestTrue(TEXT("Write C/D matrix report"), FFileHelper::SaveStringToFile(JsonText, *ReportPath)))
	{
		AddError(FString::Printf(TEXT("Could not write %s."), *ReportPath));
		return false;
	}
	AddInfo(FString::Printf(TEXT("C/D workload matrix report: %s"), *ReportPath));
	AddInfo(TEXT("Matrix timings are same-process exploratory evidence; no winner is asserted."));
	return bAllCorrect;
}

#endif

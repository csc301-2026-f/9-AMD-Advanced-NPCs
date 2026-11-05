// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace SharedPolicyArchitectureATest
{
	constexpr int32 BatchSize = 2;
	constexpr int32 ObservationWidth = 64;
	constexpr int32 ActionWidth = 2;
	constexpr double AbsoluteTolerance = 1e-5;
	constexpr double RelativeTolerance = 1e-4;
	const TCHAR* ModelObjectPath = TEXT("/Game/A_all_heads.A_all_heads");
	const TCHAR* RuntimeName = TEXT("NNERuntimeORTCpu");

	const double ExpectedExpert0[BatchSize][ActionWidth] = {
		{-0.1623554528, -0.1630928367},
		{-0.1648618877, -0.1687339097},
	};
	const double ExpectedExpert1[BatchSize][ActionWidth] = {
		{0.0694475845, -0.0019764062},
		{0.0638729781, -0.0013816468},
	};

	bool ShapeIs(TConstArrayView<uint32> Shape, uint32 Batch, uint32 Width)
	{
		return Shape.Num() == 2 && Shape[0] == Batch && Shape[1] == Width;
	}

	FString ShapeText(TConstArrayView<uint32> Shape)
	{
		FString Text = TEXT("[");
		for (int32 Index = 0; Index < Shape.Num(); ++Index)
		{
			if (Index > 0)
			{
				Text += TEXT(", ");
			}
			Text += FString::FromInt(int32(Shape[Index]));
		}
		Text += TEXT("]");
		return Text;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSharedPolicyArchitectureAOrtTest,
	"Schola.Inference.SharedPolicy.ArchitectureA.NNERuntimeORTCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyArchitectureAOrtTest::RunTest(const FString&)
{
	using namespace SharedPolicyArchitectureATest;
	using namespace UE::NNE;

	UNNEModelData* ModelData = LoadObject<UNNEModelData>(nullptr, ModelObjectPath);
	if (!TestNotNull(TEXT("Imported NNE Model Data /Game/A_all_heads"), ModelData))
	{
		AddError(TEXT("Architecture A compatibility requires the imported asset /Game/A_all_heads."));
		return false;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!TestTrue(TEXT("NNERuntimeORTCpu is registered"), Runtime.IsValid()))
	{
		AddError(TEXT("Enable the NNERuntimeORT plugin. NNERuntimeORTCpu was not found."));
		return false;
	}

	if (!TestTrue(TEXT("NNERuntimeORTCpu can create Architecture A"), Runtime->CanCreateModelCPU(ModelData) == EResultStatus::Ok))
	{
		AddError(TEXT("NNERuntimeORTCpu rejected A_all_heads. The ONNX graph uses an operator this runtime does not support."));
		return false;
	}

	TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
	if (!TestTrue(TEXT("CreateModelCPU"), Model.IsValid()))
	{
		AddError(TEXT("NNERuntimeORTCpu failed while creating the Architecture A model."));
		return false;
	}

	TSharedPtr<IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
	if (!TestTrue(TEXT("CreateModelInstanceCPU"), Instance.IsValid()))
	{
		AddError(TEXT("NNERuntimeORTCpu created the model but not a CPU instance."));
		return false;
	}

	const TConstArrayView<FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
	if (!TestEqual(TEXT("Architecture A input count"), InputDescs.Num(), 1))
	{
		return false;
	}
	if (!TestTrue(TEXT("Architecture A input type is float32"), InputDescs[0].GetDataType() == ENNETensorDataType::Float))
	{
		AddError(FString::Printf(TEXT("Input '%s' is not float32."), *InputDescs[0].GetName()));
		return false;
	}

	const uint32 InputDims[] = {uint32(BatchSize), uint32(ObservationWidth)};
	const TArray<FTensorShape> InputShapes = {FTensorShape::Make(MakeArrayView(InputDims))};
	if (!TestTrue(TEXT("Set input shape [2, 64]"), Instance->SetInputTensorShapes(InputShapes) == EResultStatus::Ok))
	{
		AddError(FString::Printf(TEXT("SetInputTensorShapes failed for input '%s'."), *InputDescs[0].GetName()));
		return false;
	}

	TArray<float> Observations;
	Observations.SetNumZeroed(BatchSize * ObservationWidth);
	Observations[0] = 1.f;
	Observations[ObservationWidth] = 2.f;

	const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
	const TConstArrayView<FTensorShape> ResolvedBeforeRun = Instance->GetOutputTensorShapes();
	TArray<TArray<float>> OutputStorage;
	TArray<FTensorBindingCPU> OutputBindings;
	OutputStorage.SetNum(OutputDescs.Num());
	OutputBindings.Reserve(OutputDescs.Num());
	int32 Expert0Index = INDEX_NONE;
	int32 Expert1Index = INDEX_NONE;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		const FTensorDesc& Desc = OutputDescs[Index];
		if (Desc.GetDataType() != ENNETensorDataType::Float)
		{
			AddError(FString::Printf(TEXT("Output '%s' is not float32."), *Desc.GetName()));
			return false;
		}
		if (Desc.GetName() == TEXT("expert_0_actions"))
		{
			Expert0Index = Index;
		}
		else if (Desc.GetName() == TEXT("expert_1_actions"))
		{
			Expert1Index = Index;
		}
		int32 ElementCount = BatchSize * ActionWidth;
		if (ResolvedBeforeRun.IsValidIndex(Index) && ResolvedBeforeRun[Index].Volume() > 0)
		{
			if (ResolvedBeforeRun[Index].Volume() > uint64(MAX_int32))
			{
				AddError(FString::Printf(TEXT("Output '%s' is too large to bind."), *Desc.GetName()));
				return false;
			}
			ElementCount = int32(ResolvedBeforeRun[Index].Volume());
		}
		OutputStorage[Index].SetNumZeroed(ElementCount);
		OutputBindings.Add({OutputStorage[Index].GetData(), uint64(ElementCount) * sizeof(float)});
	}
	if (!TestTrue(TEXT("Output expert_0_actions is present"), Expert0Index != INDEX_NONE)
		|| !TestTrue(TEXT("Output expert_1_actions is present"), Expert1Index != INDEX_NONE))
	{
		FString Names;
		for (const FTensorDesc& Desc : OutputDescs)
		{
			Names += Desc.GetName() + TEXT(" ");
		}
		AddError(FString::Printf(TEXT("Architecture A outputs were [%s]."), *Names));
		return false;
	}

	const FTensorBindingCPU InputBinding = {Observations.GetData(), uint64(Observations.Num()) * sizeof(float)};
	if (!TestTrue(TEXT("RunSync"), Instance->RunSync(MakeArrayView(&InputBinding, 1), OutputBindings) == EResultStatus::Ok))
	{
		AddError(TEXT("NNERuntimeORTCpu RunSync failed for Architecture A."));
		return false;
	}

	const TConstArrayView<FTensorShape> OutputShapes = Instance->GetOutputTensorShapes();
	if (!TestEqual(TEXT("Resolved output count"), OutputShapes.Num(), OutputDescs.Num()))
	{
		return false;
	}
	const int32 CheckedOutputs[] = {Expert0Index, Expert1Index};
	for (const int32 Index : CheckedOutputs)
	{
		const TConstArrayView<uint32> Shape = OutputShapes[Index].GetData();
		if (!TestTrue(
				FString::Printf(TEXT("%s shape is [2, 2], got %s"), *OutputDescs[Index].GetName(), *ShapeText(Shape)),
				ShapeIs(Shape, uint32(BatchSize), uint32(ActionWidth))))
		{
			return false;
		}
	}

	struct FExpectedOutput
	{
		int32 Index;
		const double (*Values)[ActionWidth];
	};
	const FExpectedOutput ExpectedOutputs[] = {
		{Expert0Index, ExpectedExpert0},
		{Expert1Index, ExpectedExpert1},
	};
	bool bValuesMatch = true;
	for (const FExpectedOutput& Expected : ExpectedOutputs)
	{
		const FString& Name = OutputDescs[Expected.Index].GetName();
		const TArray<float>& Actual = OutputStorage[Expected.Index];
		for (int32 Row = 0; Row < BatchSize; ++Row)
		{
			for (int32 Column = 0; Column < ActionWidth; ++Column)
			{
				const double Reference = Expected.Values[Row][Column];
				const double Value = Actual[Row * ActionWidth + Column];
				const double Tolerance = AbsoluteTolerance + RelativeTolerance * FMath::Abs(Reference);
				if (FMath::Abs(Value - Reference) > Tolerance)
				{
					AddError(FString::Printf(
						TEXT("%s[%d, %d] = %.10f, Python reference %.10f, tolerance %.10f."),
						*Name, Row, Column, Value, Reference, Tolerance));
					bValuesMatch = false;
				}
			}
		}
	}
	TestTrue(TEXT("NNE outputs match Python ONNX Runtime"), bValuesMatch);
	return bValuesMatch;
}

#endif

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace SharedPolicyArchitectureATest
{
	constexpr int32 ObservationWidth = 64;
	constexpr int32 ActionWidth = 2;
	constexpr double AbsoluteTolerance = 1e-5;
	constexpr double RelativeTolerance = 1e-4;
	const TCHAR* ModelObjectPath = TEXT("/Game/A_all_heads.A_all_heads");
	const TCHAR* RuntimeName = TEXT("NNERuntimeORTCpu");

	const double ExpectedExpert0[2][2] = {
		{-0.1623554528, -0.1630928367},
		{-0.1648618877, -0.1687339097},
	};
	const double ExpectedExpert1[2][2] = {
		{0.0694475845, -0.0019764062},
		{0.0638729781, -0.0013816468},
	};
	const int32 BatchSizes[] = {1, 2};

	bool ShapeIs(TConstArrayView<uint32> Shape, uint32 Batch, uint32 Width)
	{
		return Shape.Num() == 2 && Shape[0] == Batch && Shape[1] == Width;
	}

	FString ShapeText(TConstArrayView<int32> Shape)
	{
		FString Text = TEXT("[");
		for (int32 Index = 0; Index < Shape.Num(); ++Index)
		{
			if (Index > 0)
			{
				Text += TEXT(", ");
			}
			Text += FString::FromInt(Shape[Index]);
		}
		Text += TEXT("]");
		return Text;
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
	if (!TestEqual(TEXT("Architecture A input count"), InputDescs.Num(), 1)
		|| !TestTrue(TEXT("Architecture A input is observations"), InputDescs.Num() == 1 && InputDescs[0].GetName() == TEXT("observations")))
	{
		FString Names;
		for (const FTensorDesc& Desc : InputDescs)
		{
			Names += Desc.GetName() + TEXT(" ");
		}
		AddError(FString::Printf(TEXT("Architecture A inputs were [%s]."), *Names));
		return false;
	}
	const TConstArrayView<int32> ObservationDims = InputDescs[0].GetShape().GetData();
	if (!TestTrue(TEXT("observations is float32"), InputDescs[0].GetDataType() == ENNETensorDataType::Float)
		|| !TestEqual(TEXT("observations rank"), ObservationDims.Num(), 2)
		|| !TestEqual(TEXT("observations feature dimension"), ObservationDims.Num() == 2 ? ObservationDims[1] : 0, ObservationWidth)
		|| !TestTrue(TEXT("observations batch dimension is dynamic"), ObservationDims.Num() == 2 && ObservationDims[0] < 0))
	{
		AddError(FString::Printf(TEXT("observations contract mismatch. Declared shape was %s."), *ShapeText(ObservationDims)));
		return false;
	}

	const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
	int32 Expert0Index = INDEX_NONE;
	int32 Expert1Index = INDEX_NONE;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		if (OutputDescs[Index].GetName() == TEXT("expert_0_actions"))
		{
			Expert0Index = Index;
		}
		else if (OutputDescs[Index].GetName() == TEXT("expert_1_actions"))
		{
			Expert1Index = Index;
		}
	}
	if (!TestEqual(TEXT("Architecture A output count"), OutputDescs.Num(), 2)
		|| !TestTrue(TEXT("Output expert_0_actions is present"), Expert0Index != INDEX_NONE)
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
	const int32 CheckedOutputs[] = {Expert0Index, Expert1Index};
	for (const int32 Index : CheckedOutputs)
	{
		const FTensorDesc& Desc = OutputDescs[Index];
		const TConstArrayView<int32> Dims = Desc.GetShape().GetData();
		if (!TestTrue(FString::Printf(TEXT("%s is float32"), *Desc.GetName()), Desc.GetDataType() == ENNETensorDataType::Float)
			|| !TestEqual(FString::Printf(TEXT("%s rank"), *Desc.GetName()), Dims.Num(), 2)
			|| !TestEqual(FString::Printf(TEXT("%s feature dimension"), *Desc.GetName()), Dims.Num() == 2 ? Dims[1] : 0, ActionWidth)
			|| !TestTrue(FString::Printf(TEXT("%s batch dimension is dynamic"), *Desc.GetName()), Dims.Num() == 2 && Dims[0] < 0))
		{
			AddError(FString::Printf(TEXT("%s contract mismatch. Declared shape was %s."), *Desc.GetName(), *ShapeText(Dims)));
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
	for (const int32 BatchSize : BatchSizes)
	{
		const FString CaseName = FString::Printf(TEXT("batch=%d"), BatchSize);
		const uint32 ConcreteObservationDims[] = {uint32(BatchSize), uint32(ObservationWidth)};
		const TArray<FTensorShape> InputShapes = {FTensorShape::Make(MakeArrayView(ConcreteObservationDims))};
		if (!TestTrue(
				FString::Printf(TEXT("SetInputTensorShapes %s"), *CaseName),
				Instance->SetInputTensorShapes(InputShapes) == EResultStatus::Ok))
		{
			AddError(FString::Printf(TEXT("SetInputTensorShapes failed for Architecture A with %s."), *CaseName));
			return false;
		}

		TArray<float> Observations;
		Observations.SetNumZeroed(BatchSize * ObservationWidth);
		Observations[0] = 1.f;
		if (BatchSize > 1)
		{
			Observations[ObservationWidth] = 2.f;
		}

		TArray<TArray<float>> OutputStorage;
		TArray<FTensorBindingCPU> OutputBindings;
		OutputStorage.SetNum(OutputDescs.Num());
		OutputBindings.Reserve(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			const int32 ElementCount = BatchSize * ActionWidth;
			OutputStorage[Index].SetNumZeroed(ElementCount);
			OutputBindings.Add({OutputStorage[Index].GetData(), uint64(ElementCount) * sizeof(float)});
		}

		const FTensorBindingCPU InputBinding = {Observations.GetData(), uint64(Observations.Num()) * sizeof(float)};
		if (!TestTrue(
				FString::Printf(TEXT("RunSync %s"), *CaseName),
				Instance->RunSync(MakeArrayView(&InputBinding, 1), OutputBindings) == EResultStatus::Ok))
		{
			AddError(FString::Printf(TEXT("NNERuntimeORTCpu RunSync failed for Architecture A with %s."), *CaseName));
			return false;
		}

		const TConstArrayView<FTensorShape> OutputShapes = Instance->GetOutputTensorShapes();
		if (!TestEqual(FString::Printf(TEXT("Resolved output count (%s)"), *CaseName), OutputShapes.Num(), OutputDescs.Num()))
		{
			return false;
		}
		for (const FExpectedOutput& Expected : ExpectedOutputs)
		{
			const FString& Name = OutputDescs[Expected.Index].GetName();
			const TConstArrayView<uint32> Shape = OutputShapes[Expected.Index].GetData();
			if (!TestTrue(
					FString::Printf(TEXT("%s shape is [%d, 2] for %s, got %s"), *Name, BatchSize, *CaseName, *ShapeText(Shape)),
					ShapeIs(Shape, uint32(BatchSize), uint32(ActionWidth))))
			{
				return false;
			}

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
							TEXT("%s %s[%d, %d] = %.10f, Python reference %.10f, tolerance %.10f."),
							*CaseName, *Name, Row, Column, Value, Reference, Tolerance));
						bValuesMatch = false;
					}
				}
			}
		}
	}

	TestTrue(TEXT("NNE outputs match Python ONNX Runtime"), bValuesMatch);
	return bValuesMatch;
}

#endif

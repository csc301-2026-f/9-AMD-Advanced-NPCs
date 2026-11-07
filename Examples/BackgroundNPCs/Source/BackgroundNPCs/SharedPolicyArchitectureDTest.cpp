// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace SharedPolicyArchitectureDTest
{
	constexpr int32 ObservationWidth = 64;
	constexpr int32 ActionWidth = 2;
	constexpr double AbsoluteTolerance = 1e-5;
	constexpr double RelativeTolerance = 1e-4;
	const TCHAR* ModelObjectPath = TEXT("/Game/D_conditional.D_conditional");
	const TCHAR* RuntimeName = TEXT("NNERuntimeORTCpu");

	// Rows are independent. Batch size 1 uses the first row of each reference.
	const double ExpectedExpert0[][ActionWidth] = {
		{-0.1623554528, -0.1630928367},
		{-0.1648618877, -0.1687339097},
	};
	const double ExpectedExpert1[][ActionWidth] = {
		{0.0694475845, -0.0019764062},
		{0.0638729781, -0.0013816468},
	};

	struct FCase
	{
		int32 BatchSize;
		bool bUseFirstExpert;
		const double (*Expected)[ActionWidth];
	};

	const FCase Cases[] = {
		{1, true, ExpectedExpert0},
		{1, false, ExpectedExpert1},
		{2, true, ExpectedExpert0},
		{2, false, ExpectedExpert1},
	};

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
	FSharedPolicyArchitectureDOrtTest,
	"Schola.Inference.SharedPolicy.ArchitectureD.NNERuntimeORTCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyArchitectureDOrtTest::RunTest(const FString&)
{
	using namespace SharedPolicyArchitectureDTest;
	using namespace UE::NNE;

	UNNEModelData* ModelData = LoadObject<UNNEModelData>(nullptr, ModelObjectPath);
	if (!TestNotNull(TEXT("Imported NNE Model Data /Game/D_conditional"), ModelData))
	{
		AddError(TEXT("Architecture D compatibility requires the imported asset /Game/D_conditional."));
		return false;
	}

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!TestTrue(TEXT("NNERuntimeORTCpu is registered"), Runtime.IsValid()))
	{
		AddError(TEXT("Enable the NNERuntimeORT plugin. NNERuntimeORTCpu was not found."));
		return false;
	}

	if (!TestTrue(TEXT("NNERuntimeORTCpu can create Architecture D"), Runtime->CanCreateModelCPU(ModelData) == EResultStatus::Ok))
	{
		AddError(TEXT("NNERuntimeORTCpu rejected D_conditional. The ONNX graph uses an operator this runtime does not support."));
		return false;
	}

	TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
	if (!TestTrue(TEXT("CreateModelCPU"), Model.IsValid()))
	{
		AddError(TEXT("NNERuntimeORTCpu failed while creating the Architecture D model."));
		return false;
	}

	TSharedPtr<IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
	if (!TestTrue(TEXT("CreateModelInstanceCPU"), Instance.IsValid()))
	{
		AddError(TEXT("NNERuntimeORTCpu created the model but not a CPU instance."));
		return false;
	}

	const TConstArrayView<FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
	if (!TestEqual(TEXT("Architecture D input count"), InputDescs.Num(), 2))
	{
		return false;
	}

	int32 ObservationsIndex = INDEX_NONE;
	int32 ExpertIndex = INDEX_NONE;
	for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
	{
		if (InputDescs[Index].GetName() == TEXT("observations"))
		{
			ObservationsIndex = Index;
		}
		else if (InputDescs[Index].GetName() == TEXT("use_first_expert"))
		{
			ExpertIndex = Index;
		}
	}
	if (!TestTrue(TEXT("Input observations is present"), ObservationsIndex != INDEX_NONE)
		|| !TestTrue(TEXT("Input use_first_expert is present"), ExpertIndex != INDEX_NONE))
	{
		FString Names;
		for (const FTensorDesc& Desc : InputDescs)
		{
			Names += Desc.GetName() + TEXT(" ");
		}
		AddError(FString::Printf(TEXT("Architecture D inputs were [%s]."), *Names));
		return false;
	}

	const FTensorDesc& ObservationsDesc = InputDescs[ObservationsIndex];
	const TConstArrayView<int32> ObservationDims = ObservationsDesc.GetShape().GetData();
	if (!TestTrue(TEXT("observations is float32"), ObservationsDesc.GetDataType() == ENNETensorDataType::Float)
		|| !TestEqual(TEXT("observations rank"), ObservationDims.Num(), 2)
		|| !TestEqual(TEXT("observations feature dimension"), ObservationDims.Num() == 2 ? ObservationDims[1] : 0, ObservationWidth)
		|| !TestTrue(TEXT("observations batch dimension is dynamic"),
			ObservationDims.Num() == 2 && ObservationDims[0] < 0))
	{
		AddError(FString::Printf(TEXT("observations contract mismatch. dtype/shape was %s."), *ShapeText(ObservationDims)));
		return false;
	}

	const FTensorDesc& ExpertDesc = InputDescs[ExpertIndex];
	const TConstArrayView<int32> ExpertDims = ExpertDesc.GetShape().GetData();
	if (!TestTrue(TEXT("use_first_expert is Boolean"), ExpertDesc.GetDataType() == ENNETensorDataType::Boolean)
		|| !TestEqual(TEXT("use_first_expert is a scalar"), ExpertDims.Num(), 0))
	{
		AddError(FString::Printf(
			TEXT("use_first_expert must be a rank-0 Boolean. Declared shape was %s."), *ShapeText(ExpertDims)));
		return false;
	}

	const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
	int32 ActionsIndex = INDEX_NONE;
	for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
	{
		if (OutputDescs[Index].GetName() == TEXT("actions"))
		{
			ActionsIndex = Index;
		}
		if (OutputDescs[Index].GetDataType() != ENNETensorDataType::Float)
		{
			AddError(FString::Printf(TEXT("Output '%s' is not float32."), *OutputDescs[Index].GetName()));
			return false;
		}
	}
	if (!TestTrue(TEXT("Output actions is present"), ActionsIndex != INDEX_NONE))
	{
		FString Names;
		for (const FTensorDesc& Desc : OutputDescs)
		{
			Names += Desc.GetName() + TEXT(" ");
		}
		AddError(FString::Printf(TEXT("Architecture D outputs were [%s]."), *Names));
		return false;
	}

	bool bValuesMatch = true;
	for (const FCase& Case : Cases)
	{
		const FString CaseName = FString::Printf(
			TEXT("batch=%d use_first_expert=%s"),
			Case.BatchSize,
			Case.bUseFirstExpert ? TEXT("true") : TEXT("false"));

		const uint32 ConcreteObservationDims[] = {uint32(Case.BatchSize), uint32(ObservationWidth)};
		TArray<FTensorShape> InputShapes;
		InputShapes.Reserve(InputDescs.Num());
		for (int32 Index = 0; Index < InputDescs.Num(); ++Index)
		{
			if (Index == ObservationsIndex)
			{
				InputShapes.Add(FTensorShape::Make(MakeArrayView(ConcreteObservationDims)));
			}
			else
			{
				InputShapes.Add(FTensorShape::Make(TConstArrayView<uint32>()));
			}
		}
		if (!TestTrue(FString::Printf(TEXT("SetInputTensorShapes %s"), *CaseName), Instance->SetInputTensorShapes(InputShapes) == EResultStatus::Ok))
		{
			AddError(FString::Printf(TEXT("SetInputTensorShapes failed for Architecture D with %s."), *CaseName));
			return false;
		}
		const TConstArrayView<FTensorShape> BoundInputShapes = Instance->GetInputTensorShapes();
		if (!TestTrue(
				FString::Printf(TEXT("use_first_expert stays a rank-0 scalar for %s"), *CaseName),
				BoundInputShapes.IsValidIndex(ExpertIndex) && BoundInputShapes[ExpertIndex].Rank() == 0))
		{
			return false;
		}

		TArray<float> Observations;
		Observations.SetNumZeroed(Case.BatchSize * ObservationWidth);
		Observations[0] = 1.f;
		if (Case.BatchSize > 1)
		{
			Observations[ObservationWidth] = 2.f;
		}

		TArray<TArray<float>> OutputStorage;
		TArray<FTensorBindingCPU> OutputBindings;
		OutputStorage.SetNum(OutputDescs.Num());
		OutputBindings.Reserve(OutputDescs.Num());
		for (int32 Index = 0; Index < OutputDescs.Num(); ++Index)
		{
			const int32 ElementCount = Case.BatchSize * ActionWidth;
			OutputStorage[Index].SetNumZeroed(ElementCount);
			OutputBindings.Add({OutputStorage[Index].GetData(), uint64(ElementCount) * sizeof(float)});
		}

		// ONNX Boolean values are one byte. NNERuntimeORT rejects any other binding size.
		uint8 UseFirstExpert = Case.bUseFirstExpert ? 1 : 0;
		TArray<FTensorBindingCPU> InputBindings;
		InputBindings.SetNum(InputDescs.Num());
		InputBindings[ObservationsIndex] = {Observations.GetData(), uint64(Observations.Num()) * sizeof(float)};
		InputBindings[ExpertIndex] = {&UseFirstExpert, 1};

		if (!TestTrue(FString::Printf(TEXT("RunSync %s"), *CaseName), Instance->RunSync(InputBindings, OutputBindings) == EResultStatus::Ok))
		{
			AddError(FString::Printf(TEXT("NNERuntimeORTCpu RunSync failed for Architecture D with %s."), *CaseName));
			return false;
		}

		const TConstArrayView<FTensorShape> OutputShapes = Instance->GetOutputTensorShapes();
		if (!TestEqual(FString::Printf(TEXT("Resolved output count (%s)"), *CaseName), OutputShapes.Num(), OutputDescs.Num()))
		{
			return false;
		}
		const TConstArrayView<uint32> ActionShape = OutputShapes[ActionsIndex].GetData();
		if (!TestTrue(
				FString::Printf(TEXT("actions shape is [%d, 2] for %s, got %s"), Case.BatchSize, *CaseName, *ShapeText(ActionShape)),
				ShapeIs(ActionShape, uint32(Case.BatchSize), uint32(ActionWidth))))
		{
			return false;
		}

		const TArray<float>& Actual = OutputStorage[ActionsIndex];
		for (int32 Row = 0; Row < Case.BatchSize; ++Row)
		{
			for (int32 Column = 0; Column < ActionWidth; ++Column)
			{
				const double Reference = Case.Expected[Row][Column];
				const double Value = Actual[Row * ActionWidth + Column];
				const double Tolerance = AbsoluteTolerance + RelativeTolerance * FMath::Abs(Reference);
				if (FMath::Abs(Value - Reference) > Tolerance)
				{
					AddError(FString::Printf(
						TEXT("%s actions[%d, %d] = %.10f, Python reference %.10f, tolerance %.10f."),
						*CaseName, Row, Column, Value, Reference, Tolerance));
					bValuesMatch = false;
				}
			}
		}
	}

	TestTrue(TEXT("NNE outputs match Python ONNX Runtime"), bValuesMatch);
	return bValuesMatch;
}

#endif

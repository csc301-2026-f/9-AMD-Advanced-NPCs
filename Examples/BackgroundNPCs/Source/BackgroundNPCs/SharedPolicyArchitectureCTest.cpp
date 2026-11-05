// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace SharedPolicyArchitectureCTest
{
	constexpr int32 ObservationWidth = 64;
	constexpr int32 ActionWidth = 2;
	constexpr double AbsoluteTolerance = 1e-5;
	constexpr double RelativeTolerance = 1e-4;
	const TCHAR* RuntimeName = TEXT("NNERuntimeORTCpu");

	const double ExpectedExpert0[2][2] = {
		{-0.1623554528, -0.1630928367},
		{-0.1648618877, -0.1687339097},
	};
	const double ExpectedExpert1[2][2] = {
		{0.0694475845, -0.0019764062},
		{0.0638729781, -0.0013816468},
	};

	struct FModel
	{
		const TCHAR* Name;
		const TCHAR* ObjectPath;
		const double (*Expected)[ActionWidth];
	};

	const FModel Models[] = {
		{TEXT("C_expert_0"), TEXT("/Game/C_expert_0.C_expert_0"), ExpectedExpert0},
		{TEXT("C_expert_1"), TEXT("/Game/C_expert_1.C_expert_1"), ExpectedExpert1},
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
	FSharedPolicyArchitectureCOrtTest,
	"Schola.Inference.SharedPolicy.ArchitectureC.NNERuntimeORTCpu",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FSharedPolicyArchitectureCOrtTest::RunTest(const FString&)
{
	using namespace SharedPolicyArchitectureCTest;
	using namespace UE::NNE;

	TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(RuntimeName);
	if (!TestTrue(TEXT("NNERuntimeORTCpu is registered"), Runtime.IsValid()))
	{
		AddError(TEXT("Enable the NNERuntimeORT plugin. NNERuntimeORTCpu was not found."));
		return false;
	}

	bool bValuesMatch = true;
	for (const FModel& ModelSpec : Models)
	{
		UNNEModelData* ModelData = LoadObject<UNNEModelData>(nullptr, ModelSpec.ObjectPath);
		if (!TestNotNull(FString::Printf(TEXT("Imported NNE Model Data %s"), ModelSpec.Name), ModelData))
		{
			AddError(FString::Printf(TEXT("Architecture C compatibility requires the imported asset %s."), ModelSpec.ObjectPath));
			return false;
		}

		if (!TestTrue(
				FString::Printf(TEXT("NNERuntimeORTCpu can create %s"), ModelSpec.Name),
				Runtime->CanCreateModelCPU(ModelData) == EResultStatus::Ok))
		{
			AddError(FString::Printf(
				TEXT("NNERuntimeORTCpu rejected %s. The ONNX graph uses an operator this runtime does not support."),
				ModelSpec.Name));
			return false;
		}

		TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
		if (!TestTrue(FString::Printf(TEXT("CreateModelCPU %s"), ModelSpec.Name), Model.IsValid()))
		{
			AddError(FString::Printf(TEXT("NNERuntimeORTCpu failed while creating %s."), ModelSpec.Name));
			return false;
		}

		TSharedPtr<IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
		if (!TestTrue(FString::Printf(TEXT("CreateModelInstanceCPU %s"), ModelSpec.Name), Instance.IsValid()))
		{
			AddError(FString::Printf(TEXT("NNERuntimeORTCpu created %s but not a CPU instance."), ModelSpec.Name));
			return false;
		}

		const TConstArrayView<FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
		if (!TestEqual(FString::Printf(TEXT("%s input count"), ModelSpec.Name), InputDescs.Num(), 1)
			|| !TestTrue(
				FString::Printf(TEXT("%s input is observations"), ModelSpec.Name),
				InputDescs.Num() == 1 && InputDescs[0].GetName() == TEXT("observations")))
		{
			FString Names;
			for (const FTensorDesc& Desc : InputDescs)
			{
				Names += Desc.GetName() + TEXT(" ");
			}
			AddError(FString::Printf(TEXT("%s inputs were [%s]. Architecture C has no expert-selection input."), ModelSpec.Name, *Names));
			return false;
		}

		const FTensorDesc& ObservationsDesc = InputDescs[0];
		const TConstArrayView<int32> ObservationDims = ObservationsDesc.GetShape().GetData();
		if (!TestTrue(FString::Printf(TEXT("%s observations is float32"), ModelSpec.Name), ObservationsDesc.GetDataType() == ENNETensorDataType::Float)
			|| !TestEqual(FString::Printf(TEXT("%s observations rank"), ModelSpec.Name), ObservationDims.Num(), 2)
			|| !TestEqual(
				FString::Printf(TEXT("%s observations feature dimension"), ModelSpec.Name),
				ObservationDims.Num() == 2 ? ObservationDims[1] : 0,
				ObservationWidth)
			|| !TestTrue(
				FString::Printf(TEXT("%s observations batch dimension is dynamic"), ModelSpec.Name),
				ObservationDims.Num() == 2 && ObservationDims[0] < 0))
		{
			AddError(FString::Printf(
				TEXT("%s observations contract mismatch. dtype/shape was %s."),
				ModelSpec.Name,
				*ShapeText(ObservationDims)));
			return false;
		}

		const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
		if (!TestEqual(FString::Printf(TEXT("%s output count"), ModelSpec.Name), OutputDescs.Num(), 1)
			|| !TestTrue(
				FString::Printf(TEXT("%s output is actions"), ModelSpec.Name),
				OutputDescs.Num() == 1 && OutputDescs[0].GetName() == TEXT("actions"))
			|| !TestTrue(
				FString::Printf(TEXT("%s actions is float32"), ModelSpec.Name),
				OutputDescs.Num() == 1 && OutputDescs[0].GetDataType() == ENNETensorDataType::Float))
		{
			FString Names;
			for (const FTensorDesc& Desc : OutputDescs)
			{
				Names += Desc.GetName() + TEXT(" ");
			}
			AddError(FString::Printf(TEXT("%s outputs were [%s]."), ModelSpec.Name, *Names));
			return false;
		}

		const TConstArrayView<int32> ActionDims = OutputDescs[0].GetShape().GetData();
		if (!TestEqual(FString::Printf(TEXT("%s actions rank"), ModelSpec.Name), ActionDims.Num(), 2)
			|| !TestEqual(
				FString::Printf(TEXT("%s actions feature dimension"), ModelSpec.Name),
				ActionDims.Num() == 2 ? ActionDims[1] : 0,
				ActionWidth)
			|| !TestTrue(
				FString::Printf(TEXT("%s actions batch dimension is dynamic"), ModelSpec.Name),
				ActionDims.Num() == 2 && ActionDims[0] < 0))
		{
			AddError(FString::Printf(
				TEXT("%s actions contract mismatch. Declared shape was %s."),
				ModelSpec.Name,
				*ShapeText(ActionDims)));
			return false;
		}

		for (const int32 BatchSize : BatchSizes)
		{
			const FString CaseName = FString::Printf(TEXT("%s batch=%d"), ModelSpec.Name, BatchSize);
			const uint32 ConcreteObservationDims[] = {uint32(BatchSize), uint32(ObservationWidth)};
			const TArray<FTensorShape> InputShapes = {FTensorShape::Make(MakeArrayView(ConcreteObservationDims))};
			if (!TestTrue(
					FString::Printf(TEXT("SetInputTensorShapes %s"), *CaseName),
					Instance->SetInputTensorShapes(InputShapes) == EResultStatus::Ok))
			{
				AddError(FString::Printf(TEXT("SetInputTensorShapes failed for %s."), *CaseName));
				return false;
			}

			TArray<float> Observations;
			Observations.SetNumZeroed(BatchSize * ObservationWidth);
			Observations[0] = 1.f;
			if (BatchSize > 1)
			{
				Observations[ObservationWidth] = 2.f;
			}

			TArray<float> Actions;
			Actions.SetNumZeroed(BatchSize * ActionWidth);
			const FTensorBindingCPU InputBinding = {Observations.GetData(), uint64(Observations.Num()) * sizeof(float)};
			const FTensorBindingCPU OutputBinding = {Actions.GetData(), uint64(Actions.Num()) * sizeof(float)};

			if (!TestTrue(
					FString::Printf(TEXT("RunSync %s"), *CaseName),
					Instance->RunSync(MakeArrayView(&InputBinding, 1), MakeArrayView(&OutputBinding, 1)) == EResultStatus::Ok))
			{
				AddError(FString::Printf(TEXT("NNERuntimeORTCpu RunSync failed for %s."), *CaseName));
				return false;
			}

			const TConstArrayView<FTensorShape> OutputShapes = Instance->GetOutputTensorShapes();
			if (!TestEqual(FString::Printf(TEXT("Resolved output count (%s)"), *CaseName), OutputShapes.Num(), 1))
			{
				return false;
			}
			const TConstArrayView<uint32> ActionShape = OutputShapes[0].GetData();
			if (!TestTrue(
					FString::Printf(TEXT("actions shape is [%d, 2] for %s, got %s"), BatchSize, *CaseName, *ShapeText(ActionShape)),
					ShapeIs(ActionShape, uint32(BatchSize), uint32(ActionWidth))))
			{
				return false;
			}

			for (int32 Row = 0; Row < BatchSize; ++Row)
			{
				for (int32 Column = 0; Column < ActionWidth; ++Column)
				{
					const double Reference = ModelSpec.Expected[Row][Column];
					const double Value = Actions[Row * ActionWidth + Column];
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
	}

	TestTrue(TEXT("NNE outputs match Python ONNX Runtime"), bValuesMatch);
	return bValuesMatch;
}

#endif

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "SharedPolicyRoutingTestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"

namespace SharedPolicyRouting
{
	using namespace UE::NNE;

	namespace
	{
		const TCHAR* RuntimeName = TEXT("NNERuntimeORTCpu");
		const TCHAR* ArchitectureCPaths[ExpertCount] = {
			TEXT("/Game/C_expert_0.C_expert_0"),
			TEXT("/Game/C_expert_1.C_expert_1"),
		};
		const TCHAR* ArchitectureDPath = TEXT("/Game/D_conditional.D_conditional");

		bool Fail(FString& OutError, const FString& Message)
		{
			OutError = Message;
			return false;
		}
	}

	bool RouteRequest(
		IExpertBackend& Backend,
		TConstArrayView<float> Observations,
		TConstArrayView<int32> Experts,
		TArray<float>& OutActions,
		FRequestReport& OutReport,
		FString& OutError)
	{
		OutActions.Reset();
		OutReport.Calls.Reset();
		OutError.Reset();

		if (Experts.IsEmpty())
		{
			return Fail(OutError, TEXT("A routed request must contain at least one row."));
		}
		if (Observations.Num() != Experts.Num() * ObservationWidth)
		{
			return Fail(OutError, FString::Printf(
				TEXT("Expected %d observation values for %d rows, got %d."),
				Experts.Num() * ObservationWidth,
				Experts.Num(),
				Observations.Num()));
		}

		TArray<TArray<int32>> RowsByExpert;
		RowsByExpert.SetNum(ExpertCount);
		for (int32 Row = 0; Row < Experts.Num(); ++Row)
		{
			const int32 Expert = Experts[Row];
			if (Expert < 0 || Expert >= ExpertCount)
			{
				return Fail(OutError, FString::Printf(TEXT("Row %d requested unknown expert %d."), Row, Expert));
			}
			RowsByExpert[Expert].Add(Row);
		}

		TArray<float> RestoredActions;
		RestoredActions.SetNumUninitialized(Experts.Num() * ActionWidth);
		FRequestReport Report;
		for (int32 Expert = 0; Expert < ExpertCount; ++Expert)
		{
			const TArray<int32>& Rows = RowsByExpert[Expert];
			if (Rows.IsEmpty())
			{
				continue;
			}

			TArray<float> GroupedObservations;
			GroupedObservations.SetNumUninitialized(Rows.Num() * ObservationWidth);
			for (int32 GroupRow = 0; GroupRow < Rows.Num(); ++GroupRow)
			{
				FMemory::Memcpy(
					GroupedObservations.GetData() + GroupRow * ObservationWidth,
					Observations.GetData() + Rows[GroupRow] * ObservationWidth,
					ObservationWidth * sizeof(float));
			}

			TArray<float> GroupedActions;
			if (!Backend.Run(Expert, GroupedObservations, Rows.Num(), GroupedActions, OutError))
			{
				OutActions.Reset();
				OutReport.Calls.Reset();
				return false;
			}
			if (GroupedActions.Num() != Rows.Num() * ActionWidth)
			{
				OutActions.Reset();
				OutReport.Calls.Reset();
				return Fail(OutError, FString::Printf(
					TEXT("Expert %d returned %d action values for %d rows; expected %d."),
					Expert,
					GroupedActions.Num(),
					Rows.Num(),
					Rows.Num() * ActionWidth));
			}

			for (int32 GroupRow = 0; GroupRow < Rows.Num(); ++GroupRow)
			{
				FMemory::Memcpy(
					RestoredActions.GetData() + Rows[GroupRow] * ActionWidth,
					GroupedActions.GetData() + GroupRow * ActionWidth,
					ActionWidth * sizeof(float));
			}
			Report.Calls.Add({Expert, Rows.Num()});
		}

		OutActions = MoveTemp(RestoredActions);
		OutReport = MoveTemp(Report);
		return true;
	}

	struct FNneExpertBackend::FImpl
	{
		explicit FImpl(EArchitecture InArchitecture)
			: Architecture(InArchitecture)
		{
		}

		EArchitecture Architecture;
		TArray<TSharedPtr<IModelCPU>> Models;
		TArray<TSharedPtr<IModelInstanceCPU>> Instances;
		int32 ObservationsIndex = INDEX_NONE;
		int32 ExpertIndex = INDEX_NONE;
		int32 ActionsIndex = INDEX_NONE;
	};

	FNneExpertBackend::FNneExpertBackend(EArchitecture InArchitecture)
		: Impl(MakeUnique<FImpl>(InArchitecture))
	{
	}

	FNneExpertBackend::~FNneExpertBackend() = default;

	bool FNneExpertBackend::Initialize(FString& OutError)
	{
		OutError.Reset();
		Impl->Models.Reset();
		Impl->Instances.Reset();
		Impl->ObservationsIndex = INDEX_NONE;
		Impl->ExpertIndex = INDEX_NONE;
		Impl->ActionsIndex = INDEX_NONE;

		TWeakInterfacePtr<INNERuntimeCPU> Runtime = GetRuntime<INNERuntimeCPU>(RuntimeName);
		if (!Runtime.IsValid())
		{
			return Fail(OutError, TEXT("NNERuntimeORTCpu is not registered."));
		}

		const int32 ModelCount = Impl->Architecture == EArchitecture::C ? ExpertCount : 1;
		for (int32 ModelIndex = 0; ModelIndex < ModelCount; ++ModelIndex)
		{
			const TCHAR* ObjectPath = Impl->Architecture == EArchitecture::C
				? ArchitectureCPaths[ModelIndex]
				: ArchitectureDPath;
			UNNEModelData* ModelData = LoadObject<UNNEModelData>(nullptr, ObjectPath);
			if (ModelData == nullptr)
			{
				return Fail(OutError, FString::Printf(TEXT("Missing imported NNE model %s."), ObjectPath));
			}
			if (Runtime->CanCreateModelCPU(ModelData) != EResultStatus::Ok)
			{
				return Fail(OutError, FString::Printf(TEXT("NNERuntimeORTCpu rejected %s."), ObjectPath));
			}

			TSharedPtr<IModelCPU> Model = Runtime->CreateModelCPU(ModelData);
			if (!Model.IsValid())
			{
				return Fail(OutError, FString::Printf(TEXT("CreateModelCPU failed for %s."), ObjectPath));
			}
			TSharedPtr<IModelInstanceCPU> Instance = Model->CreateModelInstanceCPU();
			if (!Instance.IsValid())
			{
				return Fail(OutError, FString::Printf(TEXT("CreateModelInstanceCPU failed for %s."), ObjectPath));
			}

			const TConstArrayView<FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
			const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
			if (Impl->Architecture == EArchitecture::C)
			{
				if (InputDescs.Num() != 1 || InputDescs[0].GetName() != TEXT("observations")
					|| OutputDescs.Num() != 1 || OutputDescs[0].GetName() != TEXT("actions"))
				{
					return Fail(OutError, FString::Printf(TEXT("Architecture C tensor contract mismatch for %s."), ObjectPath));
				}
			}
			else
			{
				for (int32 InputIndex = 0; InputIndex < InputDescs.Num(); ++InputIndex)
				{
					if (InputDescs[InputIndex].GetName() == TEXT("observations"))
					{
						Impl->ObservationsIndex = InputIndex;
					}
					else if (InputDescs[InputIndex].GetName() == TEXT("use_first_expert"))
					{
						Impl->ExpertIndex = InputIndex;
					}
				}
				for (int32 OutputIndex = 0; OutputIndex < OutputDescs.Num(); ++OutputIndex)
				{
					if (OutputDescs[OutputIndex].GetName() == TEXT("actions"))
					{
						Impl->ActionsIndex = OutputIndex;
					}
				}
				if (Impl->ObservationsIndex == INDEX_NONE || Impl->ExpertIndex == INDEX_NONE
					|| Impl->ActionsIndex == INDEX_NONE)
				{
					return Fail(OutError, TEXT("Architecture D tensor contract mismatch."));
				}
			}

			Impl->Models.Add(MoveTemp(Model));
			Impl->Instances.Add(MoveTemp(Instance));
		}
		return true;
	}

	bool FNneExpertBackend::Run(
		int32 Expert,
		TConstArrayView<float> GroupedObservations,
		int32 RowCount,
		TArray<float>& OutGroupedActions,
		FString& OutError)
	{
		OutGroupedActions.Reset();
		OutError.Reset();
		if (Expert < 0 || Expert >= ExpertCount || RowCount <= 0
			|| GroupedObservations.Num() != RowCount * ObservationWidth)
		{
			return Fail(OutError, TEXT("Invalid grouped NNE request."));
		}

		IModelInstanceCPU* Instance = Impl->Architecture == EArchitecture::C
			? Impl->Instances[Expert].Get()
			: Impl->Instances[0].Get();
		const TConstArrayView<FTensorDesc> InputDescs = Instance->GetInputTensorDescs();
		const TConstArrayView<FTensorDesc> OutputDescs = Instance->GetOutputTensorDescs();
		const uint32 ConcreteObservationDims[] = {uint32(RowCount), uint32(ObservationWidth)};
		TArray<FTensorShape> InputShapes;
		InputShapes.Reserve(InputDescs.Num());
		for (int32 InputIndex = 0; InputIndex < InputDescs.Num(); ++InputIndex)
		{
			const bool bIsObservations = Impl->Architecture == EArchitecture::C
				|| InputIndex == Impl->ObservationsIndex;
			InputShapes.Add(bIsObservations
				? FTensorShape::Make(MakeArrayView(ConcreteObservationDims))
				: FTensorShape::Make(TConstArrayView<uint32>()));
		}
		if (Instance->SetInputTensorShapes(InputShapes) != EResultStatus::Ok)
		{
			return Fail(OutError, FString::Printf(TEXT("SetInputTensorShapes failed for expert %d."), Expert));
		}

		TArray<TArray<float>> OutputStorage;
		TArray<FTensorBindingCPU> OutputBindings;
		OutputStorage.SetNum(OutputDescs.Num());
		OutputBindings.Reserve(OutputDescs.Num());
		for (int32 OutputIndex = 0; OutputIndex < OutputDescs.Num(); ++OutputIndex)
		{
			OutputStorage[OutputIndex].SetNumZeroed(RowCount * ActionWidth);
			OutputBindings.Add({
				OutputStorage[OutputIndex].GetData(),
				uint64(OutputStorage[OutputIndex].Num()) * sizeof(float)});
		}

		uint8 UseFirstExpert = Expert == 0 ? 1 : 0;
		TArray<FTensorBindingCPU> InputBindings;
		InputBindings.SetNum(InputDescs.Num());
		if (Impl->Architecture == EArchitecture::C)
		{
			InputBindings[0] = {
				const_cast<float*>(GroupedObservations.GetData()),
				uint64(GroupedObservations.Num()) * sizeof(float)};
		}
		else
		{
			InputBindings[Impl->ObservationsIndex] = {
				const_cast<float*>(GroupedObservations.GetData()),
				uint64(GroupedObservations.Num()) * sizeof(float)};
			InputBindings[Impl->ExpertIndex] = {&UseFirstExpert, 1};
		}

		if (Instance->RunSync(InputBindings, OutputBindings) != EResultStatus::Ok)
		{
			return Fail(OutError, FString::Printf(TEXT("RunSync failed for expert %d."), Expert));
		}
		const int32 OutputIndex = Impl->Architecture == EArchitecture::C ? 0 : Impl->ActionsIndex;
		OutGroupedActions = MoveTemp(OutputStorage[OutputIndex]);
		return true;
	}
}

#endif

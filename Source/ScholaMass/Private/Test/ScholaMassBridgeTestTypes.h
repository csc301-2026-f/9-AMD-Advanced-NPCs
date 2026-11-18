#pragma once
#include "Mass/EntityFragments.h"

#include "ScholaMassInferenceProcessors.h"
#include "ScholaMassInferenceSubsystem.h"
#include "Inference/NPCBackgroundBackendProvider.h"
#include "Inference/DeterministicBatchBackend.h"

#include "ScholaMassBridgeTestTypes.generated.h"

/** Test-only access to the subsystem's transport. */
struct FScholaMassTestAccess
{
	static FScholaMassInferenceBridge& Bridge(UScholaMassInferenceSubsystem& Subsystem) { return Subsystem.Bridge; }
};
USTRUCT()
struct FScholaMassMigrationTestTag : public FMassTag
{
	GENERATED_BODY()
};

UCLASS()
class UScholaMassTestRowProvider : public UNPCBackgroundBackendProvider
{
	GENERATED_BODY()
public:
	virtual TSharedPtr<IBatchInferenceBackend> CreateBackend(const FInteractionDefinition&, FNPCRowLayout& Layout, FString& Error) override
	{
		Layout = {};
		Layout.ObsSize = Layout.ActSize = 1;
		Layout.Inputs = Layout.Outputs = { { TEXT("value"), ENPCRowDType::Float, 0, 1 } };
		Layout.UpdateHash();
		Error.Reset();
		FDeterministicBatchBackendSettings S;
		S.ObservationSize = S.ActionSize = 1;
		return MakeShared<FDeterministicBatchBackend>(S);
	}
};
UCLASS()
class UScholaMassTestGather : public UScholaMassGatherProcessor
{
	GENERATED_BODY()
public:
	UScholaMassTestGather() { bAutoRegisterWithProcessingPhases = false; }

protected:
	virtual void WriteObservations(FMassExecutionContext& Context, const FScholaMassObservationRows& Rows) override;
};
UCLASS()
class UScholaMassTestApply : public UScholaMassApplyProcessor
{
	GENERATED_BODY()
public:
	UScholaMassTestApply() { bAutoRegisterWithProcessingPhases = false; }
	int32 Applied = 0;

protected:
	virtual void ApplyActions(FMassExecutionContext&, const FScholaMassActionRows& Rows) override { Applied += Rows.Num(); }
};

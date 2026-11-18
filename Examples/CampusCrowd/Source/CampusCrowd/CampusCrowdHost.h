// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "GameFramework/Actor.h"
#include "ScholaMassPolicy.h"
#include "CrowdStats.h"
#include "CampusCrowdHost.generated.h"

class ACampusScene;
class UCampusCameraRig;
class UInstancedStaticMeshComponent;
class UNNEModelData;
class UCrowdGatherProcessor;
class UCrowdApplyProcessor;
class UCrowdMoveProcessor;

/**
 * @brief Spawns a Mass crowd on the campus, driven by one ONNX steering policy through Schola's background NPC API.
 *
 * Each frame the host applies finished actions, submits newly due observations, then moves the crowd.
 *
 * Command line: -Agents=20000 -DecisionHz=10 -MaxFPS=F -Workers=N -MaxBatch=N -MaxPendingRows=N -MaxRowsPerFrame=N;
 * capture with -CaptureSeconds=S -WarmupSeconds=S -Report=path.json; camera with -View=0..3 -FlightSeconds=S.
 */
UCLASS()
class ACampusCrowdHost : public AActor
{
	GENERATED_BODY()

public:
	ACampusCrowdHost();
	virtual void Tick(float DeltaSeconds) override;

	int32					  GetAgentCount() const { return Entities.Num(); }
	double					  GetTargetDecisionsPerSecond() const { return Entities.Num() * double(DecisionHz); }
	const FCrowdDisplayStats& GetDisplayStats() const { return Stats.GetDisplay(); }
	const FString&			  GetError() const { return Error; }
	double					  GetCaptureProgress(FString& OutPhase) const { return Stats.GetCaptureProgress(OutPhase); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;

private:
	bool BuildScene();
	bool LoadModel();
	void SpawnCrowd(int32 AgentCount);
	void WriteReport();
	void Fail(const FString& Message);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> Crowd;

	UPROPERTY()
	TObjectPtr<ACampusScene> Campus;

	UPROPERTY()
	TObjectPtr<UCampusCameraRig> CameraRig;

	UPROPERTY()
	TObjectPtr<UNNEModelData> ModelData;

	UPROPERTY()
	TObjectPtr<UCrowdGatherProcessor> Gather;

	UPROPERTY()
	TObjectPtr<UCrowdApplyProcessor> Apply;

	UPROPERTY()
	TObjectPtr<UCrowdMoveProcessor> Move;

	/** @brief The crowd's model and queue; owns sizing and background statistics. */
	FScholaMassPolicy Policy;

	FCrowdStats				  Stats;
	TArray<FMassEntityHandle> Entities;
	TArray<FTransform>		  Transforms;
	FString					  Error;
	float					  DecisionHz = 10;
	double					  Started = 0;
	double					  PreviousTick = 0;
	double					  WarmupSeconds = 10;
	bool					  bPaused = false;
	bool					  bScreenshotTaken = false;
};

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "GameFramework/Actor.h"
#if __has_include("Mass/EntityHandle.h")
#include "Mass/EntityHandle.h"
#else
#include "MassEntityHandle.h" // UE 5.7 and earlier
#endif
#include "MassEntityTypes.h"
#include "Inference/BackgroundInferenceTypes.h"
#include "CrowdTypes.h"
#include "CrowdComparisonHost.generated.h"

class UInstancedStaticMeshComponent;
class UNNEModelData;
class UCrowdGatherProcessor;
class UCrowdApplyProcessor;
class UCrowdSyncInferenceProcessor;
class UCrowdMoveProcessor;
class FNPCBackgroundScheduler;

/** Values shown on the HUD, refreshed once per second. */
struct FCrowdDisplayStats
{
    double FramesPerSecond = 0;
    double FrameMs = 0;
    double FrameP99Ms = 0;
    double InferenceMs = 0;
    double DecisionsPerSecond = 0;
    double LatencyMs = 0;
    double RowsPerModelCall = 0;
    /** Total model time per second: on the game thread (sync) or summed across workers (background). */
    double ModelMsPerSecond = 0;
    int64 DroppedRows = 0;
};

/**
 * Spawns a Mass crowd driven by one ONNX steering policy and runs it either
 * with Schola's background NPC API or with per-agent synchronous inference.
 * Everything except the inference path (scene, agents, observations, movement,
 * decision rate) is identical between the two modes.
 *
 * Command line: -Mode=Background|Sync -Agents=20000 -DecisionHz=10 -MaxFPS=F
 * -Workers=N -MaxBatch=N; capture with -CaptureSeconds=S -WarmupSeconds=S -Report=path.json
 */
UCLASS()
class ACrowdComparisonHost : public AActor
{
    GENERATED_BODY()
public:
    ACrowdComparisonHost();
    virtual void Tick(float DeltaSeconds) override;

    void SetMode(ECrowdInferenceMode NewMode);
    ECrowdInferenceMode GetMode() const { return Mode; }
    int32 GetAgentCount() const { return Entities.Num(); }
    double GetTargetDecisionsPerSecond() const { return Entities.Num() / PeriodSeconds; }
    const FCrowdDisplayStats& GetDisplayStats() const { return Display; }
    const FString& GetError() const { return Error; }
    double GetCaptureProgress(FString& OutPhase) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;

private:
    bool LoadPolicy();
    void BuildScene();
    bool SpawnCrowd();
    void RunInference(float DeltaSeconds);
    void UpdateDisplay(double Now, double FrameMs, double InferenceMs);
    void WriteReport();
    void Fail(const FString& Message);

    UPROPERTY(VisibleAnywhere) TObjectPtr<UInstancedStaticMeshComponent> Crowd;
    UPROPERTY() TObjectPtr<UNNEModelData> ModelData;
    UPROPERTY() TObjectPtr<UCrowdGatherProcessor> Gather;
    UPROPERTY() TObjectPtr<UCrowdApplyProcessor> Apply;
    UPROPERTY() TObjectPtr<UCrowdSyncInferenceProcessor> Sync;
    UPROPERTY() TObjectPtr<UCrowdMoveProcessor> Move;

    TSharedPtr<FNPCBackgroundScheduler, ESPMode::ThreadSafe> Scheduler;
    TArray<FMassEntityHandle> Entities;
    TArray<FTransform> Transforms;
    ECrowdInferenceMode Mode = ECrowdInferenceMode::Background;
    FCrowdFrameCounters Counters;
    FCrowdDisplayStats Display;
    FString Error;
    float HalfSize = 0;
    double PeriodSeconds = .5;

    // Rolling one-second display window.
    double WindowStart = 0, PreviousTick = 0;
    int32 WindowFrames = 0;
    double WindowFrameMs = 0, WindowInferenceMs = 0;
    FCrowdFrameCounters WindowCounters;
    TArray<double> RecentFrameMs;
    FNPCBackgroundStats WindowSchedulerStart, ModeSchedulerStart;

    // Optional measured capture for the comparison report.
    FString ReportPath;
    double WarmupSeconds = 10, CaptureSeconds = 0, Started = 0, CaptureStart = 0;
    bool bCapturing = false, bReported = false;
    TArray<double> CaptureFrameMs, CaptureInferenceMs;
    FCrowdFrameCounters CaptureCounters;
    FNPCBackgroundStats CaptureSchedulerStart;
};

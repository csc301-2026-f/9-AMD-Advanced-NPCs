// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdComparisonHost.h"
#include "CrowdProcessors.h"
#include "CampusScene.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "UnrealClient.h"
#include "Materials/MaterialInterface.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Inference/BackgroundInferenceScheduler.h"
#include "Inference/NNEBatchBackendProvider.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NNE.h"
#include "NNEModelData.h"
#include "NNERuntimeCPU.h"
#include "RHIGlobals.h"
#include "ScholaInferenceFragments.h"
#include "ScholaMassInferenceSubsystem.h"
#include "Serialization/JsonSerializer.h"
#include "Spaces/BoxSpace.h"
#include "UObject/ConstructorHelpers.h"
#include "Async/ParallelFor.h"

namespace
{
double Quantile(TArray<double> Values, double Q)
{
    if (Values.IsEmpty()) return 0;
    Values.Sort();
    return Values[FMath::Clamp(FMath::CeilToInt(Q * Values.Num()) - 1, 0, Values.Num() - 1)];
}

double Mean(const TArray<double>& Values)
{
    double Sum = 0;
    for (double Value : Values) Sum += Value;
    return Values.IsEmpty() ? 0 : Sum / Values.Num();
}

double RowsPerCall(const FNPCBackgroundStats& Now, const FNPCBackgroundStats& Before)
{
    const int64 Calls = Now.BackendCalls - Before.BackendCalls;
    return Calls > 0 ? double(Now.BackendRows - Before.BackendRows) / Calls : 0;
}

double ServiceMs(const FNPCBackgroundStats& Now, const FNPCBackgroundStats& Before)
{
    return (Now.BackendServiceSeconds - Before.BackendServiceSeconds) * 1000;
}

int64 Dropped(const FNPCBackgroundStats& Now, const FNPCBackgroundStats& Before)
{
    return (Now.BusyRejections - Before.BusyRejections) + (Now.ExpiredRows - Before.ExpiredRows) +
        (Now.FailedRows - Before.FailedRows);
}

FMassEntityManager& EntityManager(UWorld* World)
{
    return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
}

/** One key of the comparison flight: camera position and the point it looks at. */
struct FFlightKey
{
    double Time;
    FVector Location;
    FVector Target;
};

// A flight (keys in seconds at the default ten-second length) checked against
// the CAD roof heights: it starts 22 m over the front lawn crowd, crosses the
// road towards Queen's Park, banks over the park and pulls back until the
// whole campus model fills the frame below the HUD panels.
const FFlightKey FlightKeys[] = {
    {0.0, FVector(-3000, 17000, 2200), FVector(-11000, 9000, 0)},
    {3.0, FVector(4000, 8000, 4500), FVector(22000, 2000, 0)},
    {6.0, FVector(27000, 12000, 14000), FVector(4000, -14000, 0)},
    {10.0, FVector(38800, 164600, 152700), FVector(-13400, 21200, 0)},
};

FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, double U)
{
    return .5 * (2 * P1 + (P2 - P0) * U + (2 * P0 - 5 * P1 + 4 * P2 - P3) * U * U +
        (3 * P1 - P0 - 3 * P2 + P3) * U * U * U);
}

void EvaluateFlight(double Seconds, FVector& OutLocation, FVector& OutTarget)
{
    constexpr int32 Last = UE_ARRAY_COUNT(FlightKeys) - 1;
    const double Time = FMath::Clamp(Seconds, 0.0, FlightKeys[Last].Time);
    int32 I = 0;
    while (I < Last - 1 && Time >= FlightKeys[I + 1].Time) ++I;
    const double U = (Time - FlightKeys[I].Time) / (FlightKeys[I + 1].Time - FlightKeys[I].Time);
    const FFlightKey& K0 = FlightKeys[FMath::Max(I - 1, 0)];
    const FFlightKey& K1 = FlightKeys[I];
    const FFlightKey& K2 = FlightKeys[I + 1];
    // Mirroring the previous key past the end gives zero velocity there, so the flight settles on the overview.
    const FFlightKey& K3 = FlightKeys[I + 2 <= Last ? I + 2 : I];
    OutLocation = CatmullRom(K0.Location, K1.Location, K2.Location, K3.Location, U);
    OutTarget = CatmullRom(K0.Target, K1.Target, K2.Target, K3.Target, U);
}
}

ACrowdComparisonHost::ACrowdComparisonHost()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Crowd = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Crowd"));
    RootComponent = Crowd;
    Crowd->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Crowd->SetCastShadow(true);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    Crowd->SetStaticMesh(Cylinder.Object);
}

void ACrowdComparisonHost::BeginPlay()
{
    Super::BeginPlay();
    const TCHAR* Command = FCommandLine::Get();
    int32 AgentCount = 2000, Workers = 2, MaxBatch = 64;
    float DecisionHz = 10, MaxFPS = 60;
    FString ModeName = TEXT("Background");
    FParse::Value(Command, TEXT("View="), CameraPreset);
    FParse::Value(Command, TEXT("FlightSeconds="), FlightSeconds);
    FlightSeconds = FMath::Max(FlightSeconds, 1.0);
    CameraPreset = FMath::Clamp(CameraPreset, 0, 3);
    FParse::Value(Command, TEXT("Agents="), AgentCount);
    FParse::Value(Command, TEXT("DecisionHz="), DecisionHz);
    FParse::Value(Command, TEXT("MaxFPS="), MaxFPS);
    FParse::Value(Command, TEXT("Workers="), Workers);
    FParse::Value(Command, TEXT("MaxBatch="), MaxBatch);
    FParse::Value(Command, TEXT("Mode="), ModeName);
    FParse::Value(Command, TEXT("CaptureSeconds="), CaptureSeconds);
    FParse::Value(Command, TEXT("WarmupSeconds="), WarmupSeconds);
    FParse::Value(Command, TEXT("Report="), ReportPath);
    AgentCount = FMath::Clamp(AgentCount, 1, 100000);
    PeriodSeconds = 1.0 / FMath::Clamp(DecisionHz, .1f, 60.f);
    HalfSize = FMath::Sqrt(float(AgentCount)) * 75.f;

    if (MaxFPS > 0) IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(MaxFPS);
    if (auto* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) VSync->Set(0);

    BuildScene();
    if (!Campus || Campus->Nodes.Num() < 2) { Fail(TEXT("Campus route data could not be loaded")); return; }
    HalfSize = Campus->Extent;
    if (!LoadPolicy()) return;

    // One policy and one batching bucket shared by the whole crowd.
    FBoxSpace Observations, Actions;
    for (int32 I = 0; I < CrowdSim::ObservationSize; ++I) Observations.Add(-1e6f, 1e6f);
    for (int32 I = 0; I < CrowdSim::ActionSize; ++I) Actions.Add(-1e6f, 1e6f);
    const FInteractionDefinition Definition(TInstancedStruct<FSpace>::Make<FBoxSpace>(Observations),
        TInstancedStruct<FSpace>::Make<FBoxSpace>(Actions));
    auto* Provider = NewObject<UNNEBatchBackendProvider>(this);
    Provider->ModelData = ModelData;
    FNPCBackgroundSchedulerSettings Settings;
    Settings.WorkerCount = FMath::Clamp(Workers, 1, 64);
    Settings.MaxBatchSize = FMath::Max(1, MaxBatch);
    FString BucketError;
    auto* Service = GetWorld()->GetSubsystem<UScholaMassInferenceSubsystem>();
    const int32 Bucket = Service ? Service->AcquireBucket(*Provider, Definition, Settings, BucketError) : INDEX_NONE;
    if (Bucket == INDEX_NONE) { Fail(TEXT("Background bucket: ") + BucketError); return; }
    {
        FScopeLock Lock(&Service->Mutex);
        Scheduler = Service->Bridge.GetScheduler(Bucket);
    }

    auto& Manager = EntityManager(GetWorld());
    const UScriptStruct* Types[] = {FCrowdAgentFragment::StaticStruct(),
        FScholaInferenceStateFragment::StaticStruct(), FScholaInferenceScheduleFragment::StaticStruct()};
    const FMassArchetypeHandle Archetype = Manager.CreateArchetype(MakeArrayView(Types));
    FMassArchetypeSharedFragmentValues Shared;
    FScholaInferenceBucketFragment BucketFragment;
    BucketFragment.BucketId = Bucket;
    Shared.Add(Manager.GetOrCreateConstSharedFragment(BucketFragment));
    Shared.Sort();
    Started = FPlatformTime::Seconds();
    {
        // Fill fragments before the creation context fires ScholaMass's schedule observer.
        auto Creation = Manager.BatchCreateEntities(Archetype, Shared, AgentCount, Entities);
        Transforms.SetNum(AgentCount);
        for (int32 I = 0; I < AgentCount; ++I)
        {
            auto& Agent = Manager.GetFragmentDataChecked<FCrowdAgentFragment>(Entities[I]);
            Agent.Instance = I;
            Agent.Random = 0x9E3779B9u ^ uint32(I * 7919 + 1);
            Agent.CurrentNode = CrowdSim::NextRandom(Agent.Random) % Campus->Nodes.Num();
            const auto& Node = Campus->Nodes[Agent.CurrentNode];
            Agent.NextNode = Node.Links[CrowdSim::NextRandom(Agent.Random) % Node.Links.Num()];
            Agent.Goal = Campus->Nodes[Agent.NextNode].Position;
            Agent.Lane = (CrowdSim::NextRandom(Agent.Random) / float(MAX_uint32) - .5f) * 220.f;
            Agent.WalkSpeed = 115.f + CrowdSim::NextRandom(Agent.Random) % 70;
            Agent.Position = FMath::Lerp(Node.Position, Agent.Goal, CrowdSim::NextRandom(Agent.Random) / float(MAX_uint32));
            const FVector2f Along = (Agent.Goal - Node.Position).GetSafeNormal();
            Agent.Position += FVector2f(-Along.Y, Along.X) * Agent.Lane;
            Agent.Steering = (Agent.Goal - Agent.Position).GetSafeNormal();
            Agent.SyncNextDue = Started + PeriodSeconds * (CrowdSim::NextRandom(Agent.Random) / double(MAX_uint32));
            Manager.GetFragmentDataChecked<FScholaInferenceScheduleFragment>(Entities[I]).PeriodSeconds = PeriodSeconds;
            Transforms[I] = FTransform(FRotator::ZeroRotator, FVector(Agent.Position.X, Agent.Position.Y, 94), FVector(.6f, .6f, 1.8f));
        }
    }
    Crowd->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Campus/M_Students.M_Students")));
    Crowd->AddInstances(Transforms, false, true, false);

    Gather = NewObject<UCrowdGatherProcessor>(this);
    Apply = NewObject<UCrowdApplyProcessor>(this);
    Sync = NewObject<UCrowdSyncInferenceProcessor>(this);
    Move = NewObject<UCrowdMoveProcessor>(this);
    Gather->HalfSize = Sync->HalfSize = Move->HalfSize = HalfSize;
    Apply->Counters = Sync->Counters = &Counters;
    Sync->PeriodSeconds = PeriodSeconds;
    Move->Transforms = &Transforms;
    Move->Campus = Campus;
    for (UMassProcessor* Processor : TArray<UMassProcessor*>{Gather, Apply, Sync, Move})
        Processor->CallInitialize(this, Manager.AsShared());

    // Without the API: one model instance, reshaped once to batch 1, called per agent.
    auto Runtime = UE::NNE::GetRuntime<INNERuntimeCPU>(TEXT("NNERuntimeORTCpu"));
    Sync->Model = Runtime.IsValid() ? Runtime->CreateModelCPU(ModelData) : nullptr;
    Sync->Instance = Sync->Model ? Sync->Model->CreateModelInstanceCPU() : nullptr;
    const UE::NNE::FTensorShape Shape = UE::NNE::FTensorShape::Make({1u, uint32(CrowdSim::ObservationSize)});
    if (!Sync->Instance || Sync->Instance->SetInputTensorShapes(MakeArrayView(&Shape, 1)) != UE::NNE::EResultStatus::Ok)
    { Fail(TEXT("Could not create the synchronous ORT model instance")); return; }

    SetMode(ModeName.Equals(TEXT("Sync"), ESearchCase::IgnoreCase) || ModeName.Equals(TEXT("Synchronous"), ESearchCase::IgnoreCase)
        ? ECrowdInferenceMode::Synchronous : ECrowdInferenceMode::Background);
    PreviousTick = WindowStart = FPlatformTime::Seconds();
}

bool ACrowdComparisonHost::LoadPolicy()
{
    const FString Path = FPaths::ProjectContentDir() / TEXT("Models/crowd_policy.onnx");
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *Path)) { Fail(TEXT("Missing policy file ") + Path); return false; }
    ModelData = NewObject<UNNEModelData>(this);
    ModelData->Init(TEXT("onnx"), Bytes);
    return true;
}

void ACrowdComparisonHost::BuildScene()
{
    UWorld* World = GetWorld();
    TActorIterator<ACampusScene> It(World);
    if (It) Campus = *It;
    if (!Campus) Campus = World->SpawnActor<ACampusScene>();
    if (Campus->Nodes.IsEmpty()) Campus->LoadCampus();
    HalfSize = Campus->Extent;
    TActorIterator<ADirectionalLight> SunIt(World);
    auto* Sun = SunIt ? *SunIt : World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-50, -35, 0));
    auto* SunLight = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
    SunLight->SetMobility(EComponentMobility::Movable);
    SunLight->SetIntensity(6);
    SunLight->SetAtmosphereSunLight(true);
    TActorIterator<ASkyAtmosphere> SkyIt(World);
    if (!SkyIt) World->SpawnActor<ASkyAtmosphere>();
    TActorIterator<ASkyLight> AmbientIt(World);
    auto* Ambient = AmbientIt ? *AmbientIt : World->SpawnActor<ASkyLight>();
    Ambient->GetLightComponent()->SetMobility(EComponentMobility::Movable);
    Ambient->GetLightComponent()->bRealTimeCapture = true;
    Ambient->GetLightComponent()->SetIntensity(1.5f);
    Ambient->GetLightComponent()->RecaptureSky();

    auto* Camera = World->SpawnActor<ACameraActor>(FVector(0, HalfSize * 1.55, HalfSize * 1.25), FRotator::ZeroRotator);
    Camera->SetActorRotation((FVector(0, -HalfSize * .1, 0) - Camera->GetActorLocation()).Rotation());
    CampusCamera = Camera;
    Camera->GetCameraComponent()->SetFieldOfView(55);
    Camera->GetCameraComponent()->bConstrainAspectRatio = false;
    if (auto* Player = World->GetFirstPlayerController()) Player->SetViewTarget(Camera);
}

void ACrowdComparisonHost::SetMode(ECrowdInferenceMode NewMode)
{
    Mode = NewMode;
    Counters = WindowCounters = CaptureCounters = {};
    if (Scheduler) WindowSchedulerStart = ModeSchedulerStart = CaptureSchedulerStart = Scheduler->GetStats();
    if (Mode == ECrowdInferenceMode::Synchronous && !Entities.IsEmpty())
    {
        // Agents keep their phase offsets; restart the synchronous schedule from now.
        auto& Manager = EntityManager(GetWorld());
        const double Now = FPlatformTime::Seconds();
        for (const FMassEntityHandle Entity : Entities)
        {
            auto& Agent = Manager.GetFragmentDataChecked<FCrowdAgentFragment>(Entity);
            Agent.SyncNextDue = Now + FMath::Fmod(Agent.SyncNextDue - Started, PeriodSeconds);
        }
    }
}

void ACrowdComparisonHost::RunInference(float DeltaSeconds)
{
    auto& Manager = EntityManager(GetWorld());
    UE::Mass::FProcessingContext Context(Manager, DeltaSeconds);
    if (Mode == ECrowdInferenceMode::Synchronous)
    {
        UE::Mass::Executor::Run(*Sync, Context);
        if (Sync->bFailed) Fail(TEXT("Synchronous RunSync failed"));
    }
    else
    {
        // Apply results that finished since last frame, then submit newly due rows.
        UE::Mass::Executor::Run(*Apply, Context);
        UE::Mass::Executor::Run(*Gather, Context);
    }
}

void ACrowdComparisonHost::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (Entities.IsEmpty() || !Error.IsEmpty()) return;
    if (auto* Player = GetWorld()->GetFirstPlayerController(); Player && Player->WasInputKeyJustPressed(EKeys::Tab))
        SetMode(Mode == ECrowdInferenceMode::Background ? ECrowdInferenceMode::Synchronous : ECrowdInferenceMode::Background);

    const double Now = FPlatformTime::Seconds();
    const double FrameMs = (Now - PreviousTick) * 1000;
    PreviousTick = Now;

    UpdateCamera(DeltaSeconds);
    const FCrowdFrameCounters Before = Counters;
    RunInference(DeltaSeconds);
    const double InferenceMs = (FPlatformTime::Seconds() - Now) * 1000;

    UE::Mass::FProcessingContext MoveContext(EntityManager(GetWorld()), DeltaSeconds);
    if (!bPaused) UE::Mass::Executor::Run(*Move, MoveContext);
    KeepCrowdVisible();
    Crowd->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
    UpdateDisplay(Now, FrameMs, InferenceMs);
    if (!bScreenshotTaken && Now-Started >= FMath::Min(WarmupSeconds * .5, 5.0))
    {
        FString Screenshot;
        if (FParse::Value(FCommandLine::Get(), TEXT("Screenshot="), Screenshot))
            FScreenshotRequest::RequestScreenshot(Screenshot, !FParse::Param(FCommandLine::Get(), TEXT("CleanScreenshot")), false);
        bScreenshotTaken = true;
    }
    if (ReportPath.IsEmpty() || CaptureSeconds <= 0 || bReported) return;
    if (!bCapturing && Now - Started >= WarmupSeconds)
    {
        bCapturing = true;
        CaptureStart = Now;
        CaptureCounters = {};
        if (Scheduler) CaptureSchedulerStart = Scheduler->GetStats();
    }
    else if (bCapturing)
    {
        CaptureFrameMs.Add(FrameMs);
        CaptureInferenceMs.Add(InferenceMs);
        CaptureCounters.Decisions += Counters.Decisions - Before.Decisions;
        CaptureCounters.LatencySumMs += Counters.LatencySumMs - Before.LatencySumMs;
        if (Now - CaptureStart >= CaptureSeconds) WriteReport();
    }
}

void ACrowdComparisonHost::UpdateDisplay(double Now, double FrameMs, double InferenceMs)
{
    ++WindowFrames;
    WindowFrameMs += FrameMs;
    WindowInferenceMs += InferenceMs;
    RecentFrameMs.Add(FrameMs);
    if (RecentFrameMs.Num() > 600) RecentFrameMs.RemoveAt(0, RecentFrameMs.Num() - 600, EAllowShrinking::No);
    const double Elapsed = Now - WindowStart;
    if (Elapsed < 1) return;
    const int64 Decisions = Counters.Decisions - WindowCounters.Decisions;
    Display.FramesPerSecond = WindowFrames / Elapsed;
    Display.FrameMs = WindowFrameMs / WindowFrames;
    Display.FrameP99Ms = Quantile(RecentFrameMs, .99);
    Display.InferenceMs = WindowInferenceMs / WindowFrames;
    Display.DecisionsPerSecond = Decisions / Elapsed;
    Display.LatencyMs = Decisions > 0 ? (Counters.LatencySumMs - WindowCounters.LatencySumMs) / Decisions : 0;
    if (Scheduler && Mode == ECrowdInferenceMode::Background)
    {
        const FNPCBackgroundStats Stats = Scheduler->GetStats();
        Display.RowsPerModelCall = RowsPerCall(Stats, WindowSchedulerStart);
        Display.ModelMsPerSecond = ServiceMs(Stats, WindowSchedulerStart) / Elapsed;
        Display.DroppedRows = Dropped(Stats, ModeSchedulerStart);
        WindowSchedulerStart = Stats;
    }
    else
    {
        Display.RowsPerModelCall = 1;
        Display.ModelMsPerSecond = WindowInferenceMs / Elapsed;
        Display.DroppedRows = 0;
    }
    WindowCounters = Counters;
    WindowStart = Now;
    WindowFrames = 0;
    WindowFrameMs = WindowInferenceMs = 0;
}

double ACrowdComparisonHost::GetCaptureProgress(FString& OutPhase) const
{
    if (ReportPath.IsEmpty() || CaptureSeconds <= 0) return -1;
    const double Now = FPlatformTime::Seconds();
    if (!bCapturing) { OutPhase = TEXT("Warming up"); return FMath::Clamp((Now - Started) / FMath::Max(WarmupSeconds, .01), 0., 1.); }
    OutPhase = TEXT("Measuring");
    return FMath::Clamp((Now - CaptureStart) / CaptureSeconds, 0., 1.);
}

void ACrowdComparisonHost::WriteReport()
{
    bReported = true;
    const double Elapsed = FPlatformTime::Seconds() - CaptureStart;
    const int64 Decisions = CaptureCounters.Decisions;
    auto Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("mode"), Mode == ECrowdInferenceMode::Background ? TEXT("background") : TEXT("synchronous"));
    Root->SetNumberField(TEXT("agents"), Entities.Num());
    Root->SetStringField(TEXT("scene"), TEXT("50H_NORTH campus"));
    Root->SetNumberField(TEXT("cad_buildings"), Campus->BuildingCount);
    Root->SetNumberField(TEXT("route_nodes"), Campus->Nodes.Num());
    Root->SetNumberField(TEXT("decision_hz_per_agent"), 1.0 / PeriodSeconds);
    Root->SetNumberField(TEXT("target_decisions_per_second"), GetTargetDecisionsPerSecond());
    Root->SetNumberField(TEXT("max_fps"), IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->GetFloat());
    Root->SetNumberField(TEXT("capture_seconds"), Elapsed);
    Root->SetNumberField(TEXT("frames"), CaptureFrameMs.Num());
    Root->SetNumberField(TEXT("fps"), CaptureFrameMs.Num() / Elapsed);
    Root->SetNumberField(TEXT("frame_ms_mean"), Mean(CaptureFrameMs));
    Root->SetNumberField(TEXT("frame_ms_p50"), Quantile(CaptureFrameMs, .5));
    Root->SetNumberField(TEXT("frame_ms_p95"), Quantile(CaptureFrameMs, .95));
    Root->SetNumberField(TEXT("frame_ms_p99"), Quantile(CaptureFrameMs, .99));
    Root->SetNumberField(TEXT("inference_game_thread_ms_mean"), Mean(CaptureInferenceMs));
    Root->SetNumberField(TEXT("inference_game_thread_ms_p99"), Quantile(CaptureInferenceMs, .99));
    Root->SetNumberField(TEXT("decisions_per_second"), Decisions / Elapsed);
    Root->SetNumberField(TEXT("action_latency_ms_mean"), Decisions > 0 ? CaptureCounters.LatencySumMs / Decisions : 0);
    if (Scheduler && Mode == ECrowdInferenceMode::Background)
    {
        const FNPCBackgroundStats Stats = Scheduler->GetStats();
        Root->SetNumberField(TEXT("rows_per_model_call"), RowsPerCall(Stats, CaptureSchedulerStart));
        Root->SetNumberField(TEXT("model_calls"), double(Stats.BackendCalls - CaptureSchedulerStart.BackendCalls));
        Root->SetNumberField(TEXT("model_ms_per_second"), ServiceMs(Stats, CaptureSchedulerStart) / Elapsed);
        Root->SetNumberField(TEXT("dropped_rows"), double(Dropped(Stats, CaptureSchedulerStart)));
        Root->SetNumberField(TEXT("submitted_requests"), double(Stats.SubmittedRequests - CaptureSchedulerStart.SubmittedRequests));
        Root->SetNumberField(TEXT("busy_rejections"), double(Stats.BusyRejections - CaptureSchedulerStart.BusyRejections));
        Root->SetNumberField(TEXT("invalid_submissions"), double(Stats.InvalidSubmissions - CaptureSchedulerStart.InvalidSubmissions));
    }
    else
    {
        Root->SetNumberField(TEXT("rows_per_model_call"), 1);
        Root->SetNumberField(TEXT("model_calls"), double(Decisions));
        Root->SetNumberField(TEXT("model_ms_per_second"), Mean(CaptureInferenceMs) * CaptureInferenceMs.Num() / Elapsed);
        Root->SetNumberField(TEXT("dropped_rows"), 0);
    }
    Root->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
    Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
    TArray<TSharedPtr<FJsonValue>> Frames;
    for (double Value : CaptureFrameMs) Frames.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Value * 1000) / 1000));
    Root->SetArrayField(TEXT("frame_ms"), Frames);
    FString Json;
    FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
    const bool bSaved = FFileHelper::SaveStringToFile(Json, *ReportPath);
    UE_LOG(LogTemp, Display, TEXT("Crowd comparison report %s: %s"), bSaved ? TEXT("written") : TEXT("FAILED"), *ReportPath);
    FPlatformMisc::RequestExitWithStatus(false, bSaved ? 0 : 1);
}

void ACrowdComparisonHost::Fail(const FString& Message)
{
    Error = Message;
    UE_LOG(LogTemp, Error, TEXT("Crowd comparison: %s"), *Message);
    if (!ReportPath.IsEmpty()) FPlatformMisc::RequestExitWithStatus(false, 1);
}

void ACrowdComparisonHost::EndPlay(EEndPlayReason::Type Reason)
{
    if (auto* Subsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
    {
        auto& Manager = Subsystem->GetMutableEntityManager();
        TArray<FMassEntityHandle> Valid;
        for (const FMassEntityHandle Entity : Entities)
            if (Manager.IsEntityValid(Entity)) Valid.Add(Entity);
        if (!Valid.IsEmpty()) Manager.BatchDestroyEntities(Valid);
    }
    Entities.Empty();
    if (Sync) { Sync->Instance.Reset(); Sync->Model.Reset(); }
    Super::EndPlay(Reason);
}

void ACrowdComparisonHost::KeepCrowdVisible()
{
    // A 60 cm student is under a pixel wide from a few hundred metres and
    // disappears under temporal AA. Distant students grow so each stays at
    // least MinPixels wide; nearby ones keep their real 60 x 180 cm size.
    constexpr float Width = 60, Height = 180, MinPixels = 3;
    const auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !CampusCamera) return;
    int32 ViewWidth = 0, ViewHeight = 0;
    Player->GetViewportSize(ViewWidth, ViewHeight);
    if (ViewWidth <= 0) return;
    // The campus camera is always the view target; read it after this frame's move.
    const FVector Eye = CampusCamera->GetActorLocation();
    const float HalfFov = FMath::DegreesToRadians(CampusCamera->GetCameraComponent()->FieldOfView) / 2;
    const float ScalePerCm = MinPixels * 2 * FMath::Tan(HalfFov) / ViewWidth / Width;
    ParallelFor(Transforms.Num(), [&](int32 I)
    {
        FTransform& Transform = Transforms[I];
        FVector Location = Transform.GetLocation();
        Location.Z = Height / 2;
        const float Scale = FMath::Max(1.f, float(FVector::Dist(Eye, Location)) * ScalePerCm);
        Location.Z = 4 + Height / 2 * Scale;
        Transform.SetLocation(Location);
        Transform.SetScale3D(FVector(Width, Width, Height) * (Scale / 100));
    }, Transforms.Num() < 4096 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

void ACrowdComparisonHost::UpdateCamera(float DeltaSeconds)
{
    auto* Player = GetWorld()->GetFirstPlayerController();
    if (!Player || !CampusCamera) return;
    if (Player->WasInputKeyJustPressed(EKeys::SpaceBar)) bPaused = !bPaused;
    if (Player->WasInputKeyJustPressed(EKeys::One)) CameraPreset = 0;
    if (Player->WasInputKeyJustPressed(EKeys::Two)) CameraPreset = 1;
    if (Player->WasInputKeyJustPressed(EKeys::Three)) CameraPreset = 2;
    if (Player->WasInputKeyJustPressed(EKeys::Four))
    {
        CameraPreset = 3;
        PreviousCameraPreset = -1; // Replay the same camera path for either inference mode.
    }
    if (Player->WasInputKeyJustPressed(EKeys::R))
    {
        CampusCamera->SetActorLocation(FVector(0, HalfSize * 1.55, HalfSize * 1.25));
        CameraPreset = 0;
    }
    if (Player->WasInputKeyJustPressed(EKeys::N)) bShowRoutes = !bShowRoutes;
    if (bShowRoutes)
    {
        for (int32 I=0; I<Campus->Nodes.Num(); ++I)
        {
            const auto& Node = Campus->Nodes[I];
            for (int32 J : Node.Links) if (J>I)
            {
                const auto& End = Campus->Nodes[J];
                DrawDebugLine(GetWorld(), FVector(Node.Position.X,Node.Position.Y,30), FVector(End.Position.X,End.Position.Y,30), FColor(50,230,205), false, -1, 0, 5);
            }
        }
    }
    if (PreviousCameraPreset != CameraPreset)
    {
        FVector Center = FVector::ZeroVector;
        FVector StreetLook = FVector::ZeroVector;
        float Closest = MAX_flt;
        for (const auto& Node : Campus->Nodes)
        {
            const float Distance = (Node.Position - FVector2f(-15000,18000)).SizeSquared();
            if (Distance < Closest)
            {
                Closest = Distance;
                Center = FVector(Node.Position.X, Node.Position.Y, 80);
                const auto& Neighbour = Campus->Nodes[Node.Links[0]];
                StreetLook = FVector(Neighbour.Position.X, Neighbour.Position.Y, 150);
            }
        }
        // University College is the landmark checked against the recovered CAD regions.
        if (CameraPreset == 1) Center = FVector(-1000, 22500, 1000);
        CameraPanStarted = FPlatformTime::Seconds();
        // Scripts/render-video.sh waits for this line before it starts capturing.
        if (CameraPreset == 3) UE_LOG(LogTemp, Display, TEXT("Camera flight started"));
        const FVector Locations[] = {FVector(0, HalfSize * 1.55, HalfSize * 1.25), Center + FVector(0, 12000, 9000), Center + FVector(0,0,120), Center};
        CampusCamera->SetActorLocation(Locations[CameraPreset]);
        CampusCamera->SetActorRotation(((CameraPreset == 0 ? FVector::ZeroVector : CameraPreset == 2 ? StreetLook : Center) - CampusCamera->GetActorLocation()).Rotation());
        PreviousCameraPreset = CameraPreset;
    }
    if (CameraPreset == 3)
    {
        // Wall time gives both inference modes the same flight.
        FVector Location, Target;
        const double KeyTime = FlightKeys[UE_ARRAY_COUNT(FlightKeys) - 1].Time;
        EvaluateFlight((FPlatformTime::Seconds() - CameraPanStarted) * KeyTime / FlightSeconds, Location, Target);
        CampusCamera->SetActorLocation(Location);
        CampusCamera->SetActorRotation((Target - Location).Rotation());
        return;
    }
    FVector Motion = FVector::ZeroVector;
    if (Player->IsInputKeyDown(EKeys::W)) Motion += CampusCamera->GetActorForwardVector();
    if (Player->IsInputKeyDown(EKeys::S)) Motion -= CampusCamera->GetActorForwardVector();
    if (Player->IsInputKeyDown(EKeys::D)) Motion += CampusCamera->GetActorRightVector();
    if (Player->IsInputKeyDown(EKeys::A)) Motion -= CampusCamera->GetActorRightVector();
    if (Player->IsInputKeyDown(EKeys::E)) Motion.Z += 1;
    if (Player->IsInputKeyDown(EKeys::Q)) Motion.Z -= 1;
    CampusCamera->AddActorWorldOffset(Motion * DeltaSeconds * (Player->IsInputKeyDown(EKeys::LeftShift) ? 18000 : 4000));
    float MouseX=0, MouseY=0;
    Player->GetInputMouseDelta(MouseX, MouseY);
    if (Player->IsInputKeyDown(EKeys::RightMouseButton))
    {
        FRotator Rotation = CampusCamera->GetActorRotation();
        Rotation.Yaw += MouseX * .15f;
        Rotation.Pitch = FMath::Clamp(Rotation.Pitch + MouseY * .15f, -85.f, 15.f);
        CampusCamera->SetActorRotation(Rotation);
    }
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "CampusCrowdHost.h"
#include "CampusCameraRig.h"
#include "CampusScene.h"
#include "CrowdProcessors.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInterface.h"
#include "MassEntitySubsystem.h"
#include "MassEntityView.h"
#include "MassExecutor.h"
#include "MassProcessingContext.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "NNEModelData.h"
#include "ScholaMassInferenceSubsystem.h"
#include "UObject/ConstructorHelpers.h"
#include "UnrealClient.h"

namespace
{
	FMassEntityManager& EntityManager(UWorld* World)
	{
		return World->GetSubsystem<UMassEntitySubsystem>()->GetMutableEntityManager();
	}

	/** Returns ms since Start and resets Start to now. */
	double Lap(double& Start)
	{
		const double Now = FPlatformTime::Seconds();
		const double Ms = (Now - Start) * 1000;
		Start = Now;
		return Ms;
	}
} // namespace

ACampusCrowdHost::ACampusCrowdHost()
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

void ACampusCrowdHost::BeginPlay()
{
	Super::BeginPlay();
	const TCHAR*			Command = FCommandLine::Get();
	int32					AgentCount = 2000;
	int32					View = 0;
	float					MaxFPS = 60;
	double					FlightSeconds = 10;
	double					CaptureSeconds = 0;
	FString					ReportPath;
	FScholaMassPolicyConfig Config;
	FParse::Value(Command, TEXT("Agents="), AgentCount);
	FParse::Value(Command, TEXT("DecisionHz="), DecisionHz);
	FParse::Value(Command, TEXT("MaxFPS="), MaxFPS);
	FParse::Value(Command, TEXT("Workers="), Config.WorkerCount);
	FParse::Value(Command, TEXT("MaxBatch="), Config.MaxBatchSize);
	FParse::Value(Command, TEXT("MaxPendingRows="), Config.MaxPendingRows);
	FParse::Value(Command, TEXT("MaxRowsPerFrame="), Config.MaxRowsPerFrame);
	FParse::Value(Command, TEXT("CaptureSeconds="), CaptureSeconds);
	FParse::Value(Command, TEXT("WarmupSeconds="), WarmupSeconds);
	FParse::Value(Command, TEXT("Report="), ReportPath);
	FParse::Value(Command, TEXT("View="), View);
	FParse::Value(Command, TEXT("FlightSeconds="), FlightSeconds);
	AgentCount = FMath::Clamp(AgentCount, 1, 100000);
	DecisionHz = FMath::Clamp(DecisionHz, .1f, 60.f);
	Config.MaxPendingRows = FMath::Max(0, Config.MaxPendingRows);
	Config.MaxRowsPerFrame = FMath::Max(0, Config.MaxRowsPerFrame);
	if (MaxFPS > 0)
	{
		IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))->Set(MaxFPS);
	}
	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		VSync->Set(0);
	}

	if (!BuildScene() || !LoadModel())
	{
		return;
	}
	CameraRig = NewObject<UCampusCameraRig>(this);
	CameraRig->Initialize(*Campus, View, FlightSeconds);

	// One policy shared by the whole crowd; queue limits are sized from the crowd.
	Config.Model = ModelData;
	Config.DecisionHz = DecisionHz;
	Config.ExpectedAgents = AgentCount;
	Config.ExpectedObservationSize = CrowdSim::ObservationSize;
	Config.ExpectedActionSize = CrowdSim::ActionSize;
	FString PolicyError;
	if (UScholaMassInferenceSubsystem* Inference = GetWorld()->GetSubsystem<UScholaMassInferenceSubsystem>())
	{
		Policy = Inference->CreatePolicy(Config, PolicyError);
	}
	if (!Policy.IsValid())
	{
		Fail(TEXT("Background policy: ") + PolicyError);
		return;
	}
	SpawnCrowd(AgentCount);

	Started = PreviousTick = FPlatformTime::Seconds();
	Stats.Start(Policy, ReportPath, WarmupSeconds, CaptureSeconds);
	const FNPCBackgroundSchedulerSettings& Settings = Policy.GetSettings();
	UE_LOG(LogCampusCrowd, Display, TEXT("Crowd: %d agents at %.1f Hz (target %.0f decisions/s); %d workers, batch %d, %d rows/frame, %d pending rows"),
		AgentCount, DecisionHz, GetTargetDecisionsPerSecond(), Settings.WorkerCount, Settings.MaxBatchSize,
		Policy.GetMaxRowsPerFrame(), Settings.MaxPendingRows);
}

bool ACampusCrowdHost::BuildScene()
{
	UWorld* World = GetWorld();
	if (TActorIterator<ACampusScene> It(World); It)
	{
		Campus = *It;
	}
	if (!Campus)
	{
		Campus = World->SpawnActor<ACampusScene>();
	}
	if (Campus->Nodes.IsEmpty())
	{
		Campus->LoadCampus();
	}
	if (Campus->Nodes.Num() < 2)
	{
		Fail(TEXT("Campus route data could not be loaded"));
		return false;
	}

	TActorIterator<ADirectionalLight> SunIt(World);
	ADirectionalLight*				  Sun = SunIt ? *SunIt : World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-50, -35, 0));
	UDirectionalLightComponent*		  SunLight = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetIntensity(6);
	SunLight->SetAtmosphereSunLight(true);
	if (!TActorIterator<ASkyAtmosphere>(World))
	{
		World->SpawnActor<ASkyAtmosphere>();
	}
	TActorIterator<ASkyLight> AmbientIt(World);
	ASkyLight*				  Ambient = AmbientIt ? *AmbientIt : World->SpawnActor<ASkyLight>();
	USkyLightComponent*		  AmbientLight = Ambient->GetLightComponent();
	AmbientLight->SetMobility(EComponentMobility::Movable);
	AmbientLight->bRealTimeCapture = true;
	AmbientLight->SetIntensity(1.5f);
	AmbientLight->RecaptureSky();
	return true;
}

bool ACampusCrowdHost::LoadModel()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Models/crowd_policy.onnx");
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *Path))
	{
		Fail(TEXT("Missing policy file ") + Path);
		return false;
	}
	ModelData = NewObject<UNNEModelData>(this);
	ModelData->Init(TEXT("onnx"), Bytes);
	return true;
}

void ACampusCrowdHost::SpawnCrowd(int32 AgentCount)
{
	FMassEntityManager& Manager = EntityManager(GetWorld());
	Transforms.SetNum(AgentCount);
	const UScriptStruct* Fragments[] = { FCrowdAgentFragment::StaticStruct() };
	const auto			 Place = [this](const FMassEntityView& Entity, int32 I) {
		// Each student starts at a random point on a random route segment, in its own lane.
		FCrowdAgentFragment& Agent = Entity.GetFragmentData<FCrowdAgentFragment>();
		Agent.Instance = I;
		Agent.Random = 0x9E3779B9u ^ uint32(I * 7919 + 1);
		Agent.CurrentNode = CrowdSim::NextRandom(Agent.Random) % Campus->Nodes.Num();
		const FCampusNode& Node = Campus->Nodes[Agent.CurrentNode];
		Agent.NextNode = Node.Links[CrowdSim::NextRandom(Agent.Random) % Node.Links.Num()];
		Agent.Goal = Campus->Nodes[Agent.NextNode].Position;
		Agent.Lane = (CrowdSim::NextRandom(Agent.Random) / float(MAX_uint32) - .5f) * 220.f;
		Agent.WalkSpeed = 115.f + CrowdSim::NextRandom(Agent.Random) % 70;
		Agent.Position = FMath::Lerp(Node.Position, Agent.Goal, CrowdSim::NextRandom(Agent.Random) / float(MAX_uint32));
		const FVector2f Along = (Agent.Goal - Node.Position).GetSafeNormal();
		Agent.Position += FVector2f(-Along.Y, Along.X) * Agent.Lane;
		Agent.Steering = (Agent.Goal - Agent.Position).GetSafeNormal();
		Transforms[I] = FTransform(FRotator::ZeroRotator, FVector(Agent.Position.X, Agent.Position.Y, 94), FVector(.6f, .6f, 1.8f));
	};
	Policy.BatchCreateEntities(Manager, Fragments, AgentCount, Entities, Place);
	Crowd->SetMaterial(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Campus/M_Students.M_Students")));
	Crowd->AddInstances(Transforms, false, true, false);

	Gather = NewObject<UCrowdGatherProcessor>(this);
	Apply = NewObject<UCrowdApplyProcessor>(this);
	Move = NewObject<UCrowdMoveProcessor>(this);
	Gather->HalfSize = Move->HalfSize = Campus->Extent;
	Move->Transforms = &Transforms;
	Move->Campus = Campus;
	for (UMassProcessor* Processor : TArray<UMassProcessor*>{ Gather, Apply, Move })
	{
		Processor->CallInitialize(this, Manager.AsShared());
	}
}

void ACampusCrowdHost::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Entities.IsEmpty() || !Error.IsEmpty())
	{
		return;
	}
	const double Now = FPlatformTime::Seconds();
	const double FrameMs = (Now - PreviousTick) * 1000;
	PreviousTick = Now;

	if (APlayerController* Player = GetWorld()->GetFirstPlayerController(); Player && Player->WasInputKeyJustPressed(EKeys::SpaceBar))
	{
		bPaused = !bPaused;
	}
	CameraRig->Update(DeltaSeconds);

	// Apply results that finished since last frame, then submit newly due rows.
	FMassEntityManager&			 Manager = EntityManager(GetWorld());
	UE::Mass::FProcessingContext Context(Manager, DeltaSeconds);
	double						 Clock = FPlatformTime::Seconds();
	UE::Mass::Executor::Run(*Apply, Context);
	UE::Mass::Executor::Run(*Gather, Context);
	const double InferenceMs = Lap(Clock);
	if (!bPaused)
	{
		UE::Mass::FProcessingContext MoveContext(Manager, DeltaSeconds);
		UE::Mass::Executor::Run(*Move, MoveContext);
	}
	const double MoveMs = Lap(Clock);

	CameraRig->KeepVisible(Transforms);
	Crowd->BatchUpdateInstancesTransforms(0, Transforms, true, true, true);
	Stats.AddFrame(Now, FrameMs, InferenceMs, MoveMs);

	if (!bScreenshotTaken && Now - Started >= FMath::Min(WarmupSeconds * .5, 5.0))
	{
		FString Screenshot;
		if (FParse::Value(FCommandLine::Get(), TEXT("Screenshot="), Screenshot))
		{
			FScreenshotRequest::RequestScreenshot(Screenshot, !FParse::Param(FCommandLine::Get(), TEXT("CleanScreenshot")), false);
		}
		bScreenshotTaken = true;
	}
	if (Stats.IsCaptureComplete())
	{
		WriteReport();
	}
}

void ACampusCrowdHost::WriteReport()
{
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("agents"), Entities.Num());
	Root->SetStringField(TEXT("scene"), TEXT("50H_NORTH campus"));
	Root->SetNumberField(TEXT("cad_buildings"), Campus->BuildingCount);
	Root->SetNumberField(TEXT("route_nodes"), Campus->Nodes.Num());
	Root->SetNumberField(TEXT("decision_hz_per_agent"), DecisionHz);
	FPlatformMisc::RequestExitWithStatus(false, Stats.WriteReport(Root));
}

void ACampusCrowdHost::Fail(const FString& Message)
{
	Error = Message;
	UE_LOG(LogCampusCrowd, Error, TEXT("Crowd: %s"), *Message);
	FString ReportPath;
	if (FParse::Value(FCommandLine::Get(), TEXT("Report="), ReportPath))
	{
		FPlatformMisc::RequestExitWithStatus(false, 1);
	}
}

void ACampusCrowdHost::EndPlay(EEndPlayReason::Type Reason)
{
	if (UMassEntitySubsystem* Subsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>())
	{
		FMassEntityManager& Manager = Subsystem->GetMutableEntityManager();
		Entities.RemoveAll([&Manager](FMassEntityHandle Entity) { return !Manager.IsEntityValid(Entity); });
		if (!Entities.IsEmpty())
		{
			Manager.BatchDestroyEntities(Entities);
		}
	}
	Entities.Empty();
	Super::EndPlay(Reason);
}

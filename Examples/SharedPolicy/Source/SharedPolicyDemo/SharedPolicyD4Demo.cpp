// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "SharedPolicyD4Demo.h"

#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NNEModelData.h"
#include "Points/BoxPoint.h"
#include "Policies/NNEPerHeadSharedPolicy.h"
#include "SharedPolicyD4Models.h"
#include "Spaces/BoxSpace.h"
#include "Steppers/SharedPolicyStepper.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSharedPolicyD4, Log, All);

namespace SharedPolicyD4
{
    constexpr int32 AgentsPerRole = 3;
    constexpr float DecisionPeriod = .2f;
    constexpr float BeaconSpeed = 280.f;
    constexpr float MovementPerDecision = 32.f;
    constexpr float ObservationScale = 100.f;
    constexpr float BeaconRadius = 550.f;
    constexpr float ArenaRadius = 950.f;
    constexpr int32 SmokeSteps = 36;
    const FName TargetName(TEXT("D4_Target"));
    const FName TowardHead(TEXT("Toward"));
    const FName AwayHead(TEXT("Away"));

    UNNEModelData* MakeModel(UObject* Owner, TConstArrayView<uint8> Bytes)
    {
        auto* Model = NewObject<UNNEModelData>(Owner);
        Model->Init(TEXT("onnx"), Bytes);
        return Model;
    }
}

ASharedPolicyD4Agent::ASharedPolicyD4Agent()
{
    Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
    SetRootComponent(Body);
    Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Body->SetMobility(EComponentMobility::Movable);
    Body->SetRelativeScale3D(FVector(.48));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    Body->SetStaticMesh(Cube.Object);
}

void ASharedPolicyD4Agent::Configure(AActor& InTarget, bool bInToward, float Lane)
{
    Target = &InTarget;
    ArenaCenter = InTarget.GetActorLocation();
    if (bInToward)
    {
        // Give each follower its own spot behind the beacon.
        TargetOffset = FVector(-120.f, Lane, 0.f);
    }
    if (auto* Material = Body->CreateDynamicMaterialInstance(0))
    {
        Material->SetVectorParameterValue(TEXT("Color"),
            bInToward ? FLinearColor(.04f, .25f, 1.f) : FLinearColor(1.f, .23f, .015f));
    }
}

FVector ASharedPolicyD4Agent::GetSteeringPoint() const
{
    return Target->GetActorLocation() + TargetOffset;
}

void ASharedPolicyD4Agent::Define_Implementation(FInteractionDefinition& OutDefinition)
{
    const TArray<float> Low{-1.f, -1.f};
    const TArray<float> High{1.f, 1.f};
    OutDefinition = FInteractionDefinition(
        TInstancedStruct<FSpace>::Make<FBoxSpace>(Low, High),
        TInstancedStruct<FSpace>::Make<FBoxSpace>(Low, High));
}

void ASharedPolicyD4Agent::Observe_Implementation(FInstancedStruct& OutObservations)
{
    using namespace SharedPolicyD4;
    const FVector Delta = GetSteeringPoint() - GetActorLocation();
    const FVector RelativeDirection = Delta.GetClampedToMaxSize2D(ObservationScale) / ObservationScale;
    const TArray<float> Values{float(RelativeDirection.X), float(RelativeDirection.Y)};
    OutObservations.InitializeAs<FBoxPoint>(Values);
}

void ASharedPolicyD4Agent::Act_Implementation(const FInstancedStruct& InAction)
{
    const auto* Action = InAction.GetPtr<FBoxPoint>();
    if (!Action || Action->Values.Num() != 2)
    {
        UE_LOG(LogSharedPolicyD4, Error, TEXT("Expected a two-value steering action."));
        return;
    }
    const FVector ProposedOffset = GetActorLocation() - ArenaCenter
        + FVector(Action->Values[0], Action->Values[1], 0) * SharedPolicyD4::MovementPerDecision;
    SetActorLocation(ArenaCenter + ProposedOffset.GetClampedToMaxSize2D(SharedPolicyD4::ArenaRadius));
    bAppliedAction = true;
}

ASharedPolicyD4Demo::ASharedPolicyD4Demo()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;
}

bool ASharedPolicyD4Demo::Setup(AActor& InTarget)
{
    using namespace SharedPolicyD4;
    Target = &InTarget;
    TargetStart = Target->GetActorLocation();
    auto* TargetRoot = Target->GetRootComponent();
    if (!TargetRoot)
    {
        return false;
    }
    TargetRoot->SetMobility(EComponentMobility::Movable);

    Policy = NewObject<UNNEPerHeadSharedPolicy>(this);
    Policy->HeadModels.Add(TowardHead, MakeModel(Policy, MakeArrayView(SharedPolicyD4Models::Toward)));
    Policy->HeadModels.Add(AwayHead, MakeModel(Policy, MakeArrayView(SharedPolicyD4Models::Away)));
    if (!Policy->Init())
    {
        return false;
    }

    UWorld* World = GetWorld();
    Camera = World->SpawnActor<ACameraActor>(TargetStart + FVector(-1100, 0, 1700), FRotator(-57, 0, 0));
    if (!Camera)
    {
        return false;
    }

    TArray<FSharedNPCBinding> Bindings;
    for (int32 Index = 0; Index < 2 * AgentsPerRole; ++Index)
    {
        const bool bToward = Index < AgentsPerRole;
        const float Lane = (Index % AgentsPerRole - 1) * 145.f;
        const FVector Offset(bToward ? -250.f : 210.f, Lane, 0.f);
        auto* Agent = World->SpawnActor<ASharedPolicyD4Agent>(TargetStart + Offset, FRotator::ZeroRotator);
        if (!Agent)
        {
            return false;
        }
        Agent->Configure(*Target, bToward, Lane);
        Agents.Add(Agent);
        Bindings.Emplace(TScriptInterface<IAgent>(Agent), bToward ? TowardHead : AwayHead);
    }

    Stepper = NewObject<USharedPolicyStepper>(this);
    if (!Stepper->Init(Bindings, TScriptInterface<ISharedPolicy>(Policy)))
    {
        return false;
    }

    bSmokeTest = FParse::Param(FCommandLine::Get(), TEXT("SharedPolicyD4Smoke"));
    UE_LOG(LogSharedPolicyD4, Display, TEXT("D4 ready. Use WASD to move the beacon. Blue follows; orange moves away."));
    SetActorTickEnabled(true);
    return true;
}

void ASharedPolicyD4Demo::MoveTarget(float DeltaSeconds)
{
    using namespace SharedPolicyD4;
    APlayerController* Player = GetWorld()->GetFirstPlayerController();
    if (!Player)
    {
        return;
    }
    if (Player->GetViewTarget() != Camera)
    {
        Player->SetViewTarget(Camera);
    }
    if (auto* DefaultPawn = Cast<ADefaultPawn>(Player->GetPawn()); DefaultPawn && !DefaultPawn->IsHidden())
    {
        // The beacon uses raw key state. The default fly-around pawn has no role in the demo.
        DefaultPawn->SetActorHiddenInGame(true);
        DefaultPawn->DisableInput(Player);
    }

    const FVector Direction(
        int32(Player->IsInputKeyDown(EKeys::W)) - int32(Player->IsInputKeyDown(EKeys::S)),
        int32(Player->IsInputKeyDown(EKeys::D)) - int32(Player->IsInputKeyDown(EKeys::A)),
        0);
    if (!Direction.IsZero())
    {
        const FVector Offset = Target->GetActorLocation() - TargetStart
            + Direction.GetSafeNormal() * BeaconSpeed * DeltaSeconds;
        Target->SetActorLocation(TargetStart + Offset.GetClampedToMaxSize2D(BeaconRadius));
    }
}

bool ASharedPolicyD4Demo::RunDecision()
{
    using namespace SharedPolicyD4;
    TArray<double, TInlineAllocator<2 * AgentsPerRole>> Before;
    for (const auto& Agent : Agents)
    {
        Agent->ClearAppliedAction();
        Before.Add(FVector::Dist2D(Agent->GetSteeringPoint(), Agent->GetActorLocation()));
    }

    const int64 PreviousCalls = Policy->GetModelCallCount();
    Stepper->Step();
    const int64 Calls = Policy->GetModelCallCount() - PreviousCalls;
    int32 Applied = 0;
    int32 Toward = 0;
    int32 Away = 0;
    for (int32 Index = 0; Index < Agents.Num(); ++Index)
    {
        const auto* Agent = Agents[Index].Get();
        Applied += Agent->AppliedAction();
        const double DistanceChange =
            FVector::Dist2D(Agent->GetSteeringPoint(), Agent->GetActorLocation()) - Before[Index];
        Toward += Index < AgentsPerRole && DistanceChange < -KINDA_SMALL_NUMBER;
        Away += Index >= AgentsPerRole && DistanceChange > KINDA_SMALL_NUMBER;
    }

    ++DecisionNumber;
    if (DecisionNumber <= 2 || DecisionNumber % 5 == 0)
    {
        UE_LOG(LogSharedPolicyD4, Display,
            TEXT("Step %d | %d actions applied | %lld model calls | %d closer, %d farther"),
            DecisionNumber, Applied, static_cast<long long>(Calls), Toward, Away);
    }
    if (Applied != Agents.Num() || Calls != 2)
    {
        UE_LOG(LogSharedPolicyD4, Error, TEXT("The D4 inference step did not apply all actions in two model calls."));
        return false;
    }
    return !bSmokeTest || DecisionNumber > 2 || (Toward == AgentsPerRole && Away == AgentsPerRole);
}

void ASharedPolicyD4Demo::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    MoveTarget(DeltaSeconds);
    DecisionElapsed += DeltaSeconds;
    if (DecisionElapsed < SharedPolicyD4::DecisionPeriod)
    {
        return;
    }
    DecisionElapsed = 0.f;
    if (bSmokeTest && DecisionNumber == 1)
    {
        // Put the beacon beyond the agents to exercise a new steering direction.
        const FVector NewPosition = TargetStart + FVector(450, 150, 0);
        Target->SetActorLocation(NewPosition);
        if (!Target->GetActorLocation().Equals(NewPosition))
        {
            UE_LOG(LogSharedPolicyD4, Error, TEXT("D4 smoke: could not relocate the beacon."));
            SetActorTickEnabled(false);
            FPlatformMisc::RequestExitWithStatus(false, 1);
            return;
        }
    }
    bool bSuccess = RunDecision();
    const bool bSmokeComplete = bSmokeTest && DecisionNumber == SharedPolicyD4::SmokeSteps;
    if (bSmokeComplete)
    {
        for (int32 Index = 0; Index < SharedPolicyD4::AgentsPerRole; ++Index)
        {
            bSuccess &= FVector::Dist2D(Agents[Index]->GetActorLocation(), Agents[Index]->GetSteeringPoint()) < 5.0;
            for (int32 Other = 0; Other < Index; ++Other)
            {
                bSuccess &= FVector::Dist2D(Agents[Index]->GetActorLocation(), Agents[Other]->GetActorLocation()) > 100.0;
            }
        }
        if (const auto* Player = GetWorld()->GetFirstPlayerController())
        {
            if (const auto* DefaultPawn = Cast<ADefaultPawn>(Player->GetPawn()))
            {
                bSuccess &= DefaultPawn->IsHidden();
            }
        }
        UE_LOG(LogSharedPolicyD4, Display, TEXT("D4 follower spacing and pawn visibility: %s"),
            bSuccess ? TEXT("PASS") : TEXT("FAIL"));
    }
    if (!bSuccess || bSmokeComplete)
    {
        SetActorTickEnabled(false);
        if (bSmokeTest)
        {
            UE_LOG(LogSharedPolicyD4, Display, TEXT("D4 smoke: %s"), bSuccess ? TEXT("PASS") : TEXT("FAIL"));
            FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1);
        }
    }
}

void USharedPolicyD4WorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    FString MapName = InWorld.GetMapName();
    MapName.RemoveFromStart(InWorld.StreamingLevelsPrefix);
    if (MapName != TEXT("LV_SharedPolicyD4"))
    {
        return;
    }
    for (TActorIterator<AActor> It(&InWorld); It; ++It)
    {
        if (!It->ActorHasTag(SharedPolicyD4::TargetName)
#if WITH_EDITOR
            && It->GetActorLabel() != SharedPolicyD4::TargetName.ToString()
#endif
        )
        {
            continue;
        }
        auto* Demo = InWorld.SpawnActor<ASharedPolicyD4Demo>();
        if (Demo && Demo->Setup(**It))
        {
            return;
        }
        UE_LOG(LogSharedPolicyD4, Error, TEXT("Could not initialize the D4 scene or its NNE models."));
        if (FParse::Param(FCommandLine::Get(), TEXT("SharedPolicyD4Smoke")))
        {
            FPlatformMisc::RequestExitWithStatus(false, 1);
        }
        return;
    }
    UE_LOG(LogSharedPolicyD4, Error, TEXT("Add the D4_Target label or Actor Tag to the beacon cube."));
    if (FParse::Param(FCommandLine::Get(), TEXT("SharedPolicyD4Smoke")))
    {
        FPlatformMisc::RequestExitWithStatus(false, 1);
    }
}

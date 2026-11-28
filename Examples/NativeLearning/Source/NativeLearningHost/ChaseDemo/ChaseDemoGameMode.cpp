// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseDemoGameMode.h"
#include "ChaseDemoActor.h"
#include "ChaseDemoHUD.h"
#include "Spaces/BoxSpace.h"
#include "Points/BoxPoint.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameViewportClient.h"
#include "InputCoreTypes.h"

DEFINE_LOG_CATEGORY_STATIC(LogChaseDemo, Log, All);

namespace
{
// Generous, deliberately loose bounds: correctness of admission, not tightness, matters for this
// first increment. True positions are normalized into [0, 1]; velocities/relative terms into
// [-1, 1]. [-2, 2] safely contains every term with margin. Tightening these is future polish,
// not a migration-blocking decision (see Q10/Q12 in the chat history this file's ADR summarizes).
constexpr float ObservationBound = 2.0f;
constexpr int32 ObservationCount = 10;
constexpr int32 ActionCount = 2;
}

AChaseDemoGameMode::AChaseDemoGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    HUDClass = AChaseDemoHUD::StaticClass();
    // Autonomous-only for now (Q12): no player pawn. The WASD seam is ApplyChaserAction, not this.
    DefaultPawnClass = nullptr;
}

void AChaseDemoGameMode::BeginPlay()
{
    Super::BeginPlay();

    BuildArena();

    FActorSpawnParameters Spawn;
    Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ChaserActor = GetWorld()->SpawnActor<AChaseDemoActor>(Spawn);
    // The engine Sphere is 100 units across, so this scale makes each circle ActorDiameter wide.
    const FVector ActorScale(ActorDiameter / 100.0f);
    ChaserActor->SetRunnerAppearance(false); // coral
    ChaserActor->SetActorScale3D(ActorScale);
    RunnerActor = GetWorld()->SpawnActor<AChaseDemoActor>(Spawn);
    RunnerActor->SetRunnerAppearance(true); // mint
    RunnerActor->SetActorScale3D(ActorScale);

    // No player pawn exists yet (Q12), so without this PIE has nothing to render a view from.
    const FVector BoardCenter(BoardSize * 0.5f, BoardSize * 0.5f, 0.0f);
    const FVector CameraLocation = BoardCenter + FVector(0.0f, 0.0f, BoardSize * 1.5f);
    const FRotator CameraRotation(-90.0f, 0.0f, 0.0f); // straight down
    OverviewCamera = GetWorld()->SpawnActor<ACameraActor>(CameraLocation, CameraRotation);
    if (UCameraComponent* CameraComponent = OverviewCamera->GetCameraComponent())
    {
        CameraComponent->SetProjectionMode(ECameraProjectionMode::Orthographic);
        // Zoom is set by FitCameraToViewport (below, and every Tick) from the real viewport shape.
        CameraComponent->PostProcessSettings.bOverride_MotionBlurAmount = true;
        CameraComponent->PostProcessSettings.MotionBlurAmount = 0.0f;
    }
    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        PC->SetViewTarget(OverviewCamera);
    }
    FitCameraToViewport();

    // The Entry map has no lights of its own; Movable mobility means this illuminates immediately
    // with no lighting build required, unlike the engine's default Static mobility.
    SunLight = GetWorld()->SpawnActor<ADirectionalLight>(BoardCenter, FRotator(-50.0f, -30.0f, 0.0f));
    if (UDirectionalLightComponent* LightComponent = Cast<UDirectionalLightComponent>(SunLight->GetLightComponent()))
    {
        LightComponent->SetMobility(EComponentMobility::Movable);
        LightComponent->SetIntensity(5.0f);
        LightComponent->SetCastShadows(false);
    }

    Policy = NewObject<URLToolsNPCPolicy>(this);
    Policy->Algorithm = ERLToolsAlgorithm::SAC;
    Policy->Configuration = ConfigurationId;
    Policy->Settings.Seed = 1;
    // Small on purpose. At high F speeds the game makes moves faster than the learner trains;
    // surplus moves are then dropped (logged, harmless) instead of queueing up. A short queue
    // keeps the learner training on recent moves -- with 4096 it lagged seconds behind the game,
    // which matters right after a runner switch.
    Policy->Settings.QueueCapacity = 1024;
    Policy->Settings.LearningStarts = 200;
    Policy->Settings.TrainFrequency = 1;
    Policy->Settings.GradientSteps = FMath::Max(1, GradientStepsPerMove);
    // Snapshot-lag was the exact bug the D3 tuning worked around (see ADR 0002, step 5 /
    // GLOSSARY.md "Frozen"); publishing every update reproduces that lesson rather than
    // reintroducing update_every=50-style lag.
    Policy->Settings.PublishEveryUpdates = 1;

    FInteractionDefinition Definition;
    Definition.AgentType = TEXT("ChaseDemoChaser");
    Definition.ObsSpaceDefn.InitializeAs<FBoxSpace>();
    Definition.ObsSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(
        FBoxSpaceDimension(-ObservationBound, ObservationBound), ObservationCount);
    Definition.ObsSpaceDefn.GetMutable<FBoxSpace>().Shape = {1, ObservationCount};
    Definition.ActionSpaceDefn.InitializeAs<FBoxSpace>();
    Definition.ActionSpaceDefn.GetMutable<FBoxSpace>().Dimensions.Init(
        FBoxSpaceDimension(-1.0f, 1.0f), ActionCount);
    Definition.ActionSpaceDefn.GetMutable<FBoxSpace>().Shape = {1, ActionCount};

    if (!Policy->Init(Definition))
    {
        UE_LOG(LogChaseDemo, Error, TEXT("Policy Init rejected: %s"), *Policy->GetLastError());
    }

    RunnerRng.Initialize(1);
    WarmupRng.Initialize(7);
    ResetEpisode(/*bCountAsEpisode=*/false);
    // Without this, both actors sit at the world origin (outside the board, since the board
    // spans [0, BoardSize] with its center elsewhere) until the first decision resolves.
    ChaserActor->SetBoardPosition(State.ChaserPosition);
    RunnerActor->SetBoardPosition(State.RunnerPosition);
}

void AChaseDemoGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (Policy)
    {
        Policy->Shutdown();
    }
    Super::EndPlay(EndPlayReason);
}

TArray<float> AChaseDemoGameMode::BuildObservation() const
{
    // Layout matches the chase task's own documented convention (ChaseConfig.h comment):
    // chaser xy, runner xy, chaser vxy, runner vxy, then relative (runner - chaser) xy.
    TArray<float> Values;
    Values.Reserve(ObservationCount);
    Values.Add(State.ChaserPosition.X / BoardSize);
    Values.Add(State.ChaserPosition.Y / BoardSize);
    Values.Add(State.RunnerPosition.X / BoardSize);
    Values.Add(State.RunnerPosition.Y / BoardSize);
    Values.Add(LastChaserActionSpace.X);
    Values.Add(LastChaserActionSpace.Y);
    Values.Add(LastRunnerVelocityUnit.X);
    Values.Add(LastRunnerVelocityUnit.Y);
    Values.Add((State.RunnerPosition.X - State.ChaserPosition.X) / BoardSize);
    Values.Add((State.RunnerPosition.Y - State.ChaserPosition.Y) / BoardSize);
    return Values;
}

FVector2D AChaseDemoGameMode::ApplyChaserAction(const FVector2D& RequestedVelocity) const
{
    // The seam for later player input (Q15): a WASD override replaces RequestedVelocity's source
    // (the policy's decision) with player input, then calls this exact function unchanged.
    FVector2D Clamped = RequestedVelocity;
    const float Magnitude = Clamped.Size();
    if (Magnitude > 1.0f)
    {
        Clamped /= Magnitude;
    }
    return Clamped;
}

void AChaseDemoGameMode::ResetEpisode(bool bCountAsEpisode)
{
    if (bCountAsEpisode)
    {
        State.EpisodesCompleted++;
    }
    State.ChaserPosition = FVector2D(BoardSize * 0.1f, BoardSize * 0.1f);

    FChaseDemoRunnerPatternConfig PatternConfig;
    PatternConfig.BoardSize = BoardSize;
    PatternConfig.RunnerSpeed = RunnerSpeed;
    RunnerPatternState = StartChaseDemoRunner(RunnerPattern, PatternConfig, RunnerRng);
    State.RunnerPosition = RunnerPatternState.Position; // StartChaseDemoRunner always lands inside [0, BoardSize]^2.

    State.StepsThisEpisode = 0;
    LastChaserActionSpace = FVector2D::ZeroVector;
    LastRunnerVelocityUnit = RunnerPatternState.Velocity.GetSafeNormal();
    bHasPendingDecision = false;
    if (Policy)
    {
        Policy->ResetEpisode();
    }
}

void AChaseDemoGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // Cheap, and keeps the arena framed if the Play window is resized or undocked.
    FitCameraToViewport();

    if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
    {
        if (PC->WasInputKeyJustPressed(EKeys::F))
        {
            StepsPerTick = StepsPerTick == 1 ? 4 : StepsPerTick == 4 ? 12 : 1;
        }
        if (PC->WasInputKeyJustPressed(EKeys::SpaceBar))
        {
            bPaused = !bPaused;
        }
        if (PC->WasInputKeyJustPressed(EKeys::Tab))
        {
            bPlayerControlsRunner = !bPlayerControlsRunner;
            RecentOutcomes.Reset();
        }
        if (PC->WasInputKeyJustPressed(EKeys::L) && Policy)
        {
            // Argument is the *new* enabled state: currently frozen -> enable, and vice versa.
            const FAdaptiveOperationResult Result = Policy->SetLearningEnabled(bLearningFrozen);
            if (Result.Status != EAdaptiveOperationStatus::Pending && Result.Status != EAdaptiveOperationStatus::Succeeded)
            {
                UE_LOG(LogChaseDemo, Warning, TEXT("SetLearningEnabled was not accepted: %s"), *Result.Diagnostic);
            }
            else
            {
                bLearningFrozen = !bLearningFrozen;
                RecentOutcomes.Reset();
            }
        }

        // Direct pattern pick, matching TagArena's own number-key mode switching. A changed
        // pattern restarts the episode (not counted) so the runner's start state matches the
        // new pattern's own spawn rule instead of visibly teleporting mid-behavior.
        static const TPair<FKey, EChaseDemoRunnerPattern> PatternKeys[] = {
            {EKeys::One, EChaseDemoRunnerPattern::Straight},
            {EKeys::Two, EChaseDemoRunnerPattern::Square},
            {EKeys::Three, EChaseDemoRunnerPattern::Circle},
            {EKeys::Four, EChaseDemoRunnerPattern::RightTurn},
            {EKeys::Five, EChaseDemoRunnerPattern::Clockwise},
            {EKeys::Six, EChaseDemoRunnerPattern::Corner},
        };
        for (const auto& Pair : PatternKeys)
        {
            // Guarded against the catch-flash window: switching patterns there would force an
            // immediate ResetEpisode(false) while the flash's own deferred ResetEpisode(true)
            // (see below) is still pending, double-resetting the episode count.
            if (CatchFlashTimeRemaining <= 0.0f && PC->WasInputKeyJustPressed(Pair.Key) && RunnerPattern != Pair.Value)
            {
                RunnerPattern = Pair.Value;
                RecentOutcomes.Reset();
                ResetEpisode(/*bCountAsEpisode=*/false);
                if (ChaserActor) { ChaserActor->SetBoardPosition(State.ChaserPosition); }
                if (RunnerActor) { RunnerActor->SetBoardPosition(State.RunnerPosition); }
                break;
            }
        }
    }

    // A catch freezes stepping for CatchFlashDuration seconds (real time, not steps) so the flash
    // and "CAUGHT!" banner are actually visible before the board resets underneath them.
    if (CatchFlashTimeRemaining > 0.0f)
    {
        CatchFlashTimeRemaining -= DeltaSeconds;
        if (CatchFlashTimeRemaining <= 0.0f)
        {
            CatchFlashTimeRemaining = 0.0f;
            if (ChaserActor) { ChaserActor->SetFlashWhite(false); }
            if (RunnerActor) { RunnerActor->SetFlashWhite(false); }
            ResetEpisode(/*bCountAsEpisode=*/true);
            if (ChaserActor) { ChaserActor->SetBoardPosition(State.ChaserPosition); }
            if (RunnerActor) { RunnerActor->SetBoardPosition(State.RunnerPosition); }
        }
        return;
    }

    if (bPaused)
    {
        return;
    }

    if (!Policy || Policy->GetSessionState() != EAdaptiveSessionState::Ready)
    {
        return;
    }

    // Matches the terminal chase demo's model: movement is a fixed distance per step, not a
    // continuous real-time velocity. F/StepsPerTick controls tempo by running more steps per
    // real engine tick, not by scaling a DeltaSeconds-based distance.
    for (int32 Step = 0; Step < StepsPerTick; ++Step)
    {
        if (!StepChase())
        {
            break;
        }
    }

    if (ChaserActor) { ChaserActor->SetBoardPosition(State.ChaserPosition); }
    if (RunnerActor) { RunnerActor->SetBoardPosition(State.RunnerPosition); }
}

bool AChaseDemoGameMode::StepChase()
{
    if (!bHasPendingDecision)
    {
        TInstancedStruct<FPoint> Observation;
        Observation.InitializeAs<FBoxPoint>(BuildObservation(), TArray<int>{1, ObservationCount});
        int64 NewDecisionId = INDEX_NONE;
        if (Policy->RequestDecision(Observation, NewDecisionId, /*TimeoutSeconds=*/1.0f))
        {
            PendingDecisionId = NewDecisionId;
            PendingObservationPoint = Observation;
            bHasPendingDecision = true;
            // URLToolsNPCPolicy::DispatchDecision resolves synchronously (CompleteDecision runs
            // inside RequestDecision itself), so TryGetDecision below can already return Ready
            // this same call -- falling through (no return) is what lets one call to StepChase
            // complete a full step instead of needing two.
        }
        else
        {
            return false;
        }
    }

    FAdaptiveDecision Decision;
    const EAdaptiveRequestStatus Status = Policy->TryGetDecision(PendingDecisionId, Decision);
    if (Status == EAdaptiveRequestStatus::Pending)
    {
        return false;
    }
    if (Status != EAdaptiveRequestStatus::Ready)
    {
        // Failed/Cancelled/Expired: drop this decision and ask again next call rather than guess an action.
        bHasPendingDecision = false;
        return false;
    }

    const FBoxPoint* ActionPoint = Decision.Action.GetPtr<FBoxPoint>();
    const FVector2D PolicyVelocity = ActionPoint && ActionPoint->Values.Num() >= 2
        ? FVector2D(ActionPoint->Values[0], ActionPoint->Values[1])
        : FVector2D::ZeroVector;

    // Warm-up handover. A barely-trained network moves erratically (handing over at the first
    // update was the "thrashing"), so the chaser starts by heading straight at the runner and
    // blends to the network's action as training accumulates (WarmupStartUpdates ->
    // WarmupEndUpdates). The blended move is what gets submitted as ExecutedAction, and SAC is
    // off-policy, so warm-up moves are valid training data.
    FRLToolsLearningStats Stats;
    Policy->GetLearningStats(Stats);
    const int32 RampLength = WarmupEndUpdates - WarmupStartUpdates;
    PolicyControl = RampLength > 0
        ? FMath::Clamp(static_cast<float>(Stats.Updates - WarmupStartUpdates) / static_cast<float>(RampLength), 0.0f, 1.0f)
        : (Stats.Updates >= WarmupEndUpdates ? 1.0f : 0.0f);
    const FVector2D TowardRunner = (State.RunnerPosition - State.ChaserPosition).GetSafeNormal();
    // Small random wobble on warm-up moves so the learner sees varied actions, not one straight line.
    const FVector2D WarmupMove = TowardRunner + FVector2D(WarmupRng.FRandRange(-WarmupNoise, WarmupNoise), WarmupRng.FRandRange(-WarmupNoise, WarmupNoise));
    const FVector2D RequestedVelocity = FMath::Lerp(WarmupMove, PolicyVelocity, static_cast<double>(PolicyControl));
    const FVector2D ExecutedActionSpace = ApplyChaserAction(RequestedVelocity);
    LastChaserActionSpace = ExecutedActionSpace;

    const FVector2D DistanceBefore2D = State.RunnerPosition - State.ChaserPosition;
    const float DistanceBefore = DistanceBefore2D.Size();

    // Fixed distance per step (ChaserMaxSpeed units), matching the terminal demo's chaser_max_speed.
    State.ChaserPosition += ExecutedActionSpace * ChaserMaxSpeed;
    State.ChaserPosition.X = FMath::Clamp(State.ChaserPosition.X, 0.0f, BoardSize);
    State.ChaserPosition.Y = FMath::Clamp(State.ChaserPosition.Y, 0.0f, BoardSize);

    if (bPlayerControlsRunner)
    {
        // Same per-step distance model as the chaser -- WASD picks a direction, RunnerSpeed is
        // the fixed distance moved this step, not a continuous real-time velocity.
        const FVector2D Direction = GetPlayerRunnerInput();
        const FVector2D Delta = Direction.IsNearlyZero() ? FVector2D::ZeroVector : Direction.GetSafeNormal() * RunnerSpeed;
        State.RunnerPosition.X = FMath::Clamp(State.RunnerPosition.X + Delta.X, 0.0f, BoardSize);
        State.RunnerPosition.Y = FMath::Clamp(State.RunnerPosition.Y + Delta.Y, 0.0f, BoardSize);
        LastRunnerVelocityUnit = Direction.IsNearlyZero() ? FVector2D::ZeroVector : Direction.GetSafeNormal();
        // Kept in sync so toggling back to an automated pattern (Tab) continues from here
        // instead of teleporting back to wherever the pattern was before manual control started.
        RunnerPatternState.Position = State.RunnerPosition;
        RunnerPatternState.Velocity = Delta;
    }
    else
    {
        FChaseDemoRunnerPatternConfig PatternConfig;
        PatternConfig.BoardSize = BoardSize;
        PatternConfig.RunnerSpeed = RunnerSpeed;
        StepChaseDemoRunner(RunnerPattern, PatternConfig, RunnerPatternState, State.ChaserPosition);
        State.RunnerPosition = RunnerPatternState.Position;
        LastRunnerVelocityUnit = RunnerPatternState.Velocity.GetSafeNormal();
    }

    State.StepsThisEpisode++;

    const float DistanceAfter = (State.RunnerPosition - State.ChaserPosition).Size();
    const bool bCaught = DistanceAfter <= CatchDistance;
    const bool bTimedOut = State.StepsThisEpisode >= MaxEpisodeSteps;
    float Reward = DistanceBefore - DistanceAfter;
    if (bCaught)
    {
        Reward += CatchBonus;
        State.CatchesThisEpisodeWindow++;
    }

    TInstancedStruct<FPoint> NextObservation;
    NextObservation.InitializeAs<FBoxPoint>(BuildObservation(), TArray<int>{1, ObservationCount});
    TInstancedStruct<FPoint> ExecutedActionPoint;
    ExecutedActionPoint.InitializeAs<FBoxPoint>(
        TArray<float>{static_cast<float>(ExecutedActionSpace.X), static_cast<float>(ExecutedActionSpace.Y)},
        TArray<int>{1, ActionCount});

    FAdaptiveExperience Experience;
    Experience.SessionId = Decision.SessionId;
    Experience.AgentId = Decision.AgentId;
    Experience.EpisodeId = Decision.EpisodeId;
    Experience.DecisionId = Decision.DecisionId;
    Experience.PolicyVersion = Decision.PolicyVersion;
    Experience.Observation = PendingObservationPoint;
    Experience.ExecutedAction = ExecutedActionPoint;
    Experience.Reward = Reward;
    Experience.NextObservation = NextObservation;
    Experience.bTerminated = bCaught;
    Experience.bTruncated = !bCaught && bTimedOut;

    const EAdaptiveSubmitResult SubmitResult = Policy->SubmitExperience(Experience);
    if (SubmitResult != EAdaptiveSubmitResult::Accepted)
    {
        UE_LOG(LogChaseDemo, Verbose, TEXT("SubmitExperience: %d (%s)"),
            static_cast<int32>(SubmitResult), *Policy->GetLastError());
    }

    bHasPendingDecision = false;
    if (bCaught)
    {
        RecordOutcome(true);
        // The actual reset (and the EpisodesCompleted increment) is deferred until the flash
        // elapses (see Tick) -- change CatchFlashDuration above to retune how long this holds.
        CatchFlashTimeRemaining = CatchFlashDuration;
        if (ChaserActor) { ChaserActor->SetFlashWhite(true); }
        if (RunnerActor) { RunnerActor->SetFlashWhite(true); }
        return false; // stop the rest of this tick's StepsPerTick loop; the flash owns the freeze now.
    }
    if (bTimedOut)
    {
        RecordOutcome(false);
        ResetEpisode(/*bCountAsEpisode=*/true);
    }
    return true;
}

void AChaseDemoGameMode::RecordOutcome(bool bCaught)
{
    RecentOutcomes.Add(bCaught);
    const int32 Excess = RecentOutcomes.Num() - FMath::Max(1, RecentWindowSize);
    if (Excess > 0)
    {
        RecentOutcomes.RemoveAt(0, Excess);
    }
}

float AChaseDemoGameMode::GetRecentCatchRate() const
{
    if (RecentOutcomes.IsEmpty())
    {
        return 0.0f;
    }
    int32 Catches = 0;
    for (const bool bCaught : RecentOutcomes)
    {
        Catches += bCaught ? 1 : 0;
    }
    return float(Catches) / float(RecentOutcomes.Num());
}

FVector2D AChaseDemoGameMode::GetPlayerRunnerInput() const
{
    // Same W/A/S/D-to-XY mapping as TagArenaPawn's own human control.
    APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
    if (!PC)
    {
        return FVector2D::ZeroVector;
    }
    const float X = float(PC->IsInputKeyDown(EKeys::W)) - float(PC->IsInputKeyDown(EKeys::S));
    const float Y = float(PC->IsInputKeyDown(EKeys::D)) - float(PC->IsInputKeyDown(EKeys::A));
    return FVector2D(X, Y);
}

FRLToolsLearningStats AChaseDemoGameMode::GetDisplayStats() const
{
    FRLToolsLearningStats Stats;
    if (Policy)
    {
        Policy->GetLearningStats(Stats);
    }
    return Stats;
}

void AChaseDemoGameMode::BuildArena()
{
    // One flat grey square matching the board exactly, so the arena reads as a bounded play
    // area. A squashed Cube, not the engine Plane mesh: its size is already confirmed (the
    // chaser/runner actors rely on the same 100-unit Cube/Sphere convention), whereas assuming
    // Plane's size was exactly what produced the previous oversized/checkerboarded floor.
    const FVector BoardCenter(BoardSize * 0.5f, BoardSize * 0.5f, -1.0f); // Z=-1: strictly below
    // the actors (Z=0) so the floor can never z-fight with or occlude them.
    const float EngineCubeSize = 100.0f; // /Engine/BasicShapes/Cube.Cube is 100x100x100 units.

    AStaticMeshActor* FloorActor = GetWorld()->SpawnActor<AStaticMeshActor>(BoardCenter, FRotator::ZeroRotator);
    FloorActor->SetMobility(EComponentMobility::Movable);
    UStaticMeshComponent* FloorComponent = FloorActor->GetStaticMeshComponent();
    FloorComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
    {
        FloorComponent->SetStaticMesh(CubeMesh);
    }
    // One circle radius bigger than the board on every side: positions are clamped to
    // [0, BoardSize], so a circle on the edge has its centre on the board line and would otherwise
    // hang half off the floor.
    const float FloorSize = BoardSize + ActorDiameter;
    FloorComponent->SetRelativeScale3D(FVector(FloorSize / EngineCubeSize, FloorSize / EngineCubeSize, 0.02f));
    // Explicitly assign a material that actually has a Color parameter before tinting it --
    // CreateAndSetMaterialInstanceDynamic only tints whatever's already in the slot, and Cube's
    // default slot isn't guaranteed to already be BasicShapeMaterial.
    if (UMaterialInterface* BasicMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
    {
        FloorComponent->SetMaterial(0, BasicMaterial);
    }
    if (UMaterialInstanceDynamic* Dynamic = FloorComponent->CreateAndSetMaterialInstanceDynamic(0))
    {
        Dynamic->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.35f, 0.35f, 0.35f)); // plain grey
    }
}

void AChaseDemoGameMode::FitCameraToViewport()
{
    UCameraComponent* Camera = OverviewCamera ? OverviewCamera->GetCameraComponent() : nullptr;
    UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    if (!Camera || !Viewport)
    {
        return;
    }
    FVector2D ViewSize;
    Viewport->GetViewportSize(ViewSize);
    if (ViewSize.X < 1.0 || ViewSize.Y < 1.0)
    {
        return;
    }
    // Ortho projection: horizontal span = OrthoWidth, vertical span = OrthoWidth / aspect.
    // Pick the width that makes the whole floor (board + one radius each side) fill
    // ArenaScreenFraction of whichever screen axis is shorter.
    const float Aspect = static_cast<float>(ViewSize.X / ViewSize.Y);
    const float Span = (BoardSize + ActorDiameter) / FMath::Clamp(ArenaScreenFraction, 0.1f, 1.0f);
    const float Width = Span * FMath::Max(Aspect, 1.0f);
    if (!FMath::IsNearlyEqual(Camera->OrthoWidth, Width, 0.01f))
    {
        Camera->SetOrthoWidth(Width);
    }
}

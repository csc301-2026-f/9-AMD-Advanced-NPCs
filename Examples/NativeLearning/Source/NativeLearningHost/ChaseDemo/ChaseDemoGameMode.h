// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Minimal, watchable chase: a chaser learns (via the real Schola plugin, URLToolsNPCPolicy) to
// catch a scripted runner. Plan step 3 (scene) + step 4 (driving loop). No hand-authored map
// content: both actors are spawned in BeginPlay using engine basic-shape meshes.
//
// NOT YET COMPILED. UE 5.8 was not installed on this machine while this file was written; every
// API used here is copied from this repo's own working code (RLToolsPolicyTestHelpers.h,
// RLToolsNPCPolicy.h), not guessed, but the first real check is still the next build attempt.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Camera/CameraActor.h"
#include "Engine/DirectionalLight.h"
#include "Policies/RLToolsNPCPolicy.h"
#include "ChaseDemoRunnerPatterns.h"
#include "ChaseDemoGameMode.generated.h"

class AChaseDemoActor;

/** Plain 2D task state. Disposable test material (see GLOSSARY.md "Chase task") -- deliberately
 * not a port of Examples/AdaptiveTag's ChaseWorld, to avoid recoupling to the old prototype.
 */
USTRUCT(BlueprintType)
struct FChaseDemoState
{
    GENERATED_BODY()

    UPROPERTY()
    FVector2D ChaserPosition = FVector2D(10, 10);

    UPROPERTY()
    FVector2D RunnerPosition = FVector2D(40, 40);

    UPROPERTY()
    int32 StepsThisEpisode = 0;

    UPROPERTY()
    int32 EpisodesCompleted = 0;

    UPROPERTY()
    int32 CatchesThisEpisodeWindow = 0;
};

/**
 * Autonomous-only for now (Q12): the runner follows a fixed scripted pattern, the chaser is driven
 * by URLToolsNPCPolicy. ApplyChaserAction (below) is the one seam to swap in player input later (Q15)
 * -- do not move its logic elsewhere.
 */
UCLASS()
class NATIVELEARNINGHOST_API AChaseDemoGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AChaseDemoGameMode();

    /** Compiled profile id for the chase-sac profile added in Config/ScholaRLTools.json.
     * Project-generated profiles are identified as "<algorithm>/project/<name>", not "<algorithm>/<name>"
     * (that second form is only for the three shipped example profiles).
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    FString ConfigurationId = TEXT("sac/project/chase-sac");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float BoardSize = 100.0f;

    // Halved again from the terminal chase demo's own per-step pace (ChaseConfig.h: runner_speed
    // =1.0, chaser_max_speed=1.0) at the user's request for a slower, more watchable chase.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float ChaserMaxSpeed = 0.6Pf;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float RunnerSpeed = 0.6f;

    /** Same six behaviors as Examples/AdaptiveTag's terminal chase demo
     * (ChaseDemoRunnerPatterns.h, ported from RunnerPatterns.h/.cpp). Press 1-6 in-game to pick
     * one directly (shown on the HUD); this is just the initial value.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    EChaseDemoRunnerPattern RunnerPattern = EChaseDemoRunnerPattern::RightTurn;

    /** How many chase steps run per real engine tick; F cycles 1/4/12, matching TagArena's own
     * speed-up control. Unlike a DeltaSeconds multiplier, this matches the terminal demo's model
     * where movement is a fixed distance per step, not a continuous real-time velocity.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ChaseDemo")
    int32 StepsPerTick = 1;

    /** Space toggles this. While true, StepChase never runs -- the board, HUD counters and
     * training all freeze, same as TagArena's own bPaused.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ChaseDemo")
    bool bPaused = false;

    /** Tab toggles this. WASD then drives the runner directly (same per-step distance model as
     * the chaser) instead of RunnerPattern's scripted behavior. The chaser keeps learning either
     * way -- SubmitExperience doesn't know or care what moved the runner.
     */
    UPROPERTY(BlueprintReadOnly, Category = "ChaseDemo")
    bool bPlayerControlsRunner = false;

    /** How long the white catch-flash and the "CAUGHT!" banner hold before the episode resets.
     * Change this value to retune it -- nothing else needs editing.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float CatchFlashDuration = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float CatchDistance = 3.0f;

    /** Reward shaping lives here, not in the SAC profile (see docs/adr/0002, step 1 correction). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    float CatchBonus = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChaseDemo")
    int32 MaxEpisodeSteps = 300;

    UPROPERTY(BlueprintReadOnly, Category = "ChaseDemo")
    FChaseDemoState State;

    /** Read by ChaseDemoHUD. Owned here so the HUD stays a pure display, no logic of its own. */
    UFUNCTION(BlueprintPure, Category = "ChaseDemo")
    FRLToolsLearningStats GetDisplayStats() const;

    UFUNCTION(BlueprintPure, Category = "ChaseDemo")
    URLToolsNPCPolicy* GetPolicy() const { return Policy; }

    /** True for CatchFlashDuration seconds after a catch -- the HUD uses this to draw the
     * "CAUGHT!" banner for exactly as long as the actors are flashing white.
     */
    UFUNCTION(BlueprintPure, Category = "ChaseDemo")
    bool IsCatchFlashActive() const { return CatchFlashTimeRemaining > 0.0f; }

protected:
    void BeginPlay() override;
    void Tick(float DeltaSeconds) override;
    void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    /** The one seam for later player input (Q15): replaces this call's result with player input,
     * nothing else in Tick changes. Returns the actually-applied action (post speed-cap), which is
     * what SubmitExperience must report as ExecutedAction -- never the raw policy output.
     */
    FVector2D ApplyChaserAction(const FVector2D& RequestedVelocity) const;

    /** Runs one chase step (one decision resolve + one movement update for both actors). Returns
     * false only when a decision could not be requested/resolved this call, so the StepsPerTick
     * loop in Tick knows to stop early rather than skip ahead.
     */
    bool StepChase();
    TArray<float> BuildObservation() const;
    void ResetEpisode(bool bCountAsEpisode);

    /** WASD direction for the player-controlled runner (Tab), same W/A/S/D-to-XY mapping
     * TagArenaPawn uses. Returns zero if no player controller or no keys held.
     */
    FVector2D GetPlayerRunnerInput() const;

    /** A single flat colored square matching the board, so the arena reads as a bounded play
     * area instead of a void with two shapes floating in it. Plain and solid-colored by request
     * -- no Content/ asset needed.
     */
    void BuildArena();

    UPROPERTY()
    TObjectPtr<URLToolsNPCPolicy> Policy;

    UPROPERTY()
    TObjectPtr<AChaseDemoActor> ChaserActor;

    UPROPERTY()
    TObjectPtr<AChaseDemoActor> RunnerActor;

    // No player pawn exists (Q12: autonomous-only for now), so PIE has no camera to view through
    // unless we spawn and possess one ourselves.
    UPROPERTY()
    TObjectPtr<ACameraActor> OverviewCamera;

    // The Entry map has no light actors of its own, and the basic-shape meshes use a lit
    // material: without a movable light, the scene renders solid black regardless of camera setup.
    UPROPERTY()
    TObjectPtr<ADirectionalLight> SunLight;

    // Driving-loop bookkeeping (Q3/Q13: throwaway, experiment-scoped, not a reusable stepper).
    int64 PendingDecisionId = INDEX_NONE;
    bool bHasPendingDecision = false;
    // The exact point sent with RequestDecision, kept verbatim so the experience we submit
    // records what was actually asked, never a re-derived approximation of it.
    TInstancedStruct<FPoint> PendingObservationPoint;
    // Last applied action, in action-space units ([-1, 1] per axis) -- feeds the next
    // observation's velocity terms. Zero at episode start (chaser considered stationary).
    FVector2D LastChaserActionSpace = FVector2D::ZeroVector;
    FVector2D LastRunnerVelocityUnit = FVector2D::ZeroVector;

    // Runner pattern bookkeeping (ChaseDemoRunnerPatterns.h), ported from the terminal demo.
    FChaseDemoRunnerState RunnerPatternState;
    FRandomStream RunnerRng;

    // Counts down from CatchFlashDuration once a catch happens; StepChase stays paused and
    // ResetEpisode is deferred until it reaches zero (see Tick), so the catch is actually visible
    // for one real-time window instead of resetting in the same frame it occurred.
    float CatchFlashTimeRemaining = 0.0f;
};

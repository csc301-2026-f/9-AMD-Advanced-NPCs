// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Ported from Examples/AdaptiveTag's terminal chase demo (RunnerPatterns.h/.cpp): the same six
// runner behaviors (Straight, Square, Circle ignore the chaser; RightTurn, Clockwise, Corner are
// "habit" runners that flee within FleeRadius but always the same way), at the user's request to
// match that demo instead of the placeholder diagonal bounce this file used to have.
#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "ChaseDemoRunnerPatterns.generated.h"

UENUM(BlueprintType)
enum class EChaseDemoRunnerPattern : uint8
{
    Straight,
    Square,
    Circle,
    RightTurn,
    Clockwise,
    Corner
};

USTRUCT()
struct FChaseDemoRunnerPatternConfig
{
    GENERATED_BODY()

    float BoardSize = 100.0f;
    float RunnerSpeed = 1.0f;
    // Terminal demo defaults (ChaseConfig.h), unscaled: board_size there also defaults to 100.
    float SquareSide = 60.0f;
    float CircleRadius = 30.0f;
    float FleeRadius = 60.0f;
    float FleeTurn = 0.06f;
    float HabitTurn = 0.02f;
    float WallMargin = 4.0f;
};

USTRUCT()
struct FChaseDemoRunnerState
{
    GENERATED_BODY()

    FVector2D Position = FVector2D::ZeroVector;
    FVector2D Velocity = FVector2D::ZeroVector;
    // Square: distance along the perimeter. Circle: angle in radians. Corner: target corner 0-3
    // (-1 = none yet). Unused by Straight/RightTurn/Clockwise.
    float Phase = 0.0f;
};

FChaseDemoRunnerState StartChaseDemoRunner(EChaseDemoRunnerPattern Pattern, const FChaseDemoRunnerPatternConfig& Config, FRandomStream& Rng);
void StepChaseDemoRunner(EChaseDemoRunnerPattern Pattern, const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, const FVector2D& ChaserPosition);

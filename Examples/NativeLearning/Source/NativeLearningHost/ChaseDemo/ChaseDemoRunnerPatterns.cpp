// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Direct port of Examples/AdaptiveTag/Source/OnlineSACRuntime/Private/RunnerPatterns.cpp's math,
// using FVector2D/FMath instead of the terminal tool's portable Vec2/cstdint so this stays a
// plain UE file; the geometry (every constant and formula) is unchanged.
#include "ChaseDemoRunnerPatterns.h"

namespace
{
constexpr float TwoPi = 6.28318530718f;

void PlaceOnSquare(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner)
{
    const float Side = Config.SquareSide;
    const float Low = (Config.BoardSize - Side) / 2.0f, High = Low + Side;
    const uint32 Edge = static_cast<uint32>(Runner.Phase / Side) % 4;
    const float T = Runner.Phase - Edge * Side;
    const float S = Config.RunnerSpeed;
    switch (Edge)
    {
    case 0: Runner.Position = FVector2D(Low + T, High); Runner.Velocity = FVector2D(S, 0); break;
    case 1: Runner.Position = FVector2D(High, High - T); Runner.Velocity = FVector2D(0, -S); break;
    case 2: Runner.Position = FVector2D(High - T, Low); Runner.Velocity = FVector2D(-S, 0); break;
    default: Runner.Position = FVector2D(Low, Low + T); Runner.Velocity = FVector2D(0, S); break;
    }
}

void PlaceOnCircle(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner)
{
    const float Centre = Config.BoardSize / 2.0f;
    const float C = FMath::Cos(Runner.Phase), S = FMath::Sin(Runner.Phase);
    Runner.Position = FVector2D(Centre + Config.CircleRadius * C, Centre + Config.CircleRadius * S);
    Runner.Velocity = FVector2D(Config.RunnerSpeed * S, -Config.RunnerSpeed * C);
}

float Bounce(float P, float& V, float Size)
{
    if (P < 0) { P = -P; V = -V; }
    if (P > Size) { P = 2 * Size - P; V = -V; }
    return P < 0 ? 0 : (P > Size ? Size : P);
}

FVector2D Unit(const FVector2D& V, const FVector2D& Fallback)
{
    const float Length = V.Size();
    return Length > 1e-6f ? V / Length : Fallback;
}

// Clockwise rotation (the angle decreases), matching PlaceOnCircle.
FVector2D RotateClockwise(const FVector2D& V, float Angle)
{
    const float C = FMath::Cos(Angle), S = FMath::Sin(Angle);
    return FVector2D(V.X * C + V.Y * S, -V.X * S + V.Y * C);
}

// Direction of travel that goes clockwise around the board along the wall nearest to P.
FVector2D ClockwiseAlongWall(const FChaseDemoRunnerPatternConfig& Config, const FVector2D& P)
{
    const float B = Config.BoardSize;
    const float Top = B - P.Y, Right = B - P.X, Bottom = P.Y, Left = P.X;
    const float Nearest = FMath::Min(FMath::Min(Top, Right), FMath::Min(Bottom, Left));
    if (Nearest == Top) return FVector2D(1, 0);
    if (Nearest == Right) return FVector2D(0, -1);
    if (Nearest == Bottom) return FVector2D(-1, 0);
    return FVector2D(0, 1);
}

// Moves by Direction * RunnerSpeed. A move that would leave the board slides along the wall
// instead (clockwise if there is no sideways part), so the runner never loses speed to a wall.
void StepInside(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, FVector2D Direction)
{
    const float B = Config.BoardSize, S = Config.RunnerSpeed;
    FVector2D D = Unit(Direction, FVector2D(1, 0));
    FVector2D Next(Runner.Position.X + D.X * S, Runner.Position.Y + D.Y * S);
    if (Next.X < 0 || Next.X > B) { D.X = 0; }
    if (Next.Y < 0 || Next.Y > B) { D.Y = 0; }
    if (D.X == 0 || D.Y == 0)
    {
        D = Unit(D, ClockwiseAlongWall(Config, Runner.Position));
        Next = FVector2D(Runner.Position.X + D.X * S, Runner.Position.Y + D.Y * S);
        // In a corner the wall tangent can still point out of the board: take the first
        // clockwise direction that fits.
        const FVector2D Tangents[] = { FVector2D(1, 0), FVector2D(0, -1), FVector2D(-1, 0), FVector2D(0, 1) };
        for (const FVector2D& T : Tangents)
        {
            if (Next.X >= 0 && Next.X <= B && Next.Y >= 0 && Next.Y <= B) { break; }
            D = T;
            Next = FVector2D(Runner.Position.X + D.X * S, Runner.Position.Y + D.Y * S);
        }
    }
    Runner.Velocity = FVector2D(D.X * S, D.Y * S);
    Runner.Position = FVector2D(
        FMath::Clamp(Next.X, 0.0f, B),
        FMath::Clamp(Next.Y, 0.0f, B));
}

FVector2D CornerPoint(const FChaseDemoRunnerPatternConfig& Config, int32 Corner)
{
    const float B = Config.BoardSize;
    return FVector2D((Corner & 1) ? B : 0.0f, (Corner & 2) ? B : 0.0f);
}

float DistanceTo(const FVector2D& A, const FVector2D& B) { return (A - B).Size(); }

// Only ever turns right (clockwise). Calm: a gentle right-hand curve. Chaser close: turns right
// as fast as FleeTurn allows until it faces away from the chaser; if "away" is to its left it
// keeps turning right, so it loops the long way round.
void MoveRightTurn(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, const FVector2D& Chaser)
{
    FVector2D Heading = Unit(Runner.Velocity, FVector2D(1, 0));
    if (DistanceTo(Runner.Position, Chaser) < Config.FleeRadius)
    {
        const FVector2D Away = Unit(Runner.Position - Chaser, Heading);
        float Needed = FMath::Atan2(Heading.X * Away.Y - Heading.Y * Away.X, Heading.X * Away.X + Heading.Y * Away.Y);
        Needed = Needed <= 0 ? -Needed : TwoPi - Needed;
        Heading = RotateClockwise(Heading, FMath::Min(Needed, Config.FleeTurn));
    }
    else
    {
        Heading = RotateClockwise(Heading, Config.HabitTurn);
    }
    StepInside(Config, Runner, Heading);
}

// Always goes clockwise round the walls (pulled to WallMargin from the wall). With the chaser
// close it swerves away from it (cutting inward round it) but never stops making clockwise progress.
void MoveClockwise(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, const FVector2D& Chaser)
{
    const float B = Config.BoardSize, M = Config.WallMargin;
    const FVector2D P = Runner.Position;
    const FVector2D Along = ClockwiseAlongWall(Config, P);
    FVector2D Pull(0, 0);
    if (Along.X != 0) { const float Line = Along.X > 0 ? B - M : M; Pull.Y = (Line - P.Y) / 4.0f; }
    else { const float Line = Along.Y > 0 ? M : B - M; Pull.X = (Line - P.X) / 4.0f; }
    Pull = FVector2D(FMath::Clamp(Pull.X, -1.0f, 1.0f), FMath::Clamp(Pull.Y, -1.0f, 1.0f));
    FVector2D Heading(Along.X + Pull.X, Along.Y + Pull.Y);
    if (DistanceTo(P, Chaser) < Config.FleeRadius)
    {
        const FVector2D Away = Unit(P - Chaser, FVector2D(0, 0));
        Heading = FVector2D(Pull.X + Along.X + 2.0f * Away.X, Pull.Y + Along.Y + 2.0f * Away.Y);
        const float Progress = Heading.X * Along.X + Heading.Y * Along.Y;
        const float Length = Heading.Size();
        // Keep at least 30% of the movement clockwise along the wall, whatever the chaser does.
        if (Progress < 0.3f * Length)
        {
            const float Add = 0.3f * Length - Progress;
            Heading = FVector2D(Heading.X + Add * Along.X, Heading.Y + Add * Along.Y);
        }
    }
    StepInside(Config, Runner, Heading);
}

// Runs to a corner and waits there. When the chaser comes close it leaves at once (or switches
// mid-run) for the other corner it can reach with the biggest lead over the chaser (distance
// from the chaser minus distance from the runner).
void MoveCorner(const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, const FVector2D& Chaser)
{
    auto Lead = [&](int32 Corner) { return DistanceTo(CornerPoint(Config, Corner), Chaser) - DistanceTo(CornerPoint(Config, Corner), Runner.Position); };
    auto Best = [&](int32 Skip)
    {
        int32 BestCorner = -1;
        for (int32 C = 0; C < 4; ++C)
        {
            if (C != Skip && (BestCorner < 0 || Lead(C) > Lead(BestCorner))) { BestCorner = C; }
        }
        return BestCorner;
    };
    int32 Target = static_cast<int32>(Runner.Phase);
    if (Runner.Phase < 0) { Target = Best(-1); }
    else if (DistanceTo(Runner.Position, Chaser) < Config.FleeRadius)
    {
        // Never consider the corner it is standing in: staying always looks safest (its lead is
        // its whole distance to the chaser).
        int32 Here = -1;
        for (int32 C = 0; C < 4; ++C)
        {
            if (DistanceTo(Runner.Position, CornerPoint(Config, C)) <= 3.0f) { Here = C; }
        }
        const int32 Candidate = Best(Here);
        // It commits to a corner: 25 units of hysteresis, no dithering.
        if (Target == Here || Lead(Candidate) > Lead(Target) + 25.0f) { Target = Candidate; }
    }
    const FVector2D Corner = CornerPoint(Config, Target);
    if (DistanceTo(Runner.Position, Corner) <= Config.RunnerSpeed)
    {
        Runner.Position = Corner;
        Runner.Velocity = FVector2D::ZeroVector;
    }
    else
    {
        StepInside(Config, Runner, Corner - Runner.Position);
    }
    Runner.Phase = static_cast<float>(Target);
}
}

FChaseDemoRunnerState StartChaseDemoRunner(EChaseDemoRunnerPattern Pattern, const FChaseDemoRunnerPatternConfig& Config, FRandomStream& Rng)
{
    FChaseDemoRunnerState Runner;
    switch (Pattern)
    {
    case EChaseDemoRunnerPattern::Straight:
    {
        Runner.Position = FVector2D(Rng.FRandRange(0.0f, Config.BoardSize), Rng.FRandRange(0.0f, Config.BoardSize));
        const float Heading = Rng.FRandRange(0.0f, TwoPi);
        Runner.Velocity = FVector2D(Config.RunnerSpeed * FMath::Cos(Heading), Config.RunnerSpeed * FMath::Sin(Heading));
        break;
    }
    case EChaseDemoRunnerPattern::Square:
        Runner.Phase = Rng.FRandRange(0.0f, 4.0f * Config.SquareSide);
        PlaceOnSquare(Config, Runner);
        break;
    case EChaseDemoRunnerPattern::Circle:
        Runner.Phase = Rng.FRandRange(0.0f, TwoPi);
        PlaceOnCircle(Config, Runner);
        break;
    case EChaseDemoRunnerPattern::RightTurn:
    case EChaseDemoRunnerPattern::Clockwise:
    case EChaseDemoRunnerPattern::Corner:
    {
        Runner.Position = FVector2D(Rng.FRandRange(0.0f, Config.BoardSize), Rng.FRandRange(0.0f, Config.BoardSize));
        const float Heading = Rng.FRandRange(0.0f, TwoPi);
        Runner.Velocity = FVector2D(Config.RunnerSpeed * FMath::Cos(Heading), Config.RunnerSpeed * FMath::Sin(Heading));
        Runner.Phase = -1.0f;
        break;
    }
    }
    return Runner;
}

void StepChaseDemoRunner(EChaseDemoRunnerPattern Pattern, const FChaseDemoRunnerPatternConfig& Config, FChaseDemoRunnerState& Runner, const FVector2D& ChaserPosition)
{
    switch (Pattern)
    {
    case EChaseDemoRunnerPattern::Straight:
    {
        // FVector2D::X/Y are double; Bounce takes float& to match the ported terminal-demo
        // signature, so it needs real float lvalues rather than binding to the double members directly.
        float Vx = static_cast<float>(Runner.Velocity.X);
        float Vy = static_cast<float>(Runner.Velocity.Y);
        Runner.Position.X = Bounce(static_cast<float>(Runner.Position.X) + Vx, Vx, Config.BoardSize);
        Runner.Position.Y = Bounce(static_cast<float>(Runner.Position.Y) + Vy, Vy, Config.BoardSize);
        Runner.Velocity.X = Vx;
        Runner.Velocity.Y = Vy;
        break;
    }
    case EChaseDemoRunnerPattern::Square:
        Runner.Phase = FMath::Fmod(Runner.Phase + Config.RunnerSpeed, 4.0f * Config.SquareSide);
        PlaceOnSquare(Config, Runner);
        break;
    case EChaseDemoRunnerPattern::Circle:
        Runner.Phase -= Config.RunnerSpeed / Config.CircleRadius;
        if (Runner.Phase < 0) { Runner.Phase += TwoPi; }
        PlaceOnCircle(Config, Runner);
        break;
    case EChaseDemoRunnerPattern::RightTurn: MoveRightTurn(Config, Runner, ChaserPosition); break;
    case EChaseDemoRunnerPattern::Clockwise: MoveClockwise(Config, Runner, ChaserPosition); break;
    case EChaseDemoRunnerPattern::Corner: MoveCorner(Config, Runner, ChaserPosition); break;
    }
}

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseDemoHUD.h"
#include "ChaseDemoGameMode.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void AChaseDemoHUD::DrawHUD()
{
    Super::DrawHUD();
    const AChaseDemoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AChaseDemoGameMode>() : nullptr;
    if (!Canvas || !GameMode)
    {
        return;
    }

    // Palette and panel layout match TagArenaHUD (Examples/AdaptiveTag), at the user's request
    // for a consistent look across chase demos.
    // Smaller reference resolution than TagArenaHUD's 1600x1000 -- this HUD has far less content,
    // so a bigger scale factor ("zoomed in") reads better than TagArena's denser layout would at
    // the same physical size.
    const float S = FMath::Min(Canvas->SizeX / 900.0f, Canvas->SizeY / 560.0f);
    const FLinearColor Ink(0.86f, 0.92f, 0.98f), Muted(0.48f, 0.59f, 0.70f), Cyan(0.1f, 0.85f, 1), Gold(1, 0.76f, 0.3f);
    const FLinearColor Panel(0.012f, 0.022f, 0.037f, 0.97f);
    auto Rect = [&](float X, float Y, float Width, float Height, FLinearColor Color) { DrawRect(Color, X * S, Y * S, Width * S, Height * S); };
    auto Text = [&](const FString& Value, float X, float Y, FLinearColor Color, float Size = 0.75f)
    {
        FCanvasTextItem Item(FVector2D(X * S, Y * S), FText::FromString(Value), GEngine->GetMediumFont(), Color);
        Item.Scale = FVector2D(S * Size * 1.3f);
        Item.EnableShadow(FLinearColor(0, 0, 0, 0.6f));
        Canvas->DrawItem(Item);
    };

    const FRLToolsLearningStats Stats = GameMode->GetDisplayStats();
    const int32 Episodes = GameMode->State.EpisodesCompleted;
    const int32 RecentCount = GameMode->GetRecentCount();
    // Rolling, not all-time: an all-time rate would average away the drop-and-recover after a
    // runner switch, which is the whole point of the demo.
    const FString CatchRateText = RecentCount > 0
        ? FString::Printf(TEXT("CATCH RATE %.0f%%  (LAST %d)"), 100.0f * GameMode->GetRecentCatchRate(), RecentCount)
        : TEXT("CATCH RATE --");

    const TCHAR* PatternName = TEXT("?");
    switch (GameMode->RunnerPattern)
    {
    case EChaseDemoRunnerPattern::Straight: PatternName = TEXT("STRAIGHT"); break;
    case EChaseDemoRunnerPattern::Square: PatternName = TEXT("SQUARE"); break;
    case EChaseDemoRunnerPattern::Circle: PatternName = TEXT("CIRCLE"); break;
    case EChaseDemoRunnerPattern::RightTurn: PatternName = TEXT("RIGHT TURN"); break;
    case EChaseDemoRunnerPattern::Clockwise: PatternName = TEXT("CLOCKWISE"); break;
    case EChaseDemoRunnerPattern::Corner: PatternName = TEXT("CORNER"); break;
    }

    // Narrow, hugging the left edge -- the arena is centered on screen by the orthographic
    // camera, so this clears it entirely instead of covering half of it.
    Rect(10, 10, 230, 46, Panel);
    Rect(10, 10, 4, 46, Cyan);
    Text(TEXT("CHASE DEMO"), 24, 16, Ink, 0.9f);
    Text(GameMode->bPaused ? TEXT("PAUSED") : FString::Printf(TEXT("%dx/TICK"), GameMode->StepsPerTick),
        24, 38, GameMode->bPaused ? Gold : Muted, 0.5f);

    // Only what a developer actually needs at a glance: episode count, catch rate, whether
    // training has started (gradient updates), and what's currently driving the runner.
    Rect(10, 64, 230, 150, Panel);
    Text(FString::Printf(TEXT("EPISODES %d"), Episodes), 24, 76, Ink, 0.66f);
    Text(CatchRateText, 24, 100, Ink, 0.66f);
    Text(FString::Printf(TEXT("GRADIENT UPDATES %lld"), Stats.Updates), 24, 124, Muted, 0.58f);
    // WARM-UP shows how much of the chaser's move comes from the network yet (see
    // AChaseDemoGameMode::WarmupStartUpdates); it reads ON once the network has full control.
    const float Control = GameMode->GetPolicyControl();
    const FString LearningText = GameMode->bLearningFrozen ? FString(TEXT("LEARNING: FROZEN"))
        : Control < 1.0f ? FString::Printf(TEXT("LEARNING: WARM-UP %.0f%%"), 100.0f * Control)
        : FString(TEXT("LEARNING: ON"));
    Text(LearningText, 24, 148, GameMode->bLearningFrozen ? Gold : Muted, 0.58f);
    Text(GameMode->bPlayerControlsRunner
        ? TEXT("RUNNER: YOU (WASD)")
        : FString::Printf(TEXT("RUNNER: %s"), PatternName), 24, 176, Cyan, 0.58f);

    Rect(10, 222, 230, 90, Panel);
    Text(TEXT("SPACE PAUSE  F SPEED  L FREEZE"), 24, 232, Muted, 0.48f);
    Text(TEXT("TAB YOU/AI  1 STRAIGHT 2 SQUARE"), 24, 254, Muted, 0.48f);
    Text(TEXT("3 CIRCLE 4 RIGHTTURN 5 CW 6 CORNER"), 24, 276, Muted, 0.48f);

    // Just right of the arena's real on-screen edge (projected, so it follows the camera zoom and
    // any window size), vertically centred on it.
    if (GameMode->IsCatchFlashActive())
    {
        const FVector Edge = Project(GameMode->GetArenaRightEdgeWorld());
        Text(TEXT("CAUGHT!"), Edge.X / S + 16.0f, Edge.Y / S - 14.0f, Gold, 1.3f);
    }
}

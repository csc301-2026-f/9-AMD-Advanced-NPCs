// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdGameMode.h"
#include "CrowdComparisonHost.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"

ACrowdGameMode::ACrowdGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = ACrowdHUD::StaticClass();
}

void ACrowdGameMode::StartPlay()
{
    Host = GetWorld()->SpawnActor<ACrowdComparisonHost>();
    Super::StartPlay();
}

void ACrowdHUD::DrawHUD()
{
    Super::DrawHUD();
    const auto* GameMode = GetWorld()->GetAuthGameMode<ACrowdGameMode>();
    const ACrowdComparisonHost* Host = GameMode ? GameMode->Host.Get() : nullptr;
    if (!Host || !Canvas) return;

    UFont* Font = GEngine->GetLargeFont();
    const float Scale = FMath::Max(Canvas->ClipY / 720.f, .5f);
    const float Line = 30 * Scale, X = 20 * Scale;
    float Y = 16 * Scale;
    const bool bBackground = Host->GetMode() == ECrowdInferenceMode::Background;
    const FLinearColor Accent = bBackground ? FLinearColor(.25f, 1.f, .45f) : FLinearColor(1.f, .55f, .2f);
    const auto& Stats = Host->GetDisplayStats();

    TArray<TPair<FString, FString>> Rows;
    Rows.Emplace(TEXT("Mass agents"), FText::AsNumber(Host->GetAgentCount()).ToString());
    Rows.Emplace(TEXT("Frame rate"), FString::Printf(TEXT("%.1f FPS"), Stats.FramesPerSecond));
    Rows.Emplace(TEXT("Frame time"), FString::Printf(TEXT("%.1f ms  (p99 %.1f ms)"), Stats.FrameMs, Stats.FrameP99Ms));
    Rows.Emplace(TEXT("Inference on game thread"), FString::Printf(TEXT("%.2f ms / frame"), Stats.InferenceMs));
    Rows.Emplace(TEXT("Decisions"), FString::Printf(TEXT("%s / s  (target %s)"),
        *FText::AsNumber(FMath::RoundToInt(Stats.DecisionsPerSecond)).ToString(),
        *FText::AsNumber(FMath::RoundToInt(Host->GetTargetDecisionsPerSecond())).ToString()));
    Rows.Emplace(TEXT("Agents per model call"), FString::Printf(TEXT("%.1f"), Stats.RowsPerModelCall));
    Rows.Emplace(bBackground ? TEXT("Model time on workers") : TEXT("Model time on game thread"),
        FString::Printf(TEXT("%.0f ms / s"), Stats.ModelMsPerSecond));
    Rows.Emplace(TEXT("Observation to action"), FString::Printf(TEXT("%.1f ms"), Stats.LatencyMs));
    if (bBackground) Rows.Emplace(TEXT("Dropped requests"), FText::AsNumber(Stats.DroppedRows).ToString());

    const float Width = 560 * Scale, Height = Line * (Rows.Num() + 3) + 16 * Scale;
    DrawRect(FLinearColor(0, 0, 0, .65f), X - 12 * Scale, Y - 8 * Scale, Width, Height);
    DrawText(bBackground ? TEXT("WITH Schola background NPC API") : TEXT("WITHOUT: per-agent sync inference"),
        Accent, X, Y, Font, Scale * 1.6f);
    Y += Line * 1.5f;
    for (const auto& Row : Rows)
    {
        DrawText(Row.Key, FLinearColor(.75f, .75f, .75f), X, Y, Font, Scale * 1.25f);
        DrawText(Row.Value, FLinearColor::White, X + 260 * Scale, Y, Font, Scale * 1.25f);
        Y += Line;
    }
    FString Phase;
    const double Progress = Host->GetCaptureProgress(Phase);
    const FString Footer = !Host->GetError().IsEmpty() ? TEXT("Error: ") + Host->GetError()
        : Progress >= 0 ? FString::Printf(TEXT("%s %.0f%%"), *Phase, Progress * 100)
        : TEXT("[Tab] switch mode");
    DrawText(Footer, Host->GetError().IsEmpty() ? FLinearColor(.6f, .6f, .6f) : FLinearColor::Red, X, Y + 4 * Scale, Font, Scale * 1.1f);
}

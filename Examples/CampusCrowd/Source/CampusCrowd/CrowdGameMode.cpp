// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "CrowdGameMode.h"
#include "CampusCrowdHost.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UnrealClient.h"

ACrowdGameMode::ACrowdGameMode()
{
	DefaultPawnClass = nullptr;
	HUDClass = ACrowdHUD::StaticClass();
}

void ACrowdGameMode::StartPlay()
{
	Host = GetWorld()->SpawnActor<ACampusCrowdHost>();
	Super::StartPlay();
}

void ACrowdHUD::DrawHUD()
{
	if (FScreenshotRequest::IsScreenshotRequested() && FParse::Param(FCommandLine::Get(), TEXT("CleanScreenshot")))
	{
		return;
	}
	Super::DrawHUD();
	const auto*				GameMode = GetWorld()->GetAuthGameMode<ACrowdGameMode>();
	const ACampusCrowdHost* Host = GameMode ? GameMode->Host.Get() : nullptr;
	if (!Host || !Canvas)
	{
		return;
	}

	UFont*			   Font = GEngine->GetLargeFont();
	const float		   Scale = FMath::Max(Canvas->ClipY / 1080.f, .5f);
	const float		   Line = 30 * Scale, X = 20 * Scale;
	float			   Y = 16 * Scale;
	const FLinearColor Accent(.25f, 1.f, .45f);
	const auto&		   Stats = Host->GetDisplayStats();
	const auto&		   Policy = Stats.Policy;
	const auto		   Number = [](double Value) { return FText::AsNumber(FMath::RoundToInt64(Value)).ToString(); };

	const FLinearColor							   Normal = FLinearColor::White, Alert(1.f, .3f, .25f);
	const bool									   bShort = !Stats.Shortfall.IsEmpty();
	TArray<TTuple<FString, FString, FLinearColor>> Rows;
	Rows.Emplace(TEXT("Mass agents"), Number(Host->GetAgentCount()), Normal);
	Rows.Emplace(TEXT("Frame rate"), FString::Printf(TEXT("%.1f FPS"), Stats.FramesPerSecond), Normal);
	Rows.Emplace(TEXT("Frame time"), FString::Printf(TEXT("%.1f ms  (p99 %.1f ms)"), Stats.FrameMs, Stats.FrameP99Ms), Normal);
	Rows.Emplace(TEXT("Inference on game thread"), FString::Printf(TEXT("%.2f ms / frame"), Stats.InferenceMs), Normal);
	Rows.Emplace(TEXT("Crowd movement"), FString::Printf(TEXT("%.2f ms / frame"), Stats.MoveMs), Normal);
	Rows.Emplace(TEXT("Decisions"), FString::Printf(TEXT("%s / s  (%.0f%% of %s)"), *Number(Policy.DecisionsPerSecond), Policy.TargetFraction * 100, *Number(Host->GetTargetDecisionsPerSecond())), bShort ? Alert : Normal);
	Rows.Emplace(TEXT("Agents per model call"), FString::Printf(TEXT("%.1f"), Policy.RowsPerModelCall), Normal);
	Rows.Emplace(TEXT("Model time on workers"), FString::Printf(TEXT("%.0f ms / s"), Policy.ModelMsPerSecond), Normal);
	Rows.Emplace(TEXT("Observation to action"), FString::Printf(TEXT("%.1f ms"), Policy.LatencyMs), Normal);
	Rows.Emplace(TEXT("Dropped requests"), Number(Stats.DroppedRows), Stats.DroppedRows > 0 ? Alert : Normal);
	if (Policy.Deferred + Policy.Rejected + Policy.Invalid + Policy.Lost() > 0)
	{
		Rows.Emplace(TEXT("Unserved rows / s"), FString::Printf(TEXT("%s deferred, %s stale, %s failed"), *Number(Policy.PerSecond(Policy.Deferred)), *Number(Policy.PerSecond(Policy.Stale)), *Number(Policy.PerSecond(Policy.Expired + Policy.Failed))), Alert);
	}
	if (bShort)
	{
		Rows.Emplace(TEXT("Below target"), Stats.Shortfall, Alert);
	}

	FString		  Phase;
	const double  Progress = Host->GetCaptureProgress(Phase);
	const FString Footer = !Host->GetError().IsEmpty() ? TEXT("Error: ") + Host->GetError()
		: Progress >= 0								   ? FString::Printf(TEXT("%s %.0f%%"), *Phase, Progress * 100)
													   : FString();
	float		  Width = 590 * Scale;
	for (const auto& Row : Rows)
	{
		float TextWidth = 0, TextHeight = 0;
		GetTextSize(Row.Get<1>(), TextWidth, TextHeight, Font, Scale * 1.25f);
		Width = FMath::Max(Width, (260 + 32) * Scale + TextWidth);
	}
	const float Height = Line * (Rows.Num() + (Footer.IsEmpty() ? 2 : 3)) + 16 * Scale;
	DrawRect(FLinearColor(0, 0, 0, .65f), X - 12 * Scale, Y - 8 * Scale, Width, Height);
	DrawText(TEXT("CAMPUS / background inference"), Accent, X, Y, Font, Scale * 1.6f);
	Y += Line * 1.5f;
	for (const auto& Row : Rows)
	{
		DrawText(Row.Get<0>(), FLinearColor(.75f, .75f, .75f), X, Y, Font, Scale * 1.25f);
		DrawText(Row.Get<1>(), Row.Get<2>(), X + 260 * Scale, Y, Font, Scale * 1.25f);
		Y += Line;
	}
	if (!Footer.IsEmpty())
	{
		DrawText(Footer, Host->GetError().IsEmpty() ? FLinearColor(.6f, .6f, .6f) : FLinearColor::Red, X, Y + 4 * Scale, Font, Scale * 1.1f);
	}
	const float Bottom = Canvas->ClipY - 64 * Scale;
	DrawRect(FLinearColor(.015f, .025f, .04f, .85f), 0, Bottom, Canvas->ClipX, 64 * Scale);
	DrawText(TEXT("50H NORTH / UNIVERSITY CAMPUS"), FLinearColor(.75f, .85f, .83f), X, Bottom + 6 * Scale, Font, Scale);
	DrawText(TEXT("1/2/3 cameras   WASD + Q/E fly   RMB look   Shift fast   Space pause   N routes"),
		FLinearColor(.7f, .73f, .76f), X, Bottom + 32 * Scale, Font, Scale * .9f);
}

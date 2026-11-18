// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "CrowdStats.h"
#include "CrowdTypes.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "RHIGlobals.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	double Quantile(TArray<double> Values, double Q)
	{
		if (Values.IsEmpty())
		{
			return 0;
		}
		Values.Sort();
		return Values[FMath::Clamp(FMath::CeilToInt(Q * Values.Num()) - 1, 0, Values.Num() - 1)];
	}

	double Mean(const TArray<double>& Values)
	{
		double Sum = 0;
		for (double Value : Values)
		{
			Sum += Value;
		}
		return Values.IsEmpty() ? 0 : Sum / Values.Num();
	}

	/** Frames kept for the HUD's p99 frame time. */
	constexpr int32 RecentFrames = 600;

	/** Windows after start that include pipeline fill and are not judged against the target. */
	constexpr int32 FillWindows = 2;
} // namespace

void FCrowdStats::Start(const FScholaMassPolicy& InPolicy, const FString& InReportPath, double InWarmupSeconds, double InCaptureSeconds)
{
	Policy = InPolicy;
	ReportPath = InReportPath;
	WarmupSeconds = InWarmupSeconds;
	CaptureSeconds = InCaptureSeconds;
	Started = WindowStart = FPlatformTime::Seconds();
	StartPolicyStats = WindowPolicyStart = Policy.GetStats();
}

void FCrowdStats::AddFrame(double Now, double FrameMs, double InferenceMs, double MoveMs)
{
	if (!ReportPath.IsEmpty() && CaptureSeconds > 0 && !bReported)
	{
		if (!bCapturing && Now - Started >= WarmupSeconds)
		{
			bCapturing = true;
			CapturePolicyStart = Policy.GetStats();
		}
		else if (bCapturing)
		{
			CaptureFrameMs.Add(FrameMs);
			CaptureInferenceMs.Add(InferenceMs);
			CaptureMoveMs.Add(MoveMs);
		}
	}

	++WindowFrames;
	WindowFrameMs += FrameMs;
	WindowInferenceMs += InferenceMs;
	WindowMoveMs += MoveMs;
	RecentFrameMs.Add(FrameMs);
	if (RecentFrameMs.Num() > RecentFrames)
	{
		RecentFrameMs.RemoveAt(0, RecentFrameMs.Num() - RecentFrames, EAllowShrinking::No);
	}
	if (Now - WindowStart < 1)
	{
		return;
	}

	const FScholaMassPolicyStats  PolicyNow = Policy.GetStats();
	const FScholaMassPolicyWindow SinceStart = PolicyNow.Since(StartPolicyStats);
	Display.Policy = PolicyNow.Since(WindowPolicyStart);
	Display.FramesPerSecond = WindowFrames / (Now - WindowStart);
	Display.FrameMs = WindowFrameMs / WindowFrames;
	Display.FrameP99Ms = Quantile(RecentFrameMs, .99);
	Display.InferenceMs = WindowInferenceMs / WindowFrames;
	Display.MoveMs = WindowMoveMs / WindowFrames;
	Display.DroppedRows = SinceStart.Lost() + SinceStart.Rejected + SinceStart.Invalid;

	if (++WindowCount > FillWindows)
	{
		if (!Display.Policy.MeetsTarget())
		{
			Display.Shortfall = Display.Policy.DescribeShortfall();
			if (!bShortfallLogged)
			{
				UE_LOG(LogCampusCrowd, Warning, TEXT("Crowd: delivering %.0f%% of the target %.0f decisions/s; limited by %s"),
					Display.Policy.TargetFraction * 100, Display.Policy.TargetDecisionsPerSecond, *Display.Shortfall);
			}
			bShortfallLogged = true;
		}
		else
		{
			if (bShortfallLogged)
			{
				UE_LOG(LogCampusCrowd, Display, TEXT("Crowd: target decision rate met again"));
			}
			Display.Shortfall.Reset();
			bShortfallLogged = false;
		}
	}
	WindowPolicyStart = PolicyNow;
	WindowStart = Now;
	WindowFrames = 0;
	WindowFrameMs = WindowInferenceMs = WindowMoveMs = 0;
}

bool FCrowdStats::IsCaptureComplete() const
{
	return bCapturing && !bReported && FPlatformTime::Seconds() - CapturePolicyStart.Time >= CaptureSeconds;
}

double FCrowdStats::GetCaptureProgress(FString& OutPhase) const
{
	if (ReportPath.IsEmpty() || CaptureSeconds <= 0)
	{
		return -1;
	}
	const double Now = FPlatformTime::Seconds();
	if (!bCapturing)
	{
		OutPhase = TEXT("Warming up");
		return FMath::Clamp((Now - Started) / FMath::Max(WarmupSeconds, .01), 0., 1.);
	}
	OutPhase = TEXT("Measuring");
	return FMath::Clamp((Now - CapturePolicyStart.Time) / CaptureSeconds, 0., 1.);
}

int32 FCrowdStats::WriteReport(const TSharedRef<FJsonObject>& Root)
{
	bReported = true;
	const FScholaMassPolicyStats  PolicyNow = Policy.GetStats();
	const FScholaMassPolicyWindow Window = PolicyNow.Since(CapturePolicyStart);
	const double				  Elapsed = FMath::Max(Window.Seconds, 1e-6);
	const bool					  bTargetMet = Window.MeetsTarget();
	const FString				  Shortfall = bTargetMet ? FString() : Window.DescribeShortfall();
	const FNPCBackgroundStats&	  Scheduler = PolicyNow.Scheduler;
	const FNPCBackgroundStats&	  SchedulerStart = CapturePolicyStart.Scheduler;

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
	Root->SetNumberField(TEXT("movement_ms_mean"), Mean(CaptureMoveMs));
	Root->SetNumberField(TEXT("target_decisions_per_second"), Window.TargetDecisionsPerSecond);
	Root->SetNumberField(TEXT("decisions_per_second"), Window.DecisionsPerSecond);
	Root->SetNumberField(TEXT("target_fraction"), Window.TargetFraction);
	Root->SetBoolField(TEXT("target_met"), bTargetMet);
	if (!bTargetMet)
	{
		Root->SetStringField(TEXT("shortfall"), Shortfall);
	}
	Root->SetNumberField(TEXT("action_latency_ms_mean"), Window.LatencyMs);
	Root->SetNumberField(TEXT("workers"), Window.WorkerCount);
	Root->SetNumberField(TEXT("max_batch"), Window.MaxBatchSize);
	Root->SetNumberField(TEXT("max_rows_per_frame"), Window.MaxRowsPerFrame);
	Root->SetNumberField(TEXT("max_pending_rows"), Window.MaxPendingRows);
	Root->SetNumberField(TEXT("rows_per_model_call"), Window.RowsPerModelCall);
	Root->SetNumberField(TEXT("model_calls"), double(Scheduler.BackendCalls - SchedulerStart.BackendCalls));
	Root->SetNumberField(TEXT("model_ms_per_second"), Window.ModelMsPerSecond);
	Root->SetNumberField(TEXT("dropped_rows"), double(Window.Rejected + Window.Lost()));
	Root->SetNumberField(TEXT("submitted_requests"), double(Scheduler.SubmittedRequests - SchedulerStart.SubmittedRequests));
	Root->SetNumberField(TEXT("deferred_rows"), double(Window.Deferred));
	Root->SetNumberField(TEXT("rejected_submissions"), double(Window.Rejected + Window.Invalid));
	Root->SetNumberField(TEXT("busy_rejections"), double(Window.Rejected));
	Root->SetNumberField(TEXT("invalid_submissions"), double(Window.Invalid));
	Root->SetNumberField(TEXT("expired_rows"), double(Window.Expired));
	Root->SetNumberField(TEXT("failed_rows"), double(Window.Failed));
	Root->SetNumberField(TEXT("stale_results_discarded"), double(Window.Stale));
	Root->SetStringField(TEXT("cpu"), FPlatformMisc::GetCPUBrand().TrimStartAndEnd());
	Root->SetStringField(TEXT("gpu"), GRHIAdapterName);
	TArray<TSharedPtr<FJsonValue>> Frames;
	for (double Value : CaptureFrameMs)
	{
		Frames.Add(MakeShared<FJsonValueNumber>(FMath::RoundToDouble(Value * 1000) / 1000));
	}
	Root->SetArrayField(TEXT("frame_ms"), Frames);

	FString Json;
	FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true);
	const bool bSaved = FFileHelper::SaveStringToFile(Json, *ReportPath);
	UE_LOG(LogCampusCrowd, Display, TEXT("Crowd report %s: %s"), bSaved ? TEXT("written") : TEXT("FAILED"), *ReportPath);
	if (!bTargetMet)
	{
		UE_LOG(LogCampusCrowd, Error, TEXT("Crowd: target decision rate missed: %.0f of %.0f decisions/s (%.0f%%); limited by %s"),
			Window.DecisionsPerSecond, Window.TargetDecisionsPerSecond, Window.TargetFraction * 100, *Shortfall);
	}
	// Exit 3: report written, but the crowd did not receive the decisions it asked for.
	return !bSaved ? 1 : bTargetMet ? 0
									: 3;
}

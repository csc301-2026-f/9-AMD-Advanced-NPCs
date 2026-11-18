// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ScholaMassPolicy.h"

class FJsonObject;

/** @brief Values shown on the HUD, refreshed once per second. */
struct FCrowdDisplayStats
{
	double FramesPerSecond = 0;
	double FrameMs = 0;
	double FrameP99Ms = 0;
	/** @brief Game-thread time in ScholaMass apply and gather, including the crowd's own observation and action code. */
	double InferenceMs = 0;
	/** @brief Movement and separation update on the game thread (parallel across chunks). */
	double MoveMs = 0;
	/** @brief Rows dropped since the crowd started. */
	int64 DroppedRows = 0;
	/** @brief Decision rate, latency, model time and losses in the last window. */
	FScholaMassPolicyWindow Policy;
	/** @brief Empty when the target rate is met; otherwise the most likely limiting factors. */
	FString Shortfall;
};

/**
 * @brief Frame and policy statistics for the crowd: a rolling one-second HUD window and an
 * optional timed capture written as a JSON report.
 */
class FCrowdStats
{
public:
	/**
	 * @brief Starts both windows. A capture runs only when ReportPath is set and CaptureSeconds > 0.
	 * @param[in] Policy The crowd's policy.
	 * @param[in] ReportPath Where WriteReport saves the capture.
	 * @param[in] WarmupSeconds Time from Start before the capture begins.
	 * @param[in] CaptureSeconds Length of the measured capture.
	 */
	void Start(const FScholaMassPolicy& Policy, const FString& ReportPath, double WarmupSeconds, double CaptureSeconds);

	/** @brief Records one frame's game-thread timings and refreshes the HUD values once per second. */
	void AddFrame(double Now, double FrameMs, double InferenceMs, double MoveMs);

	const FCrowdDisplayStats& GetDisplay() const { return Display; }

	/** @brief True once the capture has run its full length and the report has not been written yet. */
	bool IsCaptureComplete() const;

	/** @brief Capture progress in [0, 1] and its phase name, or -1 when no capture was requested. */
	double GetCaptureProgress(FString& OutPhase) const;

	/**
	 * @brief Adds the capture's measurements to Root and saves it to the report path.
	 * @param[in] Root Report object holding the caller's scene fields.
	 * @return Process exit status: 0 on success, 1 if the file could not be saved, 3 if the target rate was missed.
	 */
	int32 WriteReport(const TSharedRef<FJsonObject>& Root);

private:
	FScholaMassPolicy  Policy;
	FCrowdDisplayStats Display;

	// Rolling one-second display window.
	double				   WindowStart = 0;
	int32				   WindowFrames = 0;
	double				   WindowFrameMs = 0;
	double				   WindowInferenceMs = 0;
	double				   WindowMoveMs = 0;
	int32				   WindowCount = 0;
	bool				   bShortfallLogged = false;
	TArray<double>		   RecentFrameMs;
	FScholaMassPolicyStats WindowPolicyStart;
	FScholaMassPolicyStats StartPolicyStats;

	// Optional measured capture.
	FString				   ReportPath;
	double				   Started = 0;
	double				   WarmupSeconds = 0;
	double				   CaptureSeconds = 0;
	bool				   bCapturing = false;
	bool				   bReported = false;
	TArray<double>		   CaptureFrameMs;
	TArray<double>		   CaptureInferenceMs;
	TArray<double>		   CaptureMoveMs;
	FScholaMassPolicyStats CapturePolicyStart;
};

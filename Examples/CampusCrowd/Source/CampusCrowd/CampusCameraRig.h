// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CampusCameraRig.generated.h"

class ACameraActor;
class ACampusScene;
class APlayerController;

/**
 * @brief The demo's view of the campus: camera presets, the recorded flight, free-fly controls and the route overlay.
 *
 * Keys: 1/2/3 presets, 4 flight, R reset, N routes, WASD + Q/E fly, Shift fast, right mouse look.
 */
UCLASS()
class UCampusCameraRig : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief Spawns the camera and makes it the player's view.
	 * @param[in] InCampus The campus whose routes the presets and overlay use.
	 * @param[in] Preset Starting preset, 0 to 3 (3 is the flight).
	 * @param[in] InFlightSeconds Length of the flight.
	 */
	void Initialize(ACampusScene& InCampus, int32 Preset, double InFlightSeconds);

	/** @brief Handles camera input and moves the camera for this frame. */
	void Update(float DeltaSeconds);

	/**
	 * @brief Grows distant students so each stays a few pixels wide; nearby ones keep their real size.
	 * Call after Update so it uses this frame's camera.
	 */
	void KeepVisible(TArray<FTransform>& Transforms) const;

private:
	APlayerController* GetPlayer() const;
	void			   ApplyPreset();
	void			   DrawRoutes() const;

	UPROPERTY()
	TObjectPtr<ACameraActor> Camera;

	UPROPERTY()
	TObjectPtr<ACampusScene> Campus;

	float  HalfSize = 0;
	int32  CameraPreset = 0;
	int32  PreviousCameraPreset = -1;
	double FlightStarted = 0;
	double FlightSeconds = 10;
	bool   bShowRoutes = false;
};

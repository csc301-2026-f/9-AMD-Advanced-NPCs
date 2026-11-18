// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.

#include "CampusCameraRig.h"
#include "CampusScene.h"
#include "CrowdTypes.h"
#include "Async/ParallelFor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
	/** One key of the flight: camera position and the point it looks at. */
	struct FFlightKey
	{
		double	Time;
		FVector Location;
		FVector Target;
	};

	// A flight (keys in seconds at the default ten-second length) checked against
	// the CAD roof heights: it starts 22 m over the front lawn crowd, crosses the
	// road towards Queen's Park, banks over the park and pulls back until the
	// whole campus model fills the frame below the HUD panels.
	const FFlightKey FlightKeys[] = {
		{ 0.0, FVector(-3000, 17000, 2200), FVector(-11000, 9000, 0) },
		{ 3.0, FVector(4000, 8000, 4500), FVector(22000, 2000, 0) },
		{ 6.0, FVector(27000, 12000, 14000), FVector(4000, -14000, 0) },
		{ 10.0, FVector(38800, 164600, 152700), FVector(-13400, 21200, 0) },
	};

	FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, double U)
	{
		return .5 * (2 * P1 + (P2 - P0) * U + (2 * P0 - 5 * P1 + 4 * P2 - P3) * U * U + (3 * P1 - P0 - 3 * P2 + P3) * U * U * U);
	}

	void EvaluateFlight(double Seconds, FVector& OutLocation, FVector& OutTarget)
	{
		constexpr int32 Last = UE_ARRAY_COUNT(FlightKeys) - 1;
		const double	Time = FMath::Clamp(Seconds, 0.0, FlightKeys[Last].Time);
		int32			I = 0;
		while (I < Last - 1 && Time >= FlightKeys[I + 1].Time)
		{
			++I;
		}
		const double	  U = (Time - FlightKeys[I].Time) / (FlightKeys[I + 1].Time - FlightKeys[I].Time);
		const FFlightKey& K0 = FlightKeys[FMath::Max(I - 1, 0)];
		const FFlightKey& K1 = FlightKeys[I];
		const FFlightKey& K2 = FlightKeys[I + 1];
		// Mirroring the previous key past the end gives zero velocity there, so the flight settles on the overview.
		const FFlightKey& K3 = FlightKeys[I + 2 <= Last ? I + 2 : I];
		OutLocation = CatmullRom(K0.Location, K1.Location, K2.Location, K3.Location, U);
		OutTarget = CatmullRom(K0.Target, K1.Target, K2.Target, K3.Target, U);
	}

	/** Where the campus street view is centred. */
	const FVector2f StreetViewAnchor(-15000, 18000);

	/** University College, the landmark checked against the recovered CAD regions. */
	const FVector UniversityCollege(-1000, 22500, 1000);
} // namespace

void UCampusCameraRig::Initialize(ACampusScene& InCampus, int32 Preset, double InFlightSeconds)
{
	Campus = &InCampus;
	HalfSize = InCampus.Extent;
	CameraPreset = FMath::Clamp(Preset, 0, 3);
	FlightSeconds = FMath::Max(InFlightSeconds, 1.0);

	UWorld* World = InCampus.GetWorld();
	Camera = World->SpawnActor<ACameraActor>(FVector(0, HalfSize * 1.55, HalfSize * 1.25), FRotator::ZeroRotator);
	Camera->SetActorRotation((FVector(0, -HalfSize * .1, 0) - Camera->GetActorLocation()).Rotation());
	Camera->GetCameraComponent()->SetFieldOfView(55);
	Camera->GetCameraComponent()->bConstrainAspectRatio = false;
	if (APlayerController* Player = World->GetFirstPlayerController())
	{
		Player->SetViewTarget(Camera);
	}
}

APlayerController* UCampusCameraRig::GetPlayer() const
{
	return Camera ? Camera->GetWorld()->GetFirstPlayerController() : nullptr;
}

void UCampusCameraRig::Update(float DeltaSeconds)
{
	APlayerController* Player = GetPlayer();
	if (!Player || !Campus)
	{
		return;
	}
	const FKey PresetKeys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four };
	for (int32 Preset = 0; Preset < UE_ARRAY_COUNT(PresetKeys); ++Preset)
	{
		if (Player->WasInputKeyJustPressed(PresetKeys[Preset]))
		{
			CameraPreset = Preset;
			// Pressing 4 again restarts the flight.
			if (Preset == 3)
			{
				PreviousCameraPreset = -1;
			}
		}
	}
	if (Player->WasInputKeyJustPressed(EKeys::R))
	{
		Camera->SetActorLocation(FVector(0, HalfSize * 1.55, HalfSize * 1.25));
		CameraPreset = 0;
	}
	if (Player->WasInputKeyJustPressed(EKeys::N))
	{
		bShowRoutes = !bShowRoutes;
	}
	if (bShowRoutes)
	{
		DrawRoutes();
	}
	if (PreviousCameraPreset != CameraPreset)
	{
		ApplyPreset();
	}

	if (CameraPreset == 3)
	{
		// Wall time keeps the flight independent of frame rate.
		FVector		 Location, Target;
		const double KeyTime = FlightKeys[UE_ARRAY_COUNT(FlightKeys) - 1].Time;
		EvaluateFlight((FPlatformTime::Seconds() - FlightStarted) * KeyTime / FlightSeconds, Location, Target);
		Camera->SetActorLocation(Location);
		Camera->SetActorRotation((Target - Location).Rotation());
		return;
	}

	const FVector Forward = Camera->GetActorForwardVector();
	const FVector Right = Camera->GetActorRightVector();
	FVector		  Motion = FVector::ZeroVector;
	Motion += Player->IsInputKeyDown(EKeys::W) ? Forward : FVector::ZeroVector;
	Motion -= Player->IsInputKeyDown(EKeys::S) ? Forward : FVector::ZeroVector;
	Motion += Player->IsInputKeyDown(EKeys::D) ? Right : FVector::ZeroVector;
	Motion -= Player->IsInputKeyDown(EKeys::A) ? Right : FVector::ZeroVector;
	Motion.Z += Player->IsInputKeyDown(EKeys::E) ? 1 : 0;
	Motion.Z -= Player->IsInputKeyDown(EKeys::Q) ? 1 : 0;
	Camera->AddActorWorldOffset(Motion * DeltaSeconds * (Player->IsInputKeyDown(EKeys::LeftShift) ? 18000 : 4000));

	float MouseX = 0, MouseY = 0;
	Player->GetInputMouseDelta(MouseX, MouseY);
	if (Player->IsInputKeyDown(EKeys::RightMouseButton))
	{
		FRotator Rotation = Camera->GetActorRotation();
		Rotation.Yaw += MouseX * .15f;
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch + MouseY * .15f, -85.f, 15.f);
		Camera->SetActorRotation(Rotation);
	}
}

void UCampusCameraRig::ApplyPreset()
{
	// The street view stands at the route node nearest the anchor and looks along its first link.
	FVector Center = FVector::ZeroVector;
	FVector StreetLook = FVector::ZeroVector;
	float	Closest = MAX_flt;
	for (const FCampusNode& Node : Campus->Nodes)
	{
		const float Distance = (Node.Position - StreetViewAnchor).SizeSquared();
		if (Distance < Closest)
		{
			Closest = Distance;
			Center = FVector(Node.Position.X, Node.Position.Y, 80);
			const FCampusNode& Neighbour = Campus->Nodes[Node.Links[0]];
			StreetLook = FVector(Neighbour.Position.X, Neighbour.Position.Y, 150);
		}
	}
	if (CameraPreset == 1)
	{
		Center = UniversityCollege;
	}
	FlightStarted = FPlatformTime::Seconds();
	if (CameraPreset == 3)
	{
		// Scripts/render-video.sh waits for this line before it starts capturing.
		UE_LOG(LogCampusCrowd, Display, TEXT("Camera flight started"));
	}

	const FVector Locations[] = { FVector(0, HalfSize * 1.55, HalfSize * 1.25), Center + FVector(0, 12000, 9000), Center + FVector(0, 0, 120), Center };
	const FVector Targets[] = { FVector::ZeroVector, Center, StreetLook, Center };
	Camera->SetActorLocation(Locations[CameraPreset]);
	Camera->SetActorRotation((Targets[CameraPreset] - Camera->GetActorLocation()).Rotation());
	PreviousCameraPreset = CameraPreset;
}

void UCampusCameraRig::DrawRoutes() const
{
	UWorld* World = Camera->GetWorld();
	for (int32 I = 0; I < Campus->Nodes.Num(); ++I)
	{
		const FCampusNode& Node = Campus->Nodes[I];
		for (int32 J : Node.Links)
		{
			if (J > I)
			{
				const FCampusNode& End = Campus->Nodes[J];
				DrawDebugLine(World, FVector(Node.Position.X, Node.Position.Y, 30), FVector(End.Position.X, End.Position.Y, 30),
					FColor(50, 230, 205), false, -1, 0, 5);
			}
		}
	}
}

void UCampusCameraRig::KeepVisible(TArray<FTransform>& Transforms) const
{
	// A 60 cm student is under a pixel wide from a few hundred metres and
	// disappears under temporal AA. Distant students grow so each stays at
	// least MinPixels wide; nearby ones keep their real 60 x 180 cm size.
	constexpr float			 Width = 60, Height = 180, MinPixels = 3;
	const APlayerController* Player = GetPlayer();
	if (!Player)
	{
		return;
	}
	int32 ViewWidth = 0, ViewHeight = 0;
	Player->GetViewportSize(ViewWidth, ViewHeight);
	if (ViewWidth <= 0)
	{
		return;
	}
	const FVector Eye = Camera->GetActorLocation();
	const float	  HalfFov = FMath::DegreesToRadians(Camera->GetCameraComponent()->FieldOfView) / 2;
	const float	  ScalePerCm = MinPixels * 2 * FMath::Tan(HalfFov) / ViewWidth / Width;
	const auto	  Rescale = [&](int32 I) {
		FTransform& Transform = Transforms[I];
		FVector		Location = Transform.GetLocation();
		Location.Z = Height / 2;
		const float Scale = FMath::Max(1.f, float(FVector::Dist(Eye, Location)) * ScalePerCm);
		Location.Z = 4 + Height / 2 * Scale;
		Transform.SetLocation(Location);
		Transform.SetScale3D(FVector(Width, Width, Height) * (Scale / 100));
	};
	ParallelFor(Transforms.Num(), Rescale, Transforms.Num() < 4096 ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
}

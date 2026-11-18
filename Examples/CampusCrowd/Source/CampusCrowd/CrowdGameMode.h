// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "CrowdGameMode.generated.h"

class ACampusCrowdHost;

/** Spawns the campus crowd host; a fly camera provides the view. */
UCLASS()
class ACrowdGameMode : public AGameModeBase
{
	GENERATED_BODY()
public:
	ACrowdGameMode();
	virtual void StartPlay() override;
	UPROPERTY()
	TObjectPtr<ACampusCrowdHost> Host;
};

/** Statistics overlay for the background inference crowd. */
UCLASS()
class ACrowdHUD : public AHUD
{
	GENERATED_BODY()
public:
	virtual void DrawHUD() override;
};

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#pragma once
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "CrowdGameMode.generated.h"

class ACrowdComparisonHost;

/** Spawns the campus crowd host; a fly camera provides the view. */
UCLASS()
class ACrowdGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ACrowdGameMode();
    virtual void StartPlay() override;
    UPROPERTY() TObjectPtr<ACrowdComparisonHost> Host;
};

/** Statistics overlay shown in both modes. */
UCLASS()
class ACrowdHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Styled to match TagArenaHUD (Examples/AdaptiveTag): dark panel, cyan/muted accents, same
// scale-independent layout pattern, at the user's request for a consistent team look.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ChaseDemoHUD.generated.h"

UCLASS()
class NATIVELEARNINGHOST_API AChaseDemoHUD : public AHUD
{
    GENERATED_BODY()

protected:
    void DrawHUD() override;
};

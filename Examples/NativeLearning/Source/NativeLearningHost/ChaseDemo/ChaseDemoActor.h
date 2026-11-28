// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
// Plain colored circle (engine basic-shape Sphere + a dynamic color on the engine's own default
// BasicShapeMaterial) -- no custom Content/ materials needed, just two visibly distinct colors.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChaseDemoActor.generated.h"

class UMaterialInstanceDynamic;

UCLASS()
class NATIVELEARNINGHOST_API AChaseDemoActor : public AActor
{
    GENERATED_BODY()

public:
    AChaseDemoActor();

    /** Tints the shared basic-shape material via a per-instance dynamic material instance, so
     * chaser and runner read as distinct colors without needing any Content/ asset.
     */
    void SetRunnerAppearance(bool bRunner);

    /** Sets both the component transform and State's 2D position in one call, so the two can
     * never silently drift apart (board plane is Z=0, X/Y carry the 2D position).
     */
    void SetBoardPosition(const FVector2D& Position);

    /** Swaps to solid white (bFlash=true) or back to the role color set by SetRunnerAppearance
     * (bFlash=false), for the catch flash. Never anything in between -- the GameMode owns timing.
     */
    void SetFlashWhite(bool bFlash);

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UStaticMeshComponent> Mesh;

    UPROPERTY()
    TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;

    FLinearColor RoleColor = FLinearColor::White;
};

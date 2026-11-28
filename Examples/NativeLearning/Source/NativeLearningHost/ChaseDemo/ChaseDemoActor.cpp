// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
#include "ChaseDemoActor.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

AChaseDemoActor::AChaseDemoActor()
{
    PrimaryActorTick.bCanEverTick = false;

    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    RootComponent = Mesh;
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (SphereFinder.Object)
    {
        Mesh->SetStaticMesh(SphereFinder.Object);
    }
    // The engine Sphere mesh's own default material slot isn't guaranteed to already hold
    // BasicShapeMaterial; CreateAndSetMaterialInstanceDynamic only tints whatever's already
    // assigned, so without this call it rendered as the "no material" checkerboard instead.
    if (BasicMaterialFinder.Object)
    {
        Mesh->SetMaterial(0, BasicMaterialFinder.Object);
    }
}

void AChaseDemoActor::SetRunnerAppearance(bool bRunner)
{
    // BasicShapeMaterial is the engine's own default material for Cube/Sphere/Cylinder and
    // exposes a "Color" vector parameter -- no Content/ asset needed for two distinct colors.
    RoleColor = bRunner
        ? FLinearColor(0.25f, 0.95f, 0.65f)  // runner: mint green
        : FLinearColor(1.0f, 0.32f, 0.38f);  // chaser: coral red
    DynamicMaterial = Mesh->CreateAndSetMaterialInstanceDynamic(0);
    if (DynamicMaterial)
    {
        DynamicMaterial->SetVectorParameterValue(TEXT("Color"), RoleColor);
    }
}

void AChaseDemoActor::SetBoardPosition(const FVector2D& Position)
{
    SetActorLocation(FVector(Position.X, Position.Y, 0.0f));
}

void AChaseDemoActor::SetFlashWhite(bool bFlash)
{
    if (DynamicMaterial)
    {
        DynamicMaterial->SetVectorParameterValue(TEXT("Color"), bFlash ? FLinearColor::White : RoleColor);
    }
}

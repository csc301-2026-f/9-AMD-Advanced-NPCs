// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class CampusCrowd : ModuleRules
{
    public CampusCrowd(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] {
            "ProceduralMeshComponent", "InputCore", "Json", "NNE", "RHI", "MassCore", "MassEntity",
            "Schola", "ScholaInferenceUtils", "ScholaNNE", "ScholaMass"
        });
    }
}

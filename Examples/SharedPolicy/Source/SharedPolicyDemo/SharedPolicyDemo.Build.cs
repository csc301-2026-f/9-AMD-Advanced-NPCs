// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class SharedPolicyDemo : ModuleRules
{
    public SharedPolicyDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "InputCore", "NNE", "Schola", "ScholaInferenceUtils", "ScholaNNE" });
    }
}

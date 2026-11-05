// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

// Thin wrapper: compiles the single copy of OnlineSACRuntime that lives in Examples/AdaptiveTag,
// because one Unreal project cannot reference another project's modules.
public class OnlineSACRuntime : ModuleRules
{
    public OnlineSACRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        bEnableExceptions = false;
        bUseRTTI = false;
        CppStandard = CppStandardVersion.Cpp20;
        FPSemantics = FPSemanticsMode.Precise;
        PrivateDependencyModuleNames.Add("Core");
        string AdaptiveTag = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../AdaptiveTag"));
        string Shared = Path.Combine(AdaptiveTag, "Source/OnlineSACRuntime");
        PublicIncludePaths.Add(Path.Combine(Shared, "Public"));
        PrivateIncludePaths.Add(Path.Combine(Shared, "Private"));
        PrivateDependencyModuleNames.Add("RLTools");
        PrivateDefinitions.AddRange(new string[] { "NOMINMAX", "RL_TOOLS_DEBUG" });
    }
}

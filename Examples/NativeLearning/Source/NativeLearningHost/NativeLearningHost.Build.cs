// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;
public class NativeLearningHost : ModuleRules
{
    public NativeLearningHost(ReadOnlyTargetRules Target) : base(Target)
    {
        // The chase test compiles the plain-C++ chase simulation from Examples/AdaptiveTag
        // (Private/ChaseSources.cpp), so precompiled headers and unity files are off, as in
        // Examples/AdaptiveSAC's OnlineSACRuntime wrapper.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        CppStandard = CppStandardVersion.Cpp20;
        FPSemantics = FPSemanticsMode.Precise;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Schola", "ScholaInferenceUtils", "ScholaRLTools" });
        string Chase = Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../AdaptiveTag/Source/OnlineSACRuntime"));
        PrivateIncludePaths.Add(Path.Combine(Chase, "Public"));
        PrivateDefinitions.Add("ONLINESACRUNTIME_API=");
    }
}

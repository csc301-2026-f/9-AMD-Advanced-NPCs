// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

public class ScholaRLTools : ModuleRules
{
    public ScholaRLTools(ReadOnlyTargetRules Target) : base(Target)
    {
        // RLtools operation definitions must not be merged into unrelated translation units.
        PCHUsage = PCHUsageMode.NoPCHs;
        bUseUnity = false;
        bEnableExceptions = false;
        bUseRTTI = false;
        CppStandard = CppStandardVersion.Cpp20;
        FPSemantics = FPSemanticsMode.Precise;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Schola", "ScholaInferenceUtils" });
        PrivateDependencyModuleNames.Add("RLTools");
        string RevisionFile = Path.GetFullPath(Path.Combine(ModuleDirectory, "../ThirdParty/RLTools/REVISION"));
        PrivateDefinitions.Add("SCHOLA_RLTOOLS_REVISION=\"" + File.ReadAllText(RevisionFile).Trim() + "\"");
        ExternalDependencies.Add(RevisionFile);
    }
}

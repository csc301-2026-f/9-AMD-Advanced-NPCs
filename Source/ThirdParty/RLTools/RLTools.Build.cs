// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using System.IO;
using UnrealBuildTool;

/** Header-only, pinned numerical dependency. Consumers own their compilation settings. */
public class RLTools : ModuleRules
{
    public RLTools(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        const string Revision = "b32d9985c65a5e098a6bbf190fd994962d288b99";
        string RevisionFile = Path.Combine(ModuleDirectory, "REVISION");
        string LicenseFile = Path.Combine(ModuleDirectory, "LICENSE");
        if (!File.Exists(RevisionFile) || File.ReadAllText(RevisionFile).Trim() != Revision
            || !File.Exists(Path.Combine(ModuleDirectory, "include/rl_tools/operations/cpu_mux.h"))
            || !File.Exists(LicenseFile))
        {
            throw new BuildException("Restore the pinned RLtools headers, REVISION and LICENSE under Source/ThirdParty/RLTools.");
        }
        PublicSystemIncludePaths.Add(Path.Combine(ModuleDirectory, "include"));
        PublicDefinitions.Add("NOMINMAX");
        RuntimeDependencies.Add("$(TargetOutputDir)/RLTools-LICENSE.txt", LicenseFile, StagedFileType.NonUFS);
        ExternalDependencies.Add(RevisionFile);
        ExternalDependencies.Add(LicenseFile);
    }
}

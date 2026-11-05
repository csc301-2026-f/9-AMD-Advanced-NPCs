// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class CampusCrowdEditorTarget : TargetRules
{
    public CampusCrowdEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("CampusCrowd");
    }
}

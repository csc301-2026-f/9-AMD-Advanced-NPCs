// Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
using UnrealBuildTool;

public class AdaptiveSACTarget : TargetRules
{
    public AdaptiveSACTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("OnlineSACSchola");
    }
}

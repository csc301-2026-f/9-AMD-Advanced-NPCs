# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
# Builds AdaptiveSACEditor (Schola enabled from the repository root) and runs the automation tests headless.
param([string]$Filter='ScholaOnlineLearning.SAC.Policy', [string]$EnginePath)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../AdaptiveTag/Tools/Unreal.ps1')
$engine = Resolve-UnrealEngine -EnginePath $EnginePath
$project = Join-Path $PSScriptRoot 'AdaptiveSAC.uproject'
& (Join-Path $PSScriptRoot 'Prepare-AdaptiveSAC.ps1')
& "$engine\Engine\Build\BatchFiles\Build.bat" AdaptiveSACEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE
if ($LASTEXITCODE -ne 0) { throw 'AdaptiveSAC build failed. Check the Unreal build output above.' }
$saved = Join-Path $PSScriptRoot 'Saved/OnlineSAC'
New-Item -ItemType Directory -Force $saved | Out-Null
$log = Join-Path $saved 'UnrealAutomation.log'
$editor = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editor $project "-ExecCmds=Automation RunTests $Filter; Quit" -unattended -nullrhi -nosound -nop4 -nosplash "-abslog=$log" -stdout -FullStdOutLogOutput | Out-Null
$results = Select-String -LiteralPath $log -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}'
if (!$results) { throw "No automation results found. See $log" }
$failed = $false
foreach ($r in $results) {
    "$($r.Matches[0].Groups[2].Value): $($r.Matches[0].Groups[1].Value)"
    if ($r.Matches[0].Groups[1].Value -ne 'Success') { $failed = $true }
}
if ($failed) { throw "Automation tests failed. See $log" }

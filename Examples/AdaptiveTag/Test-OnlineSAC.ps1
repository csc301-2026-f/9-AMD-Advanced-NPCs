# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
# Runs the OnlineSACRuntime tests. Native: no Unreal (uses CMake if installed, else MSVC directly).
# Unreal: builds CSC301HostEditor and runs the ScholaOnlineLearning.SAC automation tests headless.
param([ValidateSet('Native','Unreal')][string]$Mode='Native', [string]$Filter='ScholaOnlineLearning.SAC', [string]$EnginePath)
$ErrorActionPreference = 'Stop'
$saved = Join-Path $PSScriptRoot 'Saved/OnlineSAC'
New-Item -ItemType Directory -Force $saved | Out-Null
if ($Mode -eq 'Native') {
    $build = Join-Path $saved 'NativeBuild'
    & cmd /c "`"$(Join-Path $PSScriptRoot 'Tools/OnlineSACTests/Build-Native.cmd')`" `"$build`""
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
    $exe = Join-Path $build 'online_sac_tests.exe'
    & $exe baselines $saved; if ($LASTEXITCODE -ne 0) { throw 'Baselines failed.' }
    & $exe chase $saved 20000; if ($LASTEXITCODE -ne 0) { throw 'Native chase SAC thresholds failed.' }
    return
}
. (Join-Path $PSScriptRoot 'Tools/Unreal.ps1')
$engine = Resolve-UnrealEngine -EnginePath $EnginePath
& (Join-Path $PSScriptRoot 'Build.ps1') -EnginePath $engine
$log = Join-Path $saved 'UnrealAutomation.log'
$report = Join-Path $saved 'UnrealReport'
$editor = Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
& $editor (Join-Path $PSScriptRoot 'CSC301Host.uproject') "-ExecCmds=Automation RunTests $Filter; Quit" -unattended -nullrhi -nosound -nop4 -nosplash "-ReportExportPath=$report" "-abslog=$log" -stdout -FullStdOutLogOutput | Out-Null
$results = Select-String -LiteralPath $log -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}'
if (!$results) { throw "No automation results found. See $log" }
$failed = $false
foreach ($r in $results) {
    "$($r.Matches[0].Groups[2].Value): $($r.Matches[0].Groups[1].Value)"
    if ($r.Matches[0].Groups[1].Value -ne 'Success') { $failed = $true }
}
if ($failed) { throw "Automation tests failed. See $log" }

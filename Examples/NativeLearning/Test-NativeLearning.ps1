# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
[CmdletBinding()]
param(
    [string]$EnginePath,
    [string]$Filter = 'Schola.RLTools+Schola.Inference.Adaptive',
    [switch]$UseBuildAccelerator
)
$ErrorActionPreference = 'Stop'
if (!$EnginePath) {
    $install = Get-ItemProperty 'HKLM:\SOFTWARE\EpicGames\Unreal Engine\5.8' -ErrorAction SilentlyContinue
    if ($install) { $EnginePath = $install.InstalledDirectory }
}
if (!$EnginePath) { throw 'Pass -EnginePath with the Unreal Engine 5.8 installation directory.' }
$versionFile = Join-Path $EnginePath 'Engine/Build/Build.version'
$version = Get-Content -LiteralPath $versionFile -Raw | ConvertFrom-Json
if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) {
    throw 'This host targets Unreal Engine 5.8 and is validated with 5.8.4.'
}
& (Join-Path $PSScriptRoot 'Prepare-NativeLearning.ps1')
$project = Join-Path $PSScriptRoot 'NativeLearning.uproject'
$buildOptions = @('-WaitMutex', '-NoHotReloadFromIDE', '-MaxParallelActions=4')
if (!$UseBuildAccelerator) { $buildOptions += '-NoUBA' }
& (Join-Path $EnginePath 'Engine/Build/BatchFiles/Build.bat') NativeLearningEditor Win64 Development $project @buildOptions
if ($LASTEXITCODE -ne 0) { throw 'NativeLearningEditor build failed.' }

$saved = Join-Path $PSScriptRoot 'Saved/NativeLearning'
New-Item -ItemType Directory -Force $saved | Out-Null
$log = Join-Path $saved 'UnrealAutomation.log'
& (Join-Path $EnginePath 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $project "-ExecCmds=Automation RunTests $Filter; Quit" -unattended -nullrhi -nosound -nop4 -nosplash "-abslog=$log" -stdout -FullStdOutLogOutput | Out-Null
$editorExit = $LASTEXITCODE
$results = @{}
foreach ($line in Select-String -LiteralPath $log -Pattern 'Test Completed\. Result=\{(\w+)\}.*Path=\{([^}]+)\}') {
    $match = $line.Matches[0]
    $name = $match.Groups[2].Value
    if ($results.ContainsKey($name)) { throw "Duplicate result: $name" }
    $results[$name] = $match.Groups[1].Value
}
if ($editorExit -ne 0 -or !$results.Count) { throw "Editor failed or returned no tests. See $log" }
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$expected = @()
foreach ($module in @('ScholaRLTools', 'ScholaInferenceUtils')) {
    foreach ($source in Get-ChildItem -LiteralPath (Join-Path $root "Source/$module") -Filter '*Test.cpp' -Recurse -File) {
        foreach ($match in [regex]::Matches((Get-Content -LiteralPath $source.FullName -Raw), 'IMPLEMENT_SIMPLE_AUTOMATION_TEST\s*\([^,]+,\s*"([^"]+)"')) {
            $name = $match.Groups[1].Value
            if (@($Filter.Split('+') | Where-Object { $name.StartsWith($_, [StringComparison]::Ordinal) }).Count) { $expected += $name }
        }
    }
}
foreach ($name in $expected) {
    if (!$results.ContainsKey($name)) { throw "Missing automation result: $name. See $log" }
}
foreach ($name in $results.Keys) {
    if ($results[$name] -ne 'Success') { throw "Automation failed: $name = $($results[$name]). See $log" }
}
[ordered]@{ Engine = "$($version.MajorVersion).$($version.MinorVersion).$($version.PatchVersion)"; Expected = $expected.Count; Completed = $results.Count; Tests = $results } |
    ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $saved 'automation-summary.json') -Encoding utf8
Write-Output "$($results.Count) tests passed; no expected tests missing. See $log"

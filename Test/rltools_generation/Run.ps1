# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
[CmdletBinding()]
param([string]$EnginePath, [string]$DotNetPath)
$ErrorActionPreference = 'Stop'
if (!$DotNetPath -and $EnginePath) {
    $DotNetPath = Join-Path $EnginePath 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64/dotnet.exe'
}
if (!$DotNetPath) { $DotNetPath = 'dotnet' }
& $DotNetPath run --project (Join-Path $PSScriptRoot 'GeneratorTests.csproj') -c Release
if ($LASTEXITCODE -ne 0) { throw 'RLtools configuration generator checks failed.' }

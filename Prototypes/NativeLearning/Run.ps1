# Copyright (c) 2026 Advanced Micro Devices, Inc. All Rights Reserved.
param([string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
& cmake -S $PSScriptRoot -B "$PSScriptRoot/build" -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Native learner CMake configuration failed.' }
& cmake --build "$PSScriptRoot/build" --config $Configuration --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Native learner compilation failed.' }
& ctest --test-dir "$PSScriptRoot/build" -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Native learner checks failed.' }

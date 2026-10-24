@echo off
rem Builds the native OnlineSACRuntime harness with MSVC directly (used when CMake is not installed).
rem Flags mirror OnlineSACRuntime.Build.cs: C++20, no exceptions, no RTTI, precise FP, NOMINMAX, RL_TOOLS_DEBUG.
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSROOT=%%i
if not defined VSROOT ( echo Visual Studio with C++ tools not found. & exit /b 1 )
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set HERE=%~dp0
set SRC=%HERE%..\..\Source\OnlineSACRuntime
set OUT=%~1
if "%OUT%"=="" set OUT=%HERE%..\..\Saved\OnlineSAC\NativeBuild
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /O2 /GR- /EHs-c- /D_HAS_EXCEPTIONS=0 /bigobj /wd4530 /fp:precise /permissive- /MP /DNOMINMAX /DRL_TOOLS_DEBUG ^
  /I "%SRC%\Public" /I "%SRC%\Private" /external:W0 /external:I "%HERE%..\..\ThirdParty\RLtools\include" ^
  "%HERE%Main.cpp" "%SRC%\Private\OnlineSACRuntime.cpp" "%SRC%\Private\ChasePushExperiment.cpp" ^
  /Fo"%OUT%\\" /Fe"%OUT%\online_sac_tests.exe" || exit /b 1

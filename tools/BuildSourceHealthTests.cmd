@echo off
setlocal EnableExtensions
set "SOURCE_HEALTH_ROOT=%~dp0.."
set "SOURCE_HEALTH_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%SOURCE_HEALTH_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SOURCE_HEALTH_VS=%%I"
if not defined SOURCE_HEALTH_VS exit /b 2
call "%SOURCE_HEALTH_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%SOURCE_HEALTH_ROOT%" || exit /b 4
if not exist "build\source-health-tests" mkdir "build\source-health-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\SourceHealthTests.cpp /Fe:build\source-health-tests\SourceHealthTests.exe /Fo:build\source-health-tests\SourceHealthTests.obj
set "SOURCE_HEALTH_RESULT=%errorlevel%"
popd
exit /b %SOURCE_HEALTH_RESULT%

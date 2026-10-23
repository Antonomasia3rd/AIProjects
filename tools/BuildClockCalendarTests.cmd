@echo off
setlocal EnableExtensions
set "CC_VS="
set "CC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%CC_VSWHERE%" for /f "usebackq delims=" %%I in (`"%CC_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CC_VS=%%I"
if defined CC_VS call "%CC_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul || exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\content-integration" mkdir "build\content-integration"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\ClockCalendarTests.cpp /Fe:build\content-integration\ClockCalendarTests.exe /Fo:build\content-integration\ClockCalendarTests.obj /link user32.lib advapi32.lib
set "CC_STATUS=%errorlevel%"
popd
exit /b %CC_STATUS%

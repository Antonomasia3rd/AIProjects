@echo off
setlocal EnableExtensions
set "APP_LAUNCH_ROOT=%~dp0.."
set "APP_LAUNCH_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%APP_LAUNCH_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "APP_LAUNCH_VS=%%I"
if not defined APP_LAUNCH_VS exit /b 2
call "%APP_LAUNCH_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%APP_LAUNCH_ROOT%" || exit /b 4
if not exist "build\app-launcher-tests" mkdir "build\app-launcher-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /Zs tools\AppLauncherNativeCompileTests.cpp
if errorlevel 1 (popd & exit /b 5)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\AppLauncherTests.cpp /Fe:build\app-launcher-tests\AppLauncherTests.exe /Fo:build\app-launcher-tests\AppLauncherTests.obj /link gdiplus.lib shell32.lib user32.lib gdi32.lib ole32.lib uuid.lib
set "APP_LAUNCH_RESULT=%errorlevel%"
popd
exit /b %APP_LAUNCH_RESULT%

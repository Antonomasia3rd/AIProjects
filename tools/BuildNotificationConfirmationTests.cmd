@echo off
setlocal EnableExtensions
set "CONFIRMATION_VS="
set "CONFIRMATION_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%CONFIRMATION_VSWHERE%" for /f "usebackq delims=" %%I in (`"%CONFIRMATION_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CONFIRMATION_VS=%%I"
if defined CONFIRMATION_VS call "%CONFIRMATION_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul || exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\notification-tests" mkdir "build\notification-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\NotificationConfirmationTests.cpp /Fe:build\notification-tests\NotificationConfirmationTests.exe /Fo:build\notification-tests\NotificationConfirmationTests.obj
set "CONFIRMATION_STATUS=%errorlevel%"
popd
exit /b %CONFIRMATION_STATUS%

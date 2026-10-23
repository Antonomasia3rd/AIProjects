@echo off
setlocal EnableExtensions
set "ALERT_TEST_ROOT=%~dp0.."
set "ALERT_TEST_VS="
set "ALERT_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%ALERT_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ALERT_TEST_VS=%%I"
if not defined ALERT_TEST_VS exit /b 2
call "%ALERT_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%ALERT_TEST_ROOT%" || exit /b 4
if not exist "build\notification-tests" mkdir "build\notification-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\TileSourceAlertTests.cpp /Fe:build\notification-tests\TileSourceAlertTests.exe /Fo:build\notification-tests\TileSourceAlertTests.obj
set "ALERT_TEST_RESULT=%errorlevel%"
popd
exit /b %ALERT_TEST_RESULT%

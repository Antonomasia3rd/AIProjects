@echo off
setlocal EnableExtensions
set "BATTERY_TEST_VS="
set "BATTERY_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%BATTERY_TEST_VSWHERE%" for /f "usebackq delims=" %%I in (`"%BATTERY_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BATTERY_TEST_VS=%%I"
if defined BATTERY_TEST_VS call "%BATTERY_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul || exit /b 2
pushd "%~dp0.." || exit /b 3
if not exist "build\battery-tests" mkdir "build\battery-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\BatteryTests.cpp /Fe:build\battery-tests\BatteryTests.exe /Fo:build\battery-tests\BatteryTests.obj /Fd:build\battery-tests\BatteryTests.pdb
set "BATTERY_TEST_RESULT=%errorlevel%"
popd
exit /b %BATTERY_TEST_RESULT%

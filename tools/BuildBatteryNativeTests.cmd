@echo off
setlocal EnableExtensions
set "BATTERY_NATIVE_ROOT=%~dp0.."
set "BATTERY_NATIVE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%BATTERY_NATIVE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "BATTERY_NATIVE_VS=%%I"
if not defined BATTERY_NATIVE_VS exit /b 2
call "%BATTERY_NATIVE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%BATTERY_NATIVE_ROOT%" || exit /b 4
if not exist "build\battery-native-tests" mkdir "build\battery-native-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /Zs tools\BatteryNativeCompileTests.cpp
if errorlevel 1 (popd & exit /b 5)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /Od tools\BatteryNativeCompileTests.cpp /Fe:build\battery-native-tests\BatteryNativeLinkTests.exe /Fo:build\battery-native-tests\BatteryNativeLinkTests.obj /link /OPT:NOREF
if errorlevel 1 (popd & exit /b 6)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\BatteryNativeTests.cpp /Fe:build\battery-native-tests\BatteryNativeTests.exe /Fo:build\battery-native-tests\BatteryNativeTests.obj
set "BATTERY_NATIVE_RESULT=%errorlevel%"
popd
exit /b %BATTERY_NATIVE_RESULT%

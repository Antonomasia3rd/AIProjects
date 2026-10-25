@echo off
setlocal EnableExtensions
set "TILE_PAYLOAD_ROOT=%~dp0.."
set "TILE_PAYLOAD_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%TILE_PAYLOAD_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TILE_PAYLOAD_VS=%%I"
if not defined TILE_PAYLOAD_VS exit /b 2
call "%TILE_PAYLOAD_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%TILE_PAYLOAD_ROOT%" || exit /b 4
if not exist "build\tile-notification-tests" mkdir "build\tile-notification-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /Zs tools\TileNotificationNativeCompileTests.cpp
if errorlevel 1 (popd & exit /b 5)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\TileNotificationPayloadTests.cpp /Fe:build\tile-notification-tests\TileNotificationPayloadTests.exe /Fo:build\tile-notification-tests\TileNotificationPayloadTests.obj
set "TILE_PAYLOAD_RESULT=%errorlevel%"
popd
exit /b %TILE_PAYLOAD_RESULT%

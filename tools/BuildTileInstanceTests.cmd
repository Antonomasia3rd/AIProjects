@echo off
setlocal EnableExtensions
set "TILE_TEST_ROOT=%~dp0.."
set "TILE_TEST_VS="
set "TILE_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%TILE_TEST_VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%TILE_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TILE_TEST_VS=%%I"
)
if defined TILE_TEST_VS call "%TILE_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%TILE_TEST_ROOT%" || exit /b 1
if not exist "build\tile-instance-tests" mkdir "build\tile-instance-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\TileInstanceTests.cpp /Fe:build\tile-instance-tests\TileInstanceTests.exe /Fo:build\tile-instance-tests\TileInstanceTests.obj
set "TILE_TEST_STATUS=%errorlevel%"
popd
exit /b %TILE_TEST_STATUS%

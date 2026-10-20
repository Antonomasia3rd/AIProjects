@echo off
setlocal EnableExtensions
set "REG_TEST_ROOT=%~dp0.."
set "REG_TEST_VS="
set "REG_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%REG_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "REG_TEST_VS=%%I"
if not defined REG_TEST_VS exit /b 2
call "%REG_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%REG_TEST_ROOT%" || exit /b 4
if not exist "build\tile-registration-tests" mkdir "build\tile-registration-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\TileRegistrationTests.cpp /Fe:build\tile-registration-tests\TileRegistrationTests.exe /Fo:build\tile-registration-tests\TileRegistrationTests.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE DesktopStub\TileLauncher.cpp /Fe:build\tile-registration-tests\DesktopStubTileLauncher.exe /Fo:build\tile-registration-tests\DesktopStubTileLauncher.obj /link user32.lib shell32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 goto Failed
popd
exit /b 0
:Failed
set "REG_TEST_RESULT=%errorlevel%"
popd
exit /b %REG_TEST_RESULT%

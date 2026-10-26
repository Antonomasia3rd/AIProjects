@echo off
setlocal EnableExtensions
set "REG_BIND_ROOT=%~dp0.."
set "REG_BIND_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%REG_BIND_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "REG_BIND_VS=%%I"
if not defined REG_BIND_VS exit /b 2
call "%REG_BIND_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%REG_BIND_ROOT%" || exit /b 4
if not exist "build\tile-registration-tests" mkdir "build\tile-registration-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\TileRegistrationBindingTests.cpp /Fe:build\tile-registration-tests\TileRegistrationBindingTests.exe /Fo:build\tile-registration-tests\TileRegistrationBindingTests.obj /link advapi32.lib
set "REG_BIND_RESULT=%errorlevel%"
popd
exit /b %REG_BIND_RESULT%

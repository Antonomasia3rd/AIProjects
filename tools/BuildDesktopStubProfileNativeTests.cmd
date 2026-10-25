@echo off
setlocal EnableExtensions
set "PROFILE_NATIVE_ROOT=%~dp0.."
set "PROFILE_NATIVE_VS="
set "PROFILE_NATIVE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%PROFILE_NATIVE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROFILE_NATIVE_VS=%%I"
if not defined PROFILE_NATIVE_VS exit /b 2
call "%PROFILE_NATIVE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%PROFILE_NATIVE_ROOT%" || exit /b 4
if not exist "build\profile-tests" mkdir "build\profile-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\DesktopStubProfileNativeTests.cpp /Fe:build\profile-tests\DesktopStubProfileNativeTests.exe /Fo:build\profile-tests\DesktopStubProfileNativeTests.obj /link advapi32.lib shell32.lib user32.lib ole32.lib
set "PROFILE_NATIVE_RESULT=%errorlevel%"
popd
exit /b %PROFILE_NATIVE_RESULT%

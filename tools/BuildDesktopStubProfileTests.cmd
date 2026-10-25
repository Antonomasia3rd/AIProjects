@echo off
setlocal EnableExtensions
set "PROFILE_TEST_ROOT=%~dp0.."
set "PROFILE_TEST_VS="
set "PROFILE_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%PROFILE_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROFILE_TEST_VS=%%I"
if not defined PROFILE_TEST_VS exit /b 2
call "%PROFILE_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%PROFILE_TEST_ROOT%" || exit /b 4
if not exist "build\profile-tests" mkdir "build\profile-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\DesktopStubProfileTests.cpp /Fe:build\profile-tests\DesktopStubProfileTests.exe /Fo:build\profile-tests\DesktopStubProfileTests.obj
set "PROFILE_TEST_RESULT=%errorlevel%"
popd
exit /b %PROFILE_TEST_RESULT%

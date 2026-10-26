@echo off
setlocal EnableExtensions
set "PROFILE_UI_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%PROFILE_UI_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROFILE_UI_VS=%%I"
if not defined PROFILE_UI_VS exit /b 1
call "%PROFILE_UI_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0.." || exit /b 1
if not exist build\review-validation mkdir build\review-validation
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\ProfilePropertiesUiTests.cpp /Fe:build\review-validation\ProfilePropertiesUiTests.exe /Fo:build\review-validation\ProfilePropertiesUiTests.obj
set "PROFILE_UI_RESULT=%errorlevel%"
popd
exit /b %PROFILE_UI_RESULT%

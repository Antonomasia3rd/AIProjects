@echo off
setlocal EnableExtensions
set "PROP_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%PROP_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROP_VS=%%I"
if not defined PROP_VS exit /b 1
call "%PROP_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0.." || exit /b 1
if not exist build\review-validation mkdir build\review-validation
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\PropertiesUiTests.cpp /Fe:build\review-validation\PropertiesUiModelAgentTests.exe /Fo:build\review-validation\PropertiesUiModelAgentTests.obj
set "PROP_RESULT=%errorlevel%"
popd
exit /b %PROP_RESULT%

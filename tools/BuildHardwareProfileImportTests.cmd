@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "HWIMPORT_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HWIMPORT_VS=%%I"
)
if defined HWIMPORT_VS call "%HWIMPORT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist "build\review-validation" mkdir "build\review-validation"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\HardwareProfileImportTests.cpp /Fe:build\review-validation\HardwareProfileImportTests.exe /Fo:build\review-validation\HardwareProfileImportTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib /SUBSYSTEM:CONSOLE
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
echo Built the inert hardware donor import fixture. Nothing was executed.
exit /b 0

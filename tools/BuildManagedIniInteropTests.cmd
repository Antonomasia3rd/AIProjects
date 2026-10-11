@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "INIINTEROP_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "INIINTEROP_VS=%%I"
)
if defined INIINTEROP_VS call "%INIINTEROP_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist "build\review-validation" mkdir "build\review-validation"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\IniInteropNativeWorker.cpp /Fe:build\review-validation\IniInteropNativeWorker.exe /Fo:build\review-validation\IniInteropNativeWorker.obj /link advapi32.lib user32.lib shell32.lib shlwapi.lib ole32.lib uuid.lib /SUBSYSTEM:CONSOLE
if errorlevel 1 goto Result
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ManagedIniInteropTests /r:System.Core.dll /out:"build\review-validation\ManagedIniInteropTests.exe" ^
 "dependencies\managed_named_objects.cs" "dependencies\managed_ini.cs" "dependencies\managed_configuration_startup.cs" "dependencies\managed_startup_shortcut.cs" "tools\ManagedIniInteropTests.cs"
:Result
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
echo Built the mixed managed/native temporary-file fixture. Nothing was executed.
exit /b 0

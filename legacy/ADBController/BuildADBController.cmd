@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "VCVARS="

if defined VCINSTALLDIR if exist "%VCINSTALLDIR%\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VCINSTALLDIR%\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if defined VSINSTALLDIR if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCVARS if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VCVARS if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
    )
)

for %%D in ("%ProgramFiles%" "%ProgramFiles(x86)%" "D:\Program Files" "D:\Program Files (x86)") do (
    for %%Y in (2022 2019) do (
        for %%E in (BuildTools Community Professional Enterprise) do (
            if not defined VCVARS if exist "%%~D\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~D\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)

if defined VCVARS goto HaveVcvars
echo ERROR: vcvars64.bat not found. Install Visual Studio Build Tools with the C++ workload.
exit /b 1

:HaveVcvars
echo Using "%VCVARS%"
call "%VCVARS%" >nul
if errorlevel 1 exit /b %errorlevel%

pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)
if not exist "build\obj" mkdir "build\obj"
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)

if /I "%~1"=="test-inert" goto InertTests

if /I "%~1"=="check" (
    cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /Zs ADBController.cpp
    set "STATUS=!ERRORLEVEL!"
    if not "!STATUS!"=="0" (
        popd
        exit /b !STATUS!
    )
    cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 tools\ADBControllerSourceCheck.cpp /Fe:"build\obj\ADBControllerSourceCheck.exe" /Fo:"build\obj\ADBControllerSourceCheck.obj"
    set "STATUS=!ERRORLEVEL!"
    if not "!STATUS!"=="0" (
        popd
        exit /b !STATUS!
    )
    "build\obj\ADBControllerSourceCheck.exe"
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)

rc /nologo /fo"build\obj\ADBController.res" ADBController.rc
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)

cl /nologo /std:c++17 /EHsc /O2 /W4 /WX /utf-8 /DUNICODE /D_UNICODE ADBController.cpp "build\obj\ADBController.res" /Fe:"build\ADBController.exe" /Fo:"build\obj\ADBController.obj" /link comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib uxtheme.lib dwmapi.lib advapi32.lib ole32.lib uuid.lib /SUBSYSTEM:WINDOWS
set "STATUS=!ERRORLEVEL!"
if not "!STATUS!"=="0" (
    popd
    exit /b !STATUS!
)

rem Building never invokes the product unless binary-smoke is explicitly chosen.
if /I NOT "%~1"=="binary-smoke" (
    popd
    exit /b 0
)

"%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "tools\ADBControllerBinarySmoke.ps1" -Executable "build\ADBController.exe"
set "STATUS=!ERRORLEVEL!"
popd
exit /b !STATUS!

:InertTests
cl /nologo /std:c++17 /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE tools\ADBControllerRuntimeTests.cpp /Fe:"build\obj\ADBControllerRuntimeTests.exe" /Fo:"build\obj\ADBControllerRuntimeTests.obj" /link comctl32.lib comdlg32.lib shell32.lib shlwapi.lib user32.lib gdi32.lib uxtheme.lib dwmapi.lib advapi32.lib ole32.lib uuid.lib /SUBSYSTEM:CONSOLE
set "STATUS=!ERRORLEVEL!"
if "!STATUS!"=="0" (
    "build\obj\ADBControllerRuntimeTests.exe"
    set "STATUS=!ERRORLEVEL!"
)
popd
exit /b !STATUS!

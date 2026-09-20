@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
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

if defined VCVARS goto LoadCompiler
where cl.exe >nul 2>nul
if not errorlevel 1 goto HaveCompiler
echo ERROR: cl.exe not found. Install Visual Studio Build Tools with the C++ workload.
exit /b 1

:LoadCompiler
call "%VCVARS%" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
goto HaveCompiler

:HaveCompiler
pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
if errorlevel 1 (
    popd
    exit /b %ERRORLEVEL%
)

cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE tools\AppxSharingTests.cpp /Fe:build\AppxSharingTests.exe /Fo:build\AppxSharingTests.obj /link user32.lib shell32.lib ole32.lib uuid.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:CONSOLE
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)
build\AppxSharingTests.exe
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%
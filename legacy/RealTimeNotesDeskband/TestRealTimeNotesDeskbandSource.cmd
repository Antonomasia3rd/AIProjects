@echo off
setlocal EnableExtensions

cd /d "%~dp0"
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

if not exist build mkdir build
if errorlevel 1 exit /b 1

if defined VCVARS (
    call "%VCVARS%" >nul
    if errorlevel 1 exit /b %ERRORLEVEL%
)

where cl.exe >nul 2>nul
if not errorlevel 1 (
    cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX tools\RealTimeNotesDeskbandSourceCheck.cpp /Fe:build\RealTimeNotesDeskbandSourceCheck.exe /Fo:build\RealTimeNotesDeskbandSourceCheck.obj
    if errorlevel 1 exit /b %ERRORLEVEL%
    goto RunCheck
)

where g++ >nul 2>nul
if errorlevel 1 (
    echo Neither cl.exe nor g++ was found. Install Visual Studio Build Tools or MinGW-w64.
    exit /b 1
)
g++ -std=c++17 -Wall -Wextra -Werror -O2 -o build\RealTimeNotesDeskbandSourceCheck.exe tools\RealTimeNotesDeskbandSourceCheck.cpp
if errorlevel 1 exit /b %ERRORLEVEL%

:RunCheck
build\RealTimeNotesDeskbandSourceCheck.exe
exit /b %ERRORLEVEL%

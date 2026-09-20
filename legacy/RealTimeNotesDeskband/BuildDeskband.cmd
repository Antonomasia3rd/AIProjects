@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

if not exist build mkdir build
if errorlevel 1 exit /b 1

set "SOURCE=RealTimeNotesDeskband.cpp"
set "LIBS=-lole32 -loleaut32 -luuid -lshlwapi -lwinhttp -lcomctl32 -luxtheme -lgdi32 -ladvapi32 -lcrypt32 -lcomdlg32 -lshell32"
set "CXXFLAGS=-std=c++17 -O2"

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

if defined VCVARS (
    call "%VCVARS%" >nul
    if errorlevel 1 exit /b !ERRORLEVEL!
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    where g++.exe >nul 2>nul
    if not errorlevel 1 goto UseMinGW
    echo Neither g++.exe nor cl.exe was found.
    echo Install MinGW-w64 or Visual Studio C++ Build Tools.
    exit /b 1
)

if /i "%~1"=="check" (
    cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /c "%SOURCE%" /Fo:build\RealTimeNotesDeskband.check.obj
    if errorlevel 1 exit /b !ERRORLEVEL!
    call TestRealTimeNotesDeskbandSource.cmd
    exit /b !ERRORLEVEL!
)

set "OUTPUT=build\RealTimeNotesDeskband.dll"
if /i "%~1"=="new" set "OUTPUT=build\RealTimeNotesDeskband.!RANDOM!.dll"
set "IMPORT_LIBRARY=!OUTPUT:.dll=.lib!"
set "PDB_FILE=!OUTPUT:.dll=.pdb!"

cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /MT /LD "%SOURCE%" /Fo:build\RealTimeNotesDeskband.obj /Fe:"!OUTPUT!" /link /IMPLIB:"!IMPORT_LIBRARY!" /PDB:"!PDB_FILE!"
if errorlevel 1 exit /b !ERRORLEVEL!

echo Built !OUTPUT! with MSVC.
exit /b 0

:UseMinGW

if /i "%~1"=="check" (
    g++ -std=c++17 -Wall -Wextra -Werror -c "%SOURCE%" -o NUL
    if errorlevel 1 exit /b !ERRORLEVEL!
    call TestRealTimeNotesDeskbandSource.cmd
    exit /b !ERRORLEVEL!
)

set "OUTPUT=build\RealTimeNotesDeskband.dll"
if /i "%~1"=="new" set "OUTPUT=build\RealTimeNotesDeskband.%RANDOM%.dll"

g++ %CXXFLAGS% -shared -static -static-libgcc -static-libstdc++ -o "%OUTPUT%" "%SOURCE%" %LIBS%
if errorlevel 1 exit /b %ERRORLEVEL%

echo Built %OUTPUT% with MinGW.
exit /b 0

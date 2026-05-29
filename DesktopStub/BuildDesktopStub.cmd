@echo off
setlocal EnableExtensions

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
    popd
    exit /b %errorlevel%
)
if not exist "build\" (
    echo ERROR: build exists but is not a directory.
    popd
    exit /b 1
)
if not exist "build\obj" mkdir "build\obj"
if errorlevel 1 (
    popd
    exit /b %errorlevel%
)

rem ---------------------------------------------------------------------------
rem Build policy:
rem   Older revisions had argument-controlled targets such as win8, win81,
rem   broker, helpers, background, experiments, all, and check. That made local
rem   builds ambiguous: the Windows 8/8.1 manifest path could be selected at
rem   runtime while the packaged broker helper had not been built.
rem
rem   The script now deliberately ignores every argument and always builds the
rem   same stable target set:
rem     - DesktopStub.exe
rem     - DesktopStubLiveTileBroker.exe
rem
rem   The experimental background-task DLL remains in the source tree for
rem   research, but it is not part of the normal one-command build.
rem ---------------------------------------------------------------------------
if not "%~1"=="" (
    echo [i] BuildDesktopStub.cmd now ignores build arguments and always builds the same target set.
    echo [i] One or more arguments were supplied and ignored.
)

set "OUT_EXE=build\DesktopStub.exe"
set "OBJ_FILE=build\obj\DesktopStub.obj"
set "BROKER_EXE=build\DesktopStubLiveTileBroker.exe"
set "BROKER_OBJ=build\obj\LiveTileBroker.obj"

echo Building packaged Live Tile broker...
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE LiveTileBroker.cpp /Fe:%BROKER_EXE% /Fo:%BROKER_OBJ% /link windowsapp.lib runtimeobject.lib ole32.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)

echo Building main DesktopStub host...
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub.cpp /Fe:%OUT_EXE% /Fo:%OBJ_FILE% /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
popd

exit /b %STATUS%

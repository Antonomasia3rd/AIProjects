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

if /I "%RSSLIVETILE_REFRESH_TAGS%"=="1" if not defined RSSLIVETILE_VERSION if not defined RSSLIVETILE_RELEASE_TAG (
    git remote get-url origin >nul 2>nul
    if not errorlevel 1 (
        git fetch --quiet --tags origin
        if errorlevel 1 echo [!] Could not refresh RssLiveTile release tags from origin; continuing with local tags.
    )
)

set "VERSION_RESULT=build\obj\RssLiveTileVersion.txt"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%..\dependencies\resolve_release_version.ps1" -TagPrefix "RssLiveTile-v" -VersionEnvironment "RSSLIVETILE_VERSION" -ReleaseTagEnvironment "RSSLIVETILE_RELEASE_TAG" > "%VERSION_RESULT%"
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)
for /f "usebackq tokens=1,* delims==" %%A in ("%VERSION_RESULT%") do set "AIP_%%A=%%B"
del /f /q "%VERSION_RESULT%" >nul 2>nul
if not defined AIP_RELEASE_TAG (
    echo ERROR: RssLiveTile release version resolver returned no release tag.
    popd
    exit /b 1
)

set "VERSION_DEFINES=/DAIP_VERSION_MAJOR=!AIP_VERSION_MAJOR! /DAIP_VERSION_MINOR=!AIP_VERSION_MINOR! /DAIP_VERSION_BUILD=!AIP_VERSION_BUILD! /DAIP_VERSION_REVISION=!AIP_VERSION_REVISION!"
set "VERSION_INCLUDE=build\obj\RssLiveTileVersionDefines.inc"
> "%VERSION_INCLUDE%" echo #define AIP_RELEASE_TAG "!AIP_RELEASE_TAG!"
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)
echo RssLiveTile version: !AIP_RELEASE_TAG! (!AIP_VERSION!)

if /I "%~1"=="check" (
    cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE !VERSION_DEFINES! /Ibuild\obj /Zs RssLiveTile.cpp
    set "STATUS=!ERRORLEVEL!"
    if "!STATUS!"=="0" call TestRssLiveTileSource.cmd
    if "!STATUS!"=="0" set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)

set "OUT_EXE=build\RssLiveTile.exe"
set "OBJ_FILE=build\obj\RssLiveTile.obj"
set "RES_FILE=build\obj\RssLiveTile.res"

rc /nologo /dAIP_VERSION_MAJOR=!AIP_VERSION_MAJOR! /dAIP_VERSION_MINOR=!AIP_VERSION_MINOR! /dAIP_VERSION_BUILD=!AIP_VERSION_BUILD! /dAIP_VERSION_REVISION=!AIP_VERSION_REVISION! /fo"%RES_FILE%" RssLiveTile.rc
if errorlevel 1 (
    set "STATUS=!ERRORLEVEL!"
    popd
    exit /b !STATUS!
)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE !VERSION_DEFINES! /Ibuild\obj RssLiveTile.cpp "%RES_FILE%" /Fe:%OUT_EXE% /Fo:%OBJ_FILE% /link gdiplus.lib winhttp.lib gdi32.lib user32.lib shell32.lib shlwapi.lib ole32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
popd

exit /b %STATUS%

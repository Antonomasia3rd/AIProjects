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

if /I "%~1"=="check" (
    cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0602 /Zs CharmTray.cpp
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)

set "OUT_EXE=build\CharmTray.exe"
set "OBJ_FILE=build\obj\CharmTray.obj"

call :PrepareOutput "%CD%\%OUT_EXE%" "CharmTray"
set "PREPARE_STATUS=%ERRORLEVEL%"
if not "%PREPARE_STATUS%"=="0" if not "%PREPARE_STATUS%"=="2" (
    popd
    exit /b %PREPARE_STATUS%
)
if "%PREPARE_STATUS%"=="2" (
    echo WARNING: Could not overwrite build\CharmTray.exe after trying to close the running app.
    echo WARNING: Building side-by-side output build\CharmTray.side-by-side.exe instead.
    set "OUT_EXE=build\CharmTray.side-by-side.exe"
    set "OBJ_FILE=build\obj\CharmTray.side-by-side.obj"
)

cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0602 CharmTray.cpp /Fe:%OUT_EXE% /Fo:%OBJ_FILE% /link user32.lib ole32.lib shell32.lib /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
popd

exit /b %STATUS%

:PrepareOutput
set "TARGET=%~1"
set "PROCNAME=%~2"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "& { param([string]$OutputPath, [string]$ProcessName); if (-not (Test-Path -LiteralPath $OutputPath -PathType Leaf)) { exit 0 }; $resolved = (Resolve-Path -LiteralPath $OutputPath).Path; $processes = @(Get-Process -Name $ProcessName -ErrorAction SilentlyContinue | Where-Object { try { $_.Path -and [string]::Equals((Resolve-Path -LiteralPath $_.Path).Path, $resolved, [StringComparison]::OrdinalIgnoreCase) } catch { $false } }); foreach ($process in $processes) { try { if ($process.MainWindowHandle -ne [IntPtr]::Zero) { Write-Host ('Requesting graceful close for {0}[{1}]' -f $process.ProcessName, $process.Id); if ($process.CloseMainWindow() -and $process.WaitForExit(5000)) { continue } }; Write-Host ('Force stopping {0}[{1}]' -f $process.ProcessName, $process.Id); Stop-Process -Id $process.Id -Force -ErrorAction Stop; [void]$process.WaitForExit(5000) } catch { Write-Warning ('Could not stop {0}[{1}]: {2}' -f $process.ProcessName, $process.Id, $_.Exception.Message) } }; try { $stream = [IO.File]::Open($resolved, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None); $stream.Dispose(); exit 0 } catch { Write-Warning ('Output is still not writable: {0}. {1}' -f $resolved, $_.Exception.Message); exit 2 } }" "%TARGET%" "%PROCNAME%"
exit /b %ERRORLEVEL%

@echo off
setlocal EnableExtensions

where pwsh.exe >nul 2>nul
if not errorlevel 1 (
    goto UsePwsh
)

where powershell.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: PowerShell is required to run the portable TileText layout checks.
    exit /b 1
)

powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0TestTileTextLayout.ps1"
exit /b %ERRORLEVEL%

:UsePwsh
pwsh.exe -NoLogo -NoProfile -File "%~dp0TestTileTextLayout.ps1"
exit /b %ERRORLEVEL%

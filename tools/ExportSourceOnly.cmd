@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "OUT=%~1"
if not defined OUT set "OUT=%CD%\AIProjects-source.zip"

pushd "%ROOT%" || exit /b 1

git rev-parse --is-inside-work-tree >nul 2>nul
if errorlevel 1 (
    echo ERROR: ExportSourceOnly.cmd must run inside a Git working tree.
    popd
    exit /b 1
)

git diff --quiet HEAD --
if errorlevel 1 (
    echo WARNING: Working tree has uncommitted changes; git archive exports committed HEAD only.
)

git archive --format=zip --output "%OUT%" HEAD
set "STATUS=%ERRORLEVEL%"
if "%STATUS%"=="0" echo Wrote source-only archive: %OUT%
popd
exit /b %STATUS%

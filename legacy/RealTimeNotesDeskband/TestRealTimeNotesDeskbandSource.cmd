@echo off
setlocal EnableExtensions

cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ was not found on PATH.
    echo Install MinGW-w64 or use Strawberry Perl's bundled MinGW toolchain.
    exit /b 1
)

if not exist build mkdir build
if errorlevel 1 exit /b 1

g++ -std=c++17 -Wall -Wextra -O2 -o build\RealTimeNotesDeskbandSourceCheck.exe tools\RealTimeNotesDeskbandSourceCheck.cpp
if errorlevel 1 exit /b 1

build\RealTimeNotesDeskbandSourceCheck.exe
exit /b %ERRORLEVEL%

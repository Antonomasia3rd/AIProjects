@echo off
setlocal EnableExtensions EnableDelayedExpansion

cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
    echo g++ was not found on PATH.
    echo Install MinGW-w64 or use Strawberry Perl's bundled MinGW toolchain.
    exit /b 1
)

if not exist build mkdir build

set "SOURCE=RealTimeNotesDeskband.cpp"
set "LIBS=-lole32 -loleaut32 -luuid -lshlwapi -lwinhttp -lcomctl32 -luxtheme -lgdi32 -ladvapi32 -lcrypt32 -lcomdlg32 -lshell32"
set "CXXFLAGS=-std=c++17 -O2"

if /i "%~1"=="check" (
    g++ -std=c++17 -Wall -Wextra -c "%SOURCE%" -o NUL
    if errorlevel 1 exit /b !ERRORLEVEL!
    call TestRealTimeNotesDeskbandSource.cmd
    exit /b !ERRORLEVEL!
)

set "OUTPUT=build\RealTimeNotesDeskband.dll"
if /i "%~1"=="new" set "OUTPUT=build\RealTimeNotesDeskband.%RANDOM%.dll"

g++ %CXXFLAGS% -shared -static -static-libgcc -static-libstdc++ -o "%OUTPUT%" "%SOURCE%" %LIBS%
if errorlevel 1 exit /b %ERRORLEVEL%

echo Built %OUTPUT%

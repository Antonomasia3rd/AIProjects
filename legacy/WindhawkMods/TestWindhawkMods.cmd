@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "COMPILER=%WINDHAWK_COMPILER%"

if not defined COMPILER if exist "%ProgramFiles%\Windhawk\Compiler\bin\clang++.exe" set "COMPILER=%ProgramFiles%\Windhawk\Compiler\bin\clang++.exe"
if not defined COMPILER if exist "D:\Program Files\Windhawk\Compiler\bin\clang++.exe" set "COMPILER=D:\Program Files\Windhawk\Compiler\bin\clang++.exe"
if not defined COMPILER (
    echo ERROR: Windhawk clang++.exe was not found. Set WINDHAWK_COMPILER to its full path.
    exit /b 1
)

for %%A in (x86_64 i686) do (
    for %%F in (
        "local@always-uiaccess.wh.cpp"
        "local@appsfolder-unhide-hidden-apps.wh.cpp"
        "local@snipping-tool-border-fix.wh.cpp"
    ) do (
        echo Checking %%~F for %%A...
        "%COMPILER%" -x c++ -std=c++23 -target %%A-w64-mingw32 -DUNICODE -D_UNICODE -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_WIN32_IE=0x0A00 -DNTDDI_VERSION=0x0A000008 -D__USE_MINGW_ANSI_STDIO=0 -DWH_MOD -DWH_EDITING -include windhawk_api.h -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers -Wno-cast-function-type-mismatch -fsyntax-only "%ROOT%%%~F"
        if errorlevel 1 exit /b 1
    )
)

echo Windhawk mod source checks passed for x64 and x86.
exit /b 0

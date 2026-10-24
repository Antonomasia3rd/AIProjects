@echo off
setlocal EnableExtensions
set "CAL_REM_ROOT=%~dp0.."
set "CAL_REM_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%CAL_REM_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CAL_REM_VS=%%I"
if not defined CAL_REM_VS exit /b 2
call "%CAL_REM_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%CAL_REM_ROOT%" || exit /b 4
if not exist "build\calendar-reminder-tests" mkdir "build\calendar-reminder-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\CalendarReminderTests.cpp /Fe:build\calendar-reminder-tests\CalendarReminderTests.exe /Fo:build\calendar-reminder-tests\CalendarReminderTests.obj
set "CAL_REM_RESULT=%errorlevel%"
popd
exit /b %CAL_REM_RESULT%

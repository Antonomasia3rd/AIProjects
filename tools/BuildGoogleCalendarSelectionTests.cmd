@echo off
setlocal EnableExtensions
set "GOOGLE_SELECTION_ROOT=%~dp0.."
set "GOOGLE_SELECTION_VS="
set "GOOGLE_SELECTION_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%GOOGLE_SELECTION_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GOOGLE_SELECTION_VS=%%I"
if not defined GOOGLE_SELECTION_VS exit /b 2
call "%GOOGLE_SELECTION_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%GOOGLE_SELECTION_ROOT%" || exit /b 4
if not exist "build\google-calendar-selection-tests" mkdir "build\google-calendar-selection-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\GoogleCalendarSelectionTests.cpp /Fe:build\google-calendar-selection-tests\GoogleCalendarSelectionTests.exe /Fo:build\google-calendar-selection-tests\GoogleCalendarSelectionTests.obj
set "GOOGLE_SELECTION_RESULT=%errorlevel%"
popd
exit /b %GOOGLE_SELECTION_RESULT%

@echo off
setlocal EnableExtensions
set "GOOGLE_CALENDAR_TEST_ROOT=%~dp0.."
set "GOOGLE_CALENDAR_TEST_VS="
set "GOOGLE_CALENDAR_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%GOOGLE_CALENDAR_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GOOGLE_CALENDAR_TEST_VS=%%I"
if not defined GOOGLE_CALENDAR_TEST_VS exit /b 2
call "%GOOGLE_CALENDAR_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%GOOGLE_CALENDAR_TEST_ROOT%" || exit /b 4
if not exist "build\google-calendar-tests" mkdir "build\google-calendar-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\GoogleCalendarTests.cpp /Fe:build\google-calendar-tests\GoogleCalendarTests.exe /Fo:build\google-calendar-tests\GoogleCalendarTests.obj /link winhttp.lib advapi32.lib crypt32.lib user32.lib shell32.lib shlwapi.lib ole32.lib
set "GOOGLE_CALENDAR_TEST_RESULT=%errorlevel%"
popd
exit /b %GOOGLE_CALENDAR_TEST_RESULT%

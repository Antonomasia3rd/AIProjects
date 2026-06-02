@echo off
setlocal EnableExtensions

for %%I in ("%~dp0..") do set "ROOT=%%~fI"
set "DLL=%ROOT%\RealTimeNotesDeskband.dll"

if not exist "%DLL%" (
  echo ERROR: RealTimeNotesDeskband.dll was not found. Build it first with: make deskband
  exit /b 1
)

set "REGSVR32=%WINDIR%\System32\regsvr32.exe"
"%REGSVR32%" /s "%DLL%"
if errorlevel 1 (
  echo ERROR: regsvr32 failed with exit code %ERRORLEVEL%.
  exit /b %ERRORLEVEL%
)

echo Registered Real Time Notes Deskband.
echo Right-click the taskbar, open Toolbars, then select Real Time Notes.
exit /b 0

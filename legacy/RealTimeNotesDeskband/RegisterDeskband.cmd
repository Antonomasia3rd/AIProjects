@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "DLL=%~1"
if not defined DLL set "DLL=%ROOT%build\RealTimeNotesDeskband.dll"

if not exist "%DLL%" (
    echo Deskband DLL was not found:
    echo   %DLL%
    echo Build it first with BuildDeskband.cmd, or pass a DLL path.
    exit /b 1
)

"%SystemRoot%\System32\regsvr32.exe" /s "%DLL%"
if errorlevel 1 (
    echo regsvr32 failed with exit code %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

echo Registered Real Time Notes Deskband.
echo DLL: %DLL%
echo Enable it from taskbar Toolbars ^> Real Time Notes.
exit /b 0

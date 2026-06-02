@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CLSID={8D1C2A67-39F0-497E-8D3C-0D27A4A41C1F}"
set "DLL=%~1"

if not defined DLL (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Classes\CLSID\%CLSID%\InprocServer32" /ve 2^>nul ^| findstr /i "REG_SZ"') do set "DLL=%%B"
)
if not defined DLL set "DLL=%ROOT%build\RealTimeNotesDeskband.dll"

if not exist "%DLL%" (
    echo Deskband DLL was not found:
    echo   %DLL%
    echo Pass the registered DLL path, or rebuild the default DLL.
    exit /b 1
)

"%SystemRoot%\System32\regsvr32.exe" /u /s "%DLL%"
if errorlevel 1 (
    echo regsvr32 /u failed with exit code %ERRORLEVEL%.
    exit /b %ERRORLEVEL%
)

echo Unregistered Real Time Notes Deskband.
echo DLL: %DLL%
exit /b 0

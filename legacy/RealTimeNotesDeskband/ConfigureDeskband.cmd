@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CLSID={8D1C2A67-39F0-497E-8D3C-0D27A4A41C1F}"
set "DLL="

if /i "%~1"=="--dll" (
    if "%~2"=="" (
        echo --dll requires a DLL path.
        exit /b 2
    )
    set "DLL=%~2"
    set "FORWARD_ARGS=%3 %4 %5 %6 %7 %8 %9"
) else if /i "%~x1"==".dll" (
    set "DLL=%~1"
    set "FORWARD_ARGS=%2 %3 %4 %5 %6 %7 %8 %9"
) else (
    set "FORWARD_ARGS=%*"
)

if not defined DLL (
    for /f "tokens=2,*" %%A in ('reg query "HKCU\Software\Classes\CLSID\%CLSID%\InprocServer32" /ve 2^>nul ^| findstr /i "REG_SZ"') do set "DLL=%%B"
)
if not defined DLL set "DLL=%ROOT%build\RealTimeNotesDeskband.dll"

if not exist "%DLL%" (
    echo Deskband DLL was not found:
    echo   %DLL%
    echo Build or register the deskband first, or pass a DLL path.
    exit /b 1
)

"%SystemRoot%\System32\rundll32.exe" "%DLL%",ConfigureDeskband %FORWARD_ARGS%
exit /b %ERRORLEVEL%

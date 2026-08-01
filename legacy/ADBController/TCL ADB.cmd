@echo off
setlocal EnableExtensions

set "PROGRAM=%~dp0build\ADBController.exe"
if exist "%PROGRAM%" (
    start "" "%PROGRAM%"
    exit /b 0
)

echo ADBController.exe has not been built yet.
echo.
echo Build it with:
echo   BuildADBController.cmd
echo.
echo The GUI replacement intentionally keeps the ADB server running when you
echo switch TVs and scopes every command to the selected IP address.
pause
exit /b 1

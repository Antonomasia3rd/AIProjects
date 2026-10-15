@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.23 - VANILLA REFERENCE RELAY
echo ========================================================
echo Official Minecraft -^> localhost relay -^> Nifirealm
echo PASSIVE CAPTURE ONLY. No packet modifications.
echo.
echo Default local address for Minecraft:
echo   127.0.0.1 : 19132
echo.
echo Default upstream:
echo   Nifirealm.aternos.me : 24128
echo.
echo First run may request Microsoft/Xbox device-code sign-in in this terminal.
echo.

node ".\vanilla-respawn-reference-relay.js"
set "RC=%ERRORLEVEL%"

echo.
echo Node exited with code %RC%.
pause
exit /b %RC%

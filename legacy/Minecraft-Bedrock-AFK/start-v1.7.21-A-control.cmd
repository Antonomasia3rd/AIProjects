@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.21 - CONTROL
echo -----------------------------------------
echo Observes the existing state-2 packet without changing it.
echo No loading-screen/interact/init replay.
echo.

set "MC_RESPAWN_STATE2_MODE=control"
node -r ".\respawn-state2-field-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"
echo.
echo Node exited with code %RC%.
exit /b %RC%

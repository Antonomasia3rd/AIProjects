@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.21 - TEST D: zero runtime + server position
echo ----------------------------------------------------------------------
echo Changes BOTH ClientReadyToSpawn/state-2 fields:
echo   runtime_entity_id -^> 0
echo   position -^> latest server Respawn(state-0) position
echo No loading-screen/interact/init replay.
echo.

set "MC_RESPAWN_STATE2_MODE=zero-runtime-server-position"
node -r ".\respawn-state2-field-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"
echo.
echo Node exited with code %RC%.
exit /b %RC%

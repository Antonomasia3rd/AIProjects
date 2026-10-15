@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.21 - TEST B: zero runtime only
echo ----------------------------------------------------------
echo Changes ONLY Respawn ClientReadyToSpawn/state-2 runtime_entity_id:
echo   self runtime ID -^> 0
echo Position stays exactly as bot.js sends it.
echo No loading-screen/interact/init replay.
echo.

set "MC_RESPAWN_STATE2_MODE=zero-runtime"
node -r ".\respawn-state2-field-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"
echo.
echo Node exited with code %RC%.
exit /b %RC%

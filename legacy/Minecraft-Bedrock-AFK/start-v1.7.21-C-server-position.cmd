@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.21 - TEST C: server position only
echo -------------------------------------------------------------
echo Changes ONLY Respawn ClientReadyToSpawn/state-2 position:
echo   bot position -^> latest server Respawn(state-0) position
echo Runtime ID remains the bot's own runtime ID.
echo No loading-screen/interact/init replay.
echo.

set "MC_RESPAWN_STATE2_MODE=server-position"
node -r ".\respawn-state2-field-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"
echo.
echo Node exited with code %RC%.
exit /b %RC%

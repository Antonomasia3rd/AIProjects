@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.20 safe ready-handshake replay lab
echo -------------------------------------------------------------
echo - bot.js remains unchanged
echo - records the successful initial-join ready handshake
echo - after respawn finalizer, replays ONLY safe readiness packets
echo - NEVER replays StartLoadingScreen
echo - JSONL auto-closes/unlocks after the observation window
echo.

node -r ".\respawn-ready-replay-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"

echo.
echo Node exited with code %RC%.
exit /b %RC%

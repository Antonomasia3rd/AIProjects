@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.19 reference-timing lab
echo --------------------------------------------------
echo - bot.js remains unchanged
echo - EXACT test: delay ClientReadyToSpawn/state-2 until 200 ms after death
echo - final PlayerAction remains the existing wire-6 bot logic
echo - no finalizer retries
echo - JSONL auto-closes/unlocks after the observation window
echo.

node -r ".\respawn-reference-timing-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"

echo.
echo Node exited with code %RC%.
exit /b %RC%

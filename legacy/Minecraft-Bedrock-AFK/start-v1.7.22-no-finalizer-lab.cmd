@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.22 - NO FINALIZER A/B LAB
echo ----------------------------------------------------
echo Normal state-2 packet is NOT modified.
echo After server ReadyToSpawn, standalone PlayerAction wire-6 is swallowed.
echo No loading-screen/interact/init replay.
echo No action-7.
echo No PlayerAuthInput block_action.
echo.

node -r ".\respawn-no-finalizer-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"

echo.
echo Node exited with code %RC%.
exit /b %RC%

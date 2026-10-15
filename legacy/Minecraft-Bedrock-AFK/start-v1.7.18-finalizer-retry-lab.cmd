@echo off
setlocal
cd /d "%~dp0"

echo.
echo Minecraft-Bedrock-AFK v1.7.18 finalizer-retry lab
echo -------------------------------------------------
echo - bot.js remains unchanged
echo - ignores initial-join respawn packets
echo - compact console logging
echo - never sends action 7
echo - never uses PlayerAuthInput block_action
echo - retries the SAME final wire-6 PlayerAction only if no alive evidence appears
echo.

node -r ".\respawn-finalizer-retry-lab.js" ".\bot.js"
set "RC=%ERRORLEVEL%"

echo.
echo Node exited with code %RC%.
exit /b %RC%

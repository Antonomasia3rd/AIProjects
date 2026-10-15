@echo off
setlocal
cd /d "%~dp0"
echo.
echo Minecraft-Bedrock-AFK v1.7.17 diagnostic overlay
echo ------------------------------------------------
echo bot.js is NOT modified by this launcher.
echo Diagnostic JSONL will be written under .\diag\
echo.
node -r ".\respawn-lifecycle-diag-preload.js" ".\bot.js"
set "RC=%ERRORLEVEL%"
echo.
echo Node exited with code %RC%.
exit /b %RC%

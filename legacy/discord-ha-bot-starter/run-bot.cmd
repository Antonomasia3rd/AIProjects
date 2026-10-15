@echo off
cd /d "%~dp0"

if not exist "logs" mkdir "logs"

echo [%date% %time%] Starting bot...>>"logs\launcher.log"
node dist\watchdog.js >>"logs\bot.log" 2>&1
set "BOT_EXIT_CODE=%ERRORLEVEL%"

echo [%date% %time%] Bot exited with code %BOT_EXIT_CODE%.>>"logs\launcher.log"
exit /b %BOT_EXIT_CODE%

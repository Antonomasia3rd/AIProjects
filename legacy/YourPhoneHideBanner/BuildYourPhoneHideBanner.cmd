@echo off
call "%~dp0..\..\dependencies\build_registry_notification_app.cmd" "%~dp0." YourPhoneHideBanner
exit /b %ERRORLEVEL%

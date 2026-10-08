@echo off
rem CI entry. Use the separate build recipe for local scan-before-run checks.
call "%~dp0BuildRegistryNotificationAppTests.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\build\review-validation\RegistryNotificationAppTests.exe"
exit /b %ERRORLEVEL%

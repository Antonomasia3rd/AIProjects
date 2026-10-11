@echo off
rem CI entry point. Local validation uses the compile-only recipe and scan gate.
call "%~dp0BuildSecureDesktopControlTests.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\build\review-validation\SecureDesktopControlTests.exe"
exit /b %ERRORLEVEL%

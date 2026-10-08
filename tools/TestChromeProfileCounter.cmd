@echo off
setlocal EnableExtensions
rem CI entry point. Local validation should use the compile-only recipes,
rem scan each resulting executable, then run it with an independent timeout.
call "%~dp0BuildChromeProfileCounterEngineTests.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\build\review-validation\ChromeProfileCounterEngineTests.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%~dp0BuildChromeProfileCounterAppTests.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\build\review-validation\ChromeProfileCounterAppTests.exe"
exit /b %ERRORLEVEL%

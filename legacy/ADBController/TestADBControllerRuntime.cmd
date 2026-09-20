@echo off
setlocal EnableExtensions
call "%~dp0BuildADBController.cmd" test-inert
exit /b %ERRORLEVEL%

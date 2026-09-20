@echo off
setlocal EnableExtensions

call "%~dp0BuildADBController.cmd" check
exit /b %ERRORLEVEL%

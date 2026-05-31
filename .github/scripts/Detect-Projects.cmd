@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" detect-projects %*
exit /b %ERRORLEVEL%

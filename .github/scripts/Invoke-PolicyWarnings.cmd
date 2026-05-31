@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" policy-warnings %*
exit /b %ERRORLEVEL%

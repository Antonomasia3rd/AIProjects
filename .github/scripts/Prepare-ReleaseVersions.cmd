@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" prepare-release-versions %*
exit /b %ERRORLEVEL%

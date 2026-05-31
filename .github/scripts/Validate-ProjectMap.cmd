@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" validate-project-map %*
exit /b %ERRORLEVEL%

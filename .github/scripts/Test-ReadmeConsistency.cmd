@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" readme-consistency %*
exit /b %ERRORLEVEL%

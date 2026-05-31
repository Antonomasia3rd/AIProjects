@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" checksum-summary %*
exit /b %ERRORLEVEL%

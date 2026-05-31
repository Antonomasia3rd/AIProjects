@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" smoke-windows-build %*
exit /b %ERRORLEVEL%

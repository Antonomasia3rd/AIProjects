@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" publish-project-releases %*
exit /b %ERRORLEVEL%

@echo off
setlocal EnableExtensions
call "%~dp0repo-tools.cmd" test-workflow-project-selection %*
exit /b %ERRORLEVEL%

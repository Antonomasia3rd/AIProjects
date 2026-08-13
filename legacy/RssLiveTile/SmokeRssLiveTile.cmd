@echo off
setlocal EnableExtensions
call "%~dp0..\.github\scripts\repo-tools.cmd" smoke-rss-live-tile %*
exit /b %ERRORLEVEL%

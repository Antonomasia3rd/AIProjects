@echo off
setlocal EnableExtensions
call "%~dp0legacy\DNSAutoUpdate\DNSAutoUpdate.cmd" %*
exit /b %ERRORLEVEL%

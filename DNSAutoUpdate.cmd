@echo off
setlocal EnableExtensions
call "%~dp0DNSAutoUpdate\DNSAutoUpdate.cmd" %*
exit /b %ERRORLEVEL%

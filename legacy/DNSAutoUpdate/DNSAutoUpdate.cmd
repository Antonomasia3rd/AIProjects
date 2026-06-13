@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "EXE=%ROOT%build\DNSAutoUpdate.exe"
call "%ROOT%BuildDNSAutoUpdate.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

"%EXE%" %*
exit /b %ERRORLEVEL%

@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "EXE=%ROOT%build\DNSAutoUpdate.exe"
call "%ROOT%BuildDNSAutoUpdate.cmd" || exit /b %ERRORLEVEL%

"%EXE%" %*
exit /b %ERRORLEVEL%

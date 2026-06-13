@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "EXE=%ROOT%build\TaskSchedulerMigration.exe"
call "%ROOT%BuildTaskSchedulerMigration.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

"%EXE%" %*
exit /b %ERRORLEVEL%

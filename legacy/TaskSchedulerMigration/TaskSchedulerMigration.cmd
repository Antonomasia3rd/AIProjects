@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "EXE=%ROOT%build\TaskSchedulerMigration.exe"
call "%ROOT%BuildTaskSchedulerMigration.cmd" || exit /b %ERRORLEVEL%

"%EXE%" %*
exit /b %ERRORLEVEL%

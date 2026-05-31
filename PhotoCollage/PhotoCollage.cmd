@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "EXE=%ROOT%build\PhotoCollage.exe"
call "%ROOT%BuildPhotoCollage.cmd" || exit /b %ERRORLEVEL%

"%EXE%" %*
exit /b %ERRORLEVEL%

@echo off
setlocal EnableExtensions
rem CI only. Local runs should compile, scan, then execute with a timeout.
call "%~dp0BuildYouTubeMusicFrontendTests.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
"%~dp0..\build\review-validation\YouTubeMusicFrontendTests.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
powershell -NoProfile -File "%~dp0..\legacy\YouTubeMusicMigrate\tools\YouTubeMusicMigrateWrapperTests.ps1" -RepositoryRoot "%~dp0.."
exit /b %ERRORLEVEL%

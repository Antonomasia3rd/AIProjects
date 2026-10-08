@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
for %%I in ("%ROOT%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT=%ROOT%build"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:YouTubeMusicMigrate /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Web.Extensions.dll /out:"%OUT%\YouTubeMusicMigrate.exe" ^
 "%REPO%\dependencies\managed_named_objects.cs" "%REPO%\dependencies\managed_ini.cs" "%REPO%\dependencies\managed_profile.cs" ^
 "%REPO%\dependencies\managed_tray.cs" "%REPO%\dependencies\managed_configuration_tray.cs" "%REPO%\dependencies\managed_configuration_startup.cs" "%REPO%\dependencies\managed_startup_shortcut.cs" ^
 "%REPO%\dependencies\YouTubeMusicMigrate\youtube_music_profile.cs" "%REPO%\dependencies\YouTubeMusicMigrate\youtube_music_frontend.cs" "%ROOT%YouTubeMusicMigrate.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
for %%F in (youtube_music_worker_bridge.ps1 youtube_music_tidy_app.ps1 youtube_music_legacy_frontend.ps1) do (
 copy /y "%REPO%\dependencies\YouTubeMusicMigrate\%%F" "%OUT%\%%F" >nul
 if errorlevel 1 exit /b 1
)
copy /y "%REPO%\dependencies\powershell_native_launcher.ps1" "%OUT%\powershell_native_launcher.ps1" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built YouTubeMusicMigrate and its PowerShell worker files. Nothing was executed.
exit /b 0

@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT=%ROOT%\build\review-validation"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:YouTubeMusicFrontendTests /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Web.Extensions.dll /out:"%OUT%\YouTubeMusicFrontendTests.exe" ^
 "%ROOT%\dependencies\managed_named_objects.cs" "%ROOT%\dependencies\managed_ini.cs" "%ROOT%\dependencies\managed_profile.cs" ^
 "%ROOT%\dependencies\managed_tray.cs" "%ROOT%\dependencies\managed_configuration_tray.cs" "%ROOT%\dependencies\managed_configuration_startup.cs" "%ROOT%\dependencies\managed_startup_shortcut.cs" ^
 "%ROOT%\dependencies\YouTubeMusicMigrate\youtube_music_profile.cs" "%ROOT%\dependencies\YouTubeMusicMigrate\youtube_music_frontend.cs" ^
 "%ROOT%\legacy\YouTubeMusicMigrate\YouTubeMusicMigrate.cs" "%ROOT%\tools\YouTubeMusicFrontendTests.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built the inert YouTube frontend fixture. Nothing was executed.
exit /b 0

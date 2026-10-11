@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
for %%I in ("%ROOT%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%ROOT%build" mkdir "%ROOT%build"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:SecureDesktopLauncherControl ^
 /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
 /out:"%ROOT%build\SecureDesktopLauncherControl.exe" ^
 "%REPO%\dependencies\managed_ini.cs" "%REPO%\dependencies\managed_named_objects.cs" ^
 "%REPO%\dependencies\managed_profile.cs" "%REPO%\dependencies\managed_tray.cs" ^
 "%REPO%\dependencies\managed_configuration_tray.cs" "%REPO%\dependencies\managed_configuration_startup.cs" ^
 "%REPO%\dependencies\managed_startup_shortcut.cs" ^
 "%REPO%\dependencies\SecureDesktopLauncher\control_profile.cs" "%REPO%\dependencies\SecureDesktopLauncher\control_app.cs" ^
 "%ROOT%SecureDesktopLauncherControl.cs"
exit /b %ERRORLEVEL%

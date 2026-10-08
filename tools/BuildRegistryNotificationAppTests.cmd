@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "DEPS=%ROOT%\dependencies\"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%ROOT%\build\review-validation" mkdir "%ROOT%\build\review-validation"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:RegistryNotificationAppTests ^
 /r:System.Core.dll /r:System.ServiceProcess.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll ^
 /out:"%ROOT%\build\review-validation\RegistryNotificationAppTests.exe" ^
 "%DEPS%registry_notification_service.cs" "%DEPS%registry_notification_app.cs" ^
 "%DEPS%managed_named_objects.cs" "%DEPS%managed_ini.cs" "%DEPS%managed_profile.cs" ^
 "%DEPS%managed_tray.cs" "%DEPS%managed_configuration_tray.cs" ^
 "%DEPS%managed_configuration_startup.cs" "%DEPS%managed_startup_shortcut.cs" ^
 "%ROOT%\tools\RegistryNotificationAppTests.cs"
exit /b %ERRORLEVEL%

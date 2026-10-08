@echo off
setlocal EnableExtensions
rem Compile only. The product supplies its folder and metadata source name.
set "PRODUCT_DIR=%~f1"
set "PRODUCT=%~2"
set "DEPS=%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%PRODUCT_DIR%\%PRODUCT%.cs" exit /b 2
if not exist "%CSC%" exit /b 2
if not exist "%PRODUCT_DIR%\build" mkdir "%PRODUCT_DIR%\build"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe ^
 /r:System.Core.dll /r:System.ServiceProcess.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll ^
 /out:"%PRODUCT_DIR%\build\%PRODUCT%.exe" ^
 "%DEPS%registry_notification_service.cs" "%DEPS%registry_notification_app.cs" ^
 "%DEPS%managed_named_objects.cs" "%DEPS%managed_ini.cs" "%DEPS%managed_profile.cs" ^
 "%DEPS%managed_tray.cs" "%DEPS%managed_configuration_tray.cs" ^
 "%DEPS%managed_configuration_startup.cs" "%DEPS%managed_startup_shortcut.cs" ^
 "%PRODUCT_DIR%\%PRODUCT%.cs"
exit /b %ERRORLEVEL%

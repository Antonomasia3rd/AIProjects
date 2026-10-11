@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if not exist "%ROOT%\build\review-validation" mkdir "%ROOT%\build\review-validation"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:SecureDesktopControlTests ^
 /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
 /out:"%ROOT%\build\review-validation\SecureDesktopControlTests.exe" ^
 "%ROOT%\dependencies\managed_ini.cs" "%ROOT%\dependencies\managed_named_objects.cs" ^
 "%ROOT%\dependencies\managed_profile.cs" "%ROOT%\dependencies\managed_tray.cs" ^
 "%ROOT%\dependencies\managed_configuration_tray.cs" "%ROOT%\dependencies\managed_configuration_startup.cs" ^
 "%ROOT%\dependencies\managed_startup_shortcut.cs" ^
 "%ROOT%\dependencies\SecureDesktopLauncher\control_profile.cs" "%ROOT%\dependencies\SecureDesktopLauncher\control_app.cs" ^
 "%ROOT%\tools\SecureDesktopControlTests.cs"
exit /b %ERRORLEVEL%

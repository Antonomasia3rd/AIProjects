@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT=%ROOT%\build"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ManagedIniTransactionTests /r:System.Core.dll /out:"%OUT%\ManagedIniTransactionTests.exe" ^
  "%ROOT%\tools\ManagedIniTransactionTests.cs" ^
  "%ROOT%\dependencies\managed_ini.cs" ^
  "%ROOT%\dependencies\managed_named_objects.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
"%OUT%\ManagedIniTransactionTests.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ConfigurationTrayUiTests /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /out:"%OUT%\ConfigurationTrayUiTests.exe" ^
  "%ROOT%\tools\ConfigurationTrayUiTests.cs" ^
  "%ROOT%\dependencies\managed_ini.cs" ^
  "%ROOT%\dependencies\managed_named_objects.cs" ^
  "%ROOT%\dependencies\managed_tray.cs" ^
  "%ROOT%\dependencies\managed_configuration_tray.cs" ^
  "%ROOT%\dependencies\managed_configuration_startup.cs" ^
  "%ROOT%\dependencies\managed_startup_shortcut.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
"%OUT%\ConfigurationTrayUiTests.exe"
exit /b %ERRORLEVEL%

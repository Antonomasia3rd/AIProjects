@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
for %%I in ("%ROOT%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%build"
if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ChromeProfileCounter /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Web.Extensions.dll /out:"%OUT_DIR%\ChromeProfileCounter.exe" ^
  "%REPO%\dependencies\managed_named_objects.cs" ^
  "%REPO%\dependencies\managed_ini.cs" ^
  "%REPO%\dependencies\managed_profile.cs" ^
  "%REPO%\dependencies\managed_tray.cs" ^
  "%REPO%\dependencies\managed_configuration_tray.cs" ^
  "%REPO%\dependencies\managed_configuration_startup.cs" ^
  "%REPO%\dependencies\managed_startup_shortcut.cs" ^
  "%REPO%\dependencies\ChromeProfileCounter\chrome_profile_counter_engine.cs" ^
  "%REPO%\dependencies\ChromeProfileCounter\chrome_profile_counter_app.cs" ^
  "%ROOT%ChromeProfileCounter.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
copy /y "%REPO%\dependencies\powershell_native_launcher.ps1" "%OUT_DIR%\powershell_native_launcher.ps1" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built "%OUT_DIR%\ChromeProfileCounter.exe". Nothing was executed.
exit /b 0

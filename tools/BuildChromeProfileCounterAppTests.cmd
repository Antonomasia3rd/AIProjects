@echo off
setlocal EnableExtensions
rem Compile only. Scan the resulting binary before executing fixture tests.
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%\build\review-validation"
if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ChromeProfileCounterAppTests /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Web.Extensions.dll /out:"%OUT_DIR%\ChromeProfileCounterAppTests.exe" ^
  "%ROOT%\dependencies\managed_named_objects.cs" ^
  "%ROOT%\dependencies\managed_ini.cs" ^
  "%ROOT%\dependencies\managed_profile.cs" ^
  "%ROOT%\dependencies\managed_tray.cs" ^
  "%ROOT%\dependencies\managed_configuration_tray.cs" ^
  "%ROOT%\dependencies\managed_configuration_startup.cs" ^
  "%ROOT%\dependencies\managed_startup_shortcut.cs" ^
  "%ROOT%\dependencies\ChromeProfileCounter\chrome_profile_counter_engine.cs" ^
  "%ROOT%\dependencies\ChromeProfileCounter\chrome_profile_counter_app.cs" ^
  "%ROOT%\legacy\ChromeProfileCounter\ChromeProfileCounter.cs" ^
  "%ROOT%\tools\ChromeProfileCounterAppTests.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built "%OUT_DIR%\ChromeProfileCounterAppTests.exe". No tests were executed.
exit /b 0

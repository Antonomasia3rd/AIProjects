@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
for %%I in ("%ROOT%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%build"
set "OUT=%OUT_DIR%\asusblink.exe"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:winexe ^
  /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll ^
  /r:System.Management.dll /r:System.Runtime.Serialization.dll ^
  /out:"%OUT%" ^
  "%REPO%\dependencies\managed_named_objects.cs" ^
  "%REPO%\dependencies\managed_logging.cs" ^
  "%REPO%\dependencies\managed_ini.cs" ^
  "%REPO%\dependencies\managed_startup_shortcut.cs" ^
  "%REPO%\dependencies\managed_tray.cs" ^
  "%REPO%\dependencies\asusblink\asusblink_app.cs" ^
  "%ROOT%asusblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Built "%OUT%"
exit /b 0

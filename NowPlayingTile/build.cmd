@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%build"
set "OUT=%OUT_DIR%\NowPlayingTile.exe"
set "SYSTEM_RUNTIME=%WINDIR%\Microsoft.NET\assembly\GAC_MSIL\System.Runtime\v4.0_4.0.0.0__b03f5f7f11d50a3a\System.Runtime.dll"
set "WIN_METADATA=%WINDIR%\System32\WinMetadata"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)
if not exist "%SYSTEM_RUNTIME%" (
  echo ERROR: System.Runtime.dll not found at "%SYSTEM_RUNTIME%".
  exit /b 1
)
if not exist "%WIN_METADATA%\Windows.Foundation.winmd" (
  echo ERROR: Windows metadata not found under "%WIN_METADATA%".
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CSC%" /nologo /target:winexe /platform:x64 /out:"%OUT%" ^
  /r:System.dll ^
  /r:System.Core.dll ^
  /r:System.Drawing.dll ^
  /r:System.Windows.Forms.dll ^
  /r:"%SYSTEM_RUNTIME%" ^
  /r:"%WIN_METADATA%\Windows.Foundation.winmd" ^
  /r:"%WIN_METADATA%\Windows.Data.winmd" ^
  /r:"%WIN_METADATA%\Windows.Media.winmd" ^
  /r:"%WIN_METADATA%\Windows.Storage.winmd" ^
  /r:"%WIN_METADATA%\Windows.UI.winmd" ^
  "%ROOT%NowPlayingTile.cs"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Built "%OUT%"
exit /b 0

@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%REPO%\.github\build"
set "OUT=%OUT_DIR%\RepoTools.exe"
set "SRC=%REPO%\.github\tools\RepoTools.cs"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

:PickTemp
set "TMP_DIR=%OUT_DIR%\RepoTools.%RANDOM%%RANDOM%.tmp"
mkdir "%TMP_DIR%" >nul 2>nul
if errorlevel 1 goto PickTemp
set "TMP_OUT=%TMP_DIR%\RepoTools.exe"

"%CSC%" /nologo /optimize+ /warn:4 /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /out:"%TMP_OUT%" "%SRC%"
if errorlevel 1 (
  set "STATUS=%ERRORLEVEL%"
  rmdir "%TMP_DIR%" >nul 2>nul
  exit /b %STATUS%
)

copy /y "%TMP_OUT%" "%OUT%" >nul 2>nul

"%TMP_OUT%" %* --repository-root "%REPO%"
set "STATUS=%ERRORLEVEL%"
rmdir /s /q "%TMP_DIR%" >nul 2>nul
exit /b %STATUS%

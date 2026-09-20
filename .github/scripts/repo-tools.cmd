@echo off
setlocal EnableExtensions

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO_ROOT=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%REPO_ROOT%\.github\build"
set "OUT=%OUT_DIR%\RepoTools.exe"
set "SRC=%REPO_ROOT%\.github\tools\RepoTools.cs"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
set "TMP_ATTEMPTS=0"

:PickTemp
set /a TMP_ATTEMPTS+=1 >nul
if %TMP_ATTEMPTS% GTR 16 (
  echo ERROR: Could not create a temporary compiler directory under "%OUT_DIR%".
  exit /b 1
)
set "TMP_DIR=%OUT_DIR%\RepoTools.%RANDOM%%RANDOM%.tmp"
mkdir "%TMP_DIR%" >nul 2>nul
if errorlevel 1 goto PickTemp
set "TMP_OUT=%TMP_DIR%\RepoTools.exe"

"%CSC%" /nologo /optimize+ /warn:4 /r:System.Drawing.dll /r:System.Web.Extensions.dll /r:System.IO.Compression.dll /r:System.IO.Compression.FileSystem.dll /out:"%TMP_OUT%" "%SRC%"
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
  del /f /q "%TMP_OUT%" >nul 2>nul
  rmdir "%TMP_DIR%" >nul 2>nul
  exit /b %STATUS%
)

copy /y "%TMP_OUT%" "%OUT%" >nul 2>nul

"%TMP_OUT%" %* --repository-root "%REPO_ROOT%"
set "STATUS=%ERRORLEVEL%"
del /f /q "%TMP_OUT%" >nul 2>nul
rmdir "%TMP_DIR%" >nul 2>nul
exit /b %STATUS%

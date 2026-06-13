@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "CHECKER=%TEMP%\AIProjects-RegistryNotificationSourceCheck-%RANDOM%%RANDOM%.exe"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

"%CSC%" /nologo /optimize+ /target:exe /out:"%CHECKER%" "%REPO%\tools\RegistryNotificationServiceSourceCheck.cs"
if errorlevel 1 exit /b !ERRORLEVEL!

"%CHECKER%" "%REPO%"
set "STATUS=!ERRORLEVEL!"
del /f /q "%CHECKER%" >nul 2>nul
exit /b !STATUS!

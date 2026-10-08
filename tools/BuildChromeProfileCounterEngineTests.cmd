@echo off
setlocal EnableExtensions
rem Compile only. Scan the output before executing the inert fixture suite.
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%\build\review-validation"
set "OUT=%OUT_DIR%\ChromeProfileCounterEngineTests.exe"
if not exist "%CSC%" (
    echo ERROR: C# compiler not found at "%CSC%".
    exit /b 1
)
if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ChromeProfileCounterEngineTests /r:System.Core.dll /r:System.Web.Extensions.dll /out:"%OUT%" ^
    "%ROOT%\dependencies\ChromeProfileCounter\chrome_profile_counter_engine.cs" ^
    "%ROOT%\tools\ChromeProfileCounterEngineTests.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built "%OUT%". No tests were executed.
exit /b 0

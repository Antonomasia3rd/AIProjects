@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT=%ROOT%\build\review-validation"
if not exist "%OUT%" mkdir "%OUT%"
if errorlevel 1 exit /b %ERRORLEVEL%
"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:ManagedIniDocumentTests /r:System.Core.dll /out:"%OUT%\ManagedIniDocumentTests.exe" ^
 "%ROOT%\dependencies\managed_named_objects.cs" "%ROOT%\dependencies\managed_ini.cs" "%ROOT%\tools\ManagedIniDocumentTests.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
echo Built the inert multi-section INI fixture. Nothing was executed.
exit /b 0

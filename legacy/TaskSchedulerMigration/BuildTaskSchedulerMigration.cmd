@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%build"
set "OUT=%OUT_DIR%\TaskSchedulerMigration.exe"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

for %%I in ("%ROOT%..\..") do set "REPO=%%~fI"

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /r:Microsoft.CSharp.dll /out:"%OUT%" "%REPO%\dependencies\TaskSchedulerMigration\task_scheduler_migration_app.cs" "%ROOT%TaskSchedulerMigration.cs"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%ROOT%TestTaskSchedulerMigration.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

echo Built "%OUT%"
exit /b 0

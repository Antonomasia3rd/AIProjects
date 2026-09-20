@echo off
setlocal EnableExtensions

set "ROOT=%~dp0.."
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "OUT_DIR=%ROOT%\build"
set "OUT=%OUT_DIR%\LegacyUtilitiesTests.exe"

if not exist "%CSC%" (
    echo ERROR: C# compiler not found at "%CSC%".
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"
if errorlevel 1 exit /b %ERRORLEVEL%

"%CSC%" /nologo /warn:4 /optimize+ /define:LEGACY_UTILITY_TESTS /target:exe /main:LegacyUtilitiesTests /r:System.Core.dll /r:System.Drawing.dll /r:System.Windows.Forms.dll /r:System.Management.dll /r:System.Runtime.Serialization.dll /r:Microsoft.CSharp.dll /out:"%OUT%" ^
    "%ROOT%\tools\LegacyUtilitiesTests.cs" ^
    "%ROOT%\dependencies\DNSAutoUpdate\dns_auto_update_app.cs" ^
    "%ROOT%\legacy\DNSAutoUpdate\DNSAutoUpdate.cs" ^
    "%ROOT%\dependencies\PhotoCollage\photo_collage_app.cs" ^
    "%ROOT%\legacy\PhotoCollage\PhotoCollage.cs" ^
    "%ROOT%\dependencies\TaskSchedulerMigration\task_scheduler_migration_app.cs" ^
    "%ROOT%\legacy\TaskSchedulerMigration\TaskSchedulerMigration.cs" ^
    "%ROOT%\dependencies\capsblink\capsblink_app.cs" ^
    "%ROOT%\legacy\capsblink\capsblink.cs" ^
    "%ROOT%\dependencies\managed_named_objects.cs" ^
    "%ROOT%\dependencies\managed_logging.cs" ^
    "%ROOT%\dependencies\managed_ini.cs" ^
    "%ROOT%\dependencies\managed_startup_shortcut.cs" ^
    "%ROOT%\dependencies\managed_tray.cs" ^
    "%ROOT%\dependencies\asusblink\asusblink_app.cs" ^
    "%ROOT%\legacy\asusblink\asusblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%

"%OUT%" "%ROOT%" %*
exit /b %ERRORLEVEL%

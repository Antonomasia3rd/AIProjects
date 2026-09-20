@echo off
setlocal EnableExtensions EnableDelayedExpansion

for %%I in ("%~dp0..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "CHECKER=%TEMP%\AIProjects-RegistryNotificationSourceCheck-%RANDOM%%RANDOM%.exe"
set "RUNTIME=%TEMP%\AIProjects-RegistryNotificationRuntime-%RANDOM%%RANDOM%.exe"
set "ALLOW=%TEMP%\AIProjects-AllowContentService-%RANDOM%%RANDOM%.exe"
set "PHONE=%TEMP%\AIProjects-YourPhoneService-%RANDOM%%RANDOM%.exe"
set "OUTPUT=%TEMP%\AIProjects-RegistryNotificationOutput-%RANDOM%%RANDOM%.txt"
for %%I in ("%ALLOW%") do set "ALLOW_INI=%%~dpnI.ini"& set "ALLOW_LOG=%%~dpnI.log"
for %%I in ("%PHONE%") do set "PHONE_INI=%%~dpnI.ini"& set "PHONE_LOG=%%~dpnI.log"

if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /out:"%CHECKER%" "%REPO%\tools\RegistryNotificationServiceSourceCheck.cs"
if errorlevel 1 goto Fail

"%CHECKER%" "%REPO%"
if errorlevel 1 goto Fail

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /main:RegistryNotificationServiceRuntimeTests /r:System.ServiceProcess.dll /out:"%RUNTIME%" "%REPO%\dependencies\registry_notification_service.cs" "%REPO%\tools\RegistryNotificationServiceRuntimeTests.cs"
if errorlevel 1 goto Fail

"%RUNTIME%"
if errorlevel 1 goto Fail

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%ALLOW%" "%REPO%\dependencies\registry_notification_service.cs" "%REPO%\legacy\AllowContentAboveLock\AllowContentAboveLock.cs"
if errorlevel 1 goto Fail

"%CSC%" /nologo /warn:4 /warnaserror+ /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%PHONE%" "%REPO%\dependencies\registry_notification_service.cs" "%REPO%\legacy\YourPhoneHideBanner\YourPhoneHideBanner.cs"
if errorlevel 1 goto Fail

"%ALLOW%" --help >"%OUTPUT%" 2>&1
if errorlevel 1 goto Fail
findstr /c:"--install" /c:"--uninstall" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

"%ALLOW%" --version >"%OUTPUT%" 2>&1
if errorlevel 1 goto Fail
findstr /c:"AllowContentAboveLockService 1.0.0.0" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

"%PHONE%" --help >"%OUTPUT%" 2>&1
if errorlevel 1 goto Fail
findstr /c:"--install" /c:"--uninstall" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

"%PHONE%" --version >"%OUTPUT%" 2>&1
if errorlevel 1 goto Fail
findstr /c:"YourPhoneHideBannerService 1.0.0.0" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

rem No sibling INI exists, so this must fail during protected-path validation
rem before OpenSCManager/CreateService can perform a live service mutation.
"%ALLOW%" --install >"%OUTPUT%" 2>&1
if not errorlevel 1 goto Fail
findstr /c:"service executable" /c:"service configuration" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

"%PHONE%" --install >"%OUTPUT%" 2>&1
if not errorlevel 1 goto Fail
findstr /c:"service executable" /c:"service configuration" "%OUTPUT%" >nul
if errorlevel 1 goto Fail

"%ALLOW%" --unknown >"%OUTPUT%" 2>&1
if not errorlevel 1 goto Fail

if exist "%ALLOW_INI%" goto Fail
if exist "%ALLOW_LOG%" goto Fail
if exist "%PHONE_INI%" goto Fail
if exist "%PHONE_LOG%" goto Fail

echo Registry notification service binary checks passed.
set "STATUS=0"
goto Cleanup

:Fail
set "STATUS=!ERRORLEVEL!"
if "!STATUS!"=="0" set "STATUS=1"
echo ERROR: Registry notification service test failed. 1>&2

:Cleanup
del /f /q "%CHECKER%" >nul 2>nul
del /f /q "%RUNTIME%" >nul 2>nul
del /f /q "%ALLOW%" >nul 2>nul
del /f /q "%PHONE%" >nul 2>nul
del /f /q "%OUTPUT%" >nul 2>nul
del /f /q "%ALLOW_INI%" >nul 2>nul
del /f /q "%ALLOW_LOG%" >nul 2>nul
del /f /q "%PHONE_INI%" >nul 2>nul
del /f /q "%PHONE_LOG%" >nul 2>nul
exit /b !STATUS!

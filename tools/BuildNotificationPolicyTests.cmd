@echo off
setlocal EnableExtensions
set "NOTIFICATION_TEST_ROOT=%~dp0.."
set "NOTIFICATION_TEST_VS="
set "NOTIFICATION_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%NOTIFICATION_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "NOTIFICATION_TEST_VS=%%I"
if not defined NOTIFICATION_TEST_VS exit /b 2
call "%NOTIFICATION_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%NOTIFICATION_TEST_ROOT%" || exit /b 4
if not exist "build\notification-tests" mkdir "build\notification-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\NotificationPolicyTests.cpp /Fe:build\notification-tests\NotificationPolicyTests.exe /Fo:build\notification-tests\NotificationPolicyTests.obj /link ole32.lib user32.lib
if errorlevel 1 goto Done
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /DUNICODE /D_UNICODE /c tools\NotificationNativeCompileTests.cpp /Fo:build\notification-tests\NotificationNativeCompileTests.obj
:Done
set "NOTIFICATION_TEST_RESULT=%errorlevel%"
popd
exit /b %NOTIFICATION_TEST_RESULT%

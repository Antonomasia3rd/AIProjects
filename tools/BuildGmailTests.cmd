@echo off
setlocal EnableExtensions
set "GMAIL_ROOT=%~dp0.."
set "GMAIL_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%GMAIL_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GMAIL_VS=%%I"
if not defined GMAIL_VS exit /b 2
call "%GMAIL_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%GMAIL_ROOT%" || exit /b 4
if not exist "build\gmail-tests" mkdir "build\gmail-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\GmailTests.cpp /Fe:build\gmail-tests\GmailTests.exe /Fo:build\gmail-tests\GmailTests.obj /link winhttp.lib advapi32.lib crypt32.lib user32.lib shell32.lib shlwapi.lib ole32.lib
set "GMAIL_RESULT=%errorlevel%"
popd
exit /b %GMAIL_RESULT%

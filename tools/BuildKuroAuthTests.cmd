@echo off
setlocal EnableExtensions
set "KURO_AUTH_ROOT=%~dp0.."
set "KURO_AUTH_VS="
set "KURO_AUTH_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%KURO_AUTH_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "KURO_AUTH_VS=%%I"
if not defined KURO_AUTH_VS exit /b 2
call "%KURO_AUTH_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%KURO_AUTH_ROOT%" || exit /b 4
if not exist "build\kuro-auth-tests" mkdir "build\kuro-auth-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\KuroAuthTests.cpp /Fe:build\kuro-auth-tests\KuroAuthTests.exe /Fo:build\kuro-auth-tests\KuroAuthTests.obj /link winhttp.lib advapi32.lib crypt32.lib user32.lib shell32.lib shlwapi.lib
set "KURO_AUTH_RESULT=%errorlevel%"
popd
exit /b %KURO_AUTH_RESULT%

@echo off
setlocal EnableExtensions
set "KURO_TEST_ROOT=%~dp0.."
set "KURO_TEST_VS="
set "KURO_TEST_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%KURO_TEST_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "KURO_TEST_VS=%%I"
if not defined KURO_TEST_VS exit /b 2
call "%KURO_TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%KURO_TEST_ROOT%" || exit /b 4
if not exist "build\kuro-tests" mkdir "build\kuro-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\KuroNotesTests.cpp /Fe:build\kuro-tests\KuroNotesTests.exe /Fo:build\kuro-tests\KuroNotesTests.obj /link winhttp.lib advapi32.lib crypt32.lib user32.lib shell32.lib shlwapi.lib
if errorlevel 1 goto Done
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\HttpFetchTests.cpp /Fe:build\kuro-tests\HttpFetchKuroTests.exe /Fo:build\kuro-tests\HttpFetchKuroTests.obj /link winhttp.lib ws2_32.lib
:Done
set "KURO_TEST_RESULT=%errorlevel%"
popd
exit /b %KURO_TEST_RESULT%

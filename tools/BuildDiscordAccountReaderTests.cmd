@echo off
setlocal EnableExtensions
set "CONTENT_OPTIMIZE=1"
set "_CL_=/Ob1"
call "%~dp0BuildContentIntegrationTests.cmd" DiscordService
if errorlevel 1 exit /b %errorlevel%
set "READER_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%READER_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "READER_VS=%%I"
if not defined READER_VS exit /b 2
call "%READER_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%~dp0.." || exit /b 4
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX /bigobj /DUNICODE /D_UNICODE tools\DiscordAccountReaderTests.cpp build\content-integration-optimized\DiscordService.obj /Fe:build\content-integration-optimized\DiscordAccountReaderTests.exe /Fo:build\content-integration-optimized\DiscordAccountReaderTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
set "READER_RESULT=%errorlevel%"
popd
exit /b %READER_RESULT%

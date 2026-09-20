@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "SERVICE_VS="
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SERVICE_VS=%%I"
)
if defined SERVICE_VS call "%SERVICE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist build mkdir build
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\DiscordService.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\DiscordServiceTests.cpp build\DiscordService.obj /Fe:build\DiscordServiceTests.exe /Fo:build\DiscordServiceTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
if errorlevel 1 goto Failed
build\DiscordServiceTests.exe
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%
:Failed
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

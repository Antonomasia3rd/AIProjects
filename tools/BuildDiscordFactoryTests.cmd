@echo off
setlocal EnableExtensions
set "DISCORD_FACTORY_VS="
set "DISCORD_FACTORY_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%DISCORD_FACTORY_VSWHERE%" for /f "usebackq delims=" %%I in (`"%DISCORD_FACTORY_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DISCORD_FACTORY_VS=%%I"
if defined DISCORD_FACTORY_VS call "%DISCORD_FACTORY_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul || exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\discord-default-tests" mkdir "build\discord-default-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\discord-default-tests\DiscordService.obj
if errorlevel 1 goto Done
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\DiscordServiceTests.cpp build\discord-default-tests\DiscordService.obj /Fe:build\discord-default-tests\DiscordServiceTests.exe /Fo:build\discord-default-tests\DiscordServiceTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
:Done
set "DISCORD_FACTORY_STATUS=%errorlevel%"
popd
exit /b %DISCORD_FACTORY_STATUS%

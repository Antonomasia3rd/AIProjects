@echo off
setlocal EnableExtensions
set "DISCORD_MODEL_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%DISCORD_MODEL_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DISCORD_MODEL_VS=%%I"
if not defined DISCORD_MODEL_VS exit /b 1
call "%DISCORD_MODEL_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\discord-tile-model" mkdir "build\discord-tile-model"
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\DiscordTileModelTests.cpp /Fe:build\discord-tile-model\DiscordTileModelTests.exe /Fo:build\discord-tile-model\DiscordTileModelTests.obj
set "DISCORD_MODEL_RESULT=%errorlevel%"
popd
exit /b %DISCORD_MODEL_RESULT%

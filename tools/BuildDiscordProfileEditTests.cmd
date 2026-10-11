@echo off
setlocal
set "PROFILE_ROOT=%~dp0.."
set "PROFILE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%PROFILE_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%PROFILE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PROFILE_VS=%%I"
if not defined PROFILE_VS exit /b 1
call "%PROFILE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
pushd "%PROFILE_ROOT%" || exit /b 1
if not exist build mkdir build
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\DiscordProfileEditService.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\profile_edit.cpp /Fo:build\DiscordProfileEdit.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\DiscordProfileEditTests.cpp build\DiscordProfileEditService.obj build\DiscordProfileEdit.obj /Fe:build\DiscordProfileEditTests.exe /Fo:build\DiscordProfileEditTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
set "PROFILE_STATUS=%errorlevel%"
popd
exit /b %PROFILE_STATUS%
:Failed
set "PROFILE_STATUS=%errorlevel%"
popd
exit /b %PROFILE_STATUS%

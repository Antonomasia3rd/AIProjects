@echo off
setlocal EnableExtensions
set "CONTENT_ROOT=%~dp0.."
set "CONTENT_VS="
set "CONTENT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%CONTENT_VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%CONTENT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "CONTENT_VS=%%I"
)
if defined CONTENT_VS call "%CONTENT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%CONTENT_ROOT%" || exit /b 1
if not exist "build\content-integration" mkdir "build\content-integration"
set "CONTENT_OUT=build\content-integration"
set "CONTENT_FLAGS=/nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE"
set "CONTENT_NATIVE_LIBS=gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib"
if /i "%~1"=="DiscordService" goto DiscordService
if /i "%~1"=="DiscordEditor" goto DiscordEditor
if /i "%~1"=="AsusService" goto AsusService
if /i "%~1"=="HardwareProfileImportTests" goto Hardware
if /i "%~1"=="NowPlayingProfileImportTests" goto NowPlaying
if /i "%~1"=="NotesSourceTests" goto Notes
if /i "%~1"=="SmtcSourceTests" goto Smtc
if /i "%~1"=="DiscordProfileEditTests" goto Discord
if /i "%~1"=="ContentRuntimeTests" goto Runtime
if /i "%~1"=="ContentRuntimeNoHardwareTests" goto Unavailable
echo Unknown content fixture stage. This script compiles only.
set "CONTENT_STATUS=2"
goto Done
:DiscordService
cl %CONTENT_FLAGS% /c dependencies\DiscordRPC\service.cpp /Fo:%CONTENT_OUT%\DiscordService.obj
goto Result
:DiscordEditor
cl %CONTENT_FLAGS% /WX /c dependencies\DiscordRPC\profile_edit.cpp /Fo:%CONTENT_OUT%\DiscordEditor.obj
goto Result
:AsusService
cl %CONTENT_FLAGS% /WX /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:%CONTENT_OUT%\AsusService.obj
goto Result
:Hardware
cl %CONTENT_FLAGS% /WX tools\HardwareProfileImportTests.cpp /Fe:%CONTENT_OUT%\HardwareProfileImportTests.exe /Fo:%CONTENT_OUT%\HardwareProfileImportTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib
goto Result
:NowPlaying
cl %CONTENT_FLAGS% /WX tools\NowPlayingProfileImportTests.cpp /Fe:%CONTENT_OUT%\NowPlayingProfileImportTests.exe /Fo:%CONTENT_OUT%\NowPlayingProfileImportTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib
goto Result
:Notes
cl %CONTENT_FLAGS% /WX tools\NotesSourceTests.cpp /Fe:%CONTENT_OUT%\NotesSourceTests.exe /Fo:%CONTENT_OUT%\NotesSourceTests.obj /link winhttp.lib advapi32.lib
goto Result
:Smtc
cl %CONTENT_FLAGS% tools\SmtcSourceTests.cpp /Fe:%CONTENT_OUT%\SmtcSourceTests.exe /Fo:%CONTENT_OUT%\SmtcSourceTests.obj /link windowscodecs.lib advapi32.lib windowsapp.lib runtimeobject.lib ole32.lib
goto Result
:Discord
cl %CONTENT_FLAGS% /WX tools\DiscordProfileEditTests.cpp %CONTENT_OUT%\DiscordService.obj %CONTENT_OUT%\DiscordEditor.obj /Fe:%CONTENT_OUT%\DiscordProfileEditTests.exe /Fo:%CONTENT_OUT%\DiscordProfileEditTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
goto Result
:Runtime
cl %CONTENT_FLAGS% DesktopStub\tools\ContentRuntimeTests.cpp %CONTENT_OUT%\DiscordService.obj %CONTENT_OUT%\DiscordEditor.obj %CONTENT_OUT%\AsusService.obj /Fe:%CONTENT_OUT%\ContentRuntimeTests.exe /Fo:%CONTENT_OUT%\ContentRuntimeTests.obj /link %CONTENT_NATIVE_LIBS%
goto Result
:Unavailable
cl %CONTENT_FLAGS% DesktopStub\tools\ContentRuntimeNoHardwareTests.cpp %CONTENT_OUT%\DiscordService.obj %CONTENT_OUT%\DiscordEditor.obj /Fe:%CONTENT_OUT%\ContentRuntimeNoHardwareTests.exe /Fo:%CONTENT_OUT%\ContentRuntimeNoHardwareTests.obj /link %CONTENT_NATIVE_LIBS%
goto Result
:Result
set "CONTENT_STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %CONTENT_STATUS%

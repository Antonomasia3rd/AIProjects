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
if /i "%~1"=="DiscordServiceTests" goto DiscordServiceFixture
if /i "%~1"=="DiscordEditor" goto DiscordEditor
if /i "%~1"=="AsusService" goto AsusService
if /i "%~1"=="HardwareProfileImportTests" goto Hardware
if /i "%~1"=="NowPlayingProfileImportTests" goto NowPlaying
if /i "%~1"=="NotesSourceTests" goto Notes
if /i "%~1"=="SmtcSourceTests" goto Smtc
if /i "%~1"=="WinrtFactoryLifetimeTests" goto WinrtLifetime
if /i "%~1"=="TransparentBackgroundTests" goto Transparent
if /i "%~1"=="AdaptiveTileTextLayoutTests" goto AdaptiveLayout
if /i "%~1"=="AdaptiveTileTextRenderTests" goto AdaptiveRender
if /i "%~1"=="PngCrc32Tests" goto PngCrc
if /i "%~1"=="CaptureAttemptTests" goto CaptureAttempt
if /i "%~1"=="ContentPresetTests" goto ContentPreset
if /i "%~1"=="ContentQueueTests" goto ContentQueue
if /i "%~1"=="SecondaryTileTests" goto SecondaryTiles
if /i "%~1"=="TileVisualAssetsTests" goto TileVisuals
if /i "%~1"=="TileVisualAssetsRenderTests" goto TileVisualRender
if /i "%~1"=="NotesTileRowsTests" goto NoteRows
if /i "%~1"=="MediaTileLayoutTests" goto MediaTileLayout
if /i "%~1"=="CapsBlinkEngineTests" goto CapsEngine
if /i "%~1"=="TileNotificationPayloadTests" goto TilePayload
if /i "%~1"=="BoundedCaptureProcessTests" goto BoundedCapture
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
:DiscordServiceFixture
cl %CONTENT_FLAGS% /WX tools\DiscordServiceTests.cpp %CONTENT_OUT%\DiscordService.obj /Fe:%CONTENT_OUT%\DiscordServiceTests.exe /Fo:%CONTENT_OUT%\DiscordServiceTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib
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
:WinrtLifetime
cl %CONTENT_FLAGS% /WX tools\WinrtFactoryLifetimeTests.cpp /Fe:%CONTENT_OUT%\WinrtFactoryLifetimeTests.exe /Fo:%CONTENT_OUT%\WinrtFactoryLifetimeTests.obj /link windowsapp.lib runtimeobject.lib ole32.lib
goto Result
:Transparent
call tools\BuildTransparentBackgroundTests.cmd
goto Result
:AdaptiveLayout
call tools\BuildAdaptiveTileTextTests.cmd Layout
goto Result
:AdaptiveRender
call tools\BuildAdaptiveTileTextTests.cmd Render
goto Result
:PngCrc
cl %CONTENT_FLAGS% /WX tools\PngCrc32Tests.cpp /Fe:%CONTENT_OUT%\PngCrc32Tests.exe /Fo:%CONTENT_OUT%\PngCrc32Tests.obj
goto Result
:CaptureAttempt
cl %CONTENT_FLAGS% /WX tools\CaptureAttemptTests.cpp /Fe:%CONTENT_OUT%\CaptureAttemptTests.exe /Fo:%CONTENT_OUT%\CaptureAttemptTests.obj
goto Result
:ContentPreset
cl %CONTENT_FLAGS% /WX tools\ContentPresetTests.cpp /Fe:%CONTENT_OUT%\ContentPresetTests.exe /Fo:%CONTENT_OUT%\ContentPresetTests.obj
goto Result
:ContentQueue
cl %CONTENT_FLAGS% /WX tools\ContentQueueTests.cpp /Fe:%CONTENT_OUT%\ContentQueueTests.exe /Fo:%CONTENT_OUT%\ContentQueueTests.obj
goto Result
:SecondaryTiles
cl %CONTENT_FLAGS% /WX tools\SecondaryTileTests.cpp /Fe:%CONTENT_OUT%\SecondaryTileTests.exe /Fo:%CONTENT_OUT%\SecondaryTileTests.obj
goto Result
:TilePayload
cl %CONTENT_FLAGS% /WX tools\TileNotificationPayloadTests.cpp /Fe:%CONTENT_OUT%\TileNotificationPayloadTests.exe /Fo:%CONTENT_OUT%\TileNotificationPayloadTests.obj
goto Result
:TileVisuals
cl %CONTENT_FLAGS% /WX tools\TileVisualAssetsTests.cpp /Fe:%CONTENT_OUT%\TileVisualAssetsTests.exe /Fo:%CONTENT_OUT%\TileVisualAssetsTests.obj
goto Result
:NoteRows
cl %CONTENT_FLAGS% /WX tools\NotesTileRowsTests.cpp /Fe:%CONTENT_OUT%\NotesTileRowsTests.exe /Fo:%CONTENT_OUT%\NotesTileRowsTests.obj
goto Result
:MediaTileLayout
cl %CONTENT_FLAGS% /WX tools\MediaTileLayoutTests.cpp /Fe:%CONTENT_OUT%\MediaTileLayoutTests.exe /Fo:%CONTENT_OUT%\MediaTileLayoutTests.obj
goto Result
:TileVisualRender
cl %CONTENT_FLAGS% tools\TileVisualAssetsRenderTests.cpp /Fe:%CONTENT_OUT%\TileVisualAssetsRenderTests.exe /Fo:%CONTENT_OUT%\TileVisualAssetsRenderTests.obj /link gdiplus.lib gdi32.lib user32.lib shell32.lib shlwapi.lib advapi32.lib
goto Result
:CapsEngine
cl %CONTENT_FLAGS% /WX tools\CapsBlinkEngineTests.cpp /Fe:%CONTENT_OUT%\CapsBlinkEngineTests.exe /Fo:%CONTENT_OUT%\CapsBlinkEngineTests.obj
goto Result
:BoundedCapture
cl %CONTENT_FLAGS% /WX tools\BoundedCaptureProcessTests.cpp /Fe:%CONTENT_OUT%\BoundedCaptureProcessTests.exe /Fo:%CONTENT_OUT%\BoundedCaptureProcessTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib crypt32.lib
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

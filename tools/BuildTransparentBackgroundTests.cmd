@echo off
setlocal EnableExtensions
rem Compile only. Execution belongs to the scanned, bounded review runner.
set "TRANSPARENT_ROOT=%~dp0.."
set "TRANSPARENT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%TRANSPARENT_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%TRANSPARENT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TRANSPARENT_VS=%%I"
if not defined TRANSPARENT_VS exit /b 1
call "%TRANSPARENT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%TRANSPARENT_ROOT%" || exit /b 1
set "TRANSPARENT_OUT=build\content-integration"
if not exist "%TRANSPARENT_OUT%" mkdir "%TRANSPARENT_OUT%"
rem Reuse only dependency objects built in this same validation run. The caller
rem first compiles DiscordService, DiscordEditor and inert AsusService stages.
if not exist "%TRANSPARENT_OUT%\DiscordService.obj" goto Missing
if not exist "%TRANSPARENT_OUT%\DiscordEditor.obj" goto Missing
if not exist "%TRANSPARENT_OUT%\AsusService.obj" goto Missing
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\TransparentBackgroundTests.cpp "%TRANSPARENT_OUT%\DiscordService.obj" "%TRANSPARENT_OUT%\DiscordEditor.obj" "%TRANSPARENT_OUT%\AsusService.obj" /Fe:"%TRANSPARENT_OUT%\TransparentBackgroundTests.exe" /Fo:"%TRANSPARENT_OUT%\TransparentBackgroundTests.obj" /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
set "TRANSPARENT_STATUS=%ERRORLEVEL%"
goto Done
:Missing
echo Build the three inert dependency stages with BuildContentIntegrationTests.cmd first.
set "TRANSPARENT_STATUS=2"
:Done
popd
exit /b %TRANSPARENT_STATUS%

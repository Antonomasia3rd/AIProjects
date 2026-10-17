@echo off
setlocal EnableExtensions
rem Compile only. Scan and run each fixture separately in the bounded runner.
set "ADAPTIVE_ROOT=%~dp0.."
set "ADAPTIVE_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%ADAPTIVE_VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%ADAPTIVE_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ADAPTIVE_VS=%%I"
if not defined ADAPTIVE_VS exit /b 1
call "%ADAPTIVE_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%ADAPTIVE_ROOT%" || exit /b 1
set "ADAPTIVE_OUT=build\content-integration"
set "ADAPTIVE_OPTIMIZATION=/fp:precise"
if "%CONTENT_OPTIMIZE%"=="1" set "ADAPTIVE_OUT=build\content-integration-optimized"
if "%CONTENT_OPTIMIZE%"=="1" set "ADAPTIVE_OPTIMIZATION=/O2 /fp:precise"
if not exist "%ADAPTIVE_OUT%" mkdir "%ADAPTIVE_OUT%"
if /i "%~1"=="Layout" goto Layout
if /i "%~1"=="Render" goto Render
set "ADAPTIVE_STATUS=2"
goto Done
:Layout
cl /nologo %ADAPTIVE_OPTIMIZATION% /utf-8 /std:c++17 /EHsc /W4 DesktopStub\tools\AdaptiveTileTextLayoutTests.cpp /Fe:"%ADAPTIVE_OUT%\AdaptiveTileTextLayoutTests.exe" /Fo:"%ADAPTIVE_OUT%\AdaptiveTileTextLayoutTests.obj"
goto Result
:Render
rem Dependency objects must be compiled in this same coordinated validation run.
if not exist "%ADAPTIVE_OUT%\DiscordService.obj" goto Missing
if not exist "%ADAPTIVE_OUT%\DiscordEditor.obj" goto Missing
if not exist "%ADAPTIVE_OUT%\AsusService.obj" goto Missing
cl /nologo %ADAPTIVE_OPTIMIZATION% /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\AdaptiveTileTextRenderTests.cpp "%ADAPTIVE_OUT%\DiscordService.obj" "%ADAPTIVE_OUT%\DiscordEditor.obj" "%ADAPTIVE_OUT%\AsusService.obj" /Fe:"%ADAPTIVE_OUT%\AdaptiveTileTextRenderTests.exe" /Fo:"%ADAPTIVE_OUT%\AdaptiveTileTextRenderTests.obj" /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
goto Result
:Missing
echo Build DiscordService, DiscordEditor and inert AsusService first.
set "ADAPTIVE_STATUS=2"
goto Done
:Result
set "ADAPTIVE_STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %ADAPTIVE_STATUS%

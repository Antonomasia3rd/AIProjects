@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "NPIMPORT_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "NPIMPORT_VS=%%I"
)
if defined NPIMPORT_VS call "%NPIMPORT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist "build\review-validation" mkdir "build\review-validation"
if /i "%~1"=="asus" goto Asus
if /i "%~1"=="discord" goto Discord
if /i "%~1"=="runtime" goto Runtime
if /i "%~1"=="passive" goto Passive
set "STATUS=2"
goto Done
:Asus
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:build\review-validation\NowPlayingAsusServiceOct11.obj
goto Result
:Discord
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\review-validation\NowPlayingDiscordServiceOct11.obj
if errorlevel 1 goto Result
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\profile_edit.cpp /Fo:build\review-validation\NowPlayingDiscordProfileEditOct11.obj
goto Result
:Runtime
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\ContentRuntimeTests.cpp build\review-validation\NowPlayingDiscordServiceOct11.obj build\review-validation\NowPlayingDiscordProfileEditOct11.obj build\review-validation\NowPlayingAsusServiceOct11.obj /Fe:build\review-validation\NowPlayingContentRuntimeTestsOct11.exe /Fo:build\review-validation\NowPlayingContentRuntimeTestsOct11.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
goto Result
:Passive
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /DDESKTOPSTUB_INERT_HARDWARE /DAIP_ASUS_NO_NATIVE_BACKEND DesktopStub\DesktopStub.cpp build\review-validation\NowPlayingDiscordServiceOct11.obj build\review-validation\NowPlayingDiscordProfileEditOct11.obj build\review-validation\NowPlayingAsusServiceOct11.obj /Fe:build\review-validation\DesktopStubNowPlayingPassiveOct11.exe /Fo:build\review-validation\DesktopStubNowPlayingPassiveOct11.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
:Result
set "STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %STATUS%

@echo off
setlocal EnableExtensions
set "TEST_ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" exit /b 1
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "TEST_VS=%%I"
if not defined TEST_VS exit /b 1
call "%TEST_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
pushd "%TEST_ROOT%" || exit /b 1
if not exist build mkdir build
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\ContentRuntimeDiscordService.obj
set "TEST_STATUS=%ERRORLEVEL%"
if not "%TEST_STATUS%"=="0" goto Done
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:build\ContentRuntimeAsusService.obj
set "TEST_STATUS=%ERRORLEVEL%"
if not "%TEST_STATUS%"=="0" goto Done
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\ContentRuntimeTests.cpp build\ContentRuntimeDiscordService.obj build\ContentRuntimeAsusService.obj /Fe:build\ContentRuntimeTests.exe /Fo:build\ContentRuntimeTests.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
set "TEST_STATUS=%ERRORLEVEL%"
if not "%TEST_STATUS%"=="0" goto Done
build\ContentRuntimeTests.exe
set "TEST_STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %TEST_STATUS%

@echo off
setlocal EnableExtensions
set "DISCORD_UI_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%DISCORD_UI_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "DISCORD_UI_VS=%%I"
if not defined DISCORD_UI_VS exit /b 1
call "%DISCORD_UI_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\discord-reader-ui" mkdir "build\discord-reader-ui"
if /i "%~1"=="CompileHost" goto CompileHost
if /i "%~1"=="Host" goto Host
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\DiscordReaderLayoutTests.cpp /Fe:build\discord-reader-ui\DiscordReaderLayoutTests.exe /Fo:build\discord-reader-ui\DiscordReaderLayoutTests.obj
goto Result
:CompileHost
cl /nologo /utf-8 /std:c++17 /O2 /Ob1 /EHsc /W4 /bigobj /DUNICODE /D_UNICODE /DDESKTOPSTUB_INERT_HARDWARE /DAIP_ASUS_NO_NATIVE_BACKEND /c DesktopStub\DesktopStub.cpp /Fo:build\discord-reader-ui\DesktopStubUiCompile.obj
goto Result
:Host
cl /nologo /utf-8 /std:c++17 /O2 /Ob1 /EHsc /W4 /bigobj /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\discord-reader-ui\DiscordReaderService.obj
if errorlevel 1 goto Result
cl /nologo /utf-8 /std:c++17 /O2 /Ob1 /EHsc /W4 /bigobj /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\profile_edit.cpp /Fo:build\discord-reader-ui\DiscordReaderEditor.obj
if errorlevel 1 goto Result
cl /nologo /utf-8 /std:c++17 /O2 /Ob1 /EHsc /W4 /bigobj /DUNICODE /D_UNICODE /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:build\discord-reader-ui\DiscordReaderAsus.obj
if errorlevel 1 goto Result
cl /nologo /utf-8 /std:c++17 /O2 /Ob1 /EHsc /W4 /bigobj /DUNICODE /D_UNICODE DesktopStub\tools\DiscordReaderHostTests.cpp build\discord-reader-ui\DiscordReaderService.obj build\discord-reader-ui\DiscordReaderEditor.obj build\discord-reader-ui\DiscordReaderAsus.obj /Fe:build\discord-reader-ui\DiscordReaderHostTests.exe /Fo:build\discord-reader-ui\DiscordReaderHostTests.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
:Result
set "DISCORD_UI_RESULT=%errorlevel%"
popd
exit /b %DISCORD_UI_RESULT%

@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "HWIMPORT_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "HWIMPORT_VS=%%I"
)
if defined HWIMPORT_VS call "%HWIMPORT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist "build\review-validation" mkdir "build\review-validation"
if /i "%~1"=="portable" goto Portable
if /i "%~1"=="asus" goto Asus
if /i "%~1"=="discord" goto Discord
if /i "%~1"=="runtime" goto Runtime
if /i "%~1"=="passive" goto Passive
if /i "%~1"=="unavailable" goto Unavailable
if /i "%~1"=="source" goto Source
echo Choose portable, asus, discord, runtime, passive, unavailable or source. All stages compile only.
set "STATUS=2"
goto Done
:Portable
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX tools\ContentEngineTests.cpp /Fe:build\review-validation\HardwareContentEngineTestsOct11.exe /Fo:build\review-validation\HardwareContentEngineTestsOct11.obj
goto Result
:Asus
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:build\review-validation\HardwareAsusServiceOct11.obj
goto Result
:Discord
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /c dependencies\DiscordRPC\service.cpp /Fo:build\review-validation\HardwareDiscordServiceOct11.obj
goto Result
:Runtime
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\ContentRuntimeTests.cpp build\review-validation\HardwareDiscordServiceOct11.obj build\review-validation\HardwareAsusServiceOct11.obj /Fe:build\review-validation\HardwareContentRuntimeTestsOct11.exe /Fo:build\review-validation\HardwareContentRuntimeTestsOct11.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
goto Result
:Passive
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE /DDESKTOPSTUB_INERT_HARDWARE /DAIP_ASUS_NO_NATIVE_BACKEND DesktopStub\DesktopStub.cpp build\review-validation\HardwareDiscordServiceOct11.obj build\review-validation\HardwareAsusServiceOct11.obj /Fe:build\review-validation\DesktopStubHardwarePassiveOct11.exe /Fo:build\review-validation\DesktopStubHardwarePassiveOct11.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
goto Result
:Unavailable
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub\tools\ContentRuntimeNoHardwareTests.cpp build\review-validation\HardwareDiscordServiceOct11.obj /Fe:build\review-validation\HardwareUnavailableRuntimeTestsOct11.exe /Fo:build\review-validation\HardwareUnavailableRuntimeTestsOct11.obj /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib winhttp.lib crypt32.lib windowsapp.lib runtimeobject.lib
goto Result
:Source
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX DesktopStub\tools\DesktopStubSourceCheck.cpp /Fe:build\review-validation\HardwareDesktopSourceCheckOct11.exe /Fo:build\review-validation\HardwareDesktopSourceCheckOct11.obj
:Result
set "STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %STATUS%

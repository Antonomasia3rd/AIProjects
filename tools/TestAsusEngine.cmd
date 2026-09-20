@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "ASUS_VS="
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ASUS_VS=%%I"
)
if defined ASUS_VS call "%ASUS_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 exit /b 1
pushd "%ROOT%" || exit /b 1
if not exist build mkdir build
rem Compile native code only: the fixture executable never links it.
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE /c dependencies\asusblink\native_backend.cpp /Fo:build\AsusNativeBackend.check.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DAIP_ASUS_NO_NATIVE_BACKEND /c dependencies\asusblink\service.cpp /Fo:build\AsusServiceTests.obj
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX tools\AsusEngineTests.cpp build\AsusServiceTests.obj /Fe:build\AsusEngineTests.exe /Fo:build\AsusEngineTests.obj
if errorlevel 1 goto Failed
build\AsusEngineTests.exe
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%
:Failed
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

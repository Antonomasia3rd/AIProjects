@echo off
setlocal EnableExtensions
set "GIB_VS="
set "GIB_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%GIB_VSWHERE%" for /f "usebackq delims=" %%I in (`"%GIB_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GIB_VS=%%I"
if defined GIB_VS call "%GIB_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul || exit /b 1
pushd "%~dp0.." || exit /b 1
if not exist "build\content-integration" mkdir "build\content-integration"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\GalleryIniBatchTests.cpp /Fe:build\content-integration\GalleryIniBatchTests.exe /Fo:build\content-integration\GalleryIniBatchTests.obj /link user32.lib shell32.lib shlwapi.lib advapi32.lib
set "GIB_STATUS=%errorlevel%"
popd
exit /b %GIB_STATUS%

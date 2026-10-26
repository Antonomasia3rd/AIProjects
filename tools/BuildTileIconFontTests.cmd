@echo off
setlocal EnableExtensions
set "ICON_FONT_ROOT=%~dp0.."
set "ICON_FONT_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%ICON_FONT_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "ICON_FONT_VS=%%I"
if not defined ICON_FONT_VS exit /b 2
call "%ICON_FONT_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%ICON_FONT_ROOT%" || exit /b 4
if not exist "build\icon-font-tests" mkdir "build\icon-font-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\TileIconFontPolicyTests.cpp /Fe:build\icon-font-tests\TileIconFontPolicyTests.exe /Fo:build\icon-font-tests\TileIconFontPolicyTests.obj
if errorlevel 1 (popd & exit /b 5)
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 tools\TileIconFontNativeTests.cpp /Fe:build\icon-font-tests\TileIconFontNativeTests.exe /Fo:build\icon-font-tests\TileIconFontNativeTests.obj /link gdiplus.lib user32.lib gdi32.lib shell32.lib shlwapi.lib ole32.lib uuid.lib advapi32.lib
set "ICON_FONT_STATUS=%errorlevel%"
popd
exit /b %ICON_FONT_STATUS%

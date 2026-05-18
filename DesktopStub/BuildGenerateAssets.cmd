@echo off
setlocal

set "ROOT=%~dp0"
set "VCVARS=D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat"

if exist "%VCVARS%" goto HaveVcvars
echo ERROR: vcvars64.bat not found:
echo   "%VCVARS%"
exit /b 1

:HaveVcvars

call "%VCVARS%" >nul
if errorlevel 1 exit /b %errorlevel%

pushd "%ROOT%" || exit /b 1
cl /nologo /std:c++17 /EHsc /DUNICODE /D_UNICODE GenerateAssets.cpp /Fe:GenerateAssets.exe /Fo:GenerateAssets.obj /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
popd

exit /b %STATUS%

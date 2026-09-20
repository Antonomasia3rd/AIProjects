@echo off
setlocal EnableExtensions
set "ROOT=%~dp0.."
set "VCVARS="
if defined VCINSTALLDIR if exist "%VCINSTALLDIR%\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VCINSTALLDIR%\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS if defined VSINSTALLDIR if exist "%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%VSINSTALLDIR%\VC\Auxiliary\Build\vcvars64.bat"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not defined VCVARS if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if not defined VCVARS if exist "%%I\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%I\VC\Auxiliary\Build\vcvars64.bat"
    )
)
if defined VCVARS goto LoadCompiler
where cl.exe >nul 2>nul
if not errorlevel 1 goto Compile
echo ERROR: Visual Studio C++ Build Tools are required. Alternatively run the portable CMake suite with GCC or Clang.
exit /b 1
:LoadCompiler
call "%VCVARS%" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
:Compile
pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
if errorlevel 1 goto Failed
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX tools\ContentEngineTests.cpp /Fe:build\ContentEngineTests.exe /Fo:build\ContentEngineTests.obj
if errorlevel 1 goto Failed
build\ContentEngineTests.exe
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%
:Failed
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

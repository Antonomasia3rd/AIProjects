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
for %%D in ("%ProgramFiles%" "%ProgramFiles(x86)%" "D:\Program Files" "D:\Program Files (x86)") do (
    for %%Y in (2022 2019) do (
        for %%E in (BuildTools Community Professional Enterprise) do (
            if not defined VCVARS if exist "%%~D\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%%~D\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars64.bat"
        )
    )
)
if defined VCVARS goto LoadCompiler
where cl.exe >nul 2>nul
if not errorlevel 1 goto Compile
echo ERROR: MSVC and the Windows SDK are required for the inert Caps engine tests.
exit /b 1
:LoadCompiler
call "%VCVARS%" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
:Compile
pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE tools\CapsBlinkEngineTests.cpp /Fe:build\CapsBlinkEngineTests.exe /Fo:build\CapsBlinkEngineTests.obj /link  /SUBSYSTEM:CONSOLE
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" goto Done
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /DUNICODE /D_UNICODE /Zs tools\CapsBlinkWindowsCompile.cpp
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" goto Done
call build\CapsBlinkEngineTests.exe
set "STATUS=%ERRORLEVEL%"
:Done
popd
exit /b %STATUS%

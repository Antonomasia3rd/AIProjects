@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "VCVARS="

call "%ROOT%..\tools\TestTileTextLayout.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%ROOT%..\tools\TestContentEngine.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%
call "%ROOT%..\tools\TestContentSourceHost.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%ROOT%..\tools\TestPackagedStartup.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

call "%ROOT%..\tools\TestAppxSharing.cmd"
if errorlevel 1 exit /b %ERRORLEVEL%

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

if defined VCVARS (
    call "%VCVARS%" >nul
    if errorlevel 1 exit /b %errorlevel%
    goto HaveCompiler
)

where cl.exe >nul 2>nul
if not errorlevel 1 goto HaveCompiler
echo ERROR: cl.exe not found. Install Visual Studio Build Tools with the C++ workload.
exit /b 1

:HaveCompiler
pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
if errorlevel 1 (
    popd
    exit /b %errorlevel%
)
if not exist "build\obj" mkdir "build\obj"
if errorlevel 1 (
    popd
    exit /b %errorlevel%
)

cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE tools\DesktopStubSourceCheck.cpp /Fe:build\DesktopStubSourceCheck.exe /Fo:build\obj\DesktopStubSourceCheck.obj
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)

build\DesktopStubSourceCheck.exe %*
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)

cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE tools\TileTextLayoutTests.cpp /Fe:build\TileTextLayoutTests.exe /Fo:build\obj\TileTextLayoutTests.obj
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)

build\TileTextLayoutTests.exe
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)

cl /nologo /std:c++17 /EHsc /W4 tools\TileTextTemplateTests.cpp /Fe:build\TileTextTemplateTests.exe /Fo:build\obj\TileTextTemplateTests.obj
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)
build\TileTextTemplateTests.exe
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)

cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE tools\TileTextRenderTests.cpp /Fe:build\TileTextRenderTests.exe /Fo:build\obj\TileTextRenderTests.obj /link gdiplus.lib user32.lib gdi32.lib
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)
build\TileTextRenderTests.exe build\tile-render-smoke
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%

rem The Windows CI build calls this suite once before compiling DesktopStub.
rem Keep the host/menu/snapshot harness after the portable and rendering tests.
call "%ROOT%..\tools\TestContentRuntime.cmd"
set "STATUS=%ERRORLEVEL%"
exit /b %STATUS%

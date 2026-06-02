@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
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

if not defined VCVARS (
  echo ERROR: vcvars64.bat not found. Install Visual Studio Build Tools with the C++ workload.
  exit /b 1
)

echo Using "%VCVARS%"
call "%VCVARS%" || exit /b %errorlevel%
pushd "%ROOT%" || exit /b 1
if /i "%1"=="check" (
  cl /nologo /Zs /EHsc /W4 SecureDesktopLauncherService.cpp
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if /i "%1"=="new" (
  if not exist build mkdir build
  cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncher.new.obj /Fe.\build\SecureDesktopLauncher.new.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if not exist build mkdir build
cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncher.obj /Fe.\build\SecureDesktopLauncher.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

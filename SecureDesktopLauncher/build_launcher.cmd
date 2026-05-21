@echo off
setlocal EnableExtensions
pushd "%~dp0" || exit /b 1
call "D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || (
  popd
  exit /b 1
)
if /i "%1"=="check" (
  cl /nologo /Zs /EHsc /W4 SecureDesktopLauncherService.cpp
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if /i "%1"=="new" (
  if not exist build mkdir build
  cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncherService.new.obj /Fe.\build\SecureDesktopLauncherService.new.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if not exist build mkdir build
cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncherService.obj /Fe.\build\SecureDesktopLauncherService.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

@echo off
call "D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
if /i "%1"=="check" (
  cl /nologo /Zs /EHsc /W4 SecureDesktopLauncherService.cpp
  exit /b %errorlevel%
)
if /i "%1"=="new" (
  if not exist build mkdir build
  cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncherService.new.obj /Fe.\build\SecureDesktopLauncherService.new.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib
  exit /b %errorlevel%
)
if not exist build mkdir build
cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopLauncherService.obj /Fe.\build\SecureDesktopLauncherService.exe SecureDesktopLauncherService.cpp advapi32.lib wtsapi32.lib userenv.lib

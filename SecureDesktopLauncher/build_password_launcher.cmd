@echo off
setlocal EnableExtensions
pushd "%~dp0" || exit /b 1
call "D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || (
  popd
  exit /b 1
)
if /i "%1"=="check" (
  cl /nologo /Zs /EHsc /W4 SecureDesktopPasswordLauncher.cpp
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if /i "%1"=="new" (
  if not exist build mkdir build
  cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopPasswordLauncher.new.obj /Fe.\build\SecureDesktopPasswordLauncher.new.exe SecureDesktopPasswordLauncher.cpp bcrypt.lib shell32.lib user32.lib gdi32.lib comctl32.lib version.lib /link /SUBSYSTEM:WINDOWS
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b %STATUS%
)
if not exist build mkdir build
cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopPasswordLauncher.obj /Fe.\build\SecureDesktopPasswordLauncher.exe SecureDesktopPasswordLauncher.cpp bcrypt.lib shell32.lib user32.lib gdi32.lib comctl32.lib version.lib /link /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
popd
exit /b %STATUS%

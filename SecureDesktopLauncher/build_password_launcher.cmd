@echo off
call "D:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" || exit /b 1
if /i "%1"=="check" (
  cl /nologo /Zs /EHsc /W4 SecureDesktopPasswordLauncher.cpp
  exit /b %errorlevel%
)
if /i "%1"=="new" (
  if not exist build mkdir build
  cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopPasswordLauncher.new.obj /Fe.\build\SecureDesktopPasswordLauncher.new.exe SecureDesktopPasswordLauncher.cpp bcrypt.lib shell32.lib user32.lib gdi32.lib comctl32.lib version.lib /link /SUBSYSTEM:WINDOWS
  exit /b %errorlevel%
)
if not exist build mkdir build
cl /nologo /EHsc /W4 /Fo.\build\SecureDesktopPasswordLauncher.obj /Fe.\build\SecureDesktopPasswordLauncher.exe SecureDesktopPasswordLauncher.cpp bcrypt.lib shell32.lib user32.lib gdi32.lib comctl32.lib version.lib /link /SUBSYSTEM:WINDOWS

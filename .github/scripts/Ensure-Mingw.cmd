@echo off
setlocal EnableExtensions

where g++.exe >nul 2>nul
if not errorlevel 1 (
  g++ --version
  exit /b 0
)

choco install mingw --version=13.2.0 -y --no-progress
if errorlevel 1 exit /b %ERRORLEVEL%

set "MINGW_BIN=C:\ProgramData\chocolatey\lib\mingw\tools\install\mingw64\bin"
if not exist "%MINGW_BIN%\g++.exe" (
  for /d /r "C:\ProgramData\chocolatey\lib\mingw\tools\install" %%I in (bin) do (
    if exist "%%I\g++.exe" (
      set "MINGW_BIN=%%I"
      goto :FoundMingw
    )
  )
)

:FoundMingw
if not exist "%MINGW_BIN%\g++.exe" (
  echo ERROR: MinGW-w64 was installed, but g++.exe could not be located.
  exit /b 1
)

>>"%GITHUB_PATH%" echo %MINGW_BIN%
exit /b 0

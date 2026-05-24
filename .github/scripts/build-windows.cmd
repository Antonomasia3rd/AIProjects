@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO=%%~fI"
set "CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
set "ARTIFACTS=%TEMP%\AIProjects-artifacts-%RANDOM%%RANDOM%.txt"
set "SKIP_LIST=,"
type nul > "%ARTIFACTS%"

:ParseArgs
if "%~1"=="" goto Main
set "ARG=%~1"
if /i "!ARG:~0,6!"=="/skip:" (
  set "SKIP_LIST=!SKIP_LIST!!ARG:~6!,"
) else if /i "!ARG:~0,7!"=="--skip:" (
  set "SKIP_LIST=!SKIP_LIST!!ARG:~7!,"
) else (
  set "SKIP_LIST=!SKIP_LIST!%~1,"
)
set "SKIP_LIST=!SKIP_LIST: =!"
shift
goto ParseArgs

:Main
if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  goto Fail
)
where tar.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: tar.exe is required to create release ZIP files.
  goto Fail
)
where certutil.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: certutil.exe is required to create SHA256 files.
  goto Fail
)

call :BuildAllowContentAboveLock || goto Fail
call :BuildAsusBlink || goto Fail
call :BuildCapsBlink || goto Fail
call :BuildYourPhoneHideBanner || goto Fail
call :BuildDiscordRPC || goto Fail
call :BuildNowPlayingTile || goto Fail
call :BuildGenerateAssets || goto Fail
call :BuildCharmTray || goto Fail
call :BuildSecureDesktopLauncher || goto Fail
call :BuildRealTimeNotesDeskband || goto Fail
call :WriteChecksums || goto Fail

call :Section "Artifacts"
for /f "usebackq delims=" %%A in ("%ARTIFACTS%") do (
  if not "%%~A"=="" (
    echo %%A
    echo %%A.sha256
  )
)

del /f /q "%ARTIFACTS%" >nul 2>nul
exit /b 0

:BuildAllowContentAboveLock
call :IsSkipped AllowContentAboveLock
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build AllowContentAboveLock"
if not exist "%REPO%\AllowContentAboveLock\build" mkdir "%REPO%\AllowContentAboveLock\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%REPO%\AllowContentAboveLock\build\AllowContentAboveLock.exe" "%REPO%\AllowContentAboveLock\AllowContentAboveLock.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\AllowContentAboveLock\build\AllowContentAboveLock.exe"
exit /b %ERRORLEVEL%

:BuildAsusBlink
call :IsSkipped asusblink
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build asusblink"
if not exist "%REPO%\asusblink\build" mkdir "%REPO%\asusblink\build"
call :Run "%CSC%" /nologo /optimize+ /target:winexe /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll /r:System.Runtime.Serialization.dll /out:"%REPO%\asusblink\build\asusblink.exe" "%REPO%\asusblink\asusblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\asusblink\build\asusblink.exe"
exit /b %ERRORLEVEL%

:BuildCapsBlink
call :IsSkipped capsblink
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build capsblink"
if not exist "%REPO%\capsblink\build" mkdir "%REPO%\capsblink\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /out:"%REPO%\capsblink\build\capsblink.exe" "%REPO%\capsblink\capsblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\capsblink\build\capsblink.exe"
exit /b %ERRORLEVEL%

:BuildYourPhoneHideBanner
call :IsSkipped YourPhoneHideBanner
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build YourPhoneHideBanner"
if not exist "%REPO%\YourPhoneHideBanner\build" mkdir "%REPO%\YourPhoneHideBanner\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%REPO%\YourPhoneHideBanner\build\YourPhoneHideBanner.exe" "%REPO%\YourPhoneHideBanner\YourPhoneHideBanner.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\YourPhoneHideBanner\build\YourPhoneHideBanner.exe"
exit /b %ERRORLEVEL%

:BuildDiscordRPC
call :IsSkipped DiscordRPC
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build DiscordRPC"
pushd "%REPO%\DiscordRPC" || exit /b 1
call :Run cmd.exe /d /c build.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\DiscordRPC\build\DiscordRPC.exe"
exit /b %ERRORLEVEL%

:BuildNowPlayingTile
call :IsSkipped NowPlayingTile
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build NowPlayingTile"
pushd "%REPO%\NowPlayingTile" || exit /b 1
call :Run cmd.exe /d /c build.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :PackageNowPlayingTile
exit /b %ERRORLEVEL%

:BuildGenerateAssets
call :IsSkipped GenerateAssets
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build GenerateAssets"
pushd "%REPO%\DesktopStub" || exit /b 1
call :Run cmd.exe /d /c BuildGenerateAssets.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\DesktopStub\build\GenerateAssets.exe"
exit /b %ERRORLEVEL%

:BuildCharmTray
call :IsSkipped CharmTray
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build CharmTray"
pushd "%REPO%\CharmTray" || exit /b 1
call :Run cmd.exe /d /c BuildCharmTray.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\CharmTray\build\CharmTray.exe"
exit /b %ERRORLEVEL%

:BuildSecureDesktopLauncher
call :IsSkipped SecureDesktopLauncher
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build SecureDesktopLauncher"
pushd "%REPO%\SecureDesktopLauncher" || exit /b 1
call :Run cmd.exe /d /c build_launcher.cmd
set "STATUS=%ERRORLEVEL%"
if "%STATUS%"=="0" (
  call :Run cmd.exe /d /c build_password_launcher.cmd
  set "STATUS=%ERRORLEVEL%"
)
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :PackageSecureDesktopLauncher
exit /b %ERRORLEVEL%

:BuildRealTimeNotesDeskband
call :IsSkipped RealTimeNotesDeskband
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build RealTimeNotesDeskband"
where g++.exe >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++.exe is required for RealTimeNotesDeskband.
  exit /b 1
)
pushd "%REPO%\RealTimeNotesDeskband" || exit /b 1
call :Run cmd.exe /d /c BuildDeskband.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :PackageRealTimeNotesDeskband
exit /b %ERRORLEVEL%

:PackageNowPlayingTile
call :Section "Package NowPlayingTile"
call :CreatePackageRoot NowPlayingTile || exit /b %ERRORLEVEL%
copy /y "%REPO%\NowPlayingTile\build\NowPlayingTile.exe" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
copy /y "%REPO%\NowPlayingTile\README.md" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
for %%F in (register-dev-package.ps1 unregister-dev-package.ps1 launch-packaged.ps1 launch-widget.ps1 install-startup.ps1 uninstall-startup.ps1 open-settings.ps1) do (
  if exist "%REPO%\NowPlayingTile\%%F" copy /y "%REPO%\NowPlayingTile\%%F" "%PKG_ROOT%\" >nul || exit /b !ERRORLEVEL!
)
if exist "%REPO%\NowPlayingTile\package" xcopy /e /i /y "%REPO%\NowPlayingTile\package" "%PKG_ROOT%\package\" >nul || exit /b %ERRORLEVEL%
call :WriteBuildInfo "%PKG_ROOT%" || exit /b %ERRORLEVEL%
set "ZIP=%REPO%\NowPlayingTile\build\NowPlayingTile-windows-x64.zip"
if exist "%ZIP%" del /f /q "%ZIP%" >nul 2>nul
tar.exe -a -c -f "%ZIP%" -C "%PKG_ROOT%" .
set "STATUS=%ERRORLEVEL%"
rmdir /s /q "%PKG_ROOT%" >nul 2>nul
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%ZIP%"
exit /b %ERRORLEVEL%

:PackageSecureDesktopLauncher
call :Section "Package SecureDesktopLauncher"
call :CreatePackageRoot SecureDesktopLauncher || exit /b %ERRORLEVEL%
copy /y "%REPO%\SecureDesktopLauncher\build\SecureDesktopLauncher.exe" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
copy /y "%REPO%\SecureDesktopLauncher\build\SecureDesktopPasswordLauncher.exe" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
copy /y "%REPO%\SecureDesktopLauncher\README.md" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
call :WriteBuildInfo "%PKG_ROOT%" || exit /b %ERRORLEVEL%
set "ZIP=%REPO%\SecureDesktopLauncher\build\SecureDesktopLauncher-windows-x64.zip"
if exist "%ZIP%" del /f /q "%ZIP%" >nul 2>nul
tar.exe -a -c -f "%ZIP%" -C "%PKG_ROOT%" .
set "STATUS=%ERRORLEVEL%"
rmdir /s /q "%PKG_ROOT%" >nul 2>nul
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%ZIP%"
exit /b %ERRORLEVEL%

:PackageRealTimeNotesDeskband
call :Section "Package RealTimeNotesDeskband"
call :CreatePackageRoot RealTimeNotesDeskband || exit /b %ERRORLEVEL%
copy /y "%REPO%\RealTimeNotesDeskband\build\RealTimeNotesDeskband.dll" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
copy /y "%REPO%\RealTimeNotesDeskband\README.md" "%PKG_ROOT%\" >nul || exit /b %ERRORLEVEL%
for %%F in (RegisterDeskband.cmd UnregisterDeskband.cmd ConfigureDeskband.cmd) do (
  if exist "%REPO%\RealTimeNotesDeskband\%%F" copy /y "%REPO%\RealTimeNotesDeskband\%%F" "%PKG_ROOT%\" >nul || exit /b !ERRORLEVEL!
)
call :WriteBuildInfo "%PKG_ROOT%" || exit /b %ERRORLEVEL%
set "ZIP=%REPO%\RealTimeNotesDeskband\build\RealTimeNotesDeskband-windows-x64.zip"
if exist "%ZIP%" del /f /q "%ZIP%" >nul 2>nul
tar.exe -a -c -f "%ZIP%" -C "%PKG_ROOT%" .
set "STATUS=%ERRORLEVEL%"
rmdir /s /q "%PKG_ROOT%" >nul 2>nul
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%ZIP%"
exit /b %ERRORLEVEL%

:CreatePackageRoot
set "PKG_ROOT=%TEMP%\AIProjects-%~1-%RANDOM%%RANDOM%"
if exist "%PKG_ROOT%" rmdir /s /q "%PKG_ROOT%" >nul 2>nul
mkdir "%PKG_ROOT%"
exit /b %ERRORLEVEL%

:WriteBuildInfo
set "DEST=%~1"
set "BUILD_SHA=%GITHUB_SHA%"
if not defined BUILD_SHA (
  for /f "delims=" %%I in ('git -C "%REPO%" rev-parse HEAD 2^>nul') do if not defined BUILD_SHA set "BUILD_SHA=%%I"
)
set "BUILD_REF=%GITHUB_REF%"
if not defined BUILD_REF (
  for /f "delims=" %%I in ('git -C "%REPO%" branch --show-current 2^>nul') do if not defined BUILD_REF set "BUILD_REF=%%I"
)
set "BUILD_REPO=%GITHUB_REPOSITORY%"
if not defined BUILD_REPO set "BUILD_REPO=local"
(
  echo Repository: !BUILD_REPO!
  echo Ref: !BUILD_REF!
  echo Commit: !BUILD_SHA!
  echo BuiltOnLocal: %DATE% %TIME%
  echo Builder: GitHub Actions workflow when downloaded from a GitHub run or release.
) > "%DEST%\BUILD_INFO.txt"
exit /b %ERRORLEVEL%

:WriteChecksums
call :Section "Checksums"
for /f "usebackq delims=" %%A in ("%ARTIFACTS%") do (
  if not "%%~A"=="" call :WriteChecksum "%%A" || exit /b !ERRORLEVEL!
)
exit /b 0

:WriteChecksum
set "HASH="
for /f "tokens=* delims=" %%H in ('certutil.exe -hashfile "%~1" SHA256 ^| findstr /r /c:"^[0-9A-Fa-f][0-9A-Fa-f]"') do (
  if not defined HASH set "HASH=%%H"
)
if not defined HASH (
  echo ERROR: Could not calculate SHA256 for "%~1".
  exit /b 1
)
set "HASH=!HASH: =!"
for %%F in ("%~1") do > "%~1.sha256" echo !HASH!  %%~nxF
echo Created "%~1.sha256"
exit /b 0

:RecordArtifact
if not exist "%~1" (
  echo ERROR: Release artifact missing: "%~1".
  exit /b 1
)
for %%I in ("%~1") do echo %%~fI>>"%ARTIFACTS%"
exit /b 0

:IsSkipped
set "SKIP_RESULT=0"
echo(!SKIP_LIST! | findstr /i /c:",%~1," >nul && set "SKIP_RESULT=1"
if "!SKIP_RESULT!"=="1" echo Skipping %~1.
exit /b 0

:Section
echo.
echo ==^> %~1
exit /b 0

:Run
echo ^> %*
%*
exit /b %ERRORLEVEL%

:Fail
set "STATUS=%ERRORLEVEL%"
if "%STATUS%"=="0" set "STATUS=1"
del /f /q "%ARTIFACTS%" >nul 2>nul
echo ERROR: Windows build failed.
exit /b %STATUS%

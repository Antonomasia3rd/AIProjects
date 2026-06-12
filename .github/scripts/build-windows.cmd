@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..\..") do set "REPO=%%~fI"
set "LEGACY=%REPO%\legacy"
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
if /i "!ARG!"=="/skip:DesktopStub" set "SKIP_LIST=!SKIP_LIST!DesktopStub,"
if /i "!ARG!"=="--skip:DesktopStub" set "SKIP_LIST=!SKIP_LIST!DesktopStub,"
set "SKIP_LIST=!SKIP_LIST: =!"
shift
goto ParseArgs

:Main
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
call :BuildDNSAutoUpdate || goto Fail
call :BuildNowPlayingTile || goto Fail
call :BuildPhotoCollage || goto Fail
call :BuildDesktopStub || goto Fail
call :BuildRssLiveTile || goto Fail
call :BuildCharmTray || goto Fail
call :BuildSecureDesktopLauncher || goto Fail
call :BuildTaskSchedulerMigration || goto Fail
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
call :RequireCsc
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%LEGACY%\AllowContentAboveLock\build" mkdir "%LEGACY%\AllowContentAboveLock\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%LEGACY%\AllowContentAboveLock\build\AllowContentAboveLock.exe" "%LEGACY%\AllowContentAboveLock\AllowContentAboveLock.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\AllowContentAboveLock\build\AllowContentAboveLock.exe"
exit /b %ERRORLEVEL%

:BuildAsusBlink
call :IsSkipped asusblink
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build asusblink"
call :RequireCsc
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%LEGACY%\asusblink\build" mkdir "%LEGACY%\asusblink\build"
call :Run "%CSC%" /nologo /optimize+ /target:winexe /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll /r:System.Runtime.Serialization.dll /out:"%LEGACY%\asusblink\build\asusblink.exe" "%LEGACY%\asusblink\asusblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\asusblink\build\asusblink.exe"
exit /b %ERRORLEVEL%

:BuildCapsBlink
call :IsSkipped capsblink
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build capsblink"
call :RequireCsc
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%LEGACY%\capsblink\build" mkdir "%LEGACY%\capsblink\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /out:"%LEGACY%\capsblink\build\capsblink.exe" "%LEGACY%\capsblink\capsblink.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\capsblink\build\capsblink.exe"
exit /b %ERRORLEVEL%

:BuildYourPhoneHideBanner
call :IsSkipped YourPhoneHideBanner
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build YourPhoneHideBanner"
call :RequireCsc
if errorlevel 1 exit /b %ERRORLEVEL%
if not exist "%LEGACY%\YourPhoneHideBanner\build" mkdir "%LEGACY%\YourPhoneHideBanner\build"
call :Run "%CSC%" /nologo /optimize+ /target:exe /r:System.ServiceProcess.dll /out:"%LEGACY%\YourPhoneHideBanner\build\YourPhoneHideBanner.exe" "%LEGACY%\YourPhoneHideBanner\YourPhoneHideBanner.cs"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\YourPhoneHideBanner\build\YourPhoneHideBanner.exe"
exit /b %ERRORLEVEL%

:BuildDiscordRPC
call :IsSkipped DiscordRPC
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build DiscordRPC"
pushd "%REPO%\DiscordRPC" || exit /b 1
call :Run cmd.exe /d /c TestDiscordRPCSource.cmd
if errorlevel 1 (
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b !STATUS!
)
call :Run cmd.exe /d /c build.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\DiscordRPC\build\DiscordRPC.exe"
exit /b %ERRORLEVEL%

:BuildDNSAutoUpdate
call :IsSkipped DNSAutoUpdate
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build DNSAutoUpdate"
pushd "%LEGACY%\DNSAutoUpdate" || exit /b 1
call :Run cmd.exe /d /c BuildDNSAutoUpdate.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\DNSAutoUpdate\build\DNSAutoUpdate.exe"
exit /b %ERRORLEVEL%

:BuildNowPlayingTile
call :IsSkipped NowPlayingTile
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build NowPlayingTile"
pushd "%LEGACY%\NowPlayingTile" || exit /b 1
call :Run cmd.exe /d /c BuildNowPlayingTile.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\NowPlayingTile\build\NowPlayingTile.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\NowPlayingTile\README.md"
if errorlevel 1 exit /b %ERRORLEVEL%
exit /b 0

:BuildPhotoCollage
call :IsSkipped PhotoCollage
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build PhotoCollage"
pushd "%LEGACY%\PhotoCollage" || exit /b 1
call :Run cmd.exe /d /c BuildPhotoCollage.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\PhotoCollage\build\PhotoCollage.exe"
exit /b %ERRORLEVEL%

:BuildDesktopStub
call :IsSkipped DesktopStub
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build DesktopStub"
pushd "%REPO%\DesktopStub" || exit /b 1
call :Run cmd.exe /d /c TestDesktopStubSource.cmd
if errorlevel 1 (
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b !STATUS!
)
call :Run cmd.exe /d /c BuildDesktopStub.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\DesktopStub\build\DesktopStub.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\DesktopStub\build\DesktopStubLiveTileBroker.exe"
exit /b %ERRORLEVEL%

:BuildRssLiveTile
call :IsSkipped RssLiveTile
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build RssLiveTile"
pushd "%REPO%\RssLiveTile" || exit /b 1
call :Run cmd.exe /d /c BuildRssLiveTile.cmd check
if errorlevel 1 (
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b !STATUS!
)
call :Run cmd.exe /d /c BuildRssLiveTile.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%REPO%\RssLiveTile\build\RssLiveTile.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%REPO%\RssLiveTile\README.md"
exit /b %ERRORLEVEL%

:BuildCharmTray
call :IsSkipped CharmTray
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build CharmTray"
pushd "%LEGACY%\CharmTray" || exit /b 1
call :Run cmd.exe /d /c BuildCharmTray.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\CharmTray\build\CharmTray.exe"
exit /b %ERRORLEVEL%

:BuildSecureDesktopLauncher
call :IsSkipped SecureDesktopLauncher
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build SecureDesktopLauncher"
pushd "%LEGACY%\SecureDesktopLauncher" || exit /b 1
call :Run cmd.exe /d /c TestSecureDesktopLauncherSource.cmd
if errorlevel 1 (
  set "STATUS=%ERRORLEVEL%"
  popd
  exit /b !STATUS!
)
call :Run cmd.exe /d /c build_launcher.cmd
set "STATUS=%ERRORLEVEL%"
if "%STATUS%"=="0" (
  call :Run cmd.exe /d /c build_password_launcher.cmd
  set "STATUS=%ERRORLEVEL%"
)
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\SecureDesktopLauncher\build\SecureDesktopLauncher.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\SecureDesktopLauncher\build\SecureDesktopPasswordLauncher.exe"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\SecureDesktopLauncher\README.md"
exit /b %ERRORLEVEL%

:BuildTaskSchedulerMigration
call :IsSkipped TaskSchedulerMigration
if "!SKIP_RESULT!"=="1" exit /b 0
call :Section "Build TaskSchedulerMigration"
pushd "%LEGACY%\TaskSchedulerMigration" || exit /b 1
call :Run cmd.exe /d /c BuildTaskSchedulerMigration.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\TaskSchedulerMigration\build\TaskSchedulerMigration.exe"
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
pushd "%LEGACY%\RealTimeNotesDeskband" || exit /b 1
call :Run cmd.exe /d /c BuildDeskband.cmd
set "STATUS=%ERRORLEVEL%"
popd
if not "%STATUS%"=="0" exit /b %STATUS%
call :RecordArtifact "%LEGACY%\RealTimeNotesDeskband\build\RealTimeNotesDeskband.dll"
if errorlevel 1 exit /b %ERRORLEVEL%
call :RecordArtifact "%LEGACY%\RealTimeNotesDeskband\README.md"
if errorlevel 1 exit /b %ERRORLEVEL%
for %%F in (RegisterDeskband.cmd UnregisterDeskband.cmd ConfigureDeskband.cmd) do (
  call :RecordArtifactIfExists "%LEGACY%\RealTimeNotesDeskband\%%F" || exit /b !ERRORLEVEL!
)
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

:RecordArtifactIfExists
if exist "%~1" call :RecordArtifact "%~1"
exit /b %ERRORLEVEL%

:RecordArtifactsUnderDir
if not exist "%~1\" exit /b 0
for /r "%~1" %%F in (*) do call :RecordArtifact "%%~fF" || exit /b !ERRORLEVEL!
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

:RequireCsc
if not exist "%CSC%" (
  echo ERROR: C# compiler not found at "%CSC%".
  exit /b 1
)
exit /b 0

:Fail
set "STATUS=%ERRORLEVEL%"
if "%STATUS%"=="0" set "STATUS=1"
del /f /q "%ARTIFACTS%" >nul 2>nul
echo ERROR: Windows build failed.
exit /b %STATUS%

@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "VCVARS="

if not defined DESKTOPSTUB_PRODUCT_NAME set "DESKTOPSTUB_PRODUCT_NAME=DesktopStub"
if not defined DESKTOPSTUB_HOST_EXE_NAME set "DESKTOPSTUB_HOST_EXE_NAME=%DESKTOPSTUB_PRODUCT_NAME%.exe"
if not defined DESKTOPSTUB_BROKER_EXE_NAME set "DESKTOPSTUB_BROKER_EXE_NAME=%DESKTOPSTUB_PRODUCT_NAME%LiveTileBroker.exe"
if not defined DESKTOPSTUB_RELEASE_TAG_PREFIX set "DESKTOPSTUB_RELEASE_TAG_PREFIX=%DESKTOPSTUB_PRODUCT_NAME%-v"
call :StringLength "%DESKTOPSTUB_RELEASE_TAG_PREFIX%" DESKTOPSTUB_RELEASE_TAG_PREFIX_LEN
for %%F in ("%DESKTOPSTUB_HOST_EXE_NAME%") do set "DESKTOPSTUB_HOST_BASE=%%~nF"
for %%F in ("%DESKTOPSTUB_BROKER_EXE_NAME%") do set "DESKTOPSTUB_BROKER_BASE=%%~nF"

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

if defined VCVARS goto HaveVcvars
echo ERROR: vcvars64.bat not found. Install Visual Studio Build Tools with the C++ workload.
exit /b 1

:HaveVcvars
echo Using "%VCVARS%"
call "%VCVARS%" >nul
if errorlevel 1 exit /b %errorlevel%

pushd "%ROOT%" || exit /b 1
if not exist "build" mkdir "build"
if errorlevel 1 (
    popd
    exit /b %errorlevel%
)
if not exist "build\" (
    echo ERROR: build exists but is not a directory.
    popd
    exit /b 1
)
if not exist "build\obj" mkdir "build\obj"
if errorlevel 1 (
    popd
    exit /b %errorlevel%
)

call :ResolveDesktopStubVersion
if errorlevel 1 (
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)
set "VERSION_DEFINES=/DDESKTOPSTUB_VERSION_MAJOR=%DESKTOPSTUB_VERSION_MAJOR% /DDESKTOPSTUB_VERSION_MINOR=%DESKTOPSTUB_VERSION_MINOR% /DDESKTOPSTUB_VERSION_BUILD=%DESKTOPSTUB_VERSION_BUILD% /DDESKTOPSTUB_VERSION_REVISION=%DESKTOPSTUB_VERSION_REVISION% /DDESKTOPSTUB_RELEASE_TAG=\"%DESKTOPSTUB_RELEASE_TAG%\""
set "RC_VERSION_DEFINES=/d DESKTOPSTUB_VERSION_MAJOR=%DESKTOPSTUB_VERSION_MAJOR% /d DESKTOPSTUB_VERSION_MINOR=%DESKTOPSTUB_VERSION_MINOR% /d DESKTOPSTUB_VERSION_BUILD=%DESKTOPSTUB_VERSION_BUILD% /d DESKTOPSTUB_VERSION_REVISION=%DESKTOPSTUB_VERSION_REVISION%"
set "RC_PRODUCT_DEFINES=/d DESKTOPSTUB_PRODUCT_NAME=\"%DESKTOPSTUB_PRODUCT_NAME%\" /d DESKTOPSTUB_COMPANY_NAME=\"%DESKTOPSTUB_PRODUCT_NAME%\""
set "RC_HOST_NAME_DEFINES=%RC_PRODUCT_DEFINES% /d DESKTOPSTUB_INTERNAL_NAME=\"%DESKTOPSTUB_HOST_EXE_NAME%\" /d DESKTOPSTUB_ORIGINAL_FILENAME=\"%DESKTOPSTUB_HOST_EXE_NAME%\""
set "RC_BROKER_NAME_DEFINES=%RC_PRODUCT_DEFINES% /d DESKTOPSTUB_INTERNAL_NAME=\"%DESKTOPSTUB_BROKER_EXE_NAME%\" /d DESKTOPSTUB_ORIGINAL_FILENAME=\"%DESKTOPSTUB_BROKER_EXE_NAME%\""
echo %DESKTOPSTUB_PRODUCT_NAME% version: %DESKTOPSTUB_RELEASE_TAG% (%DESKTOPSTUB_VERSION%)

rem ---------------------------------------------------------------------------
rem Build policy:
rem   Older revisions had argument-controlled targets such as win8, win81,
rem   broker, helpers, background, experiments, all, and check. That made local
rem   builds ambiguous: the Windows 8/8.1 manifest path could be selected at
rem   runtime while the packaged broker helper had not been built.
rem
rem   The script now deliberately ignores every argument and always builds the
rem   same stable target set:
rem     - %DESKTOPSTUB_HOST_EXE_NAME%
rem     - %DESKTOPSTUB_BROKER_EXE_NAME%
rem
rem   The experimental background-task DLL remains in the source tree for
rem   research, but it is not part of the normal one-command build.
rem ---------------------------------------------------------------------------
if not "%~1"=="" (
    echo [i] BuildDesktopStub.cmd now ignores build arguments and always builds the same target set.
    echo [i] One or more arguments were supplied and ignored.
)

set "OUT_EXE=build\%DESKTOPSTUB_HOST_EXE_NAME%"
set "OBJ_FILE=build\obj\%DESKTOPSTUB_HOST_BASE%.obj"
set "RES_FILE=build\obj\DesktopStub.res"
set "BROKER_EXE=build\%DESKTOPSTUB_BROKER_EXE_NAME%"
set "BROKER_OBJ=build\obj\%DESKTOPSTUB_BROKER_BASE%.obj"
set "BROKER_RES=build\obj\LiveTileBroker.res"

echo Building packaged Live Tile broker...
rc /nologo %RC_VERSION_DEFINES% %RC_BROKER_NAME_DEFINES% /fo"%BROKER_RES%" LiveTileBroker.rc
if errorlevel 1 (
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE %VERSION_DEFINES% LiveTileBroker.cpp "%BROKER_RES%" /Fe"%BROKER_EXE%" /Fo"%BROKER_OBJ%" /link windowsapp.lib runtimeobject.lib ole32.lib /SUBSYSTEM:WINDOWS
if errorlevel 1 (
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)
if exist "%BROKER_EXE%.manifest" (
    echo Embedding packaged Live Tile broker manifest...
    mt /nologo -manifest "%BROKER_EXE%.manifest" -outputresource:"%BROKER_EXE%;#1"
    if errorlevel 1 (
        set "STATUS=%ERRORLEVEL%"
        popd
        exit /b %STATUS%
    )
)

echo Building main DesktopStub host...
rc /nologo %RC_VERSION_DEFINES% %RC_HOST_NAME_DEFINES% /fo"%RES_FILE%" DesktopStub.rc
if errorlevel 1 (
    set "STATUS=%ERRORLEVEL%"
    popd
    exit /b %STATUS%
)
cl /nologo /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE %VERSION_DEFINES% DesktopStub.cpp "%RES_FILE%" /Fe"%OUT_EXE%" /Fo"%OBJ_FILE%" /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
set "STATUS=%ERRORLEVEL%"
if not "%STATUS%"=="0" (
    popd
    exit /b %STATUS%
)
if exist "%OUT_EXE%.manifest" (
    echo Embedding main DesktopStub host manifest...
    mt /nologo -manifest "%OUT_EXE%.manifest" -outputresource:"%OUT_EXE%;#1"
    if errorlevel 1 (
        set "STATUS=%ERRORLEVEL%"
        popd
        exit /b %STATUS%
    )
)
popd

exit /b %STATUS%

:ResolveDesktopStubVersion
set "DESKTOPSTUB_VERSION_MAJOR="
set "DESKTOPSTUB_VERSION_MINOR="
set "DESKTOPSTUB_VERSION_BUILD="
set "DESKTOPSTUB_VERSION_REVISION="
if defined DESKTOPSTUB_VERSION (
    call :ParseDesktopStubVersion "%DESKTOPSTUB_VERSION%"
    if errorlevel 1 exit /b %ERRORLEVEL%
    if not defined DESKTOPSTUB_RELEASE_TAG set "DESKTOPSTUB_RELEASE_TAG=%DESKTOPSTUB_RELEASE_TAG_PREFIX%%DESKTOPSTUB_VERSION_MAJOR%"
    exit /b 0
)

if defined DESKTOPSTUB_RELEASE_TAG (
    call :VersionFromDesktopStubReleaseTag "%DESKTOPSTUB_RELEASE_TAG%"
    if errorlevel 1 exit /b %ERRORLEVEL%
    exit /b 0
)

set "LATEST_DESKTOPSTUB_TAG="
set "LATEST_DESKTOPSTUB_TAG_NUMBER=0"
for /f "delims=" %%T in ('git tag --points-at HEAD --list "%DESKTOPSTUB_RELEASE_TAG_PREFIX%*" --sort=-v:refname 2^>nul') do (
    call :DesktopStubReleaseTagNumber "%%T"
    if "!DESKTOPSTUB_TAG_NUMBER_VALID!"=="1" if not defined LATEST_DESKTOPSTUB_TAG (
        set "LATEST_DESKTOPSTUB_TAG=%%T"
        set "LATEST_DESKTOPSTUB_TAG_NUMBER=!DESKTOPSTUB_TAG_NUMBER!"
    )
)

if defined LATEST_DESKTOPSTUB_TAG (
    set "DESKTOPSTUB_RELEASE_TAG=%LATEST_DESKTOPSTUB_TAG%"
    set "DESKTOPSTUB_VERSION_MAJOR=%LATEST_DESKTOPSTUB_TAG_NUMBER%"
    set "DESKTOPSTUB_VERSION_MINOR=0"
    set "DESKTOPSTUB_VERSION_BUILD=0"
    set "DESKTOPSTUB_VERSION_REVISION=0"
    set "DESKTOPSTUB_VERSION=%DESKTOPSTUB_VERSION_MAJOR%.0.0.0"
    exit /b 0
)

set "MAX_DESKTOPSTUB_TAG_NUMBER=0"
for /f "delims=" %%T in ('git tag --list "%DESKTOPSTUB_RELEASE_TAG_PREFIX%*" --sort=-v:refname 2^>nul') do (
    call :DesktopStubReleaseTagNumber "%%T"
    if "!DESKTOPSTUB_TAG_NUMBER_VALID!"=="1" if !DESKTOPSTUB_TAG_NUMBER! GTR !MAX_DESKTOPSTUB_TAG_NUMBER! (
        set "MAX_DESKTOPSTUB_TAG_NUMBER=!DESKTOPSTUB_TAG_NUMBER!"
    )
)

set /a NEXT_DESKTOPSTUB_TAG_NUMBER=MAX_DESKTOPSTUB_TAG_NUMBER+1
if %NEXT_DESKTOPSTUB_TAG_NUMBER% LSS 1 set "NEXT_DESKTOPSTUB_TAG_NUMBER=1"
if %NEXT_DESKTOPSTUB_TAG_NUMBER% GTR 65535 (
    echo ERROR: DesktopStub release version exceeds the AppX/Win32 version limit: %NEXT_DESKTOPSTUB_TAG_NUMBER%
    exit /b 1
)
set "DESKTOPSTUB_RELEASE_TAG=%DESKTOPSTUB_RELEASE_TAG_PREFIX%%NEXT_DESKTOPSTUB_TAG_NUMBER%"
set "DESKTOPSTUB_VERSION_MAJOR=%NEXT_DESKTOPSTUB_TAG_NUMBER%"
set "DESKTOPSTUB_VERSION_MINOR=0"
set "DESKTOPSTUB_VERSION_BUILD=0"
set "DESKTOPSTUB_VERSION_REVISION=0"
set "DESKTOPSTUB_VERSION=%DESKTOPSTUB_VERSION_MAJOR%.0.0.0"
exit /b 0

:VersionFromDesktopStubReleaseTag
call :DesktopStubReleaseTagNumber "%~1"
if not "%DESKTOPSTUB_TAG_NUMBER_VALID%"=="1" (
    echo ERROR: DESKTOPSTUB_RELEASE_TAG must look like %DESKTOPSTUB_RELEASE_TAG_PREFIX%N: "%~1"
    exit /b 1
)
set "DESKTOPSTUB_VERSION_MAJOR=%DESKTOPSTUB_TAG_NUMBER%"
set "DESKTOPSTUB_VERSION_MINOR=0"
set "DESKTOPSTUB_VERSION_BUILD=0"
set "DESKTOPSTUB_VERSION_REVISION=0"
set "DESKTOPSTUB_VERSION=%DESKTOPSTUB_VERSION_MAJOR%.0.0.0"
exit /b 0

:DesktopStubReleaseTagNumber
set "DESKTOPSTUB_TAG_NUMBER_VALID=0"
set "DESKTOPSTUB_TAG_NUMBER=0"
set "TAG_VALUE=%~1"
if /i not "!TAG_VALUE:~0,%DESKTOPSTUB_RELEASE_TAG_PREFIX_LEN%!"=="%DESKTOPSTUB_RELEASE_TAG_PREFIX%" exit /b 0
set "TAG_NUMBER=!TAG_VALUE:~%DESKTOPSTUB_RELEASE_TAG_PREFIX_LEN%!"
if "!TAG_NUMBER!"=="" exit /b 0
echo(!TAG_NUMBER!| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 exit /b 0
set /a PARSED_TAG_NUMBER=!TAG_NUMBER! 2>nul
if errorlevel 1 exit /b 0
if !PARSED_TAG_NUMBER! LSS 1 exit /b 0
if !PARSED_TAG_NUMBER! GTR 65535 exit /b 0
set "DESKTOPSTUB_TAG_NUMBER_VALID=1"
set "DESKTOPSTUB_TAG_NUMBER=!PARSED_TAG_NUMBER!"
exit /b 0

:StringLength
setlocal EnableDelayedExpansion
set "STRING_LENGTH_VALUE=%~1"
set "STRING_LENGTH_COUNT=0"
:StringLengthLoop
if defined STRING_LENGTH_VALUE (
    set "STRING_LENGTH_VALUE=!STRING_LENGTH_VALUE:~1!"
    set /a STRING_LENGTH_COUNT+=1
    goto StringLengthLoop
)
endlocal & set "%~2=%STRING_LENGTH_COUNT%"
exit /b 0

:ParseDesktopStubVersion
set "RAW_DESKTOPSTUB_VERSION=%~1"
for /f "tokens=1-5 delims=." %%A in ("%RAW_DESKTOPSTUB_VERSION%") do (
    set "VERSION_PART_1=%%A"
    set "VERSION_PART_2=%%B"
    set "VERSION_PART_3=%%C"
    set "VERSION_PART_4=%%D"
    set "VERSION_PART_EXTRA=%%E"
)
if not "%VERSION_PART_EXTRA%"=="" goto InvalidDesktopStubVersion
call :ValidateDesktopStubVersionPart "%VERSION_PART_1%" || goto InvalidDesktopStubVersion
set "DESKTOPSTUB_VERSION_MAJOR=%DESKTOPSTUB_VERSION_PART%"
call :ValidateDesktopStubVersionPart "%VERSION_PART_2%" || goto InvalidDesktopStubVersion
set "DESKTOPSTUB_VERSION_MINOR=%DESKTOPSTUB_VERSION_PART%"
call :ValidateDesktopStubVersionPart "%VERSION_PART_3%" || goto InvalidDesktopStubVersion
set "DESKTOPSTUB_VERSION_BUILD=%DESKTOPSTUB_VERSION_PART%"
call :ValidateDesktopStubVersionPart "%VERSION_PART_4%" || goto InvalidDesktopStubVersion
set "DESKTOPSTUB_VERSION_REVISION=%DESKTOPSTUB_VERSION_PART%"
set "DESKTOPSTUB_VERSION=%DESKTOPSTUB_VERSION_MAJOR%.%DESKTOPSTUB_VERSION_MINOR%.%DESKTOPSTUB_VERSION_BUILD%.%DESKTOPSTUB_VERSION_REVISION%"
exit /b 0

:InvalidDesktopStubVersion
echo ERROR: DESKTOPSTUB_VERSION must be a four-part AppX/Win32 version with 0-65535 parts: "%RAW_DESKTOPSTUB_VERSION%"
exit /b 1

:ValidateDesktopStubVersionPart
set "DESKTOPSTUB_VERSION_PART=%~1"
if "%DESKTOPSTUB_VERSION_PART%"=="" exit /b 1
echo(%DESKTOPSTUB_VERSION_PART%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 exit /b 1
set /a DESKTOPSTUB_VERSION_PART=%DESKTOPSTUB_VERSION_PART% 2>nul
if errorlevel 1 exit /b 1
if %DESKTOPSTUB_VERSION_PART% LSS 0 exit /b 1
if %DESKTOPSTUB_VERSION_PART% GTR 65535 exit /b 1
exit /b 0

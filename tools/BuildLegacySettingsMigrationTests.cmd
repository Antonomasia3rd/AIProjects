@echo off
setlocal EnableExtensions
set "LEGACY_MIGRATION_ROOT=%~dp0.."
set "LEGACY_MIGRATION_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%LEGACY_MIGRATION_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "LEGACY_MIGRATION_VS=%%I"
if not defined LEGACY_MIGRATION_VS exit /b 2
call "%LEGACY_MIGRATION_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 3
pushd "%LEGACY_MIGRATION_ROOT%" || exit /b 4
if not exist "build\legacy-migration-tests" mkdir "build\legacy-migration-tests"
cl /nologo /utf-8 /std:c++17 /EHsc /W4 /WX /O2 /DUNICODE /D_UNICODE tools\LegacySettingsMigrationTests.cpp /Fe:build\legacy-migration-tests\LegacySettingsMigrationTests.exe /Fo:build\legacy-migration-tests\LegacySettingsMigrationTests.obj
set "LEGACY_MIGRATION_STATUS=%errorlevel%"
popd
exit /b %LEGACY_MIGRATION_STATUS%

@echo off
setlocal EnableExtensions
set "GAME_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq delims=" %%I in (`"%GAME_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GAME_VS=%%I"
if not defined GAME_VS exit /b 1
call "%GAME_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
pushd "%~dp0.." || exit /b 1
if not exist build\review-validation mkdir build\review-validation
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\GameTileLayoutTests.cpp /Fe:build\review-validation\GameTileLayoutAgentTests.exe /Fo:build\review-validation\GameTileLayoutAgentTests.obj
if errorlevel 1 goto failure
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /WX tools\TileVisualAssetsTests.cpp /Fe:build\review-validation\GameCornerAssetPlanAgentTests.exe /Fo:build\review-validation\GameCornerAssetPlanAgentTests.obj
if errorlevel 1 goto failure
cl /nologo /utf-8 /std:c++17 /O2 /EHsc /W4 /DUNICODE /D_UNICODE tools\TileVisualAssetsRenderTests.cpp /Fe:build\review-validation\GameCornerAssetRenderAgentTests.exe /Fo:build\review-validation\GameCornerAssetRenderAgentTests.obj /link user32.lib gdi32.lib gdiplus.lib ole32.lib advapi32.lib shell32.lib
set "GAME_RESULT=%errorlevel%"
popd
exit /b %GAME_RESULT%
:failure
set "GAME_RESULT=%errorlevel%"
popd
exit /b %GAME_RESULT%

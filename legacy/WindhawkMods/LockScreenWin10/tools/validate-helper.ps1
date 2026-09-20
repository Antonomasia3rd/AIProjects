param(
    [string]$Windhawk = 'C:\Program Files\Windhawk',
    [string]$ManifestTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\mt.exe'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$buildPath = Join-Path $project 'build'
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$compiler = Join-Path $Windhawk 'Compiler\bin\clang++.exe'
$executable = Join-Path $buildPath 'validate_helper.exe'
& $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-DUNICODE' '-D_UNICODE' '-DWH_MOD' '-DWH_EDITING' `
    '-DWH_MOD_ID=L"lockscreen-helper-test"' '-include' 'windhawk_api.h' `
    (Join-Path $PSScriptRoot 'validate_helper.cpp') '-o' $executable '-lruntimeobject' '-lole32' '-loleaut32' '-lcomctl32'
if ($LASTEXITCODE -ne 0) { throw 'Helper validation host failed to build' }
foreach ($library in @('libc++','libunwind')) {
    Copy-Item -LiteralPath (Join-Path $Windhawk ('Compiler\x86_64-w64-mingw32\bin\' + $library + '.dll')) `
        -Destination (Join-Path $buildPath ($library + '.whl'))
}
& $ManifestTool '-nologo' '-manifest' (Join-Path $PSScriptRoot 'xaml-test.manifest') ('-outputresource:' + $executable + ';#1')
if ($LASTEXITCODE -ne 0) { throw 'Could not embed the hosting manifest' }
& $executable 2>&1 | Tee-Object -FilePath (Join-Path $buildPath 'helper-validation.log')
if ($LASTEXITCODE -ne 0) { throw 'Helper validation failed; see build/helper-validation.log' }

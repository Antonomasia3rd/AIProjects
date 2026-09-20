param(
    [string]$Python = 'python',
    [string]$Windhawk = 'C:\Program Files\Windhawk',
    [string]$ManifestTool = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.19041.0\x64\mt.exe'
)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
$buildPath = Join-Path $project 'build'
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
& $Python (Join-Path $PSScriptRoot 'prepare_xaml_validation.py')
if ($LASTEXITCODE -ne 0) { throw 'Failed to prepare XAML cases' }
$compiler = Join-Path $Windhawk 'Compiler\bin\clang++.exe'
$executable = Join-Path $buildPath 'validate_xaml.exe'
& $compiler '-std=c++23' '-target' 'x86_64-w64-mingw32' '-DUNICODE' '-D_UNICODE' `
    (Join-Path $PSScriptRoot 'validate_xaml.cpp') '-o' $executable '-lruntimeobject' '-lole32' '-loleaut32'
if ($LASTEXITCODE -ne 0) { throw 'XAML validation host failed to build' }
# Windhawk's compiler imports its private runtime using the .whl suffix.
foreach ($library in @('libc++','libunwind')) {
    Copy-Item -LiteralPath (Join-Path $Windhawk ('Compiler\x86_64-w64-mingw32\bin\' + $library + '.dll')) `
        -Destination (Join-Path $buildPath ($library + '.whl'))
}
& $ManifestTool '-nologo' '-manifest' (Join-Path $PSScriptRoot 'xaml-test.manifest') ('-outputresource:' + $executable + ';#1')
if ($LASTEXITCODE -ne 0) { throw 'Could not embed the XAML hosting manifest' }
& $executable 2>&1 | Tee-Object -FilePath (Join-Path $buildPath 'xaml-validation.log')
if ($LASTEXITCODE -ne 0) { throw 'XAML validation failed; see build/xaml-validation.log' }

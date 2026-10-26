[CmdletBinding()]
param(
    [string]$Windhawk = "$env:ProgramFiles\Windhawk",
    [string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$source = Join-Path $PSScriptRoot 'local@logonui-tweaks.wh.cpp'
$compiler = Join-Path $Windhawk 'Compiler\bin\clang++.exe'
$engine = Get-ChildItem -LiteralPath (Join-Path $Windhawk 'Engine') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
if (-not $engine) { throw 'No installed Windhawk engine was found.' }
$importLibrary = Join-Path $engine.FullName '64\windhawk.lib'
foreach ($inputPath in @($source, $compiler, $importLibrary)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing input: $inputPath" }
}
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\..\analysis\logonui-back-2026-10-26\candidate-validation'
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$text = [System.IO.File]::ReadAllText($source)
$versionMatch = [regex]::Match($text, '(?m)^// @version\s+(\S+)')
if (-not $versionMatch.Success) { throw 'Candidate version metadata is missing.' }
$version = $versionMatch.Groups[1].Value
$before = (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash
$output = Join-Path $OutputDirectory "logonui-tweaks-$version-x64-candidate.dll"
$arguments = @('-std=c++23', '-O2', '-shared', '-target', 'x86_64-w64-mingw32',
    '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
    '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
    '-DWH_MOD', '-DWH_MOD_ID=L"logonui-tweaks"', ('-DWH_MOD_VERSION=L"' + $version + '"'),
    '-include', 'windhawk_api.h', $source, $importLibrary,
    '-ladvapi32', '-lole32', '-loleaut32', '-lruntimeobject', '-lgdi32', '-lwtsapi32',
    '-Wl,--export-all-symbols', '-o', $output)
& $compiler @arguments 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'candidate-build.log')
if ($LASTEXITCODE -ne 0) { throw "Candidate build failed: $LASTEXITCODE" }
if ((Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash -ne $before) {
    throw 'Candidate source changed during compilation.'
}
$receipt = [ordered]@{
    source = [System.IO.Path]::GetFullPath($source)
    sourceSha256 = $before
    version = $version
    target = 'x86_64-w64-mingw32'
    engineVersion = $engine.Name
    compiler = $compiler
    output = $output
    outputSha256 = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash
    compiledUtc = [DateTime]::UtcNow.ToString('o')
    installed = $false
    runtimeValidated = $false
    unresolved = @('XAML cleanup lifecycle P1', 'Native Back backend and compatibility port')
}
$receipt | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'candidate-build.json') -Encoding UTF8
Write-Output "Built x64 candidate without installing or loading it: $output"

[CmdletBinding()]
param(
    [string]$Compiler = "$env:ProgramFiles\Windhawk\Compiler\bin\clang++.exe",
    [string]$OutputDirectory,
    [switch]$SkipSyntaxCheck
)
$ErrorActionPreference = 'Stop'
$candidate = Join-Path $PSScriptRoot 'local@logonui-tweaks.wh.cpp'
$hostTests = Join-Path $PSScriptRoot 'LogonUiTweaksHostTests.cpp'
if (-not $OutputDirectory) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\..\analysis\logonui-back-2026-10-26\candidate-validation'
}
foreach ($inputPath in @($Compiler, $candidate, $hostTests)) {
    if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing input: $inputPath" }
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$sourceHash = (Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash
$text = [System.IO.File]::ReadAllText($candidate)
function Get-Excerpt([string]$first, [string]$next) {
    $begin = $text.IndexOf($first, [System.StringComparison]::Ordinal)
    if ($begin -lt 0) { throw "Source marker missing: $first" }
    $end = $text.IndexOf($next, $begin + $first.Length, [System.StringComparison]::Ordinal)
    if ($end -lt 0) { throw "Source end marker missing: $next" }
    return $text.Substring($begin, $end - $begin)
}
$encoding = New-Object System.Text.UTF8Encoding($false)
$resolver = (Get-Excerpt 'struct StobjectPeView {' 'std::vector<DWORD> FindWideLiteralRvas(') +
    (Get-Excerpt 'bool IsExpectedOnLoadedMemberOffset(' 'void* ResolveOnLoadedBySymbols(')
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'logonui-resolver-excerpt.inc'), $resolver, $encoding)
$init = Get-Excerpt 'BOOL Wh_ModInit() {' 'BOOL Wh_ModSettingsChanged('
[System.IO.File]::WriteAllText((Join-Path $OutputDirectory 'logonui-init-excerpt.inc'), $init, $encoding)

if (-not $SkipSyntaxCheck) {
    $syntaxArgs = @('-x', 'c++', '-std=c++23', '-target', 'x86_64-w64-mingw32',
        '-DUNICODE', '-D_UNICODE', '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00',
        '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008', '-D__USE_MINGW_ANSI_STDIO=0',
        '-DWH_MOD', '-DWH_EDITING', '-include', 'windhawk_api.h', '-fsyntax-only', $candidate)
    & $Compiler @syntaxArgs
    if ($LASTEXITCODE -ne 0) { throw "Candidate syntax check failed: $LASTEXITCODE" }
    Write-Output 'Complete candidate x64 syntax check passed.'
}
$exe = Join-Path $OutputDirectory 'LogonUiTweaksInertHostTests.exe'
& $Compiler -std=c++23 -target x86_64-w64-mingw32 -municode -static -O0 -I $OutputDirectory $hostTests -o $exe
if ($LASTEXITCODE -ne 0) { throw "Inert host-test compilation failed: $LASTEXITCODE" }

# Targets below were independently named by matching-image public PDBs.
# These images are read into ordinary byte buffers; no image is loaded or called.
$samples = @(
    @('F:\Windows\System32\Windows.UI.Logon.dll', '0x49760'),
    @('E:\Windows\System32\Windows.UI.Logon.dll', '0x472D0'),
    @('K:\Windows\System32\Windows.UI.Logon.dll', '0x1B630')
)
$testArgs = @()
foreach ($sample in $samples) {
    if (-not (Test-Path -LiteralPath $sample[0] -PathType Leaf)) { throw "Missing test image: $($sample[0])" }
    $testArgs += $sample[0]
    $testArgs += $sample[1]
}
& $exe @testArgs | Tee-Object -FilePath (Join-Path $OutputDirectory 'candidate-host-tests.log')
if ($LASTEXITCODE -ne 0) { throw "Inert host tests failed: $LASTEXITCODE" }
if ((Get-FileHash -LiteralPath $candidate -Algorithm SHA256).Hash -ne $sourceHash) {
    throw 'Candidate source changed during validation.'
}
$receipt = [ordered]@{
    source = [System.IO.Path]::GetFullPath($candidate)
    sourceSha256 = $sourceHash
    completeSyntaxCheck = -not $SkipSyntaxCheck
    hostTestsSha256 = (Get-FileHash -LiteralPath $hostTests -Algorithm SHA256).Hash
    compiler = $Compiler
    executableSha256 = (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    verifiedUtc = [DateTime]::UtcNow.ToString('o')
    integrationsExercised = $false
    samples = @($samples | ForEach-Object {
        [ordered]@{path=$_[0]; expectedRva=$_[1]; sha256=(Get-FileHash -LiteralPath $_[0] -Algorithm SHA256).Hash}
    })
}
$receipt | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'candidate-tests.json') -Encoding UTF8
Write-Output 'No LogonUI, power, audio, hardware, session, or Windhawk integrations were exercised.'

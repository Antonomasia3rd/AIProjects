[CmdletBinding()]
param(
    [string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)))
)

$ErrorActionPreference = "Stop"
$sourcePath = Join-Path (Join-Path (Join-Path $RepositoryRoot "dependencies") "ChromeProfileCounter") "chrome_profile_counter_app.ps1"
$wrapperPath = Join-Path (Join-Path (Join-Path $RepositoryRoot "legacy") "ChromeProfileCounter") "ChromeProfileCounter.ps1"

foreach ($path in @($sourcePath, $wrapperPath)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing ChromeProfileCounter source: $path"
    }
    $tokens = $null
    $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile(
        $path,
        [ref]$tokens,
        [ref]$errors) | Out-Null
    if ($errors.Count -ne 0) {
        throw ("PowerShell parser errors in {0}: {1}" -f $path, (($errors | ForEach-Object { $_.Message }) -join "; "))
    }
}

$source = Get-Content -LiteralPath $sourcePath -Raw
$wrapper = Get-Content -LiteralPath $wrapperPath -Raw
$required = @(
    "function Backup-LocalState",
    "function Write-LocalStateAtomically",
    "[IO.File]::Move(`$temporary, `$destination)",
    "[IO.File]::Replace(`$temporary, `$LocalState, `$null)",
    "Chrome started before Local State could be replaced"
)
foreach ($text in $required) {
    if (-not $source.Contains($text)) {
        throw "ChromeProfileCounter is missing the expected atomic-write contract: $text"
    }
}
if ($source -match '\[IO\.File\]::WriteAllText\(\s*\$LocalState') {
    throw "ChromeProfileCounter still writes Local State directly."
}
if (-not $wrapper.Contains("& `$implementation @args")) {
    throw "ChromeProfileCounter legacy launcher does not forward to dependencies."
}
if ($wrapper.Contains("`$LASTEXITCODE")) {
    throw "ChromeProfileCounter legacy launcher must not return a stale host exit code."
}

Write-Host "ChromeProfileCounter source checks passed."

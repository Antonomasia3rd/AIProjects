param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path

function Read-Source([string]$RelativePath) {
    Get-Content -LiteralPath (Join-Path $ProjectRoot $RelativePath) -Raw
}

function Assert-Matches {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Description
    )

    if ($Text -notmatch $Pattern) {
        throw "GenerateAssets source regression: $Description"
    }
}

$generation = Read-Source 'src\ga_generation.inc'
$registration = Read-Source 'src\ga_registration.inc'
$uiLogging = Read-Source 'src\ga_ui_logging.inc'
$defaults = Read-Source 'src\ga_config_defaults.inc'

Assert-Matches $generation `
    '(?s)static bool StartupGeneratedAssetsPresent.*CurrentManifestDisplayInfo\(\).*ManifestAssetPathForTile\(manifestInfo,\s*t\).*ResolveManifestAssetPath\(exeDir,\s*manifestAssetPath\).*if \(!IsValidGeneratedPng\(basePath\)\)\s*return false;' `
    'startup skip validation must use manifest-resolved asset paths and require the manifest base asset'

Assert-Matches $registration `
    '(?s)static void LogComRegistrationOperationFailure.*op\.ErrorCode\(\)\.value.*LogComRegistrationDeploymentResult\(op\.GetResults\(\)\)' `
    'COM registration failures must log async HRESULT and deployment-result details'

Assert-Matches $registration `
    'AsyncStatus::Error' `
    'COM registration must handle failed async status before op.get() can hide status diagnostics'

foreach ($key in @(
    'ComRegistrationAsyncError',
    'ComRegistrationDeploymentError',
    'ComRegistrationDeploymentMessage'
)) {
    Assert-Matches $defaults ([regex]::Escape($key)) "default string missing for $key"
    Assert-Matches $uiLogging ([regex]::Escape("g_ui.$($key.Substring(0,1).ToLowerInvariant())$($key.Substring(1))")) "UI field load missing for $key"
    Assert-Matches $uiLogging "RequireFormat\(g_ui\.$($key.Substring(0,1).ToLowerInvariant())$($key.Substring(1))" "format validation missing for $key"
}

Write-Host 'GenerateAssets source regression checks passed.'

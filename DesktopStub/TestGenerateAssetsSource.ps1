<#
.SYNOPSIS
Maintainer-only source regression checks for GenerateAssets.

.DESCRIPTION
This script does not build or launch GenerateAssets.exe. It verifies that a
small set of source-level safety fixes remain present, covering paths that are
awkward to exercise manually: manifest-resolved startup asset validation, COM
registration diagnostics, force-shutdown cleanup recording, and matching UI
string defaults/format validation.

Normal users do not need this file to build or run the app.
#>
[CmdletBinding()]
param(
    [switch]$ListChecks
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0

$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:CheckCount = 0

function Read-Source {
    param([Parameter(Mandatory=$true)][string]$RelativePath)

    $path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "GenerateAssets source regression: missing source file '$RelativePath'"
    }

    Get-Content -LiteralPath $path -Raw
}

function Join-Source {
    param([Parameter(Mandatory=$true)][string[]]$RelativePaths)

    ($RelativePaths | ForEach-Object { Read-Source $_ }) -join "`n"
}

function New-SourceCheck {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$SourceName,
        [Parameter(Mandatory=$true)][string]$SourceText,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Failure
    )

    [PSCustomObject]@{
        Name = $Name
        SourceName = $SourceName
        SourceText = $SourceText
        Pattern = $Pattern
        Failure = $Failure
    }
}

function Assert-SourceCheck {
    param([Parameter(Mandatory=$true)]$Check)

    $script:CheckCount++
    if ($Check.SourceText -notmatch $Check.Pattern) {
        throw "GenerateAssets source regression: $($Check.Failure) [$($Check.Name) in $($Check.SourceName)]"
    }

    Write-Host "ok - $($Check.Name)"
}

function Assert-UiStringWired {
    param(
        [Parameter(Mandatory=$true)][string]$Key,
        [Parameter(Mandatory=$true)][string]$Defaults,
        [Parameter(Mandatory=$true)][string]$UiSources
    )

    $property = "$($Key.Substring(0,1).ToLowerInvariant())$($Key.Substring(1))"

    Assert-SourceCheck (New-SourceCheck `
        -Name "UI string default exists: $Key" `
        -SourceName 'src\ga_config_defaults.inc' `
        -SourceText $Defaults `
        -Pattern ([regex]::Escape($Key)) `
        -Failure "default string missing for $Key")

    Assert-SourceCheck (New-SourceCheck `
        -Name "UI string is loaded: $Key" `
        -SourceName 'UI/string sources' `
        -SourceText $UiSources `
        -Pattern ([regex]::Escape("g_ui.$property")) `
        -Failure "UI field load missing for $Key")

    Assert-SourceCheck (New-SourceCheck `
        -Name "UI format is validated: $Key" `
        -SourceName 'UI/string sources' `
        -SourceText $UiSources `
        -Pattern "RequireFormat\(g_ui\.$property" `
        -Failure "format validation missing for $Key")
}

$generation = Read-Source 'src\ga_generation.inc'
$app = Read-Source 'src\ga_app.inc'
$registration = Read-Source 'src\ga_registration.inc'
$defaults = Read-Source 'src\ga_config_defaults.inc'
$uiSources = Join-Source @(
    'src\ga_ui_logging.inc',
    'src\ga_ui_state.inc',
    'src\ga_logging_core.inc',
    'src\ga_manifest.inc',
    'src\ga_ui_strings.inc',
    'src\ga_runtime_helpers.inc'
)

$sourceChecks = @(
    (New-SourceCheck `
        -Name 'Startup skip validates manifest-resolved assets' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)static bool StartupGeneratedAssetsPresent.*CurrentManifestDisplayInfo\(\).*ManifestAssetPathForTile\(manifestInfo,\s*t\).*ResolveManifestAssetPath\(exeDir,\s*manifestAssetPath\).*if \(!IsValidGeneratedPng\(basePath\)\)\s*return false;' `
        -Failure 'startup skip validation must use manifest-resolved asset paths and require the manifest base asset'),

    (New-SourceCheck `
        -Name 'COM registration logs async and deployment details' `
        -SourceName 'src\ga_registration.inc' `
        -SourceText $registration `
        -Pattern '(?s)static void LogComRegistrationOperationFailure.*op\.ErrorCode\(\)\.value.*LogComRegistrationDeploymentResult\(op\.GetResults\(\)\)' `
        -Failure 'COM registration failures must log async HRESULT and deployment-result details'),

    (New-SourceCheck `
        -Name 'COM registration handles AsyncStatus::Error' `
        -SourceName 'src\ga_registration.inc' `
        -SourceText $registration `
        -Pattern 'AsyncStatus::Error' `
        -Failure 'COM registration must handle failed async status before op.get() can hide status diagnostics'),

    (New-SourceCheck `
        -Name 'Force shutdown records skipped cleanup first' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)static void ForceShutdownNow\(\).*RecordForceShutdownPendingCleanup\(\).*ExitProcess\(0\)' `
        -Failure 'force shutdown must persist skipped-cleanup details before bypassing normal shutdown'),

    (New-SourceCheck `
        -Name 'Startup warns after previous forced shutdown' `
        -SourceName 'src\ga_app.inc' `
        -SourceText $app `
        -Pattern '(?s)ConsumePreviousForceShutdownCleanupWarning\(previousForceShutdownCleanupWarning\).*QueueStartupWarning\(previousForceShutdownCleanupWarning\).*MessageBoxW\(' `
        -Failure 'startup must warn the user when a previous force shutdown skipped cleanup')
)

$uiStringKeys = @(
    'ComRegistrationAsyncError',
    'ComRegistrationDeploymentError',
    'ComRegistrationDeploymentMessage'
)

if ($ListChecks) {
    Write-Host 'GenerateAssets source regression guardrails:'
    foreach ($check in $sourceChecks) {
        Write-Host "- $($check.Name)"
    }
    foreach ($key in $uiStringKeys) {
        Write-Host "- UI string default/load/format wiring: $key"
    }
    return
}

Write-Host 'Running GenerateAssets maintainer source regression checks...'
Write-Host 'This checks source guardrails only; it does not build or launch the app.'

foreach ($check in $sourceChecks) {
    Assert-SourceCheck $check
}

foreach ($key in $uiStringKeys) {
    Assert-UiStringWired -Key $key -Defaults $defaults -UiSources $uiSources
}

Write-Host "GenerateAssets source regression checks passed ($script:CheckCount checks)."

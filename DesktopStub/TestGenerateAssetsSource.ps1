[CmdletBinding()]
param([switch]$ListChecks)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2.0
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$script:CheckCount = 0

function Read-Source([string]$RelativePath) {
    $path = Join-Path $ProjectRoot $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "GenerateAssets source regression: missing source file '$RelativePath'"
    }
    Get-Content -LiteralPath $path -Raw
}

function Assert-Match([string]$Name, [string]$SourceName, [string]$SourceText, [string]$Pattern, [string]$Failure) {
    $script:CheckCount++
    if ($SourceText -notmatch $Pattern) {
        throw "GenerateAssets source regression: $Failure [$Name in $SourceName]"
    }
    Write-Host "ok - $Name"
}

function Assert-NotMatch([string]$Name, [string]$SourceName, [string]$SourceText, [string]$Pattern, [string]$Failure) {
    $script:CheckCount++
    if ($SourceText -match $Pattern) {
        throw "GenerateAssets source regression: $Failure [$Name in $SourceName]"
    }
    Write-Host "ok - $Name"
}

function Assert-LiveTileUpdateDoesNotClearExistingTile([string]$LiveTileSource) {
    $script:CheckCount++
    $updateFunction = [regex]::Match($LiveTileSource, '(?ms)^static bool Appx_Update_LiveTile\s*\((?:(?!^static bool ).)*').Value
    if ([string]::IsNullOrWhiteSpace($updateFunction)) {
        throw 'GenerateAssets source regression: could not locate Appx_Update_LiveTile for clear-before-update guard [Live Tile update does not clear the existing tile first in src\ga_live_tile.inc]'
    }
    if ($updateFunction -match 'updater\.Clear\s*\(') {
        throw 'GenerateAssets source regression: Live Tile updater must not call Clear before Update because that causes a visible blank tile during refresh [Live Tile update does not clear the existing tile first in src\ga_live_tile.inc]'
    }
    Write-Host 'ok - Live Tile update does not clear the existing tile first'
}

$buildScript = Read-Source 'BuildGenerateAssets.cmd'
$generation = Read-Source 'src\ga_generation.inc'
$manifest = Read-Source 'src\ga_manifest.inc'
$liveTile = Read-Source 'src\ga_live_tile.inc'
$defaults = Read-Source 'src\ga_config_defaults.inc'
$runtime = Read-Source 'src\ga_runtime_helpers.inc'
$commandLine = Read-Source 'src\ga_command_line.inc'
$tray = Read-Source 'src\ga_tray.inc'

if ($ListChecks) {
    @(
        'Build script always builds host and broker',
        'Manifest executable fallback uses Win10 host and Win8 broker',
        'Live Tile update mode skips AppX registration path',
        'Live Tile update does not clear the existing tile first',
        'Windows 8 broker helper is the default stabilized Live Tile path'
    ) | ForEach-Object { Write-Host "- $_" }
    return
}

Write-Host 'Running GenerateAssets maintainer source regression checks...'
Write-Host 'This checks source guardrails only; it does not build or launch the app.'

Assert-Match 'Build script ignores legacy target arguments' 'BuildGenerateAssets.cmd' $buildScript '(?s)Build policy:.*ignores every argument.*GenerateAssetsLiveTileBroker\.exe.*One or more arguments were supplied and ignored' 'BuildGenerateAssets.cmd must accept but ignore old target arguments so every invocation builds the same outputs'
Assert-Match 'Build script always builds host and broker' 'BuildGenerateAssets.cmd' $buildScript '(?s)Building packaged Live Tile broker.*LiveTileBroker\.cpp.*Building main GenerateAssets host.*GenerateAssets\.cpp' 'BuildGenerateAssets.cmd must always build both GenerateAssets.exe and GenerateAssetsLiveTileBroker.exe'
Assert-NotMatch 'Build script no longer has argument-selected targets' 'BuildGenerateAssets.cmd' $buildScript 'BUILD_BROKER|BUILD_BACKGROUND_TASK|unknown build option|:ParseArgs|:ShowHelp' 'BuildGenerateAssets.cmd must not restore argument-selected target branches'

Assert-Match 'Manifest executable fallback uses Win10 host and Win8 broker' 'src\ga_manifest.inc' $manifest '(?s)EffectiveManifestExecutable.*ManifestHostExecutableName\(\).*Win8LiveTileBrokerAppEnabled\(\).*ManifestLiveTileBrokerExecutableName\(\).*ManifestAppxActivationStubExecutableName\(\).*ManifestSettingValidated\(L"Executable",\s*fallback\.c_str\(\)' 'manifest executable fallback must keep Windows 10 on GenerateAssets.exe while Windows 8/8.1 targets default to the packaged broker helper'
Assert-Match 'Manifest executable path rejects traversal segments' 'src\ga_manifest.inc' $manifest '(?s)IsManifestSafeRelativePath.*segment == L"\.".*segment == L"\.\.".*IsManifestExecutableValue.*IsManifestSafeRelativePath' 'manifest executable validation must reject . and .. relative path segments'
Assert-NotMatch 'Manifest overwrite is not a persistent default' 'src\ga_config_defaults.inc' $defaults '\{L"Manifest",\s*L"OverwriteExisting"' 'manifest overwrite must not be recreated as a persistent INI default'
Assert-NotMatch 'Manifest overwrite is not persisted from tray' 'src\ga_tray.inc' $tray 'IniWrite\(L"Manifest",\s*L"OverwriteExisting"' 'tray manifest regeneration must not write a persistent OverwriteExisting key'

Assert-Match 'Live Tile Auto mode chooses by package identity' 'src\ga_runtime_helpers.inc' $runtime '(?s)ConfiguredLiveTileUpdateMode.*EffectiveLiveTileUpdateMode.*CurrentProcessHasPackageIdentity\(\).*LiveTileUpdateMode::LiveTile.*LiveTileUpdateMode::Registration' 'Auto Live Tile mode must choose Live Tile updates only when the process has package identity'
Assert-Match 'Live Tile update mode skips AppX registration path' 'src\ga_generation.inc' $generation '(?s)else if \(useLiveTileUpdateForThisRun\).*Appx_Update_(?:Or_Request_)?LiveTile\(exeDir,\s*manifestInfo,\s*liveTileUpdateAssets,\s*appUpdateFailureMessage\).*else\s*\{.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\)' 'Live Tile update mode must call the tile updater/requester instead of re-registering the AppX manifest'
Assert-LiveTileUpdateDoesNotClearExistingTile $liveTile
Assert-Match 'Live Tile update requires package identity' 'src\ga_live_tile.inc' $liveTile '(?s)GetCurrentPackageFullName.*liveTileUpdateRequiresIdentity' 'Live Tile updater must detect missing package identity and report a clear failure'
Assert-Match 'Windows 8 broker helper is the default stabilized Live Tile path' 'config/manifest/live-tile sources' ($defaults + "`n" + $manifest + "`n" + $liveTile) '(?s)Win8LiveTileOopHelper",\s*L"0".*Win8LiveTileBackgroundTask",\s*L"0".*Win8LiveTileBrokerApp",\s*L"1".*Packaged WinRT Live Tile broker reported success' 'Windows 8/8.1 compatibility mode should default to the packaged WinRT broker while leaving background/OOP experiments disabled'
Assert-Match 'Command line exposes Live Tile mode overrides' 'src\ga_command_line.inc' $commandLine '(?s)--live-tile.*--no-live-tile.*--live-tile-auto.*--live-tile-mode.*ExperimentalLiveTileUpdate' 'command-line options must expose Auto, Registration, and Live Tile update mode overrides'
Assert-Match 'Windows 8 app activation command line arguments are tolerated' 'src\ga_command_line.inc' $commandLine '(?s)TryParseAppxActivationArgument.*-ServerName:.*IsAppxActivationServerNameOption.*IsIgnoredAppxActivationArgument.*-Embedding.*appxActivationServer' 'Windows 8/8.1-style AppX activation arguments such as -ServerName:... must not be rejected as unknown command-line options'

Write-Host "GenerateAssets source regression checks passed ($script:CheckCount checks)."

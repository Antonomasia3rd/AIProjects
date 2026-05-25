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

function Assert-SourceAbsent {
    param(
        [Parameter(Mandatory=$true)][string]$Name,
        [Parameter(Mandatory=$true)][string]$SourceName,
        [Parameter(Mandatory=$true)][string]$SourceText,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Failure
    )

    $script:CheckCount++
    if ($SourceText -match $Pattern) {
        throw "GenerateAssets source regression: $Failure [$Name in $SourceName]"
    }

    Write-Host "ok - $Name"
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

    $defaultMatch = [regex]::Match(
        $Defaults,
        '\{L"' + [regex]::Escape($Key) + '",\s*L"(?<value>(?:[^"\\]|\\.)*)"'
    )
    $requiresFormatValidation = $defaultMatch.Success -and $defaultMatch.Groups['value'].Value -match '%[0-9A-Za-z]'
    if ($requiresFormatValidation) {
        Assert-SourceCheck (New-SourceCheck `
            -Name "UI format is validated: $Key" `
            -SourceName 'UI/string sources' `
            -SourceText $UiSources `
            -Pattern "RequireFormat\(g_ui\.$property" `
            -Failure "format validation missing for $Key")
    }
}

$generation = Read-Source 'src\ga_generation.inc'
$app = Read-Source 'src\ga_app.inc'
$image = Read-Source 'src\ga_image.inc'
$desktopIcon = Read-Source 'src\ga_desktop_icon_png.inc'
$registration = Read-Source 'src\ga_registration.inc'
$liveTile = Read-Source 'src\ga_live_tile.inc'
$manifest = Read-Source 'src\ga_manifest.inc'
$tray = Read-Source 'src\ga_tray.inc'
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
        -Failure 'startup must warn the user when a previous force shutdown skipped cleanup'),

    (New-SourceCheck `
        -Name 'Experimental Live Tile mode defaults off' `
        -SourceName 'src\ga_config_defaults.inc' `
        -SourceText $defaults `
        -Pattern '\{L"Settings",\s*L"ExperimentalLiveTileUpdate",\s*L"0"\}' `
        -Failure 'experimental Live Tile update must remain opt-in by default'),

    (New-SourceCheck `
        -Name 'Generated manifest launches GenerateAssets by default' `
        -SourceName 'src\ga_config_defaults.inc' `
        -SourceText $defaults `
        -Pattern '\{L"Manifest",\s*L"Executable",\s*L"GenerateAssets\.exe"\}' `
        -Failure 'manifest executable default must point at GenerateAssets.exe so packaged launches have identity'),

    (New-SourceCheck `
        -Name 'Manifest executable fallback launches GenerateAssets' `
        -SourceName 'src\ga_manifest.inc' `
        -SourceText $manifest `
        -Pattern 'ManifestSettingValidated\(L"Executable",\s*L"GenerateAssets\.exe",\s*L"ManifestExecutable",\s*IsManifestExecutableValue\)' `
        -Failure 'manifest executable fallback must point at GenerateAssets.exe'),

    (New-SourceCheck `
        -Name 'Experimental Live Tile mode skips AppX registration path' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)useLiveTileUpdateForThisRun.*Appx_Update_LiveTile\(exeDir,\s*manifestInfo,\s*liveTileUpdateAssets,\s*appUpdateFailureMessage\).*else\s*\{.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\)' `
        -Failure 'experimental Live Tile mode must call the tile updater instead of re-registering the AppX manifest'),

    (New-SourceCheck `
        -Name 'Live Tile checkbox queues one-time re-registration' `
        -SourceName 'src\ga_tray.inc' `
        -SourceText $tray `
        -Pattern '(?s)ID_EXPERIMENTAL_LIVE_TILE_UPDATE.*IniWrite\(L"Settings",\s*L"ExperimentalLiveTileUpdate".*QueueLiveTileModeReregistration\(\)' `
        -Failure 'changing the Live Tile checkbox must queue a one-time AppX re-registration'),

    (New-SourceCheck `
        -Name 'Live Tile mode change forces registration before Live Tile updates' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)LiveTileModeReregistrationPending\(\).*StartupGenerationCanSkip.*LiveTileModeReregistrationPending\(\).*return false.*liveTileModeReregistrationPending.*useLiveTileUpdateForThisRun\s*=\s*liveTileUpdateMode\s*&&\s*!liveTileModeReregistrationPending.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\).*SetLiveTileModeReregistrationPending\(false\)' `
        -Failure 'Live Tile setting changes must bypass startup skip, force one registration, and clear the pending flag after registration succeeds'),

    (New-SourceCheck `
        -Name 'Live Tile update requires package identity' `
        -SourceName 'src\ga_live_tile.inc' `
        -SourceText $liveTile `
        -Pattern '(?s)GetCurrentPackageFullName.*liveTileUpdateRequiresIdentity' `
        -Failure 'Live Tile updater must detect missing package identity and report a clear failure'),

    (New-SourceCheck `
        -Name 'Live Tile mode disables static wallpaper manifest assets' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)staticWallpaperAssetEnabled\s*=\s*!liveTileUpdateMode\s*&&\s*IniReadI\(L"Assets",\s*t\.name,\s*0\)\s*!=\s*0.*g_deleteDisabledAssets\s*\|\|\s*liveTileUpdateMode' `
        -Failure 'Live Tile mode must treat static manifest assets as disabled and remove stale files when desktop-icon fallback is off'),

    (New-SourceCheck `
        -Name 'Desktop icon placeholder assets use full tile canvases' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)LoadEmbeddedDesktopIcon\(\).*RenderDesktopIconAsset\(icon\.get\(\),\s*t\.w,\s*t\.h,\s*t\.w,\s*t\.h\).*ScalePixels\(t\.w,\s*scale\).*ScalePixels\(t\.h,\s*scale\).*RenderDesktopIconAsset\(icon\.get\(\),\s*scaledW,\s*scaledH,\s*t\.w,\s*t\.h\)' `
        -Failure 'disabled/static Desktop icon placeholders must be rendered to target tile dimensions instead of saving the raw icon directly'),

    (New-SourceCheck `
        -Name 'Desktop icon placeholder renderer uses Windows 8 tile placement' `
        -SourceName 'src\ga_image.inc' `
        -SourceText $image `
        -Pattern '(?s)DesktopIconVisibleWidthForAsset.*MulDiv\(logicalShortSide,\s*74,\s*150\).*MulDiv\(logicalVisibleW,\s*assetShortSide,\s*logicalShortSide\).*DesktopIconCenterYForAsset.*MulDiv\(logicalShortSide,\s*119,\s*300\).*MulDiv\(logicalCenterY,\s*assetHeight,\s*logicalHeight\).*static Bitmap\* RenderDesktopIconAsset.*DrawImage\(icon,\s*Rect\(dx,\s*dy,\s*targetVisibleW,\s*targetVisibleH\)' `
        -Failure 'Desktop icon placeholder renderer must use Windows 8-style logical tile sizing and placement without stretching the icon or shrinking high-DPI variants'),

    (New-SourceCheck `
        -Name 'Desktop icon placeholder uses embedded high-resolution source' `
        -SourceName 'src\ga_desktop_icon_png.inc' `
        -SourceText $desktopIcon `
        -Pattern '(?s)Embedded 864x640 RGBA PNG generated from the Windows 8\.1 Desktop tile logo proportions.*0x00,\s*0x00,\s*0x03,\s*0x60,\s*0x00,\s*0x00,\s*0x02,\s*0x80' `
        -Failure 'Desktop icon placeholder must use an embedded high-resolution PNG source so the app stays portable and avoids low-resolution upscaling'),

    (New-SourceCheck `
        -Name 'Generated asset cache restores before loading wallpaper' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)BuildGenerationStateKey\(wp\).*TryRestoreGeneratedAssetCache\(.*if \(!restoredFromGeneratedAssetCache\).*std::make_unique<Bitmap>\(wp\)' `
        -Failure 'generated asset cache hits must bypass wallpaper loading and PNG rendering'),

    (New-SourceCheck `
        -Name 'Generated asset cache is populated after successful generation' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)GeneratedAssetCache.*AddGeneratedAssetCacheFile.*if \(ok\).*SaveGeneratedAssetCache\(exeDir,\s*BuildGenerationStateKey\(wp\),\s*generatedAssetCacheFiles\)' `
        -Failure 'successful generation must save generated PNGs into the bounded asset cache for repeat slideshow wallpapers'),

    (New-SourceCheck `
        -Name 'Generated asset pre-cache warms likely slideshow siblings in the background' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)RenderWallpaperToGeneratedAssetCache.*EnumerateSiblingWallpaperCandidates.*RunGeneratedAssetPrecache.*GeneratedAssetPrecacheThread.*QueueGeneratedAssetPrecache.*MarkGenerationActive\(false\).*QueueGeneratedAssetPrecache' `
        -Failure 'successful generation must opportunistically pre-cache likely slideshow siblings without blocking the foreground tile update'),

    (New-SourceCheck `
        -Name 'Generated asset cache settings are exposed in a Caching menu' `
        -SourceName 'src\ga_tray.inc' `
        -SourceText $tray `
        -Pattern '(?s)ID_GENERATED_ASSET_CACHE.*ID_GENERATED_ASSET_PRECACHE.*beginSection\(g_ui\.cachingTitle\).*g_ui\.generatedAssetCache.*g_ui\.generatedAssetPrecache.*GeneratedAssetCacheMaxEntriesLabel.*GeneratedAssetPrecacheMaxFilesLabel.*IniWrite\(L"Settings",\s*L"GeneratedAssetCache".*IniWrite\(L"Settings",\s*L"GeneratedAssetPrecache"' `
        -Failure 'cache and slideshow pre-cache settings must be configurable from a dedicated Caching menu section'),

    (New-SourceCheck `
        -Name 'Live Tile mode writes dedicated notification assets' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)g_liveTileAssets.*Assets\\\\LiveMediumTile\.png.*Assets\\\\LiveWideTile\.png.*Assets\\\\LiveLargeTile\.png.*NewLiveTileAssetToken.*VersionedLiveTileAssetPath.*if \(liveTileUpdateMode\).*VersionedLiveTileAssetPath\(t,\s*liveTileToken\).*liveTileUpdateAssets\.push_back' `
        -Failure 'Live Tile mode must write dedicated versioned Live*.png notification assets instead of relying on manifest logo assets or overwriting the displayed tile image in place'),

    (New-SourceCheck `
        -Name 'Live Tile XML uses dedicated notification assets' `
        -SourceName 'src\ga_live_tile.inc' `
        -SourceText $liveTile `
        -Pattern '(?s)BuildLiveTileXml.*const std::vector<LiveTileUpdateAsset>& liveTileAssets.*for \(const auto& asset : liveTileAssets\).*LiveTileAssetUriIfValid\(exeDir,\s*asset\.file\).*AppendLiveTileImageBinding\(xml,\s*asset\.binding' `
        -Failure 'Live Tile XML must reference the generated Live*.png update assets')
)

$uiStringKeys = @(
    'ComRegistrationAsyncError',
    'ComRegistrationDeploymentError',
    'ComRegistrationDeploymentMessage',
    'LiveTileUpdateSummary',
    'LiveTileUpdateException',
    'LiveTileUpdateMessage',
    'LiveTilePackageIdentity',
    'CachingTitle',
    'GeneratedAssetCache',
    'GeneratedAssetPrecache',
    'GeneratedAssetCacheMaxEntriesLabel',
    'GeneratedAssetPrecacheMaxFilesLabel',
    'GeneratedAssetCacheHit',
    'GeneratedAssetCacheSaved',
    'GeneratedAssetCacheSummary',
    'GeneratedAssetPrecacheSaved',
    'GeneratedAssetPrecacheSummary'
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

Assert-SourceAbsent `
    -Name 'Live Tile update does not clear the existing tile first' `
    -SourceName 'src\ga_live_tile.inc' `
    -SourceText $liveTile `
    -Pattern 'updater\.Clear\s*\(' `
    -Failure 'Live Tile updater must not call Clear before Update because that causes a visible blank tile during refresh'

foreach ($key in $uiStringKeys) {
    Assert-UiStringWired -Key $key -Defaults $defaults -UiSources $uiSources
}

Write-Host "GenerateAssets source regression checks passed ($script:CheckCount checks)."

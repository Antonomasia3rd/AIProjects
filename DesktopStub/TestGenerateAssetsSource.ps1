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
$buildScript = Read-Source 'BuildGenerateAssets.cmd'
$image = Read-Source 'src\ga_image.inc'
$desktopIcon = Read-Source 'src\ga_desktop_icon_png.inc'
$registration = Read-Source 'src\ga_registration.inc'
$liveTile = Read-Source 'src\ga_live_tile.inc'
$manifest = Read-Source 'src\ga_manifest.inc'
$tray = Read-Source 'src\ga_tray.inc'
$runtime = Read-Source 'src\ga_runtime_helpers.inc'
$commandLine = Read-Source 'src\ga_command_line.inc'
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
        -Name 'Build script ignores legacy target arguments' `
        -SourceName 'BuildGenerateAssets.cmd' `
        -SourceText $buildScript `
        -Pattern '(?s)Build policy:.*ignores every argument.*GenerateAssetsLiveTileBroker\.exe.*if not "%~1"=="".*One or more arguments were supplied and ignored' `
        -Failure 'BuildGenerateAssets.cmd must accept but ignore old target arguments so every invocation builds the same outputs'),

    (New-SourceCheck `
        -Name 'Build script always builds host and broker' `
        -SourceName 'BuildGenerateAssets.cmd' `
        -SourceText $buildScript `
        -Pattern '(?s)echo Building packaged Live Tile broker\.\.\..*LiveTileBroker\.cpp.*echo Building main GenerateAssets host\.\.\..*GenerateAssets\.cpp' `
        -Failure 'BuildGenerateAssets.cmd must always build both GenerateAssets.exe and GenerateAssetsLiveTileBroker.exe'),

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
        -Name 'Live Tile update mode defaults auto' `
        -SourceName 'src\ga_config_defaults.inc' `
        -SourceText $defaults `
        -Pattern '\{L"Settings",\s*L"ExperimentalLiveTileUpdate",\s*L"Auto"\}' `
        -Failure 'Live Tile update mode must default to Auto so Start tile launches can use package identity while direct launches use registration'),

    (New-SourceCheck `
        -Name 'Live Tile Auto mode chooses by package identity' `
        -SourceName 'src\ga_runtime_helpers.inc' `
        -SourceText $runtime `
        -Pattern '(?s)ConfiguredLiveTileUpdateMode.*EffectiveLiveTileUpdateMode.*CurrentProcessHasPackageIdentity\(\).*LiveTileUpdateMode::LiveTile.*LiveTileUpdateMode::Registration' `
        -Failure 'Auto Live Tile mode must choose Live Tile updates only when the process has package identity'),

    (New-SourceCheck `
        -Name 'INI template does not expose manifest editor defaults' `
        -SourceName 'src\ga_config_defaults.inc' `
        -SourceText $defaults `
        -Pattern '(?s)BuildInitialIniTemplate(?!.*lines\.push_back\(L"\[Manifest\]"\))' `
        -Failure 'INI template must not recreate the redundant [Manifest] editor section'),

    (New-SourceCheck `
        -Name 'Manifest executable fallback uses Win10 host and Win8 broker' `
        -SourceName 'src\ga_manifest.inc' `
        -SourceText $manifest `
        -Pattern '(?s)EffectiveManifestExecutable.*ManifestHostExecutableName\(\).*ConfiguredManifestCompatibilityTarget.*Win8LiveTileBrokerAppEnabled\(\).*ManifestLiveTileBrokerExecutableName\(\).*ManifestAppxActivationStubExecutableName\(\).*ManifestSettingValidated\(L"Executable",\s*fallback\.c_str\(\)' `
        -Failure 'manifest executable fallback must keep Windows 10 on GenerateAssets.exe while Windows 8/8.1 targets default to the packaged broker helper'),

    (New-SourceCheck `
        -Name 'Manifest generation ignores redundant INI manifest editor' `
        -SourceName 'src\ga_manifest.inc' `
        -SourceText $manifest `
        -Pattern '(?s)Manifest generation intentionally uses built-in defaults.*legacySettingsKey.*return fallback' `
        -Failure 'generated AppxManifest.xml must use built-in defaults instead of a redundant INI manifest editor'),

    (New-SourceCheck `
        -Name 'Obsolete manifest INI editor is removed and blocked' `
        -SourceName 'command-line/config sources' `
        -SourceText ($commandLine + "`n" + $defaults) `
        -Pattern '(?s)CommandLineSettingTargetsManifestEditor.*IEquals\(entry\.section,\s*L"Manifest"\).*RemoveObsoleteManifestIniSettings.*IEquals\(sectionName,\s*L"Manifest"\).*IsLegacyManifestSettingKey' `
        -Failure 'the redundant [Manifest] INI editor must be removed from existing INIs and blocked from command-line writes'),

    (New-SourceCheck `
        -Name 'Manifest executable path rejects traversal segments' `
        -SourceName 'src\ga_manifest.inc' `
        -SourceText $manifest `
        -Pattern '(?s)IsManifestSafeRelativePath.*segment == L"\.".*segment == L"\.\.".*IsManifestExecutableValue.*IsManifestSafeRelativePath' `
        -Failure 'manifest executable validation must reject . and .. relative path segments'),

    (New-SourceCheck `
        -Name 'Live Tile update mode skips AppX registration path' `
        -SourceName 'src\ga_generation.inc' `
        -SourceText $generation `
        -Pattern '(?s)useLiveTileUpdateForThisRun.*Appx_Update_LiveTile\(exeDir,\s*manifestInfo,\s*liveTileUpdateAssets,\s*appUpdateFailureMessage\).*else\s*\{.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\)' `
        -Failure 'Live Tile update mode must call the tile updater instead of re-registering the AppX manifest'),

    (New-SourceCheck `
        -Name 'Live Tile mode menu queues one-time re-registration' `
        -SourceName 'src\ga_tray.inc' `
        -SourceText $tray `
        -Pattern '(?s)ID_LIVE_TILE_MODE_AUTO.*ID_LIVE_TILE_MODE_REGISTRATION.*ID_LIVE_TILE_MODE_LIVE_TILE.*IniWrite\(L"Settings",\s*L"ExperimentalLiveTileUpdate".*QueueLiveTileModeReregistration\(\)' `
        -Failure 'changing the Live Tile mode menu must queue a one-time AppX re-registration'),

    (New-SourceCheck `
        -Name 'Command line exposes Live Tile mode overrides' `
        -SourceName 'src\ga_command_line.inc' `
        -SourceText $commandLine `
        -Pattern '(?s)--live-tile.*--no-live-tile.*--live-tile-auto.*--live-tile-mode.*ExperimentalLiveTileUpdate' `
        -Failure 'command-line options must expose Auto, Registration, and Live Tile update mode overrides'),

    (New-SourceCheck `
        -Name 'Command line exposes one-shot manifest regeneration' `
        -SourceName 'command-line/app sources' `
        -SourceText ($commandLine + "`n" + $app) `
        -Pattern '(?s)--regenerate-manifest.*CommandLineShouldRegenerateManifest.*RegenerateAppxManifestFromConfig' `
        -Failure 'manifest regeneration must be available as a one-shot command-line action'),

    (New-SourceCheck `
        -Name 'Manifest tray action is one-shot' `
        -SourceName 'src\ga_tray.inc' `
        -SourceText $tray `
        -Pattern '(?s)ID_MANIFEST_REGENERATE.*RegenerateAppxManifestFromConfigAndLog\(\).*QueueGenerate' `
        -Failure 'tray manifest regeneration must execute once and queue generation instead of toggling a persistent INI setting'),

    (New-SourceCheck `
        -Name 'Custom INI uses separate single-instance scope' `
        -SourceName 'src\ga_app.inc' `
        -SourceText $app `
        -Pattern '(?s)ConfigureSingleInstanceIdentity\(const std::wstring& exeDir,\s*const std::wstring& iniPath,\s*bool customIniPath\).*customIniPath\s*\?\s*iniPath\s*:\s*exeDir.*ConfigureSingleInstanceIdentity\(dir,\s*g_iniPath,\s*g_commandLine\.customIniPath\)' `
        -Failure 'alternate --ini runs must not signal the default running instance'),

    (New-SourceCheck `
        -Name 'Command-line requests fail when existing instance cannot be signaled' `
        -SourceName 'src\ga_app.inc' `
        -SourceText $app `
        -Pattern '(?s)if \(!SignalExistingInstance\(singleInstanceRequest\)\).*\(singleInstanceRequest\s*&\s*SINGLE_INSTANCE_REQUEST_MASK\)\s*!=\s*SINGLE_INSTANCE_REQUEST_SHOW.*ShowCommandLineMessage\(L"Failed to signal the running GenerateAssets instance\.",\s*true\).*return 2' `
        -Failure 'command-line exit/generate/reload requests must not report success when the running instance cannot be signaled'),

    (New-SourceCheck `
        -Name 'Command-line reload only applies tray visibility when TrayIcon changed' `
        -SourceName 'command-line/app sources' `
        -SourceText ($commandLine + "`n" + $app) `
        -Pattern '(?s)CommandLineSettingChangesTrayIcon.*IEquals\(entry\.section,\s*L"Settings"\).*IEquals\(entry\.key,\s*L"TrayIcon"\).*ApplySettingsChangedByCommandLine\(bool applyTrayIcon\).*if \(applyTrayIcon\)\s*ApplyTrayIconSettingFromIni\(\).*CommandLineTrayIconChanged\(\).*SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON' `
        -Failure 'command-line settings reloads must not resurrect or hide the session tray icon unless TrayIcon changed'),

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
        -Name 'Manifest target can generate Windows 10, 8.1, and 8 manifests' `
        -SourceName 'manifest/command-line sources' `
        -SourceText ($manifest + "`n" + $commandLine + "`n" + $tray) `
        -Pattern '(?s)appx/2010/manifest.*xmlns:m2=.*2013/manifest.*AppxManifestTarget.*--manifest-target.*ID_MANIFEST_TARGET_WINDOWS10.*ID_MANIFEST_TARGET_WINDOWS81.*ID_MANIFEST_TARGET_WINDOWS8' `
        -Failure 'manifest target selection must cover Windows 10, Windows 8.1, and Windows 8 from command line and tray'),

    (New-SourceCheck `
        -Name 'Windows 8 broker helper is the default stabilized Live Tile path' `
        -SourceName 'manifest/live-tile/config sources' `
        -SourceText ($defaults + "`n" + $manifest + "`n" + $liveTile) `
        -Pattern '(?s)\{L"Settings",\s*L"Win8LiveTileOopHelper",\s*L"0"\}.*\{L"Settings",\s*L"Win8LiveTileBackgroundTask",\s*L"0"\}.*\{L"Settings",\s*L"Win8LiveTileBrokerApp",\s*L"1"\}.*Win8LiveTileBrokerApp.*IniReadI\(L"Settings",\s*L"Win8LiveTileBrokerApp",\s*1\).*Packaged WinRT Live Tile broker reported success' `
        -Failure 'Windows 8/8.1 compatibility mode should default to the packaged WinRT broker while leaving background/OOP experiments disabled'),

    (New-SourceCheck `
        -Name 'Windows 8 app activation command line arguments are tolerated' `
        -SourceName 'src\ga_command_line.inc' `
        -SourceText $commandLine `
        -Pattern '(?s)TryParseAppxActivationArgument.*-ServerName:.*IsAppxActivationServerNameOption.*IsIgnoredAppxActivationArgument.*-Embedding.*appxActivationServer' `
        -Failure 'Windows 8/8.1-style AppX activation arguments such as -ServerName:... must not be rejected as unknown command-line options'),

    (New-SourceCheck `
        -Name 'Legacy Live Tile XML uses Windows 8/8.1 templates' `
        -SourceName 'src\ga_live_tile.inc' `
        -SourceText $liveTile `
        -Pattern '(?s)LiveTileTemplateForTarget.*TileSquare150x150Image.*TileSquareImage.*TileWide310x150Image.*TileWideImage.*TileSquare310x310Image.*visual version=.*2' `
        -Failure 'Windows 8/8.1 manifest targets must use legacy tile notification templates instead of Windows 10 adaptive templates'),

    (New-SourceCheck `
        -Name 'Live Tile XML uses dedicated notification assets' `
        -SourceName 'src\ga_live_tile.inc' `
        -SourceText $liveTile `
        -Pattern '(?s)BuildLiveTileXml.*const std::vector<LiveTileUpdateAsset>& liveTileAssets.*for \(const auto& asset : liveTileAssets\).*LiveTileTemplateForTarget\(asset\.binding\).*LiveTileAssetUriIfValid\(exeDir,\s*asset\.file\).*AppendLiveTileImageBinding\(xml,\s*templateName' `
        -Failure 'Live Tile XML must reference the generated Live*.png update assets')
)

$uiStringKeys = @(
    'ComRegistrationAsyncError',
    'ComRegistrationDeploymentError',
    'ComRegistrationDeploymentMessage',
    'LiveTileUpdateSummary',
    'LiveTileModeAuto',
    'LiveTileModeRegistration',
    'LiveTileModeLiveTile',
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
    'GeneratedAssetPrecacheSummary',
    'ManifestTargetSummary',
    'AppxManifestTarget',
    'AppxManifestTargetWindows10',
    'AppxManifestTargetWindows81',
    'AppxManifestTargetWindows8',
    'ManifestTargetLabel',
    'ManifestSquare30x30LogoLabel',
    'ManifestSplashScreenImageLabel',
    'ManifestRegenerateNow',
    'GenerateScaleAutoPreservesManualToggles',
    'StartupGenerationReason',
    'CommandLineGenerationRequestReason',
    'CommandLineOnceReason',
    'CommandLineWallpaperReason',
    'CommandLineForceGenerateReason',
    'ManifestRegenerateReason',
    'CommandLineWallpaperPathInvalid',
    'CommandLineManifestSettingIgnored',
    'AppxManifestRegenerated',
    'AppxManifestRegenerateFailed'
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
    -Name 'Build script no longer has argument-selected targets' `
    -SourceName 'BuildGenerateAssets.cmd' `
    -SourceText $buildScript `
    -Pattern 'BUILD_BROKER|BUILD_BACKGROUND_TASK|unknown build option|:ParseArgs|:ShowHelp' `
    -Failure 'BuildGenerateAssets.cmd must not restore argument-selected target branches'

Assert-SourceAbsent `
    -Name 'Live Tile update does not clear the existing tile first' `
    -SourceName 'src\ga_live_tile.inc' `
    -SourceText $liveTile `
    -Pattern 'updater\.Clear\s*\(' `
    -Failure 'Live Tile updater must not call Clear before Update because that causes a visible blank tile during refresh'

Assert-SourceAbsent `
    -Name 'Manifest overwrite is not a persistent default' `
    -SourceName 'src\ga_config_defaults.inc' `
    -SourceText $defaults `
    -Pattern '\{L"Manifest",\s*L"OverwriteExisting"' `
    -Failure 'manifest overwrite must not be recreated as a persistent INI default'

Assert-SourceAbsent `
    -Name 'Manifest overwrite is not persisted from tray' `
    -SourceName 'src\ga_tray.inc' `
    -SourceText $tray `
    -Pattern 'IniWrite\(L"Manifest",\s*L"OverwriteExisting"' `
    -Failure 'tray manifest regeneration must not write a persistent OverwriteExisting key'

foreach ($key in $uiStringKeys) {
    Assert-UiStringWired -Key $key -Defaults $defaults -UiSources $uiSources
}

Write-Host "GenerateAssets source regression checks passed ($script:CheckCount checks)."

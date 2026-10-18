#requires -Version 7.0
param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
$contentBuildDirectory = if ($env:CONTENT_OPTIMIZE -ceq '1') { 'build/content-integration-optimized' } else { 'build/content-integration' }
$contentBuild = Join-Path $ReviewRoot $contentBuildDirectory
$targets = @('HardwareProfileImportTests','NowPlayingProfileImportTests',
    'NotesSourceTests','PrayerTimesModelTests','PrayerTimesSourceTests','SmtcSourceTests','DiscordServiceTests','WinrtFactoryLifetimeTests','TransparentBackgroundTests','AdaptiveTileTextLayoutTests','AdaptiveTileTextRenderTests',
    'PngCrc32Tests','CaptureAttemptTests','ContentPresetTests','ContentQueueTests','SecondaryTileTests',
    'TileVisualAssetsTests','TileVisualAssetsRenderTests','NotesTileRowsTests','MediaTileLayoutTests','GalleryEngineTests','GalleryInventoryTests','GalleryInventoryNativeTests','GalleryRuntimeTests','CapsBlinkEngineTests','TileNotificationPayloadTests','BoundedCaptureProcessTests','DiscordProfileEditTests',
    'ContentRuntimeTests','ContentRevisionHostTests','ContentRuntimeNoHardwareTests','GalleryTransactionTests')
foreach ($target in (@('DiscordService','DiscordEditor','AsusService') + $targets)) {
    # Optimizing the full host translation unit can exceed a minute on slower
    # laptops. This compile budget does not change fixture watchdogs.
    $compileSeconds = if ($target -in @('ContentRuntimeTests','ContentRevisionHostTests','ContentRuntimeNoHardwareTests','TransparentBackgroundTests','AdaptiveTileTextRenderTests')) { 240 } else { 60 }
    $built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',
        (Join-Path $PSScriptRoot 'BuildContentIntegrationTests.cmd'), $target) -Label ($target + '-content-build') -Seconds $compileSeconds
    Write-Output $built.Output.Trim()
    if ($built.ExitCode -ne 0) { throw "Content fixture compilation failed: $target" }
    if (-not $Run -or $targets -notcontains $target) { continue }
    $fixture = Join-Path $contentBuild ($target + '.exe')
    Assert-ReviewScan -Path $fixture -Label ($target + '-content')
    $tested = Invoke-ReviewProcess -File $fixture -Parameters @() -Label ($target + '-content-tests') -Seconds 60
    Write-Output $tested.Output.Trim()
    if ($tested.ExitCode -ne 0) { throw "Content fixture checks failed: $target" }
}
if (-not $Run) { Write-Output 'Compilation only. Pass -Run to scan and execute bounded inert fixtures.' }

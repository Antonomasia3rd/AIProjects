<#
One-time directory organizer for YouTube Music Tidy v21.

Default: DRY RUN.
Use -Apply only after a successful v21 consolidation run.

It never touches:
  raw_headers.txt
  youtube_music_tidy.ps1
  youtube_music_tidy_cache.json
  youtube_music_tidy_library_resolution.json
  youtube_music_tidy_library_tokens.json
  youtube_music_tidy_add_library_state.json
  youtube_music_tidy_nonmusic_playlists.txt
#>

[CmdletBinding()]
param(
    [switch]$Apply,
    [string]$Root = $PSScriptRoot
)

$ErrorActionPreference = "Stop"

$cachePath = Join-Path $Root "youtube_music_tidy_cache.json"
if (-not (Test-Path -LiteralPath $cachePath)) {
    throw "youtube_music_tidy_cache.json not found. Nothing will be moved."
}

$cache = Get-Content -LiteralPath $cachePath -Raw -Encoding UTF8 | ConvertFrom-Json

$managedCount = @($cache.ManagedPlaylists).Count
$placementCount = @($cache.PlacementHistory).Count

Write-Host ""
Write-Host "=== YouTube Music tidy directory cleanup ===" -ForegroundColor Cyan
Write-Host ("Mode: {0}" -f $(if ($Apply) { "APPLY" } else { "DRY RUN" }))
Write-Host ("Managed playlists cached: {0}" -f $managedCount)
Write-Host ("Placement history cached: {0}" -f $placementCount)

if ($managedCount -le 0) {
    throw @"
v21 managed-playlist consolidation is not present in the cache.
Run youtube_music_tidy.ps1 -ReportOnly once with v21 before archiving legacy state.
"@
}

if ($placementCount -le 0) {
    Write-Warning @"
No cached legacy placement history was found.
The migration CSVs will NOT be archived automatically because doing so would
remove exact historical placement evidence.
"@
}

$stamp = Get-Date -Format "yyyy-MM-dd_HHmmss"
$archiveRoot = Join-Path $Root "archive"
$legacyDir = Join-Path $archiveRoot ("legacy_migration_" + $stamp)
$reportsDir = Join-Path $archiveRoot ("generated_reports_" + $stamp)

$legacyFiles = @(
    "youtube_music_migration_state.json"
)

if ($placementCount -gt 0) {
    $legacyFiles += @(
        "youtube_music_migration.csv",
        "youtube_music_auto_assignments.csv"
    )
}

$reportFiles = @(
    "youtube_music_tidy_report.csv",
    "youtube_music_tidy_actions.csv",
    "youtube_music_tidy_playlist_settings.csv",
    "youtube_music_tidy_taste_lane_suggestions.csv"
)

function Plan-Move {
    param(
        [string]$Name,
        [string]$DestinationDirectory
    )

    $source = Join-Path $Root $Name
    if (-not (Test-Path -LiteralPath $source)) {
        return
    }

    $dest = Join-Path $DestinationDirectory $Name

    if ($Apply) {
        if (-not (Test-Path -LiteralPath $DestinationDirectory)) {
            New-Item -ItemType Directory -Path $DestinationDirectory -Force | Out-Null
        }

        Move-Item -LiteralPath $source -Destination $dest -Force
        Write-Host ("MOVED: {0} -> {1}" -f $Name, $DestinationDirectory) -ForegroundColor Green
    }
    else {
        Write-Host ("WOULD MOVE: {0} -> {1}" -f $Name, $DestinationDirectory)
    }
}

foreach ($name in $legacyFiles) {
    Plan-Move -Name $name -DestinationDirectory $legacyDir
}

foreach ($name in $reportFiles) {
    Plan-Move -Name $name -DestinationDirectory $reportsDir
}

Write-Host ""
Write-Host "Files intentionally left in the active directory:" -ForegroundColor Cyan
@(
    "youtube_music_tidy.ps1",
    "raw_headers.txt",
    "youtube_music_tidy_cache.json",
    "youtube_music_tidy_library_resolution.json",
    "youtube_music_tidy_library_tokens.json",
    "youtube_music_tidy_add_library_state.json",
    "youtube_music_tidy_nonmusic_playlists.txt"
) | ForEach-Object {
    $path = Join-Path $Root $_
    if (Test-Path -LiteralPath $path) {
        Write-Host ("  KEEP: {0}" -f $_)
    }
}

if (-not $Apply) {
    Write-Host ""
    Write-Host "Dry run only. Re-run with -Apply to perform the moves." -ForegroundColor Yellow
}

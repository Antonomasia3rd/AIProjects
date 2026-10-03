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

$implementation = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'dependencies/YouTubeMusicMigrate/cleanup_youtube_music_tidy_directory_app.ps1'
& $implementation -Root $Root -Apply:$Apply

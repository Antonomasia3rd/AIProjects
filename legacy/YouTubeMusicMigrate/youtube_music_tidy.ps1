<#
youtube_music_tidy_v27.ps1

Cached, resumable YouTube Music tidying/audit tool (v27).

Topology checked:
  Liked Music
    - playlist membership
    - expected / potentially wrong managed playlist
    - actual song-library status when exposed by YouTube Music

  Playlists
    - same video vs changed metadata / possible alternate version
    - expected / potentially wrong managed playlist
    - actual song-library status when exposed
    - liked status

  Song Library
    - playlist membership
    - expected / potentially wrong managed playlist
    - liked status

Design goals:
- PowerShell 5.1 only; no Python required on the user's machine.
- Reuses the proven raw_headers.txt browser-auth setup.
- First run (or -FullScan): full snapshots.
- Normal later runs: lightweight cache validation.
- Liked Music additions can be patched from the newest page when the old head
  shifts by exactly the detected count delta.
- Song Library recently-added changes can be reconciled from page 1 when the
  page proves either a pure newest-prefix addition or a reorder of the same
  cached page-1 membership; ambiguous cases still force a full scan.
- Managed playlists are persisted in the tidy cache after first discovery.
- Legacy youtube_music_migration_state.json is optional import-only input in v21.
- If no legacy state/cache exists, numbered playlists such as "01 · Drift" or
  "05 - Bass" are auto-detected from the user's playlist index.
- Main and Other are protected by title if they still exist; v21 does not depend
  on either source playlist existing.
- Every mutation category requires an explicit YES confirmation.
- ADD_LIBRARY writes use small feedback-token batches, checkpoint batch state,
  and post-verify every exact Video ID against a fresh complete Song Library snapshot.
- DNS/connection outages can wait and retry the same request instead of aborting immediately.
- HTTP request tracing is ON by default; use -QuietRequests to suppress it.
- Dynamic YouTube-generated playlists are excluded by default from topology.
- UNKNOWN Library states can be deep-resolved on demand and cached.
- Library classification and add-token acquisition are separate cached phases.
- Completed playlist snapshots checkpoint to disk after every playlist by default.
- "Possible updated/alternate version" is REPORT ONLY because different
  video IDs can legitimately represent covers, remixes, uploads, or reissues.
- "Potentially wrong" inferred from artist history is REPORT ONLY.
  Automatic playlist moves are only offered when an exact historical
  Video ID -> playlist mapping exists.

IMPORTANT:
raw_headers.txt contains live Google/YouTube session credentials. Treat it
like a password. Never upload, share, or commit it.

The internal YouTube Music API is unofficial and can change.
#>

[CmdletBinding()]
param(
    # Optional legacy import sources. v21 imports these into the main tidy
    # cache once, after which normal operation no longer depends on them.
    [string]$MigrationCsvPath = (Join-Path $PSScriptRoot "youtube_music_migration.csv"),
    [string]$AutoAssignmentsPath = (Join-Path $PSScriptRoot "youtube_music_auto_assignments.csv"),
    [string]$StatePath = (Join-Path $PSScriptRoot "youtube_music_migration_state.json"),
    [string]$HeadersPath = (Join-Path $PSScriptRoot "raw_headers.txt"),

    [string]$CachePath = (Join-Path $PSScriptRoot "youtube_music_tidy_cache.json"),
    [string]$ReportPath = (Join-Path (Join-Path $PSScriptRoot "reports") "youtube_music_tidy_report.csv"),
    [string]$ActionsPath = (Join-Path (Join-Path $PSScriptRoot "reports") "youtube_music_tidy_actions.csv"),

    # Empty defaults are intentional in the shareable build. The first-run
    # CLUI discovers the authenticated account and stores the expected identity
    # in the non-secret config file after an explicit YES.
    [string]$ExpectedAccountName = "",
    [string]$ExpectedChannelHandle = "",

    [string]$ConfigPath = (Join-Path $PSScriptRoot "youtube_music_tidy_config.json"),

    # Force the console setup wizard even when configuration already exists.
    [switch]$Setup,

    # Disable the automatic menu/wizard when launching without operational
    # switches. Existing CLI automation can use this to stay non-interactive.
    [switch]$NoInteractive,

    [int]$HeadCheckCount = 5,
    [int]$BatchSize = 20,
    [double]$BatchDelaySeconds = 2.0,
    [int]$MaxRetries = 4,
    [int]$RetryBaseSeconds = 2,

    [switch]$FullScan,
    [switch]$ReportOnly,
    [switch]$ManagedPlaylistsOnly,

    # Audit/normalize the auto-detected numbered managed playlists to an
    # explicit settings policy that does not depend on Main/Other existing:
    #   visibility = PUBLIC
    #   description = per-playlist two-line text modeled after the old Main/Other
    #                 descriptions, using "split from main/other" instead of
    #                 "migrated from spotify"
    #   community voting = EVERYONE
    #   collaboration = ENABLED
    # Sort order and add-to-top are intentionally left alone.
    [switch]$NormalizeManagedPlaylistSettings,

    [string]$PlaylistSettingsReportPath = (Join-Path (Join-Path $PSScriptRoot "reports") "youtube_music_tidy_playlist_settings.csv"),

    # Metadata-only taste-lane suggestions for new users/friends. This does NOT
    # listen to audio, create playlists, or move songs. It weighs existing
    # playlist names heavily and title/artist text lightly, then writes a
    # review-only suggestion CSV.
    [switch]$SuggestTasteLanes,
    [int]$TasteLaneCount = 8,
    [string]$TasteLaneSuggestionsPath = (Join-Path (Join-Path $PSScriptRoot "reports") "youtube_music_tidy_taste_lane_suggestions.csv"),

    # Skip playlist network probes and trust cached playlist snapshots.
    # Useful immediately after a recent successful full playlist scan.
    [switch]$ReusePlaylistCache,

    # Generate optional LIKE actions. Without this switch, liked status is
    # still reported, but LIKE mutations are not added to the actions CSV.
    [switch]$IncludeLikeActions,

    # Kept for backward compatibility. HTTP tracing is ON by default in v9.
    [switch]$TraceRequests,

    # Suppress per-request HTTP lines while retaining playlist/page progress.
    [switch]$QuietRequests,

    # Include YouTube-generated/dynamic playlist IDs (RD*, LRSR*, SS).
    [switch]$IncludeDynamicPlaylists,

    # Additional saved playlists whose entries should remain visible/audited
    # but should not be treated as Song-Library candidates when an item is
    # sourced ONLY from excluded non-music playlists.
    [string[]]$AdditionalNonMusicPlaylistIds = @(),

    # Optional persistent config: one playlist ID per line. Blank lines and
    # lines beginning with # are ignored. Built-in exclusions are always kept.
    [string]$NonMusicPlaylistConfigPath = (Join-Path $PSScriptRoot "youtube_music_tidy_nonmusic_playlists.txt"),

    # Deep-resolve UNKNOWN Library states with exact-video-ID matches from
    # authenticated YouTube Music search results. Results are cached to disk.
    [switch]$ResolveUnknownLibrary,

    # Max UNKNOWN tracks to resolve per run. 0 = no limit.
    [int]$ResolveLibraryLimit = 250,

    [string]$LibraryResolutionCachePath = (Join-Path $PSScriptRoot "youtube_music_tidy_library_resolution.json"),

    # Enrich confirmed InLibrary=NO catalogue songs with YouTube-provided
    # add-to-Library feedback tokens. This is READ-ONLY; it only makes future
    # ADD_LIBRARY proposals actionable.
    [switch]$EnrichLibraryTokens,

    # Max confirmed-missing songs to attempt per run. 0 = no limit.
    [int]$EnrichLibraryTokenLimit = 100,

    # Retry rows that a previous token-enrichment pass could not make
    # actionable under the current token strategy.
    [switch]$RetryUnavailableLibraryTokens,

    [string]$LibraryTokenCachePath = (Join-Path $PSScriptRoot "youtube_music_tidy_library_tokens.json"),

    # Safety cap for actual ADD_LIBRARY writes after confirmation.
    # 0 = no cap.
    [int]$AddLibraryWriteLimit = 25,

    # Number of add-to-Library feedback tokens submitted in one /feedback
    # request. YouTube Music accepts a list of feedbackTokens; 20 is a
    # deliberately conservative working batch size, not a claimed API maximum.
    [int]$AddLibraryFeedbackBatchSize = 20,

    # Durable, token-free checkpoint of ADD_LIBRARY write/verification results.
    [string]$AddLibraryMutationStatePath = (Join-Path $PSScriptRoot "youtube_music_tidy_add_library_state.json"),

    # Give YouTube Music a short moment to surface newly-added songs before
    # the authoritative post-write Library verification snapshot.
    [int]$AddLibraryVerificationDelaySeconds = 8,

    # A recent complete Library snapshot can be reused for write preflight only
    # after its recently-added head has been checked unchanged in THIS run.
    # Interactive mode still requires exact YES before reuse. This is mainly
    # for resuming safely after an auth-only failure without rereading 30+ pages.
    [switch]$ReuseRecentWritePreflight,
    [int]$RecentWritePreflightMaxAgeMinutes = 30,

    # DNS / connection outages are retried for this many minutes independently
    # from normal HTTP retry count. 0 disables the extended outage wait.
    [int]$NetworkOutageRetryMinutes = 30,

    [int]$CheckpointEveryPlaylists = 1
)

# Keep product defaults beside this entry point; the shared engine must not
# read auth/config/cache files from its dependency directory.
$implementation = Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'dependencies/YouTubeMusicMigrate/youtube_music_tidy_app.ps1'
$invocationArguments = @{}
foreach ($entry in $PSBoundParameters.GetEnumerator()) {
    $invocationArguments[$entry.Key] = $entry.Value
}
. $implementation -DataRoot $PSScriptRoot -InvocationParameters $invocationArguments

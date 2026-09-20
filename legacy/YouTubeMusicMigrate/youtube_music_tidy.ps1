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

$ErrorActionPreference = "Stop"

# v9: request tracing is the default. Use -QuietRequests to suppress it.
$TraceRequests = -not $QuietRequests.IsPresent

$YtmDomain = "https://music.youtube.com"
$YtmApiKey = "AIzaSyC9XL3ZjWddXya6X74dJoCTL-WEYFDNX30"
$YtmClientName = "WEB_REMIX"
$YtmClientVersion = "1.$((Get-Date).ToUniversalTime().ToString('yyyyMMdd')).01.00"



# v27 cooperative runtime controls.
#
# PowerShell 5.1 does not provide a safe way to asynchronously suspend an
# in-flight Invoke-RestMethod and later resume it. Instead, PP requests a pause
# at the next safe checkpoint: before the next HTTP attempt, during retry/delay
# waits, or between playlist checkpoints. This avoids pausing after a mutating
# request has succeeded but before its durable state checkpoint is written.
$script:RuntimeStartedAt = Get-Date
$script:RuntimePhase = "startup"
$script:RuntimePauseMenuActive = $false
$script:RuntimePauseCount = 0
$script:RuntimeControlAvailable = $false
$script:RuntimeDoublePressWindowMilliseconds = 2000

if (-not $NoInteractive.IsPresent) {
    try {
        $null = [Console]::KeyAvailable
        $script:RuntimeControlAvailable = $true
    }
    catch {
        $script:RuntimeControlAvailable = $false
    }
}

function New-DefaultUserConfig {
    return [pscustomobject]@{
        Version = 1
        ExpectedAccountName = ""
        ExpectedChannelHandle = ""
        SetupCompletedAt = $null
        LastAuthRefreshAt = $null
        ManagedSetupCompleted = $false
    }
}

function Load-UserConfig {
    if (-not (Test-Path -LiteralPath $ConfigPath)) {
        return New-DefaultUserConfig
    }

    try {
        $cfg = Get-Content -LiteralPath $ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($cfg.PSObject.Properties.Name -notcontains "Version") {
            $cfg | Add-Member -NotePropertyName Version -NotePropertyValue 1
        }
        if ($cfg.PSObject.Properties.Name -notcontains "ExpectedAccountName") {
            $cfg | Add-Member -NotePropertyName ExpectedAccountName -NotePropertyValue ""
        }
        if ($cfg.PSObject.Properties.Name -notcontains "ExpectedChannelHandle") {
            $cfg | Add-Member -NotePropertyName ExpectedChannelHandle -NotePropertyValue ""
        }
        if ($cfg.PSObject.Properties.Name -notcontains "SetupCompletedAt") {
            $cfg | Add-Member -NotePropertyName SetupCompletedAt -NotePropertyValue $null
        }
        if ($cfg.PSObject.Properties.Name -notcontains "LastAuthRefreshAt") {
            $cfg | Add-Member -NotePropertyName LastAuthRefreshAt -NotePropertyValue $null
        }
        if ($cfg.PSObject.Properties.Name -notcontains "ManagedSetupCompleted") {
            $cfg | Add-Member -NotePropertyName ManagedSetupCompleted -NotePropertyValue $false
        }
        return $cfg
    }
    catch {
        throw ("Could not parse config file: {0}`n{1}" -f $ConfigPath, $_.Exception.Message)
    }
}

function Save-UserConfig {
    param($Config)

    $tmp = "$ConfigPath.tmp"
    $Config | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $ConfigPath -Force
}

function Reset-BrowserAuthRuntime {
    $script:BrowserAuth = $null
    $script:BrowserSession = $null
}

function Test-LooksLikeBrowserAuthClipboard {
    param([string]$Raw)

    if ([string]::IsNullOrWhiteSpace($Raw)) { return $false }

    $hasMusicHost = (
        $Raw -match '(?i)music\.youtube\.com' -or
        $Raw -match '(?i)\borigin:\s*https://music\.youtube\.com' -or
        $Raw -match '(?i)"origin"\s*=\s*"https://music\.youtube\.com'
    )

    $hasAuthMaterial = (
        $Raw -match '(?i)\bcookie\s*[:=]' -or
        $Raw -match '(?i)System\.Net\.Cookie\('
    )

    return ($hasMusicHost -and $hasAuthMaterial)
}

function Invoke-ClipboardBrowserAuthSetup {
    param(
        $Config,
        [switch]$AllowOverwrite,
        [switch]$Compact
    )

    if ($Compact) {
        Write-Section "Refresh browser authentication"
        Write-Host "Open/keep music.youtube.com on the intended account." -ForegroundColor Cyan
        Write-Host "DevTools -> Network -> successful POST /browse -> Copy -> Copy as PowerShell."
        Write-Host "Return here without pasting the copied command."
        Write-Warning "Keep the YouTube Music tab open until the pending write finishes."
    }
    else {
        Write-Section "Browser authentication setup"
        Write-Host "This keeps the setup inside this PowerShell window." -ForegroundColor Cyan
        Write-Host ""
        Write-Host "Chrome / Edge:"
        Write-Host "  1. Open https://music.youtube.com and select the intended account."
        Write-Host "  2. Press F12 (or Ctrl+Shift+I) -> Network."
        Write-Host "  3. Filter for: /browse"
        Write-Host "  4. Click Library / navigate until a POST /browse request has Status 200."
        Write-Host "  5. Right-click that request -> Copy -> Copy as PowerShell."
        Write-Host "  6. Return here. Do NOT paste it into the console."
        Write-Host ""
        Write-Warning "The copied request contains live Google/YouTube session credentials. Never share it."
    }

    if ((Test-Path -LiteralPath $HeadersPath) -and -not $AllowOverwrite) {
        Write-Host ""
        $overwrite = Read-Host "raw_headers.txt already exists. Type YES to replace it from the clipboard"
        if ($overwrite -cne "YES") {
            Write-Host "Keeping the existing authentication file." -ForegroundColor Yellow
            return $false
        }
    }

    [void](Read-Host "After Copy as PowerShell is on your clipboard, press ENTER")

    $getClipboard = Get-Command Get-Clipboard -ErrorAction SilentlyContinue
    if ($null -eq $getClipboard) {
        throw @"
Get-Clipboard is unavailable in this PowerShell environment.
Create raw_headers.txt manually using the browser instructions in the README,
then rerun the setup wizard.
"@
    }

    $raw = [string](Get-Clipboard -Raw)
    if (-not (Test-LooksLikeBrowserAuthClipboard $raw)) {
        throw @"
Clipboard does not look like an authenticated music.youtube.com request.

Nothing was written.

Copy a Status-200 POST /browse request using Chrome/Edge:
  Network -> /browse -> right-click -> Copy -> Copy as PowerShell
then rerun setup.
"@
    }

    $raw | Set-Content -LiteralPath $HeadersPath -Encoding UTF8
    Reset-BrowserAuthRuntime

    Write-Host ("Saved browser authentication to: {0}" -f $HeadersPath) -ForegroundColor Green
    Write-Host "The file remains local and is never embedded in the shareable script." -ForegroundColor DarkYellow

    if ($null -ne $Config) {
        $Config.LastAuthRefreshAt = Get-NowIso
        Save-UserConfig $Config
    }

    return $true
}

function Invoke-IdentitySetup {
    param($Config)

    Write-Section "Account identity setup"

    # This is a read-only authenticated request. We intentionally discover the
    # account before assigning an expected identity on a brand-new install.
    $info = Get-YtmAccountInfo

    Write-Host "Authenticated YouTube Music account:"
    Write-Host ("  Name:   {0}" -f [string]$info.AccountName)
    if ($info.ChannelHandle) {
        Write-Host ("  Handle: {0}" -f [string]$info.ChannelHandle)
    }
    if ($null -ne $script:BrowserAuth -and $script:BrowserAuth.PageId) {
        Write-Host "  Brand delegation: detected"
    }

    Write-Host ""
    Write-Warning "Writes will later be blocked unless the live account matches this saved identity."
    $answer = Read-Host "Type YES to save this as the expected target account"
    if ($answer -cne "YES") {
        throw "Account identity was not approved. Setup stopped without mutations."
    }

    $Config.ExpectedAccountName = [string]$info.AccountName
    $Config.ExpectedChannelHandle = [string]$info.ChannelHandle
    Save-UserConfig $Config

    $script:ConfiguredAccountName = [string]$Config.ExpectedAccountName
    $script:ConfiguredChannelHandle = [string]$Config.ExpectedChannelHandle

    Write-Host "Expected account identity saved." -ForegroundColor Green
    return $info
}

function Show-CluiMainMenu {
    Write-Section "Console menu"
    Write-Host "1  Quick read-only tidy (incremental/cache-aware)"
    Write-Host "2  Full read-only audit"
    Write-Host "3  Taste/category discovery (read-only, metadata-based)"
    Write-Host "4  Normalize managed playlist settings (write prompts still required)"
    Write-Host "5  First-time / reconfigure setup"
    Write-Host "6  Refresh browser authentication from clipboard"
    Write-Host "7  Check current authentication / target account"
    Write-Host "8  Show local cache + mutation status (no network)"
    Write-Host "9  Runtime controls help"
    Write-Host "Q  Quit"
    Write-Host ""
    if ($script:RuntimeControlAvailable) {
        Write-Host "While running: press P twice within 2 seconds to pause at the next safe point." -ForegroundColor DarkCyan
    }
    Write-Host ""

    while ($true) {
        $choice = (Read-Host "Choose").Trim().ToUpperInvariant()
        switch ($choice) {
            "1" {
                $script:CluiMode = "QUICK"
                return
            }
            "2" {
                $script:CluiMode = "FULL"
                return
            }
            "3" {
                $script:CluiMode = "TASTE"
                return
            }
            "4" {
                $script:CluiMode = "SETTINGS"
                return
            }
            "5" {
                $script:CluiMode = "SETUP"
                return
            }
            "6" {
                $script:CluiMode = "AUTH"
                return
            }
            "7" {
                Invoke-CluiAccountCheck
                Write-Host ""
                continue
            }
            "8" {
                Show-CluiLocalStatus
                Write-Host ""
                continue
            }
            "9" {
                Show-RuntimeControlHelp
                Write-Host ""
                continue
            }
            "Q" {
                $script:CluiMode = "QUIT"
                return
            }
            default {
                Write-Host "Enter 1-9 or Q." -ForegroundColor Yellow
            }
        }
    }
}

function Set-CluiOperationalMode {
    param([string]$Mode)

    switch ($Mode) {
        "QUICK" {
            $script:ReportOnly = [switch]$true
            $script:ReusePlaylistCache = [switch]$true
        }
        "FULL" {
            $script:ReportOnly = [switch]$true
            $script:FullScan = [switch]$true
        }
        "TASTE" {
            $script:ReportOnly = [switch]$true
            $script:FullScan = [switch]$true
            $script:SuggestTasteLanes = [switch]$true
        }
        "SETTINGS" {
            $script:ReusePlaylistCache = [switch]$true
            $script:NormalizeManagedPlaylistSettings = [switch]$true
        }
        "SETUP" {
            $script:ReportOnly = [switch]$true
            $script:FullScan = [switch]$true
            $script:SuggestTasteLanes = [switch]$true
            $script:SetupWizardActive = $true
            $script:ConfigureNonMusicAfterIndex = $true
            $script:OfferManagedPlaylistCreation = $true
        }
    }
}

function Invoke-NonMusicPlaylistConsoleSetup {
    param(
        $PlaylistIndex,
        $Config
    )

    Write-Section "Non-music playlist setup"
    Write-Host "These playlists stay visible in reports, but source-only items from them"
    Write-Host "will not be treated as Song-Library music candidates."
    Write-Host ""
    Write-Host "Enter playlist numbers separated by commas. Press ENTER for none."
    Write-Host ""

    $choices = @(
        @($PlaylistIndex.Playlists) |
        Where-Object {
            $_.PlaylistId -and
            -not (Test-IsDynamicPlaylist ([string]$_.PlaylistId) ([string]$_.Title))
        }
    )

    for ($i = 0; $i -lt $choices.Count; $i++) {
        Write-Host ("{0,3}. {1}" -f ($i + 1), [string]$choices[$i].Title)
    }

    Write-Host ""
    $rawChoice = (Read-Host "Non-music playlist numbers").Trim()

    $selectedIds = [System.Collections.Generic.List[string]]::new()
    if ($rawChoice) {
        foreach ($piece in ($rawChoice -split ',')) {
            $n = 0
            if (-not [int]::TryParse($piece.Trim(), [ref]$n)) {
                Write-Warning ("Ignoring invalid selection: {0}" -f $piece.Trim())
                continue
            }
            if ($n -lt 1 -or $n -gt $choices.Count) {
                Write-Warning ("Ignoring out-of-range selection: {0}" -f $n)
                continue
            }

            $id = [string]$choices[$n - 1].PlaylistId
            if ($id -and -not $selectedIds.Contains($id)) {
                $selectedIds.Add($id)
            }
        }
    }

    @(
        "# YouTube Music Tidy non-music/source-only exclusions."
        "# Generated by the v27 console setup wizard."
        foreach ($id in $selectedIds) { $id }
    ) | Set-Content -LiteralPath $NonMusicPlaylistConfigPath -Encoding UTF8

    $NonMusicPlaylistIdSet.Clear()
    foreach ($id in $selectedIds) {
        [void]$NonMusicPlaylistIdSet.Add($id)
    }
    foreach ($id in @($AdditionalNonMusicPlaylistIds)) {
        $candidate = ([string]$id).Trim()
        if ($candidate) {
            [void]$NonMusicPlaylistIdSet.Add($candidate)
        }
    }

    Write-Host ("Saved {0} non-music exclusion(s)." -f $selectedIds.Count) -ForegroundColor Green
}

function New-YtmPlaylist {
    param(
        [string]$Title,
        [string]$Description,
        [ValidateSet("PRIVATE","PUBLIC","UNLISTED")][string]$Privacy = "PRIVATE"
    )

    $response = Invoke-YtmRequest `
        -Endpoint "playlist/create" `
        -Body @{
            title = $Title
            description = $Description
            privacyStatus = $Privacy
        } `
        -ContextLabel ("creating managed playlist '{0}'" -f $Title)

    $playlistId = [string](Get-NestedValue $response @("playlistId"))
    if (-not $playlistId) {
        throw ("Playlist creation did not return a playlistId for '{0}'." -f $Title)
    }

    return $playlistId
}

function Invoke-ManagedPlaylistCreationWizard {
    param(
        $Suggestions,
        $Cache,
        $Config
    )

    $rows = @($Suggestions)
    if ($rows.Count -eq 0) {
        Write-Warning "No taste-lane suggestions were available to create."
        return @{}
    }

    Write-Section "Managed playlist creation"
    Write-Host "Suggested categories are metadata-based. No audio was analyzed." -ForegroundColor DarkYellow
    Write-Host ""
    for ($i = 0; $i -lt $rows.Count; $i++) {
        Write-Host ("{0,2}. {1,-24} -> {2}" -f `
            ($i + 1), [string]$rows[$i].Lane, [string]$rows[$i].SuggestedPlaylistTitle)
    }

    Write-Host ""
    $selection = (Read-Host "Enter numbers to create (comma-separated), A for all, or ENTER to skip").Trim()
    if (-not $selection) {
        Write-Host "Playlist creation skipped." -ForegroundColor Yellow
        return @{}
    }

    $selected = [System.Collections.Generic.List[object]]::new()
    if ($selection.ToUpperInvariant() -eq "A") {
        foreach ($row in $rows) { $selected.Add($row) }
    }
    else {
        foreach ($piece in ($selection -split ',')) {
            $n = 0
            if (-not [int]::TryParse($piece.Trim(), [ref]$n)) {
                Write-Warning ("Ignoring invalid selection: {0}" -f $piece.Trim())
                continue
            }
            if ($n -lt 1 -or $n -gt $rows.Count) {
                Write-Warning ("Ignoring out-of-range selection: {0}" -f $n)
                continue
            }
            $row = $rows[$n - 1]
            if (-not $selected.Contains($row)) {
                $selected.Add($row)
            }
        }
    }

    if ($selected.Count -eq 0) {
        Write-Host "No valid categories selected." -ForegroundColor Yellow
        return @{}
    }

    Write-Host ""
    Write-Host "Initial playlist visibility:"
    Write-Host "  1. PRIVATE (safest default)"
    Write-Host "  2. PUBLIC"
    Write-Host "  3. UNLISTED"
    $privacyChoice = (Read-Host "Choose 1-3 [1]").Trim()
    $privacy = "PRIVATE"
    if ($privacyChoice -eq "2") { $privacy = "PUBLIC" }
    elseif ($privacyChoice -eq "3") { $privacy = "UNLISTED" }

    Write-Host ""
    Write-Host ("Will create {0} empty managed playlist(s) as {1}:" -f $selected.Count, $privacy)
    foreach ($row in $selected) {
        Write-Host ("  - {0}" -f [string]$row.SuggestedPlaylistTitle)
    }

    $confirm = Read-Host "Type YES to create these playlists"
    if ($confirm -cne "YES") {
        Write-Host "Playlist creation cancelled." -ForegroundColor Yellow
        return @{}
    }

    if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "managed playlist creation")) {
        return @{}
    }

    $created = @{}
    foreach ($row in $selected) {
        $title = [string]$row.SuggestedPlaylistTitle
        $meaning = [string]$row.Meaning
        $description = @(
            ("organized by taste - {0}" -f $meaning.ToLowerInvariant()),
            "if there is a song that doesn't match the playlist category let me know!"
        ) -join "`n"

        $id = New-YtmPlaylist `
            -Title $title `
            -Description $description `
            -Privacy $privacy

        $created[$id] = $title
        Write-Host ("  CREATED: {0} ({1})" -f $title, $id) -ForegroundColor Green
    }

    if ($created.Count -gt 0) {
        $Cache.ManagedPlaylists = @(
            foreach ($id in $created.Keys) {
                [pscustomobject]@{
                    PlaylistId = [string]$id
                    Title = [string]$created[$id]
                }
            }
        )
        Save-TidyCache $Cache

        $Config.ManagedSetupCompleted = $true
        $Config.SetupCompletedAt = Get-NowIso
        Save-UserConfig $Config
    }

    return $created
}

function Write-Section([string]$Text) {
    Write-Host ""
    Write-Host "=== $Text ===" -ForegroundColor Cyan
}

function Get-BackoffSeconds([int]$Attempt) {
    $seconds = [Math]::Pow(2, [Math]::Max(0, $Attempt - 1)) * $RetryBaseSeconds
    return [Math]::Min([int]$seconds, 60)
}

function Get-NestedValue {
    param(
        $Node,
        [object[]]$Path
    )

    $current = $Node
    foreach ($part in $Path) {
        if ($null -eq $current) { return $null }

        if ($part -is [int]) {
            $arr = @($current)
            if ($part -lt 0 -or $part -ge $arr.Count) { return $null }
            $current = $arr[$part]
            continue
        }

        $prop = $current.PSObject.Properties[[string]$part]
        if ($null -eq $prop) { return $null }
        $current = $prop.Value
    }
    return $current
}

function Find-PropertyValues {
    param(
        $Node,
        [string]$PropertyName
    )

    $results = [System.Collections.Generic.List[object]]::new()

    function Visit-Node($Value) {
        if ($null -eq $Value) { return }

        if ($Value -is [string] -or $Value -is [ValueType]) {
            return
        }

        if ($Value -is [System.Collections.IDictionary]) {
            foreach ($key in $Value.Keys) {
                $child = $Value[$key]
                if ([string]$key -eq $PropertyName) {
                    $results.Add($child)
                }
                Visit-Node $child
            }
            return
        }

        if ($Value -is [System.Collections.IEnumerable] -and -not ($Value -is [pscustomobject])) {
            foreach ($child in $Value) {
                Visit-Node $child
            }
            return
        }

        foreach ($prop in $Value.PSObject.Properties) {
            if ($prop.Name -eq $PropertyName) {
                $results.Add($prop.Value)
            }
            Visit-Node $prop.Value
        }
    }

    Visit-Node $Node
    return $results.ToArray()
}

function Read-RawBrowserHeaders {
    if (-not (Test-Path -LiteralPath $HeadersPath)) {
        throw @"
Browser authentication file not found:
  $HeadersPath

Run the script with no arguments (or use -Setup) for the console setup wizard.

Manual fallback — Chrome / Edge:
  DevTools -> Network -> select an authenticated POST /browse request
  -> right-click -> Copy -> Copy as PowerShell
  -> paste the ENTIRE copied request into raw_headers.txt

Firefox:
  Copy Request Headers and paste those raw headers into raw_headers.txt.

IMPORTANT: raw_headers.txt contains live Google/YouTube session credentials.
Do not upload, share, or commit it.
"@
    }

    $raw = Get-Content -LiteralPath $HeadersPath -Raw -Encoding UTF8
    $headers = @{}
    $sourceFormat = "Raw request headers"

    # Keep only headers that are useful for authenticated YouTube Music calls.
    # In particular, ignore the copied Authorization value because we generate a
    # fresh SAPISIDHASH for every request.
    $allowedHeaders = [System.Collections.Generic.HashSet[string]]::new()
    @(
        "accept",
        "accept-language",
        "cookie",
        "origin",
        "referer",
        "user-agent",
        "x-client-data",
        "x-goog-authuser",
        "x-goog-pageid",
        "x-goog-visitor-id",
        "x-origin",
        "x-youtube-bootstrap-logged-in",
        "x-youtube-client-name",
        "x-youtube-client-version"
    ) | ForEach-Object { [void]$allowedHeaders.Add($_) }

    $looksLikeCopiedPowerShell = (
        $raw -match '\$session\.Cookies\.Add' -or
        $raw -match 'System\.Net\.Cookie\(' -or
        $raw -match 'Invoke-WebRequest'
    )

    if ($looksLikeCopiedPowerShell) {
        $sourceFormat = "Chrome/Edge Copy as PowerShell"

        # Parse cookie constructor calls as inert TEXT.
        # We deliberately DO NOT Invoke-Expression or execute raw_headers.txt.
        $cookiePairs = [System.Collections.Generic.List[string]]::new()
        $cookieRegex = [regex]'System\.Net\.Cookie\(\s*"(?<name>[^"]+)"\s*,\s*"(?<value>[^"]*)"'

        foreach ($match in $cookieRegex.Matches($raw)) {
            $name = $match.Groups["name"].Value
            $value = $match.Groups["value"].Value
            if ($name) {
                $cookiePairs.Add("$name=$value")
            }
        }

        if ($cookiePairs.Count -gt 0) {
            $headers["cookie"] = ($cookiePairs -join "; ")
        }

        # Parse the -Headers @{ "name"="value" } style lines.
        # Relevant YouTube auth headers do not contain escaped quotes, but do a
        # minimal PowerShell-string unescape anyway.
        $headerRegex = [regex]'(?m)^\s*"(?<name>[^"]+)"\s*=\s*"(?<value>[^"]*)"\s*$'
        foreach ($match in $headerRegex.Matches($raw)) {
            $name = $match.Groups["name"].Value.Trim().ToLowerInvariant()
            if (-not $allowedHeaders.Contains($name)) { continue }
            if ($name -eq "cookie") { continue }

            $value = $match.Groups["value"].Value
            $value = $value -replace '``', '`'
            $value = $value -replace '`"', '"'
            $headers[$name] = $value
        }

        # Chrome emits User-Agent separately on the WebRequestSession object.
        $uaMatch = [regex]::Match(
            $raw,
            '(?m)^\s*\$session\.UserAgent\s*=\s*"(?<value>[^"]*)"\s*$'
        )
        if ($uaMatch.Success) {
            $headers["user-agent"] = $uaMatch.Groups["value"].Value
        }
    }
    else {
        foreach ($line in ($raw -split "`r?`n")) {
            $trimmed = $line.Trim()
            if (-not $trimmed) { continue }
            if ($trimmed.StartsWith(":")) { continue }

            if ($trimmed -match '^([^:]+):\s*(.*)$') {
                $name = $matches[1].Trim().ToLowerInvariant()
                $value = $matches[2].Trim()

                if ($allowedHeaders.Contains($name)) {
                    $headers[$name] = $value
                }
            }
        }
    }

    if (-not $headers.ContainsKey("cookie") -or -not $headers["cookie"]) {
        throw @"
Could not find browser cookies in raw_headers.txt.

If using Chrome/Edge, save the ENTIRE "Copy as PowerShell" request, including the
`$session.Cookies.Add(...) lines.

If using Firefox/raw headers, make sure the Cookie: header is included.
"@
    }

    if (-not $headers.ContainsKey("x-goog-authuser")) {
        throw @"
raw_headers.txt is missing x-goog-authuser.

Use an authenticated music.youtube.com POST /browse request, not an anonymous or
static-file request.
"@
    }

    # Prefer the secure cookie used by current YouTube web clients, but accept
    # SAPISID as a fallback for older/different sessions.
    $sapisid = $null
    if ($headers["cookie"] -match '(?:^|;\s*)__Secure-3PAPISID=([^;]+)') {
        $sapisid = $matches[1]
    }
    elseif ($headers["cookie"] -match '(?:^|;\s*)SAPISID=([^;]+)') {
        $sapisid = $matches[1]
    }

    if (-not $sapisid) {
        throw @"
The copied session does not contain __Secure-3PAPISID or SAPISID.

Copy a fresh authenticated POST /browse request from music.youtube.com while
signed in to the intended YouTube/Brand Account.
"@
    }

    $origin = $YtmDomain
    if ($headers.ContainsKey("origin") -and $headers["origin"]) {
        $origin = $headers["origin"]
    }
    elseif ($headers.ContainsKey("x-origin") -and $headers["x-origin"]) {
        $origin = $headers["x-origin"]
    }

    $clientVersion = $YtmClientVersion
    if ($headers.ContainsKey("x-youtube-client-version") -and $headers["x-youtube-client-version"]) {
        $clientVersion = $headers["x-youtube-client-version"]
    }

    $pageId = $null
    if ($headers.ContainsKey("x-goog-pageid") -and $headers["x-goog-pageid"]) {
        $pageId = [string]$headers["x-goog-pageid"]
    }

    return [pscustomobject]@{
        Headers = $headers
        Sapisid = $sapisid
        Origin = $origin
        ClientVersion = $clientVersion
        SourceFormat = $sourceFormat
        PageId = $pageId
    }
}

function New-SapisidHash([string]$Sapisid, [string]$Origin) {
    $timestamp = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds().ToString()
    $text = "$timestamp $Sapisid $Origin"
    $sha1 = [System.Security.Cryptography.SHA1]::Create()
    try {
        $bytes = [Text.Encoding]::UTF8.GetBytes($text)
        $hash = $sha1.ComputeHash($bytes)
        $hex = -join ($hash | ForEach-Object { $_.ToString("x2") })
        return "SAPISIDHASH ${timestamp}_$hex"
    }
    finally {
        $sha1.Dispose()
    }
}

function New-YtmContext {
    $user = @{}

    # Brand-account browser sessions commonly expose the delegated channel/page
    # ID in X-Goog-PageId. Match YouTube/ytmusicapi behavior by also putting it
    # into context.user.onBehalfOfUser.
    if ($null -ne $script:BrowserAuth -and $script:BrowserAuth.PageId) {
        $user["onBehalfOfUser"] = [string]$script:BrowserAuth.PageId
    }

    $clientVersion = $YtmClientVersion
    if ($null -ne $script:BrowserAuth -and $script:BrowserAuth.ClientVersion) {
        $clientVersion = [string]$script:BrowserAuth.ClientVersion
    }

    return @{
        context = @{
            client = @{
                clientName = $YtmClientName
                clientVersion = $clientVersion
                hl = "en"
                gl = "ID"
            }
            user = $user
        }
    }
}

function Get-HttpErrorInfo {
    param($ErrorRecord)

    $status = $null
    $body = $null
    $message = $ErrorRecord.Exception.Message

    try {
        if ($ErrorRecord.Exception.Response) {
            try { $status = [int]$ErrorRecord.Exception.Response.StatusCode } catch {}
            try {
                $stream = $ErrorRecord.Exception.Response.GetResponseStream()
                if ($stream) {
                    $reader = New-Object System.IO.StreamReader($stream)
                    $body = $reader.ReadToEnd()
                    $reader.Dispose()
                }
            } catch {}
        }
    } catch {}

    if (-not $body) {
        try { $body = $ErrorRecord.ErrorDetails.Message } catch {}
    }

    # Windows PowerShell 5.1 sometimes does not expose StatusCode through the
    # exception object even though the message contains "(404) Not Found".
    if ($null -eq $status -and $message -match '\((\d{3})\)') {
        $status = [int]$matches[1]
    }

    return [pscustomobject]@{
        StatusCode = $status
        Body = $body
        Message = $message
    }
}

$BrowserAuth = $null
$BrowserSession = $null

function New-YtmWebSession {
    if ($null -eq $script:BrowserAuth) {
        $script:BrowserAuth = Read-RawBrowserHeaders
    }

    $session = New-Object Microsoft.PowerShell.Commands.WebRequestSession

    if ($script:BrowserAuth.Headers.ContainsKey("user-agent") -and $script:BrowserAuth.Headers["user-agent"]) {
        $session.UserAgent = [string]$script:BrowserAuth.Headers["user-agent"]
    }
    else {
        $session.UserAgent = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/140 Safari/537.36"
    }

    $rawCookie = [string]$script:BrowserAuth.Headers["cookie"]
    foreach ($piece in ($rawCookie -split ';')) {
        $piece = $piece.Trim()
        if (-not $piece) { continue }

        $eq = $piece.IndexOf('=')
        if ($eq -le 0) { continue }

        $name = $piece.Substring(0, $eq).Trim()
        $value = $piece.Substring($eq + 1)

        try {
            $cookie = New-Object System.Net.Cookie($name, $value, "/", ".youtube.com")
            $session.Cookies.Add($cookie)
        }
        catch {
            throw "Failed to load browser cookie '$name' into the PowerShell web session."
        }
    }

    # ytmusicapi itself supplies SOCS=CAI. Add it if the copied browser request
    # didn't include it.
    try {
        $soc = $session.Cookies.GetCookies([Uri]$YtmDomain)["SOCS"]
        if ($null -eq $soc) {
            $session.Cookies.Add((New-Object System.Net.Cookie("SOCS", "CAI", "/", ".youtube.com")))
        }
    } catch {}

    return $session
}

function Invoke-YtmRequest {
    param(
        [Parameter(Mandatory=$true)][string]$Endpoint,
        [Parameter(Mandatory=$true)]$Body,
        [string]$ContextLabel = "YouTube Music request",
        [string]$AdditionalQuery = ""
    )

    if ($null -eq $script:BrowserAuth) {
        $script:BrowserAuth = Read-RawBrowserHeaders
    }
    if ($null -eq $script:BrowserSession) {
        $script:BrowserSession = New-YtmWebSession
    }

    $attempt = 0
    $networkOutageStarted = $null

    while ($true) {
        Invoke-RuntimeControlCheckpoint -Phase ("before HTTP {0}: {1}" -f `
            $Endpoint, $ContextLabel)

        $attempt++
        $script:RequestCount++
        $requestNo = $script:RequestCount

        $headers = @{}
        foreach ($key in $script:BrowserAuth.Headers.Keys) {
            if ($key -in @("cookie", "user-agent")) { continue }
            $headers[$key] = $script:BrowserAuth.Headers[$key]
        }

        $headers["authorization"] = New-SapisidHash $script:BrowserAuth.Sapisid $script:BrowserAuth.Origin
        $headers["origin"] = $script:BrowserAuth.Origin
        $headers["x-origin"] = $script:BrowserAuth.Origin
        $headers["accept"] = "*/*"

        $payload = @{}
        foreach ($prop in $Body.PSObject.Properties) {
            $payload[$prop.Name] = $prop.Value
        }
        if ($Body -is [System.Collections.IDictionary]) {
            foreach ($key in $Body.Keys) {
                $payload[$key] = $Body[$key]
            }
        }

        $ctx = New-YtmContext
        $payload["context"] = $ctx.context

        $url = "{0}/youtubei/v1/{1}?alt=json&key={2}" -f $YtmDomain, $Endpoint, $YtmApiKey
        if ($AdditionalQuery) {
            if ($AdditionalQuery.StartsWith("&")) {
                $url += $AdditionalQuery
            }
            else {
                $url += ("&" + $AdditionalQuery.TrimStart("?"))
            }
        }

        $json = $payload | ConvertTo-Json -Depth 100 -Compress
        $bytes = [Text.Encoding]::UTF8.GetBytes($json)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()

        if ($TraceRequests) {
            Write-ProgressLine ("HTTP #{0} -> {1} | attempt {2} | {3}" -f `
                $requestNo, $Endpoint, $attempt, $ContextLabel) DarkGray
        }

        try {
            $result = Invoke-RestMethod `
                -Method Post `
                -Uri $url `
                -WebSession $script:BrowserSession `
                -Headers $headers `
                -ContentType "application/json; charset=utf-8" `
                -Body $bytes `
                -TimeoutSec 45

            $sw.Stop()

            if ($null -ne $networkOutageStarted) {
                Write-ProgressLine ("Network recovered after {0:N1}s." -f `
                    ((Get-Date) - $networkOutageStarted).TotalSeconds) Green
                $networkOutageStarted = $null
            }

            if ($TraceRequests) {
                Write-ProgressLine ("HTTP #{0} <- OK in {1:N2}s | {2}" -f `
                    $requestNo, $sw.Elapsed.TotalSeconds, $ContextLabel) DarkGreen
            }
            return $result
        }
        catch {
            $sw.Stop()
            $info = Get-HttpErrorInfo $_
            $status = $info.StatusCode
            $combined = (($info.Message, $info.Body) -join " ").Trim()

            $isNetworkOutage = (
                $null -eq $status -and
                $combined -match '(?i)(remote name could not be resolved|could not resolve|name resolution|temporary failure in name resolution|no such host|nodename nor servname|dns|network is unreachable|host is down|unable to connect|connection.*(failed|closed|reset|aborted)|connection.*timed out|remote server|transport connection)'
            )

            $retryableHttp = (
                $status -in @(408, 429, 500, 502, 503, 504) -or
                ($null -eq $status -and $combined -match '(?i)(temporar|timeout|timed out|connection|remote server|backend)') -or
                $combined -match '(?i)rate.?limit'
            )

            if ($TraceRequests) {
                Write-ProgressLine ("HTTP #{0} <- FAIL in {1:N2}s status={2} | {3}" -f `
                    $requestNo, $sw.Elapsed.TotalSeconds, $status, $ContextLabel) DarkYellow
            }

            if ($status -in @(401, 403)) {
                throw @"
YouTube Music rejected the browser session during $ContextLabel.
HTTP $status
$combined

Your raw browser headers/cookies may have expired, the wrong Google/Brand account
may be active, or YouTube may be throttling the session. No further writes were attempted.
Copy a fresh authenticated music.youtube.com POST /browse request (Chrome/Edge: Copy as PowerShell) into raw_headers.txt and verify again.
"@
            }

            if ($isNetworkOutage -and $NetworkOutageRetryMinutes -gt 0) {
                if ($null -eq $networkOutageStarted) {
                    $networkOutageStarted = Get-Date
                }

                $outageElapsed = ((Get-Date) - $networkOutageStarted).TotalMinutes

                if ($outageElapsed -lt $NetworkOutageRetryMinutes) {
                    # Network outages use their own retry budget. Cap the wait so
                    # recovery is noticed reasonably quickly.
                    $delay = [Math]::Min(30, [Math]::Max(3, (Get-BackoffSeconds ([Math]::Min($attempt, 5)))))
                    Write-Warning ((
                        "Network/DNS outage during {0}. {1} " +
                        "Waiting {2}s and retrying the SAME request; outage budget {3} min."
                    ) -f $ContextLabel, $combined, $delay, $NetworkOutageRetryMinutes)
                    Start-PausableSleep -Seconds $delay -Phase ("network/retry wait: {0}" -f $ContextLabel)
                    continue
                }

                throw @"
Network/DNS outage persisted for $NetworkOutageRetryMinutes minute(s) during $ContextLabel.
$combined

Completed sections/playlists already checkpointed to disk remain reusable.
Re-run WITHOUT -FullScan to resume from the cache.
"@
            }

            if ($retryableHttp -and $attempt -le $MaxRetries) {
                $delay = Get-BackoffSeconds $attempt
                Write-Warning "Temporary failure during $ContextLabel (attempt $attempt/$($MaxRetries + 1), request #$requestNo). Retrying in $delay second(s)..."
                Start-PausableSleep -Seconds $delay -Phase ("network/retry wait: {0}" -f $ContextLabel)
                continue
            }

            throw @"
YouTube Music request failed during $ContextLabel.
$combined

The script stopped rather than continuing to hammer the service.
Completed sections/playlists already checkpointed to disk remain reusable.
Re-run WITHOUT -FullScan to resume from the cache.
"@
        }
    }
}

function Get-YtmAccountInfo {
    $response = Invoke-YtmRequest -Endpoint "account/account_menu" -Body @{} -ContextLabel "account verification"

    $base = @(
        "actions", 0,
        "openPopupAction",
        "popup",
        "multiPageMenuRenderer",
        "header",
        "activeAccountHeaderRenderer"
    )

    $name = Get-NestedValue $response ($base + @("accountName", "runs", 0, "text"))
    $handle = Get-NestedValue $response ($base + @("channelHandle", "runs", 0, "text"))

    if (-not $name) {
        throw "Could not read the authenticated account name from YouTube Music. Refresh raw_headers.txt and try again."
    }

    return [pscustomobject]@{
        AccountName = [string]$name
        ChannelHandle = [string]$handle
    }
}

function Confirm-YtmAccount {
    $info = Get-YtmAccountInfo

    Write-Section "Verifying authenticated YouTube Music account"
    Write-Host "Authenticated account:"
    Write-Host "  Name:   $($info.AccountName)"
    if ($info.ChannelHandle) {
        Write-Host "  Handle: $($info.ChannelHandle)"
    }
    if ($null -ne $script:BrowserAuth) {
        Write-Host "  Auth source: $($script:BrowserAuth.SourceFormat)"
        Write-Host "  Client ver.: $($script:BrowserAuth.ClientVersion)"
        if ($script:BrowserAuth.PageId) {
            Write-Host "  Brand delegation: detected"
        }
    }

    Write-Host ""
    Write-Host "Expected target:"
    Write-Host "  Name:   $ExpectedAccountName"
    if ($ExpectedChannelHandle) {
        Write-Host "  Handle: $ExpectedChannelHandle"
    }

    if ($info.AccountName -cne $ExpectedAccountName) {
        throw @"
ACCOUNT NAME MISMATCH.
Expected '$ExpectedAccountName' but YouTube Music reports '$($info.AccountName)'.
No playlist writes were attempted.
"@
    }

    if ($ExpectedChannelHandle -and $info.ChannelHandle -cne $ExpectedChannelHandle) {
        throw @"
CHANNEL HANDLE MISMATCH.
Expected '$ExpectedChannelHandle' but YouTube Music reports '$($info.ChannelHandle)'.
No playlist writes were attempted.
"@
    }

    Write-Host ""
    Write-Host "Account MATCHED. Target is safe." -ForegroundColor Green
    return $info
}



function Confirm-YtmAccountWithAuthRecovery {
    param(
        [string]$WriteContext = "pending write"
    )

    $refreshCount = 0

    while ($true) {
        try {
            [void](Confirm-YtmAccount)

            if ($refreshCount -gt 0) {
                Write-Host ""
                Write-Host "Authentication recovered and the expected account matches." -ForegroundColor Green
                Write-Host ("Pending operation: {0}" -f $WriteContext)
                $continueAnswer = Read-Host "Type YES to continue the already-confirmed write without rerunning its completed preflight"
                if ($continueAnswer -cne "YES") {
                    Write-Host "Pending write cancelled. No mutation request was sent after auth recovery." -ForegroundColor Yellow
                    return $false
                }
            }

            return $true
        }
        catch {
            $message = [string]$_.Exception.Message
            $authRejected = (
                $message -match '(?i)(rejected the browser session|HTTP\s+(401|403)|UNAUTHENTICATED|missing required authentication credential)'
            )

            if (-not $authRejected -or $NoInteractive.IsPresent) {
                throw
            }

            Write-Host ""
            Write-Warning ("Browser authentication expired during final account verification for: {0}" -f $WriteContext)
            Write-Host "No mutation request from this write block has been sent yet." -ForegroundColor Green
            Write-Host "The completed scan/preflight stays in memory and will NOT be repeated." -ForegroundColor Cyan
            Write-Host ""
            Write-Host "Open/keep YouTube Music on the intended account, copy a successful POST /browse"
            Write-Host "as PowerShell, then return to this same console."

            $choice = (Read-Host "Type R to refresh auth from the clipboard and retry, or Q to cancel").Trim().ToUpperInvariant()
            if ($choice -eq "Q") {
                Write-Host "Pending write cancelled safely." -ForegroundColor Yellow
                return $false
            }
            if ($choice -ne "R") {
                Write-Host "Enter R or Q." -ForegroundColor Yellow
                continue
            }

            try {
                $refreshed = Invoke-ClipboardBrowserAuthSetup `
                    -Config $script:UserConfig `
                    -AllowOverwrite `
                    -Compact

                if (-not $refreshed) {
                    Write-Warning "Authentication refresh was not completed. The pending write remains paused."
                    continue
                }

                $refreshCount++
                continue
            }
            catch {
                Write-Warning ("Authentication refresh failed: {0}" -f $_.Exception.Message)
                Write-Host "The pending write remains paused; no mutation request was sent." -ForegroundColor Yellow
                continue
            }
        }
    }
}
function Load-MigrationState {
    if (-not (Test-Path -LiteralPath $StatePath)) {
        return $null
    }

    try {
        return Get-Content -LiteralPath $StatePath -Raw -Encoding UTF8 | ConvertFrom-Json
    }
    catch {
        Write-Warning (("Could not parse optional legacy migration state '{0}'. " +
            "v27 will try cache/playlist-name discovery instead.") -f $StatePath)
        return $null
    }
}

function Get-PlaylistIdFromState {
    param(
        $State,
        [string]$Title
    )

    $match = @($State.playlists | Where-Object { $_.title -eq $Title } | Select-Object -First 1)
    if ($match.Count -eq 0 -or -not $match[0].id) {
        return $null
    }

    return [string]$match[0].id
}

function Get-DestinationTitles {
    param(
        $Rows,
        [bool]$WithOverlays
    )

    $set = [System.Collections.Generic.HashSet[string]]::new()

    foreach ($row in $Rows) {
        if ($row.Unavailable -eq "YES") { continue }

        if ($row.'Primary Playlist' -and $row.'Primary Playlist' -ne "09 · Inbox / Review") {
            [void]$set.Add([string]$row.'Primary Playlist')
        }

        if ($WithOverlays) {
            if ($row.'JP Overlay') {
                [void]$set.Add([string]$row.'JP Overlay')
            }

            if ($row.'Other Overlays') {
                foreach ($overlay in ($row.'Other Overlays' -split ';')) {
                    $overlay = $overlay.Trim()
                    if ($overlay) {
                        [void]$set.Add($overlay)
                    }
                }
            }
        }
    }

    return @($set) | Sort-Object
}

function Get-DestinationsForRow {
    param(
        $Row,
        [bool]$WithOverlays
    )

    $destinations = @()

    if ($Row.'Primary Playlist' -and $Row.'Primary Playlist' -ne "09 · Inbox / Review") {
        $destinations += [string]$Row.'Primary Playlist'
    }

    if ($WithOverlays) {
        if ($Row.'JP Overlay') {
            $destinations += [string]$Row.'JP Overlay'
        }

        if ($Row.'Other Overlays') {
            $destinations += @(
                $Row.'Other Overlays' -split ';' |
                ForEach-Object { $_.Trim() } |
                Where-Object { $_ }
            )
        }
    }

    return @($destinations | Select-Object -Unique)
}

function Get-ContinuationToken {
    param($Contents)

    $items = @($Contents)
    if ($items.Count -eq 0) { return $null }

    for ($i = $items.Count - 1; $i -ge 0; $i--) {
        $item = $items[$i]

        $token = Get-NestedValue $item @(
            "continuationItemRenderer",
            "continuationEndpoint",
            "continuationCommand",
            "token"
        )
        if ($token) { return [string]$token }

        $commands = Get-NestedValue $item @(
            "continuationItemRenderer",
            "continuationEndpoint",
            "commandExecutorCommand",
            "commands"
        )

        foreach ($command in @($commands)) {
            $requestType = Get-NestedValue $command @("continuationCommand", "request")
            $candidate = Get-NestedValue $command @("continuationCommand", "token")
            if ($candidate -and (-not $requestType -or $requestType -eq "CONTINUATION_REQUEST_TYPE_BROWSE")) {
                return [string]$candidate
            }
        }
    }

    return $null
}


function Get-ContinuationTokenDeep {
    param($Node)

    $direct = Get-ContinuationToken $Node
    if ($direct) { return [string]$direct }

    $renderers = @(Find-PropertyValues $Node "continuationItemRenderer")
    for ($i = $renderers.Count - 1; $i -ge 0; $i--) {
        $renderer = $renderers[$i]

        $token = Get-NestedValue $renderer @(
            "continuationEndpoint",
            "continuationCommand",
            "token"
        )
        if ($token) { return [string]$token }

        $commands = Get-NestedValue $renderer @(
            "continuationEndpoint",
            "commandExecutorCommand",
            "commands"
        )
        foreach ($command in @($commands)) {
            $requestType = Get-NestedValue $command @("continuationCommand", "request")
            $candidate = Get-NestedValue $command @("continuationCommand", "token")
            if ($candidate -and (-not $requestType -or $requestType -eq "CONTINUATION_REQUEST_TYPE_BROWSE")) {
                return [string]$candidate
            }
        }
    }

    return $null
}

function Get-LegacyContinuationToken {
    param($Node)

    # Older library/grid continuations use:
    # continuations[0].nextContinuationData.continuation
    $nextData = @(Find-PropertyValues $Node "nextContinuationData")
    foreach ($entry in $nextData) {
        $token = Get-NestedValue $entry @("continuation")
        if ($token) { return [string]$token }
    }

    # Some responses use nextRadioContinuationData.
    $radioData = @(Find-PropertyValues $Node "nextRadioContinuationData")
    foreach ($entry in $radioData) {
        $token = Get-NestedValue $entry @("continuation")
        if ($token) { return [string]$token }
    }

    return $null
}

function Get-LegacyContinuationQuery {
    param([string]$Token)
    if (-not $Token) { return "" }

    # Mirrors ytmusicapi get_continuation_string:
    # &ctoken=<token>&continuation=<token>
    return ("&ctoken={0}&continuation={0}" -f $Token)
}

function Get-VideoIdFromPlaylistItem {
    param($Item)

    $renderers = @(Find-PropertyValues $Item "musicResponsiveListItemRenderer")
    foreach ($renderer in $renderers) {
        $removed = @(
            Find-PropertyValues $renderer "removedVideoId" |
            Where-Object { $_ } |
            Select-Object -First 1
        )
        if ($removed.Count -gt 0) {
            return [string]$removed[0]
        }

        $videoIds = @(
            Find-PropertyValues $renderer "videoId" |
            Where-Object { $_ } |
            Select-Object -First 1
        )
        if ($videoIds.Count -gt 0) {
            return [string]$videoIds[0]
        }
    }

    return $null
}

function Add-VideoIdsFromContents {
    param(
        $Contents,
        [System.Collections.Generic.HashSet[string]]$Target
    )

    foreach ($item in @($Contents)) {
        $videoId = Get-VideoIdFromPlaylistItem $item
        if ($videoId) {
            [void]$Target.Add($videoId)
        }
    }
}

function Get-YtmPlaylistVideoIds {
    param(
        [string]$PlaylistId,
        [string]$Title
    )

    $ids = [System.Collections.Generic.HashSet[string]]::new()

    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = "VL$PlaylistId" } `
        -ContextLabel "reading playlist '$Title'"

    # Current ytmusicapi (2026) navigates owned playlists through:
    # contents.twoColumnBrowseResultsRenderer.secondaryContents
    #   .sectionListRenderer.contents[0].musicPlaylistShelfRenderer
    #
    # Use the exact current path first. Keep fallbacks for older/single-column
    # layouts and for future minor renderer reshuffles.
    $shelf = Get-NestedValue $response @(
        "contents",
        "twoColumnBrowseResultsRenderer",
        "secondaryContents",
        "sectionListRenderer",
        "contents",
        0,
        "musicPlaylistShelfRenderer"
    )

    if ($null -eq $shelf) {
        $shelf = Get-NestedValue $response @(
            "contents",
            "singleColumnBrowseResultsRenderer",
            "tabs",
            0,
            "tabRenderer",
            "content",
            "sectionListRenderer",
            "contents",
            0,
            "musicPlaylistShelfRenderer"
        )
    }

    if ($null -eq $shelf) {
        $shelves = @(Find-PropertyValues $response "musicPlaylistShelfRenderer")
        foreach ($candidate in $shelves) {
            $contentsCandidate = Get-NestedValue $candidate @("contents")
            if ($contentsCandidate) {
                $shelf = $candidate
                break
            }
        }
    }

    if ($null -eq $shelf) {
        # Save only the returned YouTube Music JSON, never request headers or
        # cookies. This makes a future renderer change diagnosable locally.
        $debugPath = Join-Path $PSScriptRoot "ytm_last_playlist_response.json"
        try {
            $response | ConvertTo-Json -Depth 100 | Set-Content -LiteralPath $debugPath -Encoding UTF8
        } catch {}

        throw @"
Could not locate the playlist track shelf for '$Title' ($PlaylistId).

The authenticated request succeeded, but the response layout did not match the
known YouTube Music playlist layouts.

A local diagnostic response was saved to:
  $debugPath

It contains the returned playlist-page JSON, NOT your raw_headers.txt cookies.
Do not delete or change the destination playlists; no write was attempted.
"@
    }

    $contents = @(Get-NestedValue $shelf @("contents"))
    Add-VideoIdsFromContents $contents $ids
    $token = Get-ContinuationToken $contents

    $pages = 1
    while ($token) {
        $pages++
        if ($pages -gt 100) {
            throw "Stopped after 100 continuation pages while reading '$Title' to avoid an accidental infinite loop."
        }

        $next = Invoke-YtmRequest `
            -Endpoint "browse" `
            -Body @{ continuation = $token } `
            -ContextLabel "reading continuation for '$Title'"

        # Current continuation layout used by ytmusicapi's get_continuations_2025:
        # onResponseReceivedActions[0].appendContinuationItemsAction.continuationItems
        $continuationItems = Get-NestedValue $next @(
            "onResponseReceivedActions",
            0,
            "appendContinuationItemsAction",
            "continuationItems"
        )

        if ($null -eq $continuationItems) {
            # Fallback for response variants.
            $appendActions = @(Find-PropertyValues $next "appendContinuationItemsAction")
            foreach ($action in $appendActions) {
                $candidate = Get-NestedValue $action @("continuationItems")
                if ($candidate) {
                    $continuationItems = @($candidate)
                    break
                }
            }
        }

        if ($null -eq $continuationItems) {
            # Older continuation response layout.
            $oldStyle = @(Find-PropertyValues $next "musicPlaylistShelfContinuation")
            foreach ($candidate in $oldStyle) {
                $candidateContents = Get-NestedValue $candidate @("contents")
                if ($candidateContents) {
                    $continuationItems = @($candidateContents)
                    break
                }
            }
        }

        if ($null -eq $continuationItems -or @($continuationItems).Count -eq 0) {
            break
        }

        $continuationItems = @($continuationItems)
        Add-VideoIdsFromContents $continuationItems $ids
        $token = Get-ContinuationToken $continuationItems
    }

    return ,$ids
}

function Invoke-AddBatch {
    param(
        [string]$PlaylistId,
        [string]$Title,
        [string[]]$VideoIds
    )

    $actions = @()
    foreach ($videoId in $VideoIds) {
        $actions += @{
            action = "ACTION_ADD_VIDEO"
            addedVideoId = $videoId
        }
    }

    $response = Invoke-YtmRequest `
        -Endpoint "browse/edit_playlist" `
        -Body @{
            playlistId = $PlaylistId
            actions = $actions
        } `
        -ContextLabel "adding tracks to '$Title'"

    $status = [string](Get-NestedValue $response @("status"))
    $success = $status -match "SUCCEEDED"

    return [pscustomobject]@{
        Success = $success
        Status = $status
        Response = $response
    }
}

$ErrorActionPreference = "Stop"

# Keep regenerable CSV output out of the active runtime root.
foreach ($outputPath in @(
    $ReportPath,
    $ActionsPath,
    $PlaylistSettingsReportPath,
    $TasteLaneSuggestionsPath
)) {
    $parent = Split-Path -Parent $outputPath
    if ($parent -and -not (Test-Path -LiteralPath $parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
}

# Filled after the playlist index is available. v21 accepts arbitrary
# numbered user categories instead of hardcoding this installation's 01-08 list.
$DestinationTitles = @()

# Protect legacy source playlists by title. IDs are intentionally not hardcoded
# so this file can be shared without carrying a specific account's playlist IDs.
$ProtectedPlaylistIds = @()
$ProtectedPlaylistTitles = @("Main", "Other")

# Keep account-specific non-music exclusions in the optional text config file
# (one playlist ID per line) rather than embedding them in the shareable script.
$BuiltInNonMusicPlaylistIds = @()

$NonMusicPlaylistIdSet = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::Ordinal
)

foreach ($playlistIdKey in @($BuiltInNonMusicPlaylistIds + $AdditionalNonMusicPlaylistIds)) {
    $candidate = ([string]$playlistIdKey).Trim()
    if ($candidate) {
        [void]$NonMusicPlaylistIdSet.Add($candidate)
    }
}

if (Test-Path -LiteralPath $NonMusicPlaylistConfigPath) {
    foreach ($line in @(Get-Content -LiteralPath $NonMusicPlaylistConfigPath -ErrorAction Stop)) {
        $candidate = ([string]$line).Trim()
        if (-not $candidate) { continue }
        if ($candidate.StartsWith("#")) { continue }

        # Permit a trailing human comment: PLxxxx # description
        $hashIndex = $candidate.IndexOf("#")
        if ($hashIndex -ge 0) {
            $candidate = $candidate.Substring(0, $hashIndex).Trim()
        }

        if ($candidate) {
            [void]$NonMusicPlaylistIdSet.Add($candidate)
        }
    }
}

$ScannerRevision = 6
$TrackMetadataRevision = 1

$LibraryResolverRevision = 5
$LibraryTokenRevision = 2

# Current ytmusicapi search filter params (Sep 2026):
# public songs filter and library-scoped songs filter.
$SearchSongsParams = "EgWKAQIIAWoMEA4QChADEAQQCRAF"
$SearchVideosParams = "EgWKAQIQAWoMEA4QChADEAQQCRAF"
$SearchLibrarySongsParams = "EgWKAQIIAWoKEAUQCRADEAoYBA%3D%3D"
$script:RequestCount = 0
$script:RunStopwatch = [System.Diagnostics.Stopwatch]::StartNew()

function Format-Elapsed {
    param([TimeSpan]$Elapsed)

    $hours = [int][Math]::Floor($Elapsed.TotalHours)
    if ($hours -gt 0) {
        return ("{0:00}:{1:00}:{2:00}" -f $hours, $Elapsed.Minutes, $Elapsed.Seconds)
    }

    return ("{0:00}:{1:00}" -f $Elapsed.Minutes, $Elapsed.Seconds)
}

function Write-ProgressLine {
    param(
        [string]$Text,
        [ConsoleColor]$Color = [ConsoleColor]::Gray
    )
    $elapsed = Format-Elapsed $script:RunStopwatch.Elapsed
    Write-Host ("[{0}] {1}" -f $elapsed, $Text) -ForegroundColor $Color
}

# Current ytmusicapi prepare_order_params("recently_added")
$LibraryRecentlyAddedParams = "ggMGKgQIABAB"

function Get-NowIso {
    return (Get-Date).ToUniversalTime().ToString("o")
}



function Show-RuntimeControlHelp {
    Write-Section "Runtime controls"

    if ($script:RuntimeControlAvailable) {
        Write-Host "PP  Pause at the next safe point (press P twice within 2 seconds)." -ForegroundColor Cyan
        Write-Host "    The first P arms the pause; the second P confirms it."
    }
    else {
        Write-Host "PP pause is unavailable in this host/input mode." -ForegroundColor Yellow
        Write-Host "It requires an interactive ConsoleHost-compatible keyboard."
    }

    Write-Host ""
    Write-Host "The pause is cooperative, not a thread suspension:"
    Write-Host "  - if an HTTP request is currently in flight, it finishes/fails first"
    Write-Host "  - pause is then honored before the next request / during retry waits"
    Write-Host "  - mutating responses are checkpointed by their caller before the"
    Write-Host "    next pause-safe point"
    Write-Host ""
    Write-Host "Paused menu:"
    Write-Host "  R  Resume"
    Write-Host "  S  Show runtime status"
    Write-Host "  V  Verify current authentication / target account"
    Write-Host "  A  Refresh browser authentication from clipboard, then verify"
    Write-Host "  H  Show this help"
    Write-Host ""
    Write-Host "Ctrl+C remains the emergency stop. PP is preferable when you just need"
    Write-Host "to wait out bad connectivity without throwing away in-memory progress."
    Write-Host ""
    Write-Host "Do not type YES/other prompt answers early while a job is running;"
    Write-Host "single-key runtime control polling may consume queued keystrokes."
}

function Show-RuntimeStatus {
    $elapsed = (Get-Date) - $script:RuntimeStartedAt

    Write-Host ""
    Write-Host "Runtime status:" -ForegroundColor Cyan
    Write-Host ("  Elapsed:       {0:hh\\:mm\\:ss}" -f $elapsed)
    Write-Host ("  Phase:         {0}" -f [string]$script:RuntimePhase)
    Write-Host ("  HTTP requests: {0}" -f [int]$script:RequestCount)
    Write-Host ("  Pauses:        {0}" -f [int]$script:RuntimePauseCount)
    if ($ExpectedAccountName) {
        Write-Host ("  Target:        {0} {1}" -f `
            [string]$ExpectedAccountName, [string]$ExpectedChannelHandle)
    }
    Write-Host ("  Outage budget: {0} min" -f [int]$NetworkOutageRetryMinutes)
    Write-Host ("  Cache:         {0}" -f $CachePath)
}

function Test-CurrentAccountForUtility {
    if ($ExpectedAccountName) {
        [void](Confirm-YtmAccount)
        return
    }

    $info = Get-YtmAccountInfo
    Write-Host ""
    Write-Host ("Authenticated as: {0} {1}" -f `
        [string]$info.AccountName, [string]$info.ChannelHandle) -ForegroundColor Green
    Write-Host "No expected target identity has been saved yet." -ForegroundColor Yellow
}

function Invoke-RuntimePauseMenu {
    param([string]$Phase = "")

    if ($script:RuntimePauseMenuActive) {
        return
    }

    $script:RuntimePauseMenuActive = $true
    $script:RuntimePauseCount++

    try {
        Write-Host ""
        Write-Host "=== PAUSED ===" -ForegroundColor Yellow
        if ($Phase) {
            Write-Host ("Safe point: {0}" -f $Phase)
        }
        else {
            Write-Host ("Safe point: {0}" -f [string]$script:RuntimePhase)
        }
        Write-Host "No new HTTP request will be started while this menu is open." -ForegroundColor DarkYellow
        Write-Host ""
        Write-Host "R  Resume"
        Write-Host "S  Runtime status"
        Write-Host "V  Verify auth/account"
        Write-Host "A  Refresh browser auth from clipboard + verify"
        Write-Host "H  Help"
        Write-Host ""

        while ($true) {
            $choice = (Read-Host "Paused").Trim().ToUpperInvariant()

            switch ($choice) {
                "R" {
                    Write-Host "Resuming..." -ForegroundColor Green
                    return
                }

                "S" {
                    Show-RuntimeStatus
                }

                "V" {
                    try {
                        Test-CurrentAccountForUtility
                        Write-Host "Authentication/account verification passed; still paused." -ForegroundColor Green
                    }
                    catch {
                        Write-Warning $_.Exception.Message
                        Write-Host "Still paused." -ForegroundColor Yellow
                    }
                }

                "A" {
                    try {
                        $refreshed = Invoke-ClipboardBrowserAuthSetup `
                            -Config $script:UserConfig `
                            -AllowOverwrite `
                            -Compact

                        if ($refreshed) {
                            Test-CurrentAccountForUtility
                            Write-Host "Authentication refreshed and verified; still paused." -ForegroundColor Green
                        }
                        else {
                            Write-Host "Authentication was not changed; still paused." -ForegroundColor Yellow
                        }
                    }
                    catch {
                        Write-Warning $_.Exception.Message
                        Write-Host "Refresh/verification failed; still paused." -ForegroundColor Yellow
                    }
                }

                "H" {
                    Show-RuntimeControlHelp
                }

                default {
                    Write-Host "Enter R, S, V, A, or H." -ForegroundColor Yellow
                }
            }
        }
    }
    finally {
        $script:RuntimePauseMenuActive = $false
    }
}

function Invoke-RuntimeControlCheckpoint {
    param([string]$Phase = "")

    if ($Phase) {
        $script:RuntimePhase = $Phase
    }

    if (
        -not $script:RuntimeControlAvailable -or
        $script:RuntimePauseMenuActive -or
        $NoInteractive.IsPresent
    ) {
        return
    }

    try {
        if (-not [Console]::KeyAvailable) {
            return
        }

        $first = [Console]::ReadKey($true)
        if ($first.Key -ne [ConsoleKey]::P) {
            return
        }

        Write-Host ""
        Write-Host ("[controls] Pause armed - press P again within {0:N1}s." -f `
            ($script:RuntimeDoublePressWindowMilliseconds / 1000.0)) -ForegroundColor DarkCyan

        $deadline = (Get-Date).AddMilliseconds(
            $script:RuntimeDoublePressWindowMilliseconds
        )

        while ((Get-Date) -lt $deadline) {
            if ([Console]::KeyAvailable) {
                $next = [Console]::ReadKey($true)
                if ($next.Key -eq [ConsoleKey]::P) {
                    Invoke-RuntimePauseMenu -Phase $script:RuntimePhase
                    return
                }
            }

            # Native sleep on purpose: calling Start-PausableSleep here would
            # recurse back into this checkpoint.
            Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds 50
        }

        Write-Host "[controls] Second P not received; continuing." -ForegroundColor DarkGray
    }
    catch {
        # Some hosts expose ConsoleHost-like behavior but do not permit
        # KeyAvailable/ReadKey. Disable controls rather than breaking the job.
        $script:RuntimeControlAvailable = $false
        Write-Warning "Runtime PP pause controls became unavailable in this host. The operation will continue normally."
    }
}

function Start-PausableSleep {
    param(
        [int]$Seconds = 0,
        [int]$Milliseconds = 0,
        [string]$Phase = "waiting"
    )

    $totalMilliseconds = (
        ([Math]::Max(0, $Seconds) * 1000) +
        [Math]::Max(0, $Milliseconds)
    )

    $remaining = [int64]$totalMilliseconds

    while ($remaining -gt 0) {
        Invoke-RuntimeControlCheckpoint -Phase $Phase

        $slice = [int][Math]::Min(200, $remaining)
        Microsoft.PowerShell.Utility\Start-Sleep -Milliseconds $slice
        $remaining -= $slice
    }
}

function Invoke-CluiAccountCheck {
    Write-Section "Authentication / account check"

    try {
        Test-CurrentAccountForUtility
        Write-Host ""
        Write-Host "Read-only authentication check passed." -ForegroundColor Green
    }
    catch {
        Write-Warning $_.Exception.Message
        Write-Host "No writes were attempted." -ForegroundColor Yellow
    }
}

function Show-CluiLocalStatus {
    Write-Section "Local cache / mutation status"

    Write-Host ("Expected target: {0} {1}" -f `
        [string]$script:UserConfig.ExpectedAccountName,
        [string]$script:UserConfig.ExpectedChannelHandle)
    Write-Host ("Last auth refresh: {0}" -f `
        $(if ($script:UserConfig.LastAuthRefreshAt) {
            [string]$script:UserConfig.LastAuthRefreshAt
        }
        else { "(unknown)" }))
    Write-Host ""

    try {
        $localCache = Load-TidyCache

        Write-Host ("Cache path:       {0}" -f $CachePath)
        Write-Host ("Last scan:        {0}" -f `
            $(if ($localCache.LastScan) { [string]$localCache.LastScan } else { "(none)" }))
        Write-Host ("Last full scan:   {0}" -f `
            $(if ($localCache.LastFullScan) { [string]$localCache.LastFullScan } else { "(none)" }))
        Write-Host ("Force full next:  {0}" -f [bool]$localCache.ForceFullNextRun)

        if ($null -ne $localCache.Likes) {
            Write-Host ("Liked cache:      {0} item(s), complete={1}, scanned={2}" -f `
                @($localCache.Likes.Items).Count,
                [bool]$localCache.Likes.Complete,
                [string]$localCache.Likes.ScannedAt)
        }
        else {
            Write-Host "Liked cache:      (none)"
        }

        if ($null -ne $localCache.Library) {
            $incrementalMode = ""
            if ($localCache.Library.PSObject.Properties.Name -contains "IncrementalMode") {
                $incrementalMode = [string]$localCache.Library.IncrementalMode
            }

            Write-Host ("Library cache:    {0} item(s), complete={1}, scanned={2}{3}" -f `
                @($localCache.Library.Items).Count,
                [bool]$localCache.Library.Complete,
                [string]$localCache.Library.ScannedAt,
                $(if ($incrementalMode) { ", incremental=" + $incrementalMode } else { "" }))
        }
        else {
            Write-Host "Library cache:    (none)"
        }

        Write-Host ("Cached playlists: {0}" -f @($localCache.Playlists).Count)
        Write-Host ("Managed cached:   {0}" -f @($localCache.ManagedPlaylists).Count)
        Write-Host ("Placement rows:   {0}" -f @($localCache.PlacementHistory).Count)
    }
    catch {
        Write-Warning ("Could not read tidy cache status: {0}" -f $_.Exception.Message)
    }

    Write-Host ""

    try {
        $mutationState = Load-AddLibraryMutationState
        Write-Host ("ADD_LIBRARY state: {0}" -f $AddLibraryMutationStatePath)
        Write-Host ("Last updated:       {0}" -f `
            $(if ($mutationState.LastUpdated) {
                [string]$mutationState.LastUpdated
            }
            else { "(none)" }))

        $groups = @(
            @($mutationState.Results) |
            Group-Object Status |
            Sort-Object Name
        )

        if ($groups.Count -eq 0) {
            Write-Host "Mutation rows:      (none)"
        }
        else {
            Write-Host "Mutation rows:"
            foreach ($g in $groups) {
                Write-Host ("  {0,-20} {1,5}" -f [string]$g.Name, [int]$g.Count)
            }
        }
    }
    catch {
        Write-Warning ("Could not read ADD_LIBRARY mutation status: {0}" -f $_.Exception.Message)
    }
}

function Test-IsDynamicPlaylist {
    param(
        [string]$PlaylistId,
        [string]$Title = ""
    )

    if (-not $PlaylistId) { return $false }
    if ($PlaylistId -match '^RD') { return $true }
    if ($PlaylistId -match '^LRSR') { return $true }
    if ($PlaylistId -eq 'SS') { return $true }
    return $false
}

function New-LibraryResolutionCache {
    return [pscustomobject]@{
        Version = 2
        ResolverRevision = $LibraryResolverRevision
        LastUpdated = $null
        Results = @()
    }
}

function Load-LibraryResolutionCache {
    if (-not (Test-Path -LiteralPath $LibraryResolutionCachePath)) {
        return New-LibraryResolutionCache
    }

    try {
        $cache = Get-Content -LiteralPath $LibraryResolutionCachePath -Raw -Encoding UTF8 | ConvertFrom-Json

        if ($null -eq $cache.Results) {
            $cache | Add-Member -NotePropertyName Results -NotePropertyValue @()
        }

        if ($cache.PSObject.Properties.Name -notcontains "ResolverRevision") {
            $cache | Add-Member -NotePropertyName ResolverRevision -NotePropertyValue 1
        }

        if ($cache.PSObject.Properties.Name -notcontains "Version") {
            $cache | Add-Member -NotePropertyName Version -NotePropertyValue 1
        }

        return $cache
    }
    catch {
        Write-Warning "Could not parse Library-resolution cache; starting a new one."
        return New-LibraryResolutionCache
    }
}

function Get-LibraryResolutionMap {
    param($ResolutionCache)

    $map = @{}
    foreach ($row in @($ResolutionCache.Results)) {
        if ($row.VideoId) {
            $map[[string]$row.VideoId] = $row
        }
    }
    return $map
}

function Save-LibraryResolutionMap {
    param($ResolutionMap)

    $obj = [pscustomobject]@{
        Version = 2
        ResolverRevision = $LibraryResolverRevision
        LastUpdated = (Get-NowIso)
        Results = @($ResolutionMap.Values | Sort-Object ResolvedAt, VideoId)
    }

    $tmp = "$LibraryResolutionCachePath.tmp"
    $obj | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $LibraryResolutionCachePath -Force
}


function New-LibraryTokenCache {
    return [pscustomobject]@{
        Version = 1
        TokenRevision = $LibraryTokenRevision
        LastUpdated = $null
        Results = @()
    }
}

function Load-LibraryTokenCache {
    if (-not (Test-Path -LiteralPath $LibraryTokenCachePath)) {
        return New-LibraryTokenCache
    }

    try {
        $cache = Get-Content -LiteralPath $LibraryTokenCachePath -Raw -Encoding UTF8 | ConvertFrom-Json

        if ($null -eq $cache.Results) {
            $cache | Add-Member -NotePropertyName Results -NotePropertyValue @()
        }

        if ($cache.PSObject.Properties.Name -notcontains "TokenRevision") {
            $cache | Add-Member -NotePropertyName TokenRevision -NotePropertyValue 0
        }

        if ($cache.PSObject.Properties.Name -notcontains "Version") {
            $cache | Add-Member -NotePropertyName Version -NotePropertyValue 1
        }

        return $cache
    }
    catch {
        Write-Warning "Could not parse Library-token cache; starting a new one."
        return New-LibraryTokenCache
    }
}

function Get-LibraryTokenMap {
    param($TokenCache)

    $map = @{}
    foreach ($row in @($TokenCache.Results)) {
        if ($row.VideoId) {
            $map[[string]$row.VideoId] = $row
        }
    }
    return $map
}

function Save-LibraryTokenMap {
    param($TokenMap)

    $obj = [pscustomobject]@{
        Version = 1
        TokenRevision = $LibraryTokenRevision
        LastUpdated = (Get-NowIso)
        Results = @($TokenMap.Values | Sort-Object AttemptedAt, VideoId)
    }

    $tmp = "$LibraryTokenCachePath.tmp"
    $obj | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $LibraryTokenCachePath -Force
}


function New-AddLibraryMutationState {
    return [pscustomobject]@{
        Version = 1
        LastUpdated = $null
        Results = @()
    }
}

function Load-AddLibraryMutationState {
    if (-not (Test-Path -LiteralPath $AddLibraryMutationStatePath)) {
        return New-AddLibraryMutationState
    }

    try {
        $state = Get-Content -LiteralPath $AddLibraryMutationStatePath -Raw -Encoding UTF8 | ConvertFrom-Json

        if ($null -eq $state.Results) {
            $state | Add-Member -NotePropertyName Results -NotePropertyValue @()
        }

        if ($state.PSObject.Properties.Name -notcontains "Version") {
            $state | Add-Member -NotePropertyName Version -NotePropertyValue 1
        }

        return $state
    }
    catch {
        throw ("Could not parse ADD_LIBRARY mutation state '{0}'. Refusing writes until it is repaired." -f `
            $AddLibraryMutationStatePath)
    }
}

function Get-AddLibraryMutationMap {
    param($State)

    $map = @{}
    foreach ($row in @($State.Results)) {
        if ($row.VideoId) {
            $map[[string]$row.VideoId] = $row
        }
    }
    return $map
}

function Save-AddLibraryMutationMap {
    param($MutationMap)

    $obj = [pscustomobject]@{
        Version = 1
        LastUpdated = (Get-NowIso)
        Results = @($MutationMap.Values | Sort-Object VideoId)
    }

    $tmp = "$AddLibraryMutationStatePath.tmp"
    $obj | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $AddLibraryMutationStatePath -Force
}

function Set-AddLibraryMutationResult {
    param(
        [hashtable]$MutationMap,
        [string]$VideoId,
        [string]$Title,
        [string]$Artist,
        [string]$Status,
        [string]$ApiAcceptedAt = "",
        [string]$VerifiedAt = "",
        [int]$BatchOrdinal = 0,
        [int]$BatchSize = 0,
        [string]$Note = ""
    )

    if (-not $VideoId) { return }

    $old = $null
    if ($MutationMap.ContainsKey($VideoId)) {
        $old = $MutationMap[$VideoId]
    }

    if (-not $ApiAcceptedAt -and $null -ne $old -and $old.ApiAcceptedAt) {
        $ApiAcceptedAt = [string]$old.ApiAcceptedAt
    }
    if (-not $VerifiedAt -and $null -ne $old -and $old.VerifiedAt) {
        $VerifiedAt = [string]$old.VerifiedAt
    }
    if ($BatchOrdinal -eq 0 -and $null -ne $old -and $old.BatchOrdinal) {
        $BatchOrdinal = [int]$old.BatchOrdinal
    }
    if ($BatchSize -eq 0 -and $null -ne $old -and $old.BatchSize) {
        $BatchSize = [int]$old.BatchSize
    }

    $MutationMap[$VideoId] = [pscustomobject]@{
        VideoId = $VideoId
        Title = $Title
        Artist = $Artist
        Status = $Status
        ApiAcceptedAt = $ApiAcceptedAt
        VerifiedAt = $VerifiedAt
        BatchOrdinal = $BatchOrdinal
        BatchSize = $BatchSize
        Note = $Note
        UpdatedAt = (Get-NowIso)
    }
}

function Get-LibraryVideoIdSet {
    param($LibrarySnapshot)

    $set = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    if ($null -eq $LibrarySnapshot) {
        return $set
    }

    foreach ($item in @($LibrarySnapshot.Items)) {
        if ($item.VideoId) {
            [void]$set.Add([string]$item.VideoId)
        }
    }
    return $set
}

function Get-FreshCompleteLibrarySnapshotForWriteSafety {
    param([string]$Reason)

    Write-ProgressLine ("Refreshing complete Song Library for write safety: {0}" -f $Reason) Yellow
    $first = Get-LibraryFirstPage
    $snapshot = Get-LibraryFullSnapshot $first

    if (-not [bool]$snapshot.Complete) {
        throw @"
Song Library verification snapshot is incomplete during $Reason.
ADD_LIBRARY writes are aborted rather than guessing current membership.
No further Library writes were attempted.
"@
    }

    return $snapshot
}

function Normalize-ArtistKey {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) { return "" }

    $value = $Text.Trim().ToLowerInvariant()
    $value = $value -replace '\s*-\s*topic$', ''
    $value = $value -replace '\s+', ' '
    return $value.Trim()
}

function Normalize-IdentityText {
    param([string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text)) { return "" }

    $value = $Text.ToLowerInvariant()
    $value = $value -replace '\[[^\]]*\]', ' '
    $value = $value -replace '\([^\)]*(official|audio|video|visuali[sz]er|lyrics?)[^\)]*\)', ' '
    $value = $value -replace '(official\s+music\s+video|official\s+audio|lyrics?\s+video)', ' '
    $value = $value -replace '[^\p{L}\p{N}]+', ' '
    $value = $value -replace '\s+', ' '
    return $value.Trim()
}

function Get-IdentityKey {
    param(
        [string]$Title,
        [string]$Artist
    )

    return ("{0}||{1}" -f (Normalize-IdentityText $Artist), (Normalize-IdentityText $Title))
}

function Get-TextRuns {
    param($Runs)

    $parts = [System.Collections.Generic.List[string]]::new()
    foreach ($run in @($Runs)) {
        $text = Get-NestedValue $run @("text")
        if ($null -ne $text -and [string]$text -ne "") {
            $parts.Add([string]$text)
        }
    }
    return ($parts -join "")
}

function Get-FirstFeedbackToken {
    param($Endpoint)

    if ($null -eq $Endpoint) { return $null }

    $token = Get-NestedValue $Endpoint @("feedbackEndpoint", "feedbackToken")
    if ($token) { return [string]$token }

    $tokens = @(Find-PropertyValues $Endpoint "feedbackToken")
    foreach ($t in $tokens) {
        if ($t -is [string] -and $t) {
            return [string]$t
        }
    }
    return $null
}

function Get-MenuObjects {
    param($Renderer)

    $objects = [System.Collections.Generic.List[object]]::new()

    $menuItems = @(
        Get-NestedValue $Renderer @("menu", "menuRenderer", "items")
    )

    foreach ($item in $menuItems) {
        # Current ytmusicapi parser key.
        $toggle = Get-NestedValue $item @("toggleMenuServiceItemRenderer")
        if ($null -ne $toggle) {
            $objects.Add($toggle)
            continue
        }

        # Older/alternate layout retained for compatibility.
        $legacyToggle = Get-NestedValue $item @("musicToggleMenuServiceItemRenderer")
        if ($null -ne $legacyToggle) {
            $objects.Add($legacyToggle)
            continue
        }

        # Current ytmusicapi also accepts menuServiceItemRenderer as a song
        # menu item because some layouts expose BOOKMARK icons there.
        $service = Get-NestedValue $item @("menuServiceItemRenderer")
        if ($null -ne $service) {
            $objects.Add($service)
        }
    }

    if ($objects.Count -gt 0) {
        return @($objects)
    }

    # Rare layout fallback.
    foreach ($obj in @(Find-PropertyValues $Renderer "toggleMenuServiceItemRenderer")) {
        if ($null -ne $obj) { $objects.Add($obj) }
    }
    foreach ($obj in @(Find-PropertyValues $Renderer "musicToggleMenuServiceItemRenderer")) {
        if ($null -ne $obj) { $objects.Add($obj) }
    }
    foreach ($obj in @(Find-PropertyValues $Renderer "menuServiceItemRenderer")) {
        if ($null -ne $obj) { $objects.Add($obj) }
    }

    return @($objects)
}

function Convert-YtmRendererToTrack {
    param(
        $Row,
        [string]$SourceKind = "",
        [string]$PlaylistId = "",
        [string]$PlaylistTitle = ""
    )

    $renderer = Get-NestedValue $Row @("musicResponsiveListItemRenderer")
    if ($null -eq $renderer) {
        $renderer = $Row
    }
    if ($null -eq $renderer) { return $null }

    $videoId = Get-NestedValue $renderer @("playlistItemData", "videoId")

    # Current playlist rows commonly expose the playable ID through the
    # thumbnail play button / first title navigation endpoint instead.
    if (-not $videoId) {
        $videoId = Get-NestedValue $renderer @(
            "overlay",
            "musicItemThumbnailOverlayRenderer",
            "content",
            "musicPlayButtonRenderer",
            "playNavigationEndpoint",
            "watchEndpoint",
            "videoId"
        )
    }

    $flexColumns = @(Get-NestedValue $renderer @("flexColumns"))

    if (-not $videoId -and $flexColumns.Count -gt 0) {
        $titleRunsProbe = @(
            Get-NestedValue $flexColumns[0] @(
                "musicResponsiveListItemFlexColumnRenderer",
                "text",
                "runs"
            )
        )
        foreach ($run in $titleRunsProbe) {
            $candidateId = Get-NestedValue $run @(
                "navigationEndpoint",
                "watchEndpoint",
                "videoId"
            )
            if ($candidateId) {
                $videoId = [string]$candidateId
                break
            }
        }
    }

    # Only unusual/unplayable rows pay the recursive fallback cost.
    if (-not $videoId) {
        $ids = @(
            Find-PropertyValues $renderer "videoId" |
            Where-Object { $_ -is [string] -and $_ } |
            Select-Object -Unique
        )
        if ($ids.Count -gt 0) {
            $videoId = [string]$ids[0]
        }
    }

    if (-not $videoId) { return $null }

    $videoType = Get-NestedValue $renderer @(
        "overlay",
        "musicItemThumbnailOverlayRenderer",
        "content",
        "musicPlayButtonRenderer",
        "playNavigationEndpoint",
        "watchEndpoint",
        "watchEndpointMusicSupportedConfigs",
        "watchEndpointMusicConfig",
        "musicVideoType"
    )

    if (-not $videoType) {
        $videoType = Get-NestedValue $renderer @(
            "overlay",
            "musicItemThumbnailOverlayRenderer",
            "content",
            "musicPlayButtonRenderer",
            "playNavigationEndpoint",
            "watchEndpoint",
            "musicSupportedConfigs",
            "musicVideoType"
        )
    }

    $title = ""
    $artist = ""
    $albumBrowseId = ""

    if ($flexColumns.Count -gt 0) {
        $titleRuns = Get-NestedValue $flexColumns[0] @(
            "musicResponsiveListItemFlexColumnRenderer",
            "text",
            "runs"
        )
        if ($titleRuns) {
            $title = Get-TextRuns $titleRuns
        }
    }

    if ($flexColumns.Count -gt 1) {
        $artistRuns = @(
            Get-NestedValue $flexColumns[1] @(
                "musicResponsiveListItemFlexColumnRenderer",
                "text",
                "runs"
            )
        )

        foreach ($run in $artistRuns) {
            $pageType = Get-NestedValue $run @(
                "navigationEndpoint",
                "browseEndpoint",
                "browseEndpointContextSupportedConfigs",
                "browseEndpointContextMusicConfig",
                "pageType"
            )

            $browseId = Get-NestedValue $run @(
                "navigationEndpoint",
                "browseEndpoint",
                "browseId"
            )
            if (
                -not $albumBrowseId -and
                $browseId -and
                (
                    [string]$browseId -match '^MPRE' -or
                    [string]$browseId -match 'release_detail'
                )
            ) {
                $albumBrowseId = [string]$browseId
            }

            $candidate = Get-NestedValue $run @("text")
            if (
                -not $artist -and
                $candidate -and
                $pageType -in @("MUSIC_PAGE_TYPE_ARTIST", "MUSIC_PAGE_TYPE_UNKNOWN")
            ) {
                $artist = [string]$candidate
            }
        }

        if (-not $artist) {
            foreach ($run in $artistRuns) {
                $candidate = [string](Get-NestedValue $run @("text"))
                if (
                    $candidate -and
                    $candidate -notmatch '^\s*[•·]\s*$' -and
                    $candidate -notmatch '^\d+:\d{2}$' -and
                    $candidate -notmatch '^(Song|Video|Album)$'
                ) {
                    $artist = $candidate
                    break
                }
            }
        }
    }

    if (-not $title) {
        # Extremely rare fallback only.
        $allTexts = @(
            Find-PropertyValues $renderer "text" |
            Where-Object { $_ -is [string] -and $_ } |
            Select-Object -Unique
        )
        if ($allTexts.Count -gt 0) {
            $title = [string]$allTexts[0]
        }
    }

    $setVideoId = $null
    $inLibrary = $null
    $addLibraryToken = $null
    $removeLibraryToken = $null

    # Current ytmusicapi likewise inspects the direct menu items to obtain
    # playlistEditEndpoint setVideoId and song-library toggle state.
    $menuItems = @(
        Get-NestedValue $renderer @("menu", "menuRenderer", "items")
    )

    foreach ($menuItemWrapper in $menuItems) {
        if (-not $setVideoId) {
            $serviceRenderer = Get-NestedValue $menuItemWrapper @("menuServiceItemRenderer")
            if ($null -ne $serviceRenderer) {
                $serviceEndpoint = Get-NestedValue $serviceRenderer @("serviceEndpoint")
                if ($null -eq $serviceEndpoint) {
                    $serviceEndpoint = Get-NestedValue $serviceRenderer @("defaultServiceEndpoint")
                }

                $actions = @(
                    Get-NestedValue $serviceEndpoint @("playlistEditEndpoint", "actions")
                )
                if ($actions.Count -gt 0) {
                    $candidateSetId = Get-NestedValue $actions[0] @("setVideoId")
                    if ($candidateSetId) {
                        $setVideoId = [string]$candidateSetId
                    }

                    if (-not $videoId) {
                        $removedId = Get-NestedValue $actions[0] @("removedVideoId")
                        if ($removedId) { $videoId = [string]$removedId }
                    }
                }
            }
        }

        # Current ytmusicapi parse_song_menu_data accepts either
        # toggleMenuServiceItemRenderer OR menuServiceItemRenderer.
        # v14 accidentally looked only for the obsolete/alternate
        # musicToggleMenuServiceItemRenderer key, which hid feedback tokens.
        $songMenuItem = Get-NestedValue $menuItemWrapper @("toggleMenuServiceItemRenderer")

        if ($null -eq $songMenuItem) {
            $songMenuItem = Get-NestedValue $menuItemWrapper @("musicToggleMenuServiceItemRenderer")
        }

        if ($null -eq $songMenuItem) {
            $songMenuItem = Get-NestedValue $menuItemWrapper @("menuServiceItemRenderer")
        }

        if ($null -ne $songMenuItem) {
            $icon = Get-NestedValue $songMenuItem @("defaultIcon", "iconType")
            if (-not $icon) {
                $icon = Get-NestedValue $songMenuItem @("icon", "iconType")
            }

            if ($icon -eq "BOOKMARK_BORDER") {
                $inLibrary = $false
                $addLibraryToken = Get-FirstFeedbackToken (
                    Get-NestedValue $songMenuItem @("defaultServiceEndpoint")
                )
                $removeLibraryToken = Get-FirstFeedbackToken (
                    Get-NestedValue $songMenuItem @("toggledServiceEndpoint")
                )
            }
            elseif ($icon -eq "BOOKMARK") {
                $inLibrary = $true
                $addLibraryToken = Get-FirstFeedbackToken (
                    Get-NestedValue $songMenuItem @("toggledServiceEndpoint")
                )
                $removeLibraryToken = Get-FirstFeedbackToken (
                    Get-NestedValue $songMenuItem @("defaultServiceEndpoint")
                )
            }
        }
    }

    # Layout fallback for setVideoId only if the direct menu path did not expose it.
    if (-not $setVideoId) {
        $setIds = @(
            Find-PropertyValues $renderer "setVideoId" |
            Where-Object { $_ -is [string] -and $_ }
        )
        if ($setIds.Count -gt 0) {
            $setVideoId = [string]$setIds[0]
        }
    }

    return [pscustomobject]@{
        VideoId = [string]$videoId
        Title = [string]$title
        Artist = [string]$artist
        VideoType = [string]$videoType
        AlbumBrowseId = [string]$albumBrowseId
        SetVideoId = [string]$setVideoId
        InLibrary = $inLibrary
        AddLibraryToken = [string]$addLibraryToken
        RemoveLibraryToken = [string]$removeLibraryToken
        SourceKind = $SourceKind
        PlaylistId = $PlaylistId
        PlaylistTitle = $PlaylistTitle
        IdentityKey = (Get-IdentityKey $title $artist)
    }
}

function Get-RendererTracksFromNode {
    param(
        $Node,
        [string]$SourceKind = "",
        [string]$PlaylistId = "",
        [string]$PlaylistTitle = ""
    )

    $tracks = [System.Collections.Generic.List[object]]::new()
    $renderers = [System.Collections.Generic.List[object]]::new()

    # Fast path for playlist/library continuation contents, which are already
    # arrays of row wrappers.
    foreach ($candidate in @($Node)) {
        $direct = Get-NestedValue $candidate @("musicResponsiveListItemRenderer")
        if ($null -ne $direct) {
            $renderers.Add($direct)
        }
    }

    # First-page full responses need one recursive discovery pass.
    if ($renderers.Count -eq 0) {
        foreach ($renderer in @(Find-PropertyValues $Node "musicResponsiveListItemRenderer")) {
            if ($null -ne $renderer) {
                $renderers.Add($renderer)
            }
        }
    }

    foreach ($renderer in $renderers) {
        $item = Convert-YtmRendererToTrack `
            -Row $renderer `
            -SourceKind $SourceKind `
            -PlaylistId $PlaylistId `
            -PlaylistTitle $PlaylistTitle

        if ($null -ne $item) {
            $tracks.Add($item)
        }
    }

    return @($tracks)
}

function Get-UniqueTracks {
    param($Tracks)

    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $result = [System.Collections.Generic.List[object]]::new()

    foreach ($item in @($Tracks)) {
        if ($null -eq $item -or -not $item.VideoId) { continue }

        $dedupeKey = [string]$item.VideoId
        if ($item.PlaylistId -and $item.SetVideoId) {
            $dedupeKey = "{0}|{1}" -f $item.PlaylistId, $item.SetVideoId
        }

        if ($seen.Add($dedupeKey)) {
            $result.Add($item)
        }
    }

    return @($result)
}

function Get-ContinuationItems {
    param($Response)

    $items = Get-NestedValue $Response @(
        "onResponseReceivedActions",
        0,
        "appendContinuationItemsAction",
        "continuationItems"
    )

    if ($null -eq $items) {
        $appendActions = @(Find-PropertyValues $Response "appendContinuationItemsAction")
        foreach ($action in $appendActions) {
            $candidate = Get-NestedValue $action @("continuationItems")
            if ($candidate) {
                $items = @($candidate)
                break
            }
        }
    }

    if ($null -eq $items) {
        $shelfContinuations = @(Find-PropertyValues $Response "musicPlaylistShelfContinuation")
        foreach ($cont in $shelfContinuations) {
            $candidate = Get-NestedValue $cont @("contents")
            if ($candidate) {
                $items = @($candidate)
                break
            }
        }
    }

    if ($null -eq $items) {
        $musicShelfContinuations = @(Find-PropertyValues $Response "musicShelfContinuation")
        foreach ($cont in $musicShelfContinuations) {
            $candidate = Get-NestedValue $cont @("contents")
            if ($candidate) {
                $items = @($candidate)
                break
            }
        }
    }

    return @($items)
}

function Get-TrackCountFromResponse {
    param($Response)

    $headerCandidates = @(
        Get-NestedValue $Response @(
            "contents", "twoColumnBrowseResultsRenderer",
            "tabs", 0, "tabRenderer", "content",
            "sectionListRenderer", "contents", 0,
            "musicEditablePlaylistDetailHeaderRenderer",
            "header", "musicResponsiveHeaderRenderer"
        ),
        Get-NestedValue $Response @(
            "contents", "twoColumnBrowseResultsRenderer",
            "tabs", 0, "tabRenderer", "content",
            "sectionListRenderer", "contents", 0,
            "musicResponsiveHeaderRenderer"
        )
    )

    foreach ($header in $headerCandidates) {
        if ($null -eq $header) { continue }

        $runs = Get-NestedValue $header @("secondSubtitle", "runs")
        if ($runs) {
            $text = Get-TextRuns $runs
            $matches = [regex]::Matches($text, '\d[\d,\.]*')
            foreach ($m in $matches) {
                $digits = ([string]$m.Value) -replace '[^\d]', ''
                $value = 0
                if ([int]::TryParse($digits, [ref]$value)) {
                    if ($value -ge 0) { return $value }
                }
            }
        }
    }

    return $null
}

function Get-HeadIds {
    param(
        $Tracks,
        [int]$Count = 5
    )

    return @(
        $Tracks |
        Where-Object { $_.VideoId } |
        Select-Object -First $Count |
        ForEach-Object { [string]$_.VideoId }
    )
}

function Test-SameStringArray {
    param(
        $A,
        $B
    )

    $aa = @($A)
    $bb = @($B)

    if ($aa.Count -ne $bb.Count) { return $false }
    for ($i = 0; $i -lt $aa.Count; $i++) {
        if ([string]$aa[$i] -ne [string]$bb[$i]) {
            return $false
        }
    }
    return $true
}

function Get-PlaylistFirstPage {
    param(
        [string]$PlaylistId,
        [string]$PlaylistTitle
    )

    $browseId = if ($PlaylistId.StartsWith("VL")) { $PlaylistId } else { "VL$PlaylistId" }

    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = $browseId } `
        -ContextLabel ("quick-checking playlist '{0}'" -f $PlaylistTitle)

    $tracks = Get-RendererTracksFromNode `
        -Node $response `
        -SourceKind "Playlist" `
        -PlaylistId $PlaylistId `
        -PlaylistTitle $PlaylistTitle

    $tracks = Get-UniqueTracks $tracks
    $count = Get-TrackCountFromResponse $response

    return [pscustomobject]@{
        Response = $response
        Tracks = @($tracks)
        Count = $count
        HeadIds = @(Get-HeadIds $tracks $HeadCheckCount)
        Continuation = (Get-ContinuationToken (Get-NestedValue $response @(
            "contents", "twoColumnBrowseResultsRenderer",
            "secondaryContents", "sectionListRenderer",
            "contents", 0, "musicPlaylistShelfRenderer", "contents"
        )))
    }
}

function Get-PlaylistFullSnapshot {
    param(
        [string]$PlaylistId,
        [string]$PlaylistTitle,
        $FirstPage = $null
    )

    $scanSw = [System.Diagnostics.Stopwatch]::StartNew()

    if ($null -eq $FirstPage) {
        $FirstPage = Get-PlaylistFirstPage $PlaylistId $PlaylistTitle
    }

    $all = [System.Collections.Generic.List[object]]::new()
    foreach ($item in @($FirstPage.Tracks)) {
        $all.Add($item)
    }

    $page = 1
    $firstCount = @($FirstPage.Tracks).Count
    $token = [string]$FirstPage.Continuation
    if (-not $token) {
        $token = Get-ContinuationTokenDeep $FirstPage.Response
    }

    Write-ProgressLine ("    page {0}: +{1} item(s), raw total={2}, continuation={3}" -f `
        $page, $firstCount, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray

    $seenTokens = [System.Collections.Generic.HashSet[string]]::new()
    $complete = $true

    while ($token) {
        if (-not $seenTokens.Add($token)) {
            Write-Warning ("Continuation repeated for '{0}'. Snapshot marked incomplete." -f $PlaylistTitle)
            $complete = $false
            break
        }

        if ($BatchDelaySeconds -gt 0) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 250)) -Phase "batch delay"
        }

        $page++
        $next = Invoke-YtmRequest `
            -Endpoint "browse" `
            -Body @{ continuation = $token } `
            -ContextLabel ("reading page {0} continuation for playlist '{1}'" -f $page, $PlaylistTitle)

        $items = @(Get-ContinuationItems $next)
        if ($items.Count -eq 0) {
            Write-Warning ("Playlist '{0}' continuation page {1} returned no continuation items. Snapshot marked incomplete." -f `
                $PlaylistTitle, $page)
            $complete = $false
            break
        }

        $parsed = @(Get-RendererTracksFromNode `
            -Node $items `
            -SourceKind "Playlist" `
            -PlaylistId $PlaylistId `
            -PlaylistTitle $PlaylistTitle
        )

        foreach ($item in $parsed) {
            $all.Add($item)
        }

        $token = Get-ContinuationToken $items
        if (-not $token) {
            $token = Get-ContinuationTokenDeep $next
        }

        Write-ProgressLine ("    page {0}: +{1} item(s), raw total={2}, continuation={3}" -f `
            $page, $parsed.Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray
    }

    $unique = @(Get-UniqueTracks $all)
    $count = $FirstPage.Count
    if ($null -eq $count) { $count = $unique.Count }

    if ($null -ne $count -and [int]$count -gt $unique.Count) {
        Write-Warning ("Playlist '{0}' header says {1} track(s), but only {2} unique track(s) were parsed. Snapshot marked incomplete." -f `
            $PlaylistTitle, $count, $unique.Count)
        $complete = $false
    }

    $scanSw.Stop()
    Write-ProgressLine ("    DONE '{0}': {1} unique item(s), {2} page(s), complete={3}, {4:N1}s" -f `
        $PlaylistTitle, $unique.Count, $page, $complete, $scanSw.Elapsed.TotalSeconds) DarkCyan

    return [pscustomobject]@{
        PlaylistId = $PlaylistId
        PlaylistTitle = $PlaylistTitle
        Count = $count
        HeadIds = @(Get-HeadIds $unique $HeadCheckCount)
        Items = @($unique)
        Complete = $complete
        Pages = $page
        ScannedAt = (Get-NowIso)
    }
}

function Get-LikedFirstPage {
    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = "VLLM" } `
        -ContextLabel "quick-checking Liked Music"

    $tracks = @(Get-RendererTracksFromNode -Node $response -SourceKind "Liked")
    $tracks = @(Get-UniqueTracks $tracks)

    # Liked Music is the LM playlist. Current playlist pagination uses the
    # 2025 continuationItemRenderer body-token flow.
    $trackContents = Get-NestedValue $response @(
        "contents", "twoColumnBrowseResultsRenderer",
        "secondaryContents", "sectionListRenderer",
        "contents", 0, "musicPlaylistShelfRenderer", "contents"
    )

    $token = Get-ContinuationToken $trackContents
    if (-not $token) {
        $token = Get-ContinuationTokenDeep $response
    }

    return [pscustomobject]@{
        Response = $response
        Tracks = $tracks
        Count = (Get-TrackCountFromResponse $response)
        HeadIds = @(Get-HeadIds $tracks $HeadCheckCount)
        Continuation = [string]$token
    }
}


function Try-ApplyLikedAdditionDelta {
    param(
        $FirstPage,
        $CachedSnapshot
    )

    if ($null -eq $FirstPage -or $null -eq $CachedSnapshot) {
        return $null
    }

    if (
        $null -eq $FirstPage.Count -or
        $null -eq $CachedSnapshot.Count
    ) {
        return $null
    }

    $oldCount = [int]$CachedSnapshot.Count
    $newCount = [int]$FirstPage.Count
    $delta = $newCount - $oldCount

    # This fast path is intentionally addition-only.
    # A removal can occur anywhere in LM, so a smaller count still requires a
    # full refresh. Same-count head changes can mean add+remove/reorder.
    if ($delta -le 0) {
        return $null
    }

    $firstTracks = @($FirstPage.Tracks)
    $cachedHead = @($CachedSnapshot.HeadIds)

    if ($cachedHead.Count -eq 0) {
        return $null
    }

    # Need the newly-added prefix plus enough old head IDs to prove that the
    # current first page is simply the previous snapshot shifted downward.
    if (($delta + $cachedHead.Count) -gt $firstTracks.Count) {
        return $null
    }

    $shiftedHead = @(
        $firstTracks |
        Select-Object -Skip $delta -First $cachedHead.Count |
        ForEach-Object { [string]$_.VideoId }
    )

    if (-not (Test-SameStringArray $shiftedHead $cachedHead)) {
        return $null
    }

    $newItems = @($firstTracks | Select-Object -First $delta)
    if ($newItems.Count -ne $delta) {
        return $null
    }

    # Refuse the fast path if any proposed "new" ID already exists in the
    # cached snapshot. That indicates a reorder/duplicate rather than a pure
    # newest-first append.
    $cachedIds = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($item in @($CachedSnapshot.Items)) {
        if ($null -ne $item -and $item.VideoId) {
            [void]$cachedIds.Add([string]$item.VideoId)
        }
    }

    foreach ($item in $newItems) {
        if ($null -eq $item -or -not $item.VideoId) {
            return $null
        }
        if ($cachedIds.Contains([string]$item.VideoId)) {
            return $null
        }
    }

    $merged = @($newItems) + @($CachedSnapshot.Items)
    $merged = @(Get-UniqueTracks $merged)

    if ($merged.Count -ne $newCount) {
        return $null
    }

    $pages = $CachedSnapshot.Pages
    if ($newCount -gt 0) {
        # LM currently paginates at 100 rows/page in this script's parser.
        $pages = [int][Math]::Ceiling(([double]$newCount) / 100.0)
    }

    return [pscustomobject]@{
        Count = $newCount
        HeadIds = @(Get-HeadIds $merged $HeadCheckCount)
        Items = @($merged)
        Complete = $true
        Pages = $pages
        ScannedAt = (Get-NowIso)
        IncrementalUpdate = $true
        IncrementalAdded = $delta
    }
}

function Get-LikedFullSnapshot {
    param($FirstPage = $null)

    $scanSw = [System.Diagnostics.Stopwatch]::StartNew()

    if ($null -eq $FirstPage) {
        $FirstPage = Get-LikedFirstPage
    }

    $all = [System.Collections.Generic.List[object]]::new()
    foreach ($item in @($FirstPage.Tracks)) { $all.Add($item) }

    $page = 1
    $token = [string]$FirstPage.Continuation
    $seenTokens = [System.Collections.Generic.HashSet[string]]::new()
    $complete = $true

    Write-ProgressLine ("Liked page 1: +{0}, raw total={1}, continuation={2}" -f `
        @($FirstPage.Tracks).Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray

    while ($token) {
        if (-not $seenTokens.Add($token)) {
            Write-Warning "Liked Music continuation repeated. Snapshot marked incomplete."
            $complete = $false
            break
        }

        if ($BatchDelaySeconds -gt 0) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 250)) -Phase "batch delay"
        }

        $page++
        $next = Invoke-YtmRequest `
            -Endpoint "browse" `
            -Body @{ continuation = $token } `
            -ContextLabel ("reading Liked Music page {0}" -f $page)

        $items = @(Get-ContinuationItems $next)
        if ($items.Count -eq 0) {
            Write-Warning ("Liked Music page {0} returned no continuation items. Snapshot marked incomplete." -f $page)
            $complete = $false
            break
        }

        $parsed = @(Get-RendererTracksFromNode -Node $items -SourceKind "Liked")
        foreach ($item in $parsed) { $all.Add($item) }

        $token = Get-ContinuationToken $items
        if (-not $token) { $token = Get-ContinuationTokenDeep $next }

        Write-ProgressLine ("Liked page {0}: +{1}, raw total={2}, continuation={3}" -f `
            $page, $parsed.Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray
    }

    $unique = @(Get-UniqueTracks $all)
    $count = $FirstPage.Count
    if ($null -eq $count) { $count = $unique.Count }

    if ($null -ne $count -and [int]$count -gt $unique.Count) {
        Write-Warning ("Liked Music header says {0}, but only {1} unique item(s) were parsed. Snapshot marked incomplete." -f `
            $count, $unique.Count)
        $complete = $false
    }

    $scanSw.Stop()
    Write-ProgressLine ("Liked Music DONE: {0} unique, {1} page(s), complete={2}, {3:N1}s" -f `
        $unique.Count, $page, $complete, $scanSw.Elapsed.TotalSeconds) DarkCyan

    return [pscustomobject]@{
        Count = $count
        HeadIds = @(Get-HeadIds $unique $HeadCheckCount)
        Items = $unique
        Complete = $complete
        Pages = $page
        ScannedAt = (Get-NowIso)
    }
}


function Try-ApplyLibraryFirstPageDelta {
    param(
        $FirstPage,
        $CachedSnapshot
    )

    if ($null -eq $FirstPage -or $null -eq $CachedSnapshot) {
        return $null
    }

    $cachedComplete = $false
    if ($CachedSnapshot.PSObject.Properties.Name -contains "Complete") {
        $cachedComplete = [bool]$CachedSnapshot.Complete
    }
    if (-not $cachedComplete) {
        return $null
    }

    $current = @(
        $FirstPage.Tracks |
        Where-Object { $null -ne $_ -and $_.VideoId }
    )
    $cachedItems = @(
        $CachedSnapshot.Items |
        Where-Object { $null -ne $_ -and $_.VideoId }
    )

    if ($current.Count -eq 0 -or $cachedItems.Count -eq 0) {
        return $null
    }

    $cachedIds = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal
    )
    foreach ($item in $cachedItems) {
        [void]$cachedIds.Add([string]$item.VideoId)
    }

    $currentIds = @($current | ForEach-Object { [string]$_.VideoId })
    $cachedPageIds = @(
        $cachedItems |
        Select-Object -First $current.Count |
        ForEach-Object { [string]$_.VideoId }
    )

    # ------------------------------------------------------------------
    # Case 1: page-1 membership is identical but YouTube shuffled the
    # recently-added ordering. This is common shortly after a bulk add.
    # We can update only the cached order; membership/count did not change.
    # ------------------------------------------------------------------
    if ($cachedPageIds.Count -eq $currentIds.Count) {
        $currentSet = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::Ordinal
        )
        $cachedPageSet = [System.Collections.Generic.HashSet[string]]::new(
            [System.StringComparer]::Ordinal
        )

        foreach ($id in $currentIds) { [void]$currentSet.Add($id) }
        foreach ($id in $cachedPageIds) { [void]$cachedPageSet.Add($id) }

        if ($currentSet.SetEquals($cachedPageSet)) {
            $pageSet = [System.Collections.Generic.HashSet[string]]::new(
                [System.StringComparer]::Ordinal
            )
            foreach ($id in $currentIds) { [void]$pageSet.Add($id) }

            $remainder = @(
                $cachedItems |
                Where-Object { -not $pageSet.Contains([string]$_.VideoId) }
            )

            foreach ($item in $current) {
                $item.InLibrary = $true
            }

            $merged = @(Get-UniqueTracks (@($current) + @($remainder)))

            if ($merged.Count -ne $cachedItems.Count) {
                return $null
            }

            return [pscustomobject]@{
                Count = if ($null -ne $CachedSnapshot.Count) {
                    [int]$CachedSnapshot.Count
                }
                else {
                    $merged.Count
                }
                HeadIds = @(Get-HeadIds $current $HeadCheckCount)
                Items = @($merged)
                Complete = $true
                Pages = $CachedSnapshot.Pages
                # ScannedAt remains the last authoritative complete crawl.
                ScannedAt = $CachedSnapshot.ScannedAt
                LastIncrementalCheckAt = (Get-NowIso)
                IncrementalUpdate = $true
                IncrementalMode = "PAGE1_REORDER"
                IncrementalAdded = 0
            }
        }
    }

    # ------------------------------------------------------------------
    # Case 2: newest-first prefix additions.
    #
    # Example, page size 25:
    #   cached page: A B C D ...
    #   current page: X Y A B C D ...
    #
    # Unknown IDs must be a contiguous prefix, at least one old cached ID
    # must still overlap page 1, and the known tail must exactly equal the
    # corresponding prefix of the old cached page. This proves a prepend,
    # rather than guessing from an arbitrary head change.
    # ------------------------------------------------------------------
    $newPrefix = [System.Collections.Generic.List[object]]::new()
    $firstKnownIndex = -1

    for ($i = 0; $i -lt $current.Count; $i++) {
        $id = [string]$current[$i].VideoId

        if ($cachedIds.Contains($id)) {
            $firstKnownIndex = $i
            break
        }

        $newPrefix.Add($current[$i])
    }

    # No overlap means there may be >= one full page of additions or a larger
    # topology change. Do not guess; let the caller perform a full refresh.
    if ($firstKnownIndex -lt 0) {
        return $null
    }

    # If the first item is already known, this was not a clean prefix-add case
    # (and the same-membership reorder case above already had its chance).
    if ($newPrefix.Count -eq 0) {
        return $null
    }

    # After the first known cached item, every remaining page-1 item must also
    # be known. Unknown IDs interspersed with old ones are ambiguous.
    for ($i = $firstKnownIndex; $i -lt $current.Count; $i++) {
        if (-not $cachedIds.Contains([string]$current[$i].VideoId)) {
            return $null
        }
    }

    $knownTailIds = @(
        $current |
        Select-Object -Skip $newPrefix.Count |
        ForEach-Object { [string]$_.VideoId }
    )
    $expectedOldPrefix = @(
        $cachedItems |
        Select-Object -First $knownTailIds.Count |
        ForEach-Object { [string]$_.VideoId }
    )

    if (-not (Test-SameStringArray $knownTailIds $expectedOldPrefix)) {
        return $null
    }

    # Refuse to treat a new Video ID as an addition when its normalized
    # artist/title identity already exists under another cached Video ID.
    # That can be a canonical/alternate-ID swap and deserves a full refresh.
    $cachedIdentityKeys = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal
    )
    foreach ($item in $cachedItems) {
        $identity = Get-IdentityKey `
            -Title ([string]$item.Title) `
            -Artist ([string]$item.Artist)

        if ($identity) {
            [void]$cachedIdentityKeys.Add($identity)
        }
    }

    foreach ($item in @($newPrefix)) {
        $identity = Get-IdentityKey `
            -Title ([string]$item.Title) `
            -Artist ([string]$item.Artist)

        if ($identity -and $cachedIdentityKeys.Contains($identity)) {
            return $null
        }
    }

    $currentPageSet = [System.Collections.Generic.HashSet[string]]::new(
        [System.StringComparer]::Ordinal
    )
    foreach ($id in $currentIds) {
        [void]$currentPageSet.Add($id)
    }

    $remainder = @(
        $cachedItems |
        Where-Object { -not $currentPageSet.Contains([string]$_.VideoId) }
    )

    foreach ($item in $current) {
        $item.InLibrary = $true
    }

    $merged = @(Get-UniqueTracks (@($current) + @($remainder)))
    $expectedMergedCount = $cachedItems.Count + $newPrefix.Count

    if ($merged.Count -ne $expectedMergedCount) {
        return $null
    }

    return [pscustomobject]@{
        Count = if ($null -ne $CachedSnapshot.Count) {
            ([int]$CachedSnapshot.Count + $newPrefix.Count)
        }
        else {
            $merged.Count
        }
        HeadIds = @(Get-HeadIds $current $HeadCheckCount)
        Items = @($merged)
        Complete = $true
        Pages = $CachedSnapshot.Pages
        # Preserve the timestamp of the last authoritative complete crawl.
        ScannedAt = $CachedSnapshot.ScannedAt
        LastIncrementalCheckAt = (Get-NowIso)
        IncrementalUpdate = $true
        IncrementalMode = "RECENT_PREFIX_ADD"
        IncrementalAdded = $newPrefix.Count
    }
}

function Get-LibraryFirstPage {
    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{
            browseId = "FEmusic_liked_videos"
            params = $LibraryRecentlyAddedParams
        } `
        -ContextLabel "quick-checking song Library"

    $tracks = @(Get-RendererTracksFromNode -Node $response -SourceKind "Library")
    $tracks = @(Get-UniqueTracks $tracks)

    return [pscustomobject]@{
        Response = $response
        Tracks = $tracks
        HeadIds = @(Get-HeadIds $tracks $HeadCheckCount)
        LegacyContinuation = [string](Get-LegacyContinuationToken $response)
    }
}

function Get-LibraryFullSnapshot {
    param($FirstPage = $null)

    $scanSw = [System.Diagnostics.Stopwatch]::StartNew()

    if ($null -eq $FirstPage) {
        $FirstPage = Get-LibraryFirstPage
    }

    $all = [System.Collections.Generic.List[object]]::new()
    foreach ($item in @($FirstPage.Tracks)) { $all.Add($item) }

    $page = 1
    $token = [string]$FirstPage.LegacyContinuation
    $seenTokens = [System.Collections.Generic.HashSet[string]]::new()
    $complete = $true

    Write-ProgressLine ("Library page 1: +{0}, raw total={1}, continuation={2}" -f `
        @($FirstPage.Tracks).Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray

    while ($token) {
        if (-not $seenTokens.Add($token)) {
            Write-Warning "Library continuation repeated. Snapshot marked incomplete."
            $complete = $false
            break
        }

        if ($BatchDelaySeconds -gt 0) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 250)) -Phase "batch delay"
        }

        $page++
        $query = Get-LegacyContinuationQuery $token
        $next = Invoke-YtmRequest `
            -Endpoint "browse" `
            -Body @{
                browseId = "FEmusic_liked_videos"
                params = $LibraryRecentlyAddedParams
            } `
            -AdditionalQuery $query `
            -ContextLabel ("reading song Library page {0}" -f $page)

        $items = @(Get-ContinuationItems $next)
        if ($items.Count -eq 0) {
            Write-Warning ("Library page {0} returned no continuation contents. Snapshot marked incomplete." -f $page)
            $complete = $false
            break
        }

        $parsed = @(Get-RendererTracksFromNode -Node $items -SourceKind "Library")
        foreach ($item in $parsed) { $all.Add($item) }

        $token = [string](Get-LegacyContinuationToken $next)

        Write-ProgressLine ("Library page {0}: +{1}, raw total={2}, continuation={3}" -f `
            $page, $parsed.Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray
    }

    $unique = @(Get-UniqueTracks $all)

    foreach ($item in $unique) {
        $item.InLibrary = $true
    }

    $scanSw.Stop()
    Write-ProgressLine ("Library DONE: {0} unique, {1} page(s), complete={2}, {3:N1}s" -f `
        $unique.Count, $page, $complete, $scanSw.Elapsed.TotalSeconds) DarkCyan

    return [pscustomobject]@{
        Count = $unique.Count
        HeadIds = @(Get-HeadIds $unique $HeadCheckCount)
        Items = $unique
        Complete = $complete
        Pages = $page
        ScannedAt = (Get-NowIso)
    }
}

function Get-PlaylistCardListFromResponse {
    param($Response)

    $result = [System.Collections.Generic.List[object]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new()

    foreach ($renderer in @(Find-PropertyValues $Response "musicTwoRowItemRenderer")) {
        $browseId = Get-NestedValue $renderer @("navigationEndpoint", "browseEndpoint", "browseId")
        if (-not $browseId -or -not ([string]$browseId).StartsWith("VL")) { continue }

        $playlistId = ([string]$browseId).Substring(2)
        if ($playlistId -in @("LM", "SE")) { continue }
        if (-not $playlistId -or -not $seen.Add($playlistId)) { continue }

        $title = Get-TextRuns (Get-NestedValue $renderer @("title", "runs"))
        if (-not $title) { $title = $playlistId }

        $result.Add([pscustomobject]@{
            PlaylistId = $playlistId
            Title = [string]$title
        })
    }

    foreach ($renderer in @(Find-PropertyValues $Response "musicResponsiveListItemRenderer")) {
        $browseIds = @(Find-PropertyValues $renderer "browseId" | Where-Object {
            $_ -is [string] -and ([string]$_).StartsWith("VL")
        })

        if ($browseIds.Count -eq 0) { continue }

        $playlistId = ([string]$browseIds[0]).Substring(2)
        if ($playlistId -in @("LM", "SE")) { continue }
        if (-not $playlistId -or -not $seen.Add($playlistId)) { continue }

        $title = ""
        $flex = @(Get-NestedValue $renderer @("flexColumns"))
        if ($flex.Count -gt 0) {
            $title = Get-TextRuns (Get-NestedValue $flex[0] @(
                "musicResponsiveListItemFlexColumnRenderer", "text", "runs"
            ))
        }
        if (-not $title) { $title = $playlistId }

        $result.Add([pscustomobject]@{
            PlaylistId = $playlistId
            Title = [string]$title
        })
    }

    return @($result)
}

function Get-PlaylistIndexFirstPage {
    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = "FEmusic_liked_playlists" } `
        -ContextLabel "quick-checking playlist index"

    $playlists = @(Get-PlaylistCardListFromResponse $response)

    return [pscustomobject]@{
        Response = $response
        Playlists = $playlists
        HeadIds = @($playlists | Select-Object -First $HeadCheckCount | ForEach-Object { $_.PlaylistId })
        LegacyContinuation = [string](Get-LegacyContinuationToken $response)
    }
}

function Get-PlaylistIndexFull {
    param($FirstPage = $null)

    $scanSw = [System.Diagnostics.Stopwatch]::StartNew()

    if ($null -eq $FirstPage) {
        $FirstPage = Get-PlaylistIndexFirstPage
    }

    $all = [System.Collections.Generic.List[object]]::new()
    foreach ($p in @($FirstPage.Playlists)) { $all.Add($p) }

    $seenIds = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($p in @($all)) { [void]$seenIds.Add([string]$p.PlaylistId) }

    $page = 1
    $token = [string]$FirstPage.LegacyContinuation
    $seenTokens = [System.Collections.Generic.HashSet[string]]::new()
    $complete = $true

    Write-ProgressLine ("Playlist index page 1: +{0}, total={1}, continuation={2}" -f `
        @($FirstPage.Playlists).Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray

    while ($token) {
        if (-not $seenTokens.Add($token)) {
            Write-Warning "Playlist-index continuation repeated. Index marked incomplete."
            $complete = $false
            break
        }

        $page++
        $query = Get-LegacyContinuationQuery $token
        $next = Invoke-YtmRequest `
            -Endpoint "browse" `
            -Body @{ browseId = "FEmusic_liked_playlists" } `
            -AdditionalQuery $query `
            -ContextLabel ("reading playlist index page {0}" -f $page)

        $parsed = @(Get-PlaylistCardListFromResponse $next)
        foreach ($p in $parsed) {
            if ($seenIds.Add([string]$p.PlaylistId)) {
                $all.Add($p)
            }
        }

        $token = [string](Get-LegacyContinuationToken $next)

        Write-ProgressLine ("Playlist index page {0}: +{1}, total={2}, continuation={3}" -f `
            $page, $parsed.Count, $all.Count, $(if ($token) { "yes" } else { "no" })) DarkGray
    }

    $scanSw.Stop()
    Write-ProgressLine ("Playlist index DONE: {0} playlist(s), {1} page(s), complete={2}, {3:N1}s" -f `
        $all.Count, $page, $complete, $scanSw.Elapsed.TotalSeconds) DarkCyan

    return [pscustomobject]@{
        HeadIds = @($all | Select-Object -First $HeadCheckCount | ForEach-Object { $_.PlaylistId })
        Playlists = @($all)
        Complete = $complete
        Pages = $page
        ScannedAt = (Get-NowIso)
    }
}

function New-EmptyTidyCache {
    return [pscustomobject]@{
        Version = 3
        ScannerRevision = $ScannerRevision
        TrackMetadataRevision = $TrackMetadataRevision
        TrackMetadataRepairedAt = $null
        LastScan = $null
        LastFullScan = $null
        ForceFullNextRun = $false
        PlaylistIndex = $null
        Likes = $null
        Library = $null
        Playlists = @()

        # v27 portable/consolidated metadata. These make the legacy migration
        # CSV/state files optional after one successful import.
        ManagedPlaylists = @()
        PlacementHistory = @()
        PlacementHistoryImportedAt = $null
    }
}

function Load-TidyCache {
    if (-not (Test-Path -LiteralPath $CachePath)) {
        return New-EmptyTidyCache
    }

    try {
        $cache = Get-Content -LiteralPath $CachePath -Raw -Encoding UTF8 | ConvertFrom-Json
        if ($null -eq $cache.Version -or [int]$cache.Version -lt 2) {
            Write-Warning "Old/unknown tidy cache version. A full scan will rebuild it."
            $fresh = New-EmptyTidyCache
            $fresh.ForceFullNextRun = $true
            return $fresh
        }
        if ($null -eq $cache.Playlists) {
            $cache.Playlists = @()
        }
        if ($cache.PSObject.Properties.Name -notcontains "ScannerRevision") {
            $cache | Add-Member -NotePropertyName ScannerRevision -NotePropertyValue 0
        }
        if ($cache.PSObject.Properties.Name -notcontains "TrackMetadataRevision") {
            $cache | Add-Member -NotePropertyName TrackMetadataRevision -NotePropertyValue 0
        }
        if ($cache.PSObject.Properties.Name -notcontains "TrackMetadataRepairedAt") {
            $cache | Add-Member -NotePropertyName TrackMetadataRepairedAt -NotePropertyValue $null
        }
        if ($cache.PSObject.Properties.Name -notcontains "ManagedPlaylists") {
            $cache | Add-Member -NotePropertyName ManagedPlaylists -NotePropertyValue @()
        }
        if ($cache.PSObject.Properties.Name -notcontains "PlacementHistory") {
            $cache | Add-Member -NotePropertyName PlacementHistory -NotePropertyValue @()
        }
        if ($cache.PSObject.Properties.Name -notcontains "PlacementHistoryImportedAt") {
            $cache | Add-Member -NotePropertyName PlacementHistoryImportedAt -NotePropertyValue $null
        }
        return $cache
    }
    catch {
        Write-Warning "Could not parse tidy cache. A full scan will rebuild it."
        $fresh = New-EmptyTidyCache
        $fresh.ForceFullNextRun = $true
        return $fresh
    }
}

function Save-TidyCache {
    param($Cache)

    $Cache.LastScan = Get-NowIso
    $tmp = "$CachePath.tmp"
    $Cache | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath $tmp -Encoding UTF8
    Move-Item -LiteralPath $tmp -Destination $CachePath -Force
}

function Convert-CachedItems {
    param($Items)

    return @(
        foreach ($item in @($Items)) {
            [pscustomobject]@{
                VideoId = [string]$item.VideoId
                Title = [string]$item.Title
                Artist = [string]$item.Artist
                SetVideoId = [string]$item.SetVideoId
                InLibrary = $item.InLibrary
                AddLibraryToken = [string]$item.AddLibraryToken
                RemoveLibraryToken = [string]$item.RemoveLibraryToken
                SourceKind = [string]$item.SourceKind
                PlaylistId = [string]$item.PlaylistId
                PlaylistTitle = [string]$item.PlaylistTitle
                IdentityKey = [string]$item.IdentityKey
                LikeStatus = [string]$item.LikeStatus
                VideoType = [string]$item.VideoType
                AlbumBrowseId = [string]$item.AlbumBrowseId
            }
        }
    )
}

function Get-CachedPlaylistMap {
    param($Cache)

    $map = @{}
    foreach ($p in @($Cache.Playlists)) {
        if ($p.PlaylistId) {
            $map[[string]$p.PlaylistId] = $p
        }
    }
    return $map
}

function Get-ManagedPlaylistOrdinal {
    param([string]$Title)

    if ([string]$Title -match '^\s*(\d{1,2})\s*[·\-\u2013\u2014:]') {
        return [int]$matches[1]
    }
    return 9999
}

function Test-IsNumberedManagedPlaylistTitle {
    param([string]$Title)

    if ([string]::IsNullOrWhiteSpace($Title)) { return $false }

    return (
        [string]$Title -match
        '^\s*\d{1,2}\s*[·\-\u2013\u2014:]\s*\S'
    )
}

function Resolve-ManagedPlaylistMap {
    param(
        $Cache,
        $PlaylistIndex
    )

    $map = @{}
    $source = ""

    # 1) v27 cached discovery/import is authoritative for future runs.
    foreach ($entry in @($Cache.ManagedPlaylists)) {
        if ($entry.PlaylistId -and $entry.Title) {
            $map[[string]$entry.PlaylistId] = [string]$entry.Title
        }
    }
    if ($map.Count -gt 0) {
        $source = "tidy cache"
    }

    # 2) Optional one-time import from the old migration-state file.
    if ($map.Count -eq 0) {
        $state = Load-MigrationState
        if ($null -ne $state) {
            foreach ($entry in @($state.playlists)) {
                $title = [string]$entry.title
                $id = [string]$entry.id

                if ($id -and $title -and (Test-IsNumberedManagedPlaylistTitle $title)) {
                    $map[$id] = $title
                }
            }
            if ($map.Count -gt 0) {
                $source = "legacy migration state"
            }
        }
    }

    # 3) Generic/friend-friendly fallback: numbered playlist titles.
    if ($map.Count -eq 0 -and $null -ne $PlaylistIndex) {
        foreach ($entry in @($PlaylistIndex.Playlists)) {
            $title = [string]$entry.Title
            $id = [string]$entry.PlaylistId

            if ($id -and (Test-IsNumberedManagedPlaylistTitle $title)) {
                $map[$id] = $title
            }
        }
        if ($map.Count -gt 0) {
            $source = "numbered playlist auto-detection"
        }
    }

    if ($map.Count -eq 0) {
        if ($SuggestTasteLanes -and $ReportOnly) {
            Write-Warning @"
No numbered managed playlists exist yet.
Continuing because taste discovery is running in read-only mode.
Taste-lane suggestions will be generated without creating or moving anything.
"@
            $Cache.ManagedPlaylists = @()
            return @{}
        }

        throw @"
No managed playlists could be discovered.

v27 accepts any user playlist whose title begins with a number and separator,
for example:
  01 · Drift
  05 - Bass
  10: Classical

For a new/friend setup with no categories yet, run the script with no arguments
and use the console setup wizard, or use:
  -ReportOnly -SuggestTasteLanes

That mode is allowed to continue without managed playlists and produces
review-only metadata-based category suggestions.
"@
    }

    $entries = @(
        foreach ($id in @($map.Keys)) {
            [pscustomobject]@{
                PlaylistId = [string]$id
                Title = [string]$map[$id]
                Ordinal = (Get-ManagedPlaylistOrdinal ([string]$map[$id]))
            }
        }
    ) | Sort-Object Ordinal, Title

    $Cache.ManagedPlaylists = @(
        foreach ($entry in $entries) {
            [pscustomobject]@{
                PlaylistId = [string]$entry.PlaylistId
                Title = [string]$entry.Title
            }
        }
    )

    Write-ProgressLine ("Managed playlists: {0} discovered via {1}." -f `
        $map.Count, $source) Cyan

    return $map
}

function Get-KnownPlacementModel {
    param($Cache)

    $exact = @{}
    $artistCounts = @{}
    $history = [System.Collections.Generic.List[object]]::new()

    $cachedHistory = @($Cache.PlacementHistory)

    if ($cachedHistory.Count -gt 0) {
        foreach ($row in $cachedHistory) {
            $history.Add($row)
        }

        Write-ProgressLine ("Placement history: using {0} cached legacy mapping row(s)." -f `
            $cachedHistory.Count) Green
    }
    else {
        # Optional one-time consolidation from the two historical CSVs.
        if (Test-Path -LiteralPath $MigrationCsvPath) {
            foreach ($row in @(Import-Csv -LiteralPath $MigrationCsvPath -Encoding UTF8)) {
                $id = [string]$row.'Video ID'
                $playlist = [string]$row.'Primary Playlist'
                $artist = [string]$row.Artist

                if ($id -and $playlist) {
                    $history.Add([pscustomobject]@{
                        VideoId = $id
                        Playlist = $playlist
                        Artist = $artist
                        Source = "migration"
                    })
                }
            }
        }

        if (Test-Path -LiteralPath $AutoAssignmentsPath) {
            foreach ($row in @(Import-Csv -LiteralPath $AutoAssignmentsPath -Encoding UTF8)) {
                $id = [string]$row.'Video ID'
                $playlist = [string]$row.'Auto Playlist'
                $artist = [string]$row.Artist

                if ($id -and $playlist) {
                    $history.Add([pscustomobject]@{
                        VideoId = $id
                        Playlist = $playlist
                        Artist = $artist
                        Source = "auto-assignment"
                    })
                }
            }
        }

        if ($history.Count -gt 0) {
            $Cache.PlacementHistory = $history.ToArray()
            $Cache.PlacementHistoryImportedAt = Get-NowIso

            Write-ProgressLine ("Placement history consolidated into tidy cache: {0} row(s)." -f `
                $history.Count) Cyan
        }
        else {
            Write-ProgressLine "No legacy placement CSVs found; exact historical placement checks are unavailable." DarkYellow
        }
    }

    foreach ($row in @($history)) {
        $id = [string]$row.VideoId
        $playlist = [string]$row.Playlist
        $artist = Normalize-ArtistKey ([string]$row.Artist)

        # Only retain mappings that refer to a currently configured managed title.
        if ($id -and $playlist -and $DestinationTitles -contains $playlist) {
            $exact[$id] = [pscustomobject]@{
                Playlist = $playlist
                Source = [string]$row.Source
            }

            if ($artist) {
                if (-not $artistCounts.ContainsKey($artist)) {
                    $artistCounts[$artist] = @{}
                }
                if (-not $artistCounts[$artist].ContainsKey($playlist)) {
                    $artistCounts[$artist][$playlist] = 0
                }
                $artistCounts[$artist][$playlist]++
            }
        }
    }

    return [pscustomobject]@{
        Exact = $exact
        ArtistCounts = $artistCounts
    }
}

function Get-InferredPlaylistForArtist {
    param(
        [string]$Artist,
        $PlacementModel
    )

    $key = Normalize-ArtistKey $Artist
    if (-not $key -or -not $PlacementModel.ArtistCounts.ContainsKey($key)) {
        return $null
    }

    $counts = $PlacementModel.ArtistCounts[$key]
    $total = 0
    $bestTitle = ""
    $bestCount = 0

    foreach ($title in $counts.Keys) {
        $c = [int]$counts[$title]
        $total += $c
        if ($c -gt $bestCount) {
            $bestCount = $c
            $bestTitle = [string]$title
        }
    }

    if ($total -lt 2) { return $null }

    $share = [double]$bestCount / [double]$total
    if ($share -lt 0.60) { return $null }

    return [pscustomobject]@{
        Playlist = $bestTitle
        Count = $bestCount
        Total = $total
        Share = $share
    }
}

function Get-ItemBestLibraryState {
    param($Aggregate)

    if ($Aggregate.InLibraryTrue) { return "YES" }
    if ($Aggregate.InLibraryFalse) { return "NO" }
    if ($Aggregate.InLibraryByLibrarySet) { return "YES" }
    return "UNKNOWN"
}

function Get-PreviousMetadataIndex {
    param($OldCache)

    $map = @{}

    foreach ($section in @($OldCache.Likes, $OldCache.Library)) {
        if ($null -eq $section) { continue }
        foreach ($item in @($section.Items)) {
            if ($item.VideoId -and -not $map.ContainsKey([string]$item.VideoId)) {
                $map[[string]$item.VideoId] = [pscustomobject]@{
                    Title = [string]$item.Title
                    Artist = [string]$item.Artist
                }
            }
        }
    }

    foreach ($p in @($OldCache.Playlists)) {
        foreach ($item in @($p.Items)) {
            if ($item.VideoId -and -not $map.ContainsKey([string]$item.VideoId)) {
                $map[[string]$item.VideoId] = [pscustomobject]@{
                    Title = [string]$item.Title
                    Artist = [string]$item.Artist
                }
            }
        }
    }

    return $map
}



function Get-ExactTrackMatchesFromSearch {
    param(
        [string]$Query,
        [string]$VideoId,
        [string]$Params,
        [string]$ContextLabel,
        [ValidateSet("public", "library")]
        [string]$Scope = "public"
    )

    if ([string]::IsNullOrWhiteSpace($Query)) {
        return @()
    }

    $body = @{ query = $Query }
    if ($Params) {
        $body["params"] = $Params
    }

    $response = Invoke-YtmRequest `
        -Endpoint "search" `
        -Body $body `
        -ContextLabel $ContextLabel

    # ytmusicapi explicitly selects a tab for scoped search. Do the same here
    # instead of recursively parsing every tab, which could otherwise let a
    # public-catalogue result masquerade as a Library-scope positive.
    $parseNode = $response
    $tabs = @(
        Get-NestedValue $response @(
            "contents",
            "tabbedSearchResultsRenderer",
            "tabs"
        )
    )

    if ($tabs.Count -gt 0) {
        $tabIndex = if ($Scope -eq "library") { 1 } else { 0 }

        if ($tabIndex -lt $tabs.Count) {
            $tabContent = Get-NestedValue $tabs[$tabIndex] @(
                "tabRenderer",
                "content"
            )
            if ($null -ne $tabContent) {
                $parseNode = $tabContent
            }
        }
    }

    return @(
        Get-RendererTracksFromNode `
            -Node $parseNode `
            -SourceKind ("LibraryResolverSearch:{0}" -f $Scope) |
        Where-Object { [string]$_.VideoId -eq $VideoId }
    )
}

function Resolve-LibraryFromExactTrack {
    param(
        $Track,
        [string]$MethodPrefix
    )

    if ($null -eq $Track) { return $null }

    if ($Track.InLibrary -eq $true) {
        return [pscustomobject]@{
            State = "YES"
            AddLibraryToken = [string]$Track.AddLibraryToken
            RemoveLibraryToken = [string]$Track.RemoveLibraryToken
            Method = ("{0}: explicit BOOKMARK" -f $MethodPrefix)
            VideoType = [string]$Track.VideoType
            AlbumBrowseId = [string]$Track.AlbumBrowseId
        }
    }

    if ($Track.InLibrary -eq $false) {
        return [pscustomobject]@{
            State = "NO"
            AddLibraryToken = [string]$Track.AddLibraryToken
            RemoveLibraryToken = [string]$Track.RemoveLibraryToken
            Method = ("{0}: explicit BOOKMARK_BORDER" -f $MethodPrefix)
            VideoType = [string]$Track.VideoType
            AlbumBrowseId = [string]$Track.AlbumBrowseId
        }
    }

    return $null
}

function Resolve-LibraryFromAlbum {
    param(
        [string]$VideoId,
        [string]$AlbumBrowseId,
        [string]$Title
    )

    if ([string]::IsNullOrWhiteSpace($AlbumBrowseId)) {
        return $null
    }

    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = $AlbumBrowseId } `
        -ContextLabel ("checking album Library state for '{0}'" -f $Title)

    $matches = @(
        Get-RendererTracksFromNode `
            -Node $response `
            -SourceKind "LibraryResolverAlbum" |
        Where-Object { [string]$_.VideoId -eq $VideoId }
    )

    foreach ($match in $matches) {
        $resolved = Resolve-LibraryFromExactTrack `
            -Track $match `
            -MethodPrefix ("album {0}" -f $AlbumBrowseId)

        if ($null -ne $resolved) {
            return $resolved
        }
    }

    return $null
}


function Resolve-LibraryFromPlayer {
    param(
        [string]$VideoId,
        [bool]$LibrarySnapshotComplete = $false
    )

    if (-not $VideoId) { return $null }

    # ytmusicapi's current get_song() uses /player with video_id and a
    # signatureTimestamp equal to (days since Unix epoch - 1). We only need
    # videoDetails.musicVideoType; no streaming URL is consumed.
    $unixEpoch = [datetime]::SpecifyKind([datetime]"1970-01-01", [DateTimeKind]::Utc)
    $signatureTimestamp = [int][Math]::Floor(
        ((Get-Date).ToUniversalTime() - $unixEpoch).TotalDays
    ) - 1

    $response = Invoke-YtmRequest `
        -Endpoint "player" `
        -Body @{
            playbackContext = @{
                contentPlaybackContext = @{
                    signatureTimestamp = $signatureTimestamp
                }
            }
            video_id = $VideoId
        } `
        -ContextLabel ("exact player metadata for {0}" -f $VideoId)

    $returnedVideoId = [string](Get-NestedValue $response @("videoDetails", "videoId"))
    if ($returnedVideoId -and $returnedVideoId -ne $VideoId) {
        return $null
    }

    $videoType = [string](Get-NestedValue $response @("videoDetails", "musicVideoType"))
    if (-not $videoType) {
        return [pscustomobject]@{
            VideoId = $VideoId
            State = "UNKNOWN"
            AddLibraryToken = ""
            RemoveLibraryToken = ""
            Method = "exact /player response had no musicVideoType"
            ResolverRevision = $LibraryResolverRevision
            VideoType = ""
            AlbumBrowseId = ""
            Query = ""
            ResolvedAt = (Get-NowIso)
        }
    }

    if ($videoType -eq "MUSIC_VIDEO_TYPE_ATV") {
        $state = if ($LibrarySnapshotComplete) { "NO" } else { "UNKNOWN" }

        return [pscustomobject]@{
            VideoId = $VideoId
            State = $state
            AddLibraryToken = ""
            RemoveLibraryToken = ""
            Method = if ($LibrarySnapshotComplete) {
                "exact /player videoType ATV + absent from fresh complete Song Library"
            }
            else {
                "exact /player videoType ATV; Library snapshot incomplete"
            }
            ResolverRevision = $LibraryResolverRevision
            VideoType = $videoType
            AlbumBrowseId = ""
            Query = ""
            ResolvedAt = (Get-NowIso)
        }
    }

    if ($videoType -in @(
        "MUSIC_VIDEO_TYPE_OMV",
        "MUSIC_VIDEO_TYPE_UGC",
        "MUSIC_VIDEO_TYPE_OFFICIAL_SOURCE_MUSIC"
    )) {
        return [pscustomobject]@{
            VideoId = $VideoId
            State = "N/A"
            AddLibraryToken = ""
            RemoveLibraryToken = ""
            Method = ("exact /player videoType {0}" -f $videoType)
            ResolverRevision = $LibraryResolverRevision
            VideoType = $videoType
            AlbumBrowseId = ""
            Query = ""
            ResolvedAt = (Get-NowIso)
        }
    }

    # Preserve exact metadata for less-common types (e.g. private uploads),
    # but do not guess their Song Library semantics.
    return [pscustomobject]@{
        VideoId = $VideoId
        State = "UNKNOWN"
        AddLibraryToken = ""
        RemoveLibraryToken = ""
        Method = ("exact /player returned unclassified videoType {0}" -f $videoType)
        ResolverRevision = $LibraryResolverRevision
        VideoType = $videoType
        AlbumBrowseId = ""
        Query = ""
        ResolvedAt = (Get-NowIso)
    }
}

function Resolve-OneLibraryStatus {
    param(
        [string]$VideoId,
        [string]$Title,
        [string]$Artist,
        [string]$KnownVideoType = "",
        [string]$KnownAlbumBrowseId = "",
        [bool]$LibrarySnapshotComplete = $false
    )

    $primaryParts = @()
    if ($Artist) { $primaryParts += $Artist }
    if ($Title) { $primaryParts += $Title }

    $primaryQuery = ($primaryParts -join " ").Trim()
    if (-not $primaryQuery) { $primaryQuery = $VideoId }

    $titleQuery = ([string]$Title).Trim()
    $queries = [System.Collections.Generic.List[string]]::new()
    if ($primaryQuery) { $queries.Add($primaryQuery) }
    if ($titleQuery -and $titleQuery -ne $primaryQuery) { $queries.Add($titleQuery) }

    $knownType = ([string]$KnownVideoType).Trim()

    # Stage 0: decisive cached playlist metadata.
    if ($knownType -in @(
        "MUSIC_VIDEO_TYPE_OMV",
        "MUSIC_VIDEO_TYPE_UGC",
        "MUSIC_VIDEO_TYPE_OFFICIAL_SOURCE_MUSIC"
    )) {
        return [pscustomobject]@{
            VideoId = $VideoId
            State = "N/A"
            AddLibraryToken = ""
            RemoveLibraryToken = ""
            Method = ("cached playlist videoType {0}" -f $knownType)
            ResolverRevision = $LibraryResolverRevision
            VideoType = $knownType
            AlbumBrowseId = [string]$KnownAlbumBrowseId
            Query = ""
            ResolvedAt = (Get-NowIso)
        }
    }

    if ($LibrarySnapshotComplete -and $knownType -eq "MUSIC_VIDEO_TYPE_ATV") {
        return [pscustomobject]@{
            VideoId = $VideoId
            State = "NO"
            AddLibraryToken = ""
            RemoveLibraryToken = ""
            Method = "cached playlist videoType ATV + absent from fresh complete Song Library"
            ResolverRevision = $LibraryResolverRevision
            VideoType = $knownType
            AlbumBrowseId = [string]$KnownAlbumBrowseId
            Query = ""
            ResolvedAt = (Get-NowIso)
        }
    }

    # Stage 1: exact-ID /player metadata. Unlike search, this is not dependent
    # on indexing or query ranking and normally costs one request.
    $playerResult = Resolve-LibraryFromPlayer `
        -VideoId $VideoId `
        -LibrarySnapshotComplete $LibrarySnapshotComplete

    if (
        $null -ne $playerResult -and
        [string]$playerResult.State -in @("YES", "NO", "N/A")
    ) {
        return $playerResult
    }

    # Stage 2: filtered Videos search fallback. Retained for cases where
    # /player omits musicVideoType.
    foreach ($query in $queries) {
        $videoMatches = @(
            Get-ExactTrackMatchesFromSearch `
                -Query $query `
                -VideoId $VideoId `
                -Params $SearchVideosParams `
                -Scope "public" `
                -ContextLabel ("video-type fallback for '{0}'" -f $Title)
        )

        if ($videoMatches.Count -gt 0) {
            $match = $videoMatches[0]
            return [pscustomobject]@{
                VideoId = $VideoId
                State = "N/A"
                AddLibraryToken = ""
                RemoveLibraryToken = ""
                Method = ("filtered Videos fallback exact videoId '{0}'" -f $query)
                ResolverRevision = $LibraryResolverRevision
                VideoType = [string]$match.VideoType
                AlbumBrowseId = [string]$match.AlbumBrowseId
                Query = $query
                ResolvedAt = (Get-NowIso)
            }
        }
    }

    # Stage 3: filtered Songs fallback. Exact Video ID proves catalogue-song
    # identity; complete Library absence therefore gives a definite NO.
    foreach ($query in $queries) {
        $songMatches = @(
            Get-ExactTrackMatchesFromSearch `
                -Query $query `
                -VideoId $VideoId `
                -Params $SearchSongsParams `
                -Scope "public" `
                -ContextLabel ("song-type fallback for '{0}'" -f $Title)
        )

        if ($songMatches.Count -gt 0) {
            $match = $songMatches[0]
            $state = if ($LibrarySnapshotComplete) { "NO" } else { "UNKNOWN" }

            $method = if ($LibrarySnapshotComplete) {
                ("filtered Songs fallback exact videoId '{0}' + absent from fresh complete Song Library" -f $query)
            }
            else {
                ("filtered Songs fallback exact videoId '{0}', Library snapshot incomplete" -f $query)
            }

            $addToken = [string]$match.AddLibraryToken
            $removeToken = [string]$match.RemoveLibraryToken
            $albumId = [string]$match.AlbumBrowseId

            # Spend an album request only if it might recover an actionable
            # add-to-Library feedback token.
            if ($state -eq "NO" -and -not $addToken -and $albumId) {
                $albumResult = Resolve-LibraryFromAlbum `
                    -VideoId $VideoId `
                    -AlbumBrowseId $albumId `
                    -Title $Title

                if ($null -ne $albumResult) {
                    if ([string]$albumResult.AddLibraryToken) {
                        $addToken = [string]$albumResult.AddLibraryToken
                    }
                    if ([string]$albumResult.RemoveLibraryToken) {
                        $removeToken = [string]$albumResult.RemoveLibraryToken
                    }
                    $method += "; album checked for feedback token"
                }
            }

            return [pscustomobject]@{
                VideoId = $VideoId
                State = $state
                AddLibraryToken = $addToken
                RemoveLibraryToken = $removeToken
                Method = $method
                ResolverRevision = $LibraryResolverRevision
                VideoType = if ([string]$match.VideoType) {
                    [string]$match.VideoType
                }
                elseif ($null -ne $playerResult -and [string]$playerResult.VideoType) {
                    [string]$playerResult.VideoType
                }
                else {
                    "MUSIC_VIDEO_TYPE_ATV"
                }
                AlbumBrowseId = $albumId
                Query = $query
                ResolvedAt = (Get-NowIso)
            }
        }
    }

    $playerDetail = ""
    if ($null -ne $playerResult -and [string]$playerResult.Method) {
        $playerDetail = "; " + [string]$playerResult.Method
    }

    return [pscustomobject]@{
        VideoId = $VideoId
        State = "UNKNOWN"
        AddLibraryToken = ""
        RemoveLibraryToken = ""
        Method = ("v27 unresolved after exact /player + filtered-video + filtered-song checks{0}" -f $playerDetail)
        ResolverRevision = $LibraryResolverRevision
        VideoType = if ($null -ne $playerResult) { [string]$playerResult.VideoType } else { $knownType }
        AlbumBrowseId = [string]$KnownAlbumBrowseId
        Query = $primaryQuery
        ResolvedAt = (Get-NowIso)
    }
}

function Resolve-UnknownLibraryRows {
    param(
        $Rows,
        $ResolutionMap,
        [bool]$LibrarySnapshotComplete = $false
    )

    $pending = @(
        $Rows |
        Where-Object {
            if ($_.InLibrary -ne "UNKNOWN") { return $false }

            $id = [string]$_.VideoId
            if (-not $ResolutionMap.ContainsKey($id)) {
                return $true
            }

            $cached = $ResolutionMap[$id]
            $state = [string]$cached.State

            # Never repeat a decisive result. UNKNOWN entries from the older
            # v8 resolver are intentionally retried once under v9's stronger
            # strategy.
            if ($state -in @("YES", "NO", "N/A")) {
                return $false
            }

            $revision = 0
            if ($cached.PSObject.Properties.Name -contains "ResolverRevision") {
                $revision = [int]$cached.ResolverRevision
            }

            return ($revision -lt $LibraryResolverRevision)
        }
    )

    if ($ResolveLibraryLimit -gt 0) {
        $pending = @($pending | Select-Object -First $ResolveLibraryLimit)
    }

    if ($pending.Count -eq 0) {
        Write-ProgressLine "Deep Library resolver: nothing new to resolve under the current resolver revision." Green
        return
    }

    Write-Section "Deep Library resolver"
    Write-Host ("Resolver revision: {0}" -f $LibraryResolverRevision)
    Write-Host ("Pending this run:  {0}" -f $pending.Count)
    if ($ResolveLibraryLimit -gt 0) {
        Write-Host ("Per-run cap:       {0}" -f $ResolveLibraryLimit)
    }
    else {
        Write-Host "Per-run cap:       none"
    }
    Write-Host "Stages:"
    Write-Host "  0) cached playlist videoType -> immediate NO (ATV) or N/A (video)"
    Write-Host "  1) exact /player metadata by Video ID -> normally one-request classification"
    Write-Host "  2) filtered Videos/Songs search only if /player cannot classify"
    Write-Host "  3) optional album browse only to recover an ADD_LIBRARY feedback token"
    Write-Host ("Fresh complete Library snapshot available for negative inference: {0}" -f $LibrarySnapshotComplete)
    Write-Host "Each result is checkpointed immediately."

    $ordinal = 0
    foreach ($row in $pending) {
        $ordinal++
        Write-ProgressLine ("Resolver [{0}/{1}] {2} - {3}" -f `
            $ordinal, $pending.Count, [string]$row.Artist, [string]$row.Title) Cyan

        $resolved = Resolve-OneLibraryStatus `
            -VideoId ([string]$row.VideoId) `
            -Title ([string]$row.Title) `
            -Artist ([string]$row.Artist) `
            -KnownVideoType ([string]$row.VideoType) `
            -KnownAlbumBrowseId ([string]$row.AlbumBrowseId) `
            -LibrarySnapshotComplete $LibrarySnapshotComplete

        $ResolutionMap[[string]$row.VideoId] = $resolved
        Save-LibraryResolutionMap $ResolutionMap

        Write-ProgressLine ("    => {0}; add-token={1}; method={2}" -f `
            [string]$resolved.State,
            $(if ($resolved.AddLibraryToken) { "yes" } else { "no" }),
            [string]$resolved.Method) DarkCyan
    }
}


function Get-AlbumTrackMapForTokenEnrichment {
    param(
        [string]$AlbumBrowseId,
        [hashtable]$AlbumTrackCache
    )

    if ([string]::IsNullOrWhiteSpace($AlbumBrowseId)) {
        return $null
    }

    if ($AlbumTrackCache.ContainsKey($AlbumBrowseId)) {
        return $AlbumTrackCache[$AlbumBrowseId]
    }

    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = $AlbumBrowseId } `
        -ContextLabel ("token-enrichment album {0}" -f $AlbumBrowseId)

    $map = @{}
    foreach ($track in @(
        Get-RendererTracksFromNode `
            -Node $response `
            -SourceKind "LibraryTokenAlbum"
    )) {
        if ($track.VideoId) {
            $map[[string]$track.VideoId] = $track
        }
    }

    $AlbumTrackCache[$AlbumBrowseId] = $map
    return $map
}

function New-LibraryTokenResult {
    param(
        [string]$VideoId,
        [string]$State,
        [string]$AddLibraryToken = "",
        [string]$AlbumBrowseId = "",
        [string]$Method = ""
    )

    return [pscustomobject]@{
        VideoId = $VideoId
        State = $State
        AddLibraryToken = $AddLibraryToken
        AlbumBrowseId = $AlbumBrowseId
        Method = $Method
        TokenRevision = $LibraryTokenRevision
        AttemptedAt = (Get-NowIso)
    }
}

function Resolve-OneLibraryAddToken {
    param(
        $Row,
        [hashtable]$AlbumTrackCache
    )

    $videoId = [string]$Row.VideoId
    $title = [string]$Row.Title
    $artist = [string]$Row.Artist
    $knownAlbumId = [string]$Row.AlbumBrowseId

    if ($knownAlbumId) {
        $albumMap = Get-AlbumTrackMapForTokenEnrichment `
            -AlbumBrowseId $knownAlbumId `
            -AlbumTrackCache $AlbumTrackCache

        if ($null -ne $albumMap -and $albumMap.ContainsKey($videoId)) {
            $track = $albumMap[$videoId]

            if ($track.InLibrary -eq $true) {
                return New-LibraryTokenResult `
                    -VideoId $videoId `
                    -State "CONFLICT" `
                    -AlbumBrowseId $knownAlbumId `
                    -Method "album now reports the song already in Library"
            }

            if ([string]$track.AddLibraryToken) {
                return New-LibraryTokenResult `
                    -VideoId $videoId `
                    -State "AVAILABLE" `
                    -AddLibraryToken ([string]$track.AddLibraryToken) `
                    -AlbumBrowseId $knownAlbumId `
                    -Method "authenticated album exact Video ID"
            }
        }
    }

    $queries = [System.Collections.Generic.List[string]]::new()
    $primary = ((@($artist, $title) | Where-Object { $_ }) -join " ").Trim()
    if ($primary) { $queries.Add($primary) }
    if ($title -and $title -ne $primary) { $queries.Add($title) }

    foreach ($query in $queries) {
        $matches = @(
            Get-ExactTrackMatchesFromSearch `
                -Query $query `
                -VideoId $videoId `
                -Params $SearchSongsParams `
                -Scope "public" `
                -ContextLabel ("token-enrichment song search for '{0}'" -f $title)
        )

        if ($matches.Count -eq 0) { continue }

        $match = $matches[0]

        if ($match.InLibrary -eq $true) {
            return New-LibraryTokenResult `
                -VideoId $videoId `
                -State "CONFLICT" `
                -AlbumBrowseId ([string]$match.AlbumBrowseId) `
                -Method ("filtered Songs search '{0}' now reports in Library" -f $query)
        }

        if ([string]$match.AddLibraryToken) {
            return New-LibraryTokenResult `
                -VideoId $videoId `
                -State "AVAILABLE" `
                -AddLibraryToken ([string]$match.AddLibraryToken) `
                -AlbumBrowseId ([string]$match.AlbumBrowseId) `
                -Method ("filtered Songs exact Video ID '{0}'" -f $query)
        }

        $albumId = [string]$match.AlbumBrowseId
        if ($albumId) {
            $albumMap = Get-AlbumTrackMapForTokenEnrichment `
                -AlbumBrowseId $albumId `
                -AlbumTrackCache $AlbumTrackCache

            if ($null -ne $albumMap -and $albumMap.ContainsKey($videoId)) {
                $track = $albumMap[$videoId]

                if ($track.InLibrary -eq $true) {
                    return New-LibraryTokenResult `
                        -VideoId $videoId `
                        -State "CONFLICT" `
                        -AlbumBrowseId $albumId `
                        -Method ("search -> album {0} now reports in Library" -f $albumId)
                }

                if ([string]$track.AddLibraryToken) {
                    return New-LibraryTokenResult `
                        -VideoId $videoId `
                        -State "AVAILABLE" `
                        -AddLibraryToken ([string]$track.AddLibraryToken) `
                        -AlbumBrowseId $albumId `
                        -Method ("filtered Songs exact ID -> authenticated album {0}" -f $albumId)
                }
            }
        }
    }

    return New-LibraryTokenResult `
        -VideoId $videoId `
        -State "UNAVAILABLE" `
        -AlbumBrowseId $knownAlbumId `
        -Method "no add-to-Library feedback token exposed by known album or exact-ID Songs search"
}

function Enrich-LibraryAddTokens {
    param(
        $Rows,
        $TokenMap,
        [bool]$LibrarySnapshotComplete = $false
    )

    if (-not $LibrarySnapshotComplete) {
        Write-Warning "Library-token enrichment skipped because the Song Library snapshot is incomplete."
        return
    }

    $pending = @(
        $Rows |
        Where-Object {
            if ([string]$_.InLibrary -ne "NO") { return $false }
            if ([string]$_.LibraryAddTokenStatus -eq "AVAILABLE") { return $false }

            $id = [string]$_.VideoId
            if (-not $TokenMap.ContainsKey($id)) {
                return $true
            }

            $cached = $TokenMap[$id]
            $state = [string]$cached.State
            $revision = 0
            if ($cached.PSObject.Properties.Name -contains "TokenRevision") {
                $revision = [int]$cached.TokenRevision
            }

            if ($state -eq "AVAILABLE") { return $false }

            if ($RetryUnavailableLibraryTokens.IsPresent) {
                return $true
            }

            return ($revision -lt $LibraryTokenRevision)
        }
    )

    if ($EnrichLibraryTokenLimit -gt 0) {
        $pending = @($pending | Select-Object -First $EnrichLibraryTokenLimit)
    }

    if ($pending.Count -eq 0) {
        Write-ProgressLine "Library token enrichment: nothing new to attempt under the current token revision." Green
        return
    }

    Write-Section "Library add-token enrichment"
    Write-Host ("Token revision:      {0}" -f $LibraryTokenRevision)
    Write-Host ("Pending this run:    {0}" -f $pending.Count)
    if ($EnrichLibraryTokenLimit -gt 0) {
        Write-Host ("Per-run cap:         {0}" -f $EnrichLibraryTokenLimit)
    }
    else {
        Write-Host "Per-run cap:         none"
    }
    Write-Host "Stages:"
    Write-Host "  1) known authenticated album browse -> exact Video ID feedback token"
    Write-Host "  2) filtered Songs exact-ID search -> token and/or album ID"
    Write-Host "  3) discovered album browse -> exact Video ID feedback token"
    Write-Host "Only confirmed InLibrary=NO rows are eligible."
    Write-Host "Each result is checkpointed immediately. No Library mutation occurs here."

    $albumTrackCache = @{}
    $ordinal = 0

    foreach ($row in $pending) {
        $ordinal++
        Write-ProgressLine ("Token [{0}/{1}] {2} - {3}" -f `
            $ordinal, $pending.Count, [string]$row.Artist, [string]$row.Title) Cyan

        $result = Resolve-OneLibraryAddToken `
            -Row $row `
            -AlbumTrackCache $albumTrackCache

        $TokenMap[[string]$row.VideoId] = $result
        Save-LibraryTokenMap $TokenMap

        Write-ProgressLine ("    => {0}; token={1}; method={2}" -f `
            [string]$result.State,
            $(if ($result.AddLibraryToken) { "yes" } else { "no" }),
            [string]$result.Method) DarkCyan
    }
}

function Build-TidyReport {
    param(
        $Cache,
        $OldMetadata,
        $ManagedPlaylistMap,
        $PlacementModel,
        $LibraryResolutionMap,
        $LibraryTokenMap,
        $NonMusicPlaylistIdSet
    )

    $likedIds = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($item in @(Convert-CachedItems $Cache.Likes.Items)) {
        if ($item.VideoId) { [void]$likedIds.Add([string]$item.VideoId) }
    }

    $likesComplete = $false
    if (
        $null -ne $Cache.Likes -and
        $Cache.Likes.PSObject.Properties.Name -contains "Complete"
    ) {
        $likesComplete = [bool]$Cache.Likes.Complete
    }

    $libraryComplete = $false
    if (
        $null -ne $Cache.Library -and
        $Cache.Library.PSObject.Properties.Name -contains "Complete"
    ) {
        $libraryComplete = [bool]$Cache.Library.Complete
    }

    $libraryIds = [System.Collections.Generic.HashSet[string]]::new()
    foreach ($item in @(Convert-CachedItems $Cache.Library.Items)) {
        if ($item.VideoId) { [void]$libraryIds.Add([string]$item.VideoId) }
    }

    $agg = @{}

    function Add-AggregateItem {
        param(
            $Track,
            [string]$Kind,
            [string]$PlaylistId = "",
            [string]$PlaylistTitle = ""
        )

        $id = [string]$Track.VideoId
        if (-not $id) { return }

        if (-not $agg.ContainsKey($id)) {
            $agg[$id] = [pscustomobject]@{
                VideoId = $id
                Title = [string]$Track.Title
                Artist = [string]$Track.Artist
                IdentityKey = [string]$Track.IdentityKey
                Sources = [System.Collections.Generic.HashSet[string]]::new()
                Playlists = [System.Collections.Generic.List[string]]::new()
                PlaylistIds = [System.Collections.Generic.List[string]]::new()
                ManagedPlaylists = [System.Collections.Generic.List[string]]::new()
                ManagedPlaylistIds = [System.Collections.Generic.List[string]]::new()
                NonMusicPlaylists = [System.Collections.Generic.List[string]]::new()
                NonMusicPlaylistIds = [System.Collections.Generic.List[string]]::new()
                MusicContextPlaylists = [System.Collections.Generic.List[string]]::new()
                MusicContextPlaylistIds = [System.Collections.Generic.List[string]]::new()
                PlaylistEntries = [System.Collections.Generic.List[object]]::new()
                SeenLikedSource = $false
                SeenLibrarySource = $false
                InLibraryTrue = $false
                InLibraryFalse = $false
                InLibraryByLibrarySet = $false
                AddLibraryToken = ""
                VideoType = ""
                AlbumBrowseId = ""
                MetadataChanged = $false
            }
        }

        $a = $agg[$id]
        [void]$a.Sources.Add($Kind)

        if ($Kind -eq "Liked") {
            $a.SeenLikedSource = $true
        }
        elseif ($Kind -eq "Library") {
            $a.SeenLibrarySource = $true
        }

        if (-not $a.Title -and $Track.Title) { $a.Title = [string]$Track.Title }
        if (-not $a.Artist -and $Track.Artist) { $a.Artist = [string]$Track.Artist }
        if (-not $a.IdentityKey -and $Track.IdentityKey) { $a.IdentityKey = [string]$Track.IdentityKey }

        if ($Track.InLibrary -eq $true) { $a.InLibraryTrue = $true }
        elseif ($Track.InLibrary -eq $false) { $a.InLibraryFalse = $true }

        if ($Track.AddLibraryToken -and -not $a.AddLibraryToken) {
            $a.AddLibraryToken = [string]$Track.AddLibraryToken
        }

        if ($Track.VideoType -and -not $a.VideoType) {
            $a.VideoType = [string]$Track.VideoType
        }

        if ($Track.AlbumBrowseId -and -not $a.AlbumBrowseId) {
            $a.AlbumBrowseId = [string]$Track.AlbumBrowseId
        }

        if ($Kind -eq "Library") {
            $a.InLibraryByLibrarySet = $true
        }

        if ($PlaylistId) {
            $a.Playlists.Add($PlaylistTitle)
            $a.PlaylistIds.Add($PlaylistId)

            $isManagedPlaylist = $ManagedPlaylistMap.ContainsKey($PlaylistId)
            $isExcludedNonMusic = (
                -not $isManagedPlaylist -and
                $null -ne $NonMusicPlaylistIdSet -and
                $NonMusicPlaylistIdSet.Contains($PlaylistId)
            )

            if ($isManagedPlaylist) {
                $a.ManagedPlaylists.Add([string]$ManagedPlaylistMap[$PlaylistId])
                $a.ManagedPlaylistIds.Add($PlaylistId)
            }

            if ($isExcludedNonMusic) {
                $a.NonMusicPlaylists.Add($PlaylistTitle)
                $a.NonMusicPlaylistIds.Add($PlaylistId)
            }
            else {
                $a.MusicContextPlaylists.Add($PlaylistTitle)
                $a.MusicContextPlaylistIds.Add($PlaylistId)
            }

            $a.PlaylistEntries.Add([pscustomobject]@{
                PlaylistId = $PlaylistId
                PlaylistTitle = $PlaylistTitle
                SetVideoId = [string]$Track.SetVideoId
            })
        }

        if ($OldMetadata.ContainsKey($id)) {
            $old = $OldMetadata[$id]
            if (
                ([string]$old.Title -ne [string]$a.Title) -or
                ([string]$old.Artist -ne [string]$a.Artist)
            ) {
                $a.MetadataChanged = $true
            }
        }
    }

    foreach ($item in @(Convert-CachedItems $Cache.Likes.Items)) {
        Add-AggregateItem $item "Liked"
    }
    foreach ($item in @(Convert-CachedItems $Cache.Library.Items)) {
        Add-AggregateItem $item "Library"
    }
    foreach ($p in @($Cache.Playlists)) {
        foreach ($item in @(Convert-CachedItems $p.Items)) {
            Add-AggregateItem $item "Playlist" ([string]$p.PlaylistId) ([string]$p.PlaylistTitle)
        }
    }

    $identityMap = @{}
    foreach ($a in $agg.Values) {
        $key = [string]$a.IdentityKey
        if (-not $key) { continue }

        if (-not $identityMap.ContainsKey($key)) {
            $identityMap[$key] = [System.Collections.Generic.List[string]]::new()
        }
        $identityMap[$key].Add([string]$a.VideoId)
    }

    $reportRows = [System.Collections.Generic.List[object]]::new()
    $actions = [System.Collections.Generic.List[object]]::new()

    foreach ($a in $agg.Values) {
        $id = [string]$a.VideoId
        $likedState = if ($likedIds.Contains($id)) {
            "YES"
        }
        elseif ($likesComplete) {
            "NO"
        }
        else {
            "UNKNOWN"
        }
        $nonMusicNames = @($a.NonMusicPlaylists | Where-Object { $_ } | Select-Object -Unique)
        $nonMusicOnly = (
            $a.NonMusicPlaylistIds.Count -gt 0 -and
            $a.MusicContextPlaylistIds.Count -eq 0 -and
            -not $a.SeenLikedSource -and
            -not $a.SeenLibrarySource -and
            $a.ManagedPlaylistIds.Count -eq 0
        )

        $libraryState = Get-ItemBestLibraryState $a
        $libraryBasis = "unknown"

        if ($nonMusicOnly) {
            $libraryState = "N/A"
            $scopeText = if ($nonMusicNames.Count -gt 0) {
                $nonMusicNames -join " | "
            }
            else {
                @($a.NonMusicPlaylistIds | Select-Object -Unique) -join " | "
            }
            $libraryBasis = ("excluded non-music playlist only: {0}" -f $scopeText)
        }
        elseif ($a.InLibraryTrue -or $a.InLibraryFalse) {
            $libraryBasis = "playlist/menu Library toggle"
        }
        elseif ($a.InLibraryByLibrarySet) {
            $libraryBasis = "fresh Song Library exact Video ID set"
        }

        if (-not $nonMusicOnly -and $libraryState -eq "UNKNOWN" -and $libraryComplete) {
            $vt = ([string]$a.VideoType).Trim()

            if ($vt -eq "MUSIC_VIDEO_TYPE_ATV") {
                $libraryState = "NO"
                $libraryBasis = "ATV catalogue song absent from complete Song Library"
            }
            elseif ($vt -in @(
                "MUSIC_VIDEO_TYPE_OMV",
                "MUSIC_VIDEO_TYPE_UGC",
                "MUSIC_VIDEO_TYPE_OFFICIAL_SOURCE_MUSIC"
            )) {
                $libraryState = "N/A"
                $libraryBasis = ("videoType {0}" -f $vt)
            }
        }

        if (
            -not $nonMusicOnly -and
            $libraryState -eq "UNKNOWN" -and
            $null -ne $LibraryResolutionMap -and
            $LibraryResolutionMap.ContainsKey($id)
        ) {
            $resolvedLibrary = $LibraryResolutionMap[$id]
            if ([string]$resolvedLibrary.State -in @("YES", "NO", "N/A")) {
                $libraryState = [string]$resolvedLibrary.State
                $libraryBasis = ("resolver cache: {0}" -f [string]$resolvedLibrary.Method)
                if (-not $a.AddLibraryToken -and [string]$resolvedLibrary.AddLibraryToken) {
                    $a.AddLibraryToken = [string]$resolvedLibrary.AddLibraryToken
                }
            }
        }

        $libraryTokenStatus = "NOT_APPLICABLE"
        $libraryTokenMethod = ""

        if ($libraryState -eq "NO") {
            if ($a.AddLibraryToken) {
                $libraryTokenStatus = "AVAILABLE"
                $libraryTokenMethod = "playlist/menu or resolver result"
            }
            elseif (
                $null -ne $LibraryTokenMap -and
                $LibraryTokenMap.ContainsKey($id)
            ) {
                $tokenResult = $LibraryTokenMap[$id]
                $tokenState = [string]$tokenResult.State
                $libraryTokenMethod = [string]$tokenResult.Method

                if ($tokenState -eq "AVAILABLE" -and [string]$tokenResult.AddLibraryToken) {
                    $a.AddLibraryToken = [string]$tokenResult.AddLibraryToken
                    $libraryTokenStatus = "AVAILABLE"
                }
                elseif ($tokenState -eq "CONFLICT") {
                    $libraryTokenStatus = "CONFLICT"
                }
                elseif ($tokenState -eq "UNAVAILABLE") {
                    $libraryTokenStatus = "UNAVAILABLE"
                }
                else {
                    $libraryTokenStatus = "NOT_CHECKED"
                }
            }
            else {
                $libraryTokenStatus = "NOT_CHECKED"
            }
        }

        $expected = ""
        $expectedSource = ""
        $placementStatus = ""
        $placementConfidence = ""
        $inferred = $null

        if ($PlacementModel.Exact.ContainsKey($id)) {
            $expected = [string]$PlacementModel.Exact[$id].Playlist
            $expectedSource = [string]$PlacementModel.Exact[$id].Source
            $placementConfidence = "EXACT"

            $managed = @($a.ManagedPlaylists | Select-Object -Unique)
            if ($managed.Count -eq 0) {
                $placementStatus = "MISSING EXPECTED PLAYLIST"
            }
            elseif ($managed -contains $expected) {
                $wrong = @($managed | Where-Object { $_ -ne $expected })
                if ($wrong.Count -gt 0) {
                    $placementStatus = "EXPECTED + EXTRA MANAGED PLAYLIST"
                }
                else {
                    $placementStatus = "OK"
                }
            }
            else {
                $placementStatus = "WRONG MANAGED PLAYLIST"
            }
        }
        else {
            $inferred = Get-InferredPlaylistForArtist $a.Artist $PlacementModel
            if ($null -ne $inferred) {
                $expected = [string]$inferred.Playlist
                $expectedSource = ("artist-history {0}/{1}" -f $inferred.Count, $inferred.Total)
                $placementConfidence = ("INFERRED {0:P0}" -f $inferred.Share)

                $managed = @($a.ManagedPlaylists | Select-Object -Unique)
                if ($managed.Count -eq 0) {
                    $placementStatus = "POTENTIAL MISSING PLAYLIST"
                }
                elseif ($managed -contains $expected) {
                    $placementStatus = "LIKELY OK"
                }
                else {
                    $placementStatus = "POTENTIALLY WRONG PLAYLIST"
                }
            }
            else {
                $placementStatus = "NO KNOWN EXPECTATION"
            }
        }

        $alternateIds = @()
        if ($a.IdentityKey -and $identityMap.ContainsKey([string]$a.IdentityKey)) {
            $alternateIds = @(
                $identityMap[[string]$a.IdentityKey] |
                Where-Object { $_ -ne $id } |
                Select-Object -Unique
            )
        }

        # A catalogue song can have multiple Video IDs. If an exact-identity
        # alternate ID is already in the fresh Song Library, adding this exact
        # ID would create a semantic duplicate even though exact-ID membership
        # is NO. Keep the diagnostic NO state, but suppress the mutation.
        $libraryAlternateIds = @(
            foreach ($alternateId in $alternateIds) {
                if (-not $agg.ContainsKey([string]$alternateId)) { continue }

                $alternateAggregate = $agg[[string]$alternateId]
                if (
                    $alternateAggregate.InLibraryByLibrarySet -or
                    $alternateAggregate.InLibraryTrue
                ) {
                    [string]$alternateId
                }
            }
        )

        if (
            $libraryState -eq "NO" -and
            $libraryAlternateIds.Count -gt 0
        ) {
            $libraryTokenStatus = "ALTERNATE_IN_LIBRARY"
            $libraryTokenMethod = (
                "alternate exact-identity Video ID already present in fresh Song Library: {0}" -f
                ($libraryAlternateIds -join " | ")
            )
        }

        $metadataStatus = "Same / no cached change detected"
        if ($a.MetadataChanged) {
            $metadataStatus = "Metadata changed since previous snapshot"
        }
        if ($alternateIds.Count -gt 0) {
            if ($a.MetadataChanged) {
                $metadataStatus += "; possible alternate/updated Video ID also exists"
            }
            else {
                $metadataStatus = "Possible alternate/updated Video ID also exists"
            }
        }

        $playlistNames = @($a.Playlists | Select-Object -Unique)
        $managedNames = @($a.ManagedPlaylists | Select-Object -Unique)
        $sources = @($a.Sources | Sort-Object)

        $suggestedActions = [System.Collections.Generic.List[string]]::new()

        if ($libraryState -eq "NO") {
            if ($libraryAlternateIds.Count -gt 0) {
                $suggestedActions.Add("LIBRARY_ALTERNATE_PRESENT")
            }
            elseif ($a.AddLibraryToken) {
                $suggestedActions.Add("ADD_LIBRARY")
                $actions.Add([pscustomobject]@{
                    Action = "ADD_LIBRARY"
                    VideoId = $id
                    Title = $a.Title
                    Artist = $a.Artist
                    FromPlaylist = ""
                    FromPlaylistId = ""
                    SetVideoId = ""
                    ToPlaylist = ""
                    ToPlaylistId = ""
                    FeedbackToken = [string]$a.AddLibraryToken
                    Reason = "Song is not in Library"
                })
            }
            else {
                if ($libraryTokenStatus -eq "CONFLICT") {
                    $suggestedActions.Add("LIBRARY_TOKEN_CONFLICT")
                }
                elseif ($libraryTokenStatus -eq "UNAVAILABLE") {
                    $suggestedActions.Add("LIBRARY_TOKEN_UNAVAILABLE")
                }
                else {
                    $suggestedActions.Add("LIBRARY_TOKEN_NOT_CHECKED")
                }
            }
        }

        if ($likedState -eq "NO") {
            $suggestedActions.Add("OPTIONAL_LIKE")
            if ($IncludeLikeActions) {
                $actions.Add([pscustomobject]@{
                    Action = "LIKE"
                    VideoId = $id
                    Title = $a.Title
                    Artist = $a.Artist
                    FromPlaylist = ""
                    FromPlaylistId = ""
                    SetVideoId = ""
                    ToPlaylist = ""
                    ToPlaylistId = ""
                    FeedbackToken = ""
                    Reason = "Not currently in Liked Music"
                })
            }
        }

        if ($PlacementModel.Exact.ContainsKey($id)) {
            $expectedTitle = [string]$PlacementModel.Exact[$id].Playlist
            $expectedId = ""
            foreach ($playlistIdKey in $ManagedPlaylistMap.Keys) {
                if ([string]$ManagedPlaylistMap[$playlistIdKey] -eq $expectedTitle) {
                    $expectedId = [string]$playlistIdKey
                    break
                }
            }

            $managedEntries = @(
                $a.PlaylistEntries |
                Where-Object { $ManagedPlaylistMap.ContainsKey([string]$_.PlaylistId) }
            )

            $hasExpected = @(
                $managedEntries |
                Where-Object { [string]$ManagedPlaylistMap[[string]$_.PlaylistId] -eq $expectedTitle }
            ).Count -gt 0

            if (-not $hasExpected -and $expectedId) {
                $suggestedActions.Add("ADD_EXPECTED_PLAYLIST")
                $actions.Add([pscustomobject]@{
                    Action = "ADD_EXPECTED_PLAYLIST"
                    VideoId = $id
                    Title = $a.Title
                    Artist = $a.Artist
                    FromPlaylist = ""
                    FromPlaylistId = ""
                    SetVideoId = ""
                    ToPlaylist = $expectedTitle
                    ToPlaylistId = $expectedId
                    FeedbackToken = ""
                    Reason = ("Exact historical mapping from {0}" -f $PlacementModel.Exact[$id].Source)
                })
            }

            foreach ($entry in $managedEntries) {
                $currentTitle = [string]$ManagedPlaylistMap[[string]$entry.PlaylistId]
                if ($currentTitle -eq $expectedTitle) { continue }

                # Never remove from protected originals even if they were somehow
                # discovered in the managed map.
                if (
                    $ProtectedPlaylistIds -contains [string]$entry.PlaylistId -or
                    $ProtectedPlaylistTitles -contains [string]$entry.PlaylistTitle
                ) {
                    continue
                }

                if ($entry.SetVideoId) {
                    $suggestedActions.Add("REMOVE_WRONG_MANAGED_PLAYLIST")
                    $actions.Add([pscustomobject]@{
                        Action = "REMOVE_WRONG_MANAGED_PLAYLIST"
                        VideoId = $id
                        Title = $a.Title
                        Artist = $a.Artist
                        FromPlaylist = $currentTitle
                        FromPlaylistId = [string]$entry.PlaylistId
                        SetVideoId = [string]$entry.SetVideoId
                        ToPlaylist = $expectedTitle
                        ToPlaylistId = $expectedId
                        FeedbackToken = ""
                        Reason = ("Exact historical mapping expects {0}" -f $expectedTitle)
                    })
                }
            }
        }

        $reportRows.Add([pscustomobject]@{
            VideoId = $id
            Title = [string]$a.Title
            Artist = [string]$a.Artist
            VideoType = [string]$a.VideoType
            AlbumBrowseId = [string]$a.AlbumBrowseId
            Liked = $likedState
            InLibrary = $libraryState
            LibraryStatusBasis = $libraryBasis
            LibraryAddTokenStatus = $libraryTokenStatus
            LibraryTokenMethod = $libraryTokenMethod
            NonMusicOnly = $(if ($nonMusicOnly) { "YES" } else { "NO" })
            ExcludedNonMusicPlaylists = ($nonMusicNames -join " | ")
            Sources = ($sources -join " + ")
            Playlists = ($playlistNames -join " | ")
            ManagedPlaylists = ($managedNames -join " | ")
            ExpectedPlaylist = $expected
            ExpectedSource = $expectedSource
            PlacementConfidence = $placementConfidence
            PlacementStatus = $placementStatus
            MetadataStatus = $metadataStatus
            AlternateVideoIds = ($alternateIds -join " | ")
            SuggestedActions = (@($suggestedActions | Select-Object -Unique) -join " | ")
            URL = ("https://music.youtube.com/watch?v={0}" -f $id)
        })
    }

    return [pscustomobject]@{
        ReportRows = @($reportRows | Sort-Object Artist, Title, VideoId)
        Actions = @($actions)
    }
}

function Invoke-FeedbackBatch {
    param(
        [string[]]$Tokens,
        [string]$Label
    )

    if (@($Tokens).Count -eq 0) { return }

    $index = 0
    while ($index -lt $Tokens.Count) {
        $take = [Math]::Min($BatchSize, $Tokens.Count - $index)
        if ($take -eq 1) {
            $batch = @($Tokens[$index])
        }
        else {
            $batch = @($Tokens[$index..($index + $take - 1)])
        }

        [void](Invoke-YtmRequest `
            -Endpoint "feedback" `
            -Body @{ feedbackTokens = $batch } `
            -ContextLabel $Label)

        $index += $take

        if ($BatchDelaySeconds -gt 0 -and $index -lt $Tokens.Count) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 1000)) -Phase "batch delay"
        }
    }
}

function Invoke-LikeBatch {
    param($Rows)

    $index = 0
    foreach ($row in @($Rows)) {
        $index++
        [void](Invoke-YtmRequest `
            -Endpoint "like/like" `
            -Body @{ target = @{ videoId = [string]$row.VideoId } } `
            -ContextLabel ("liking song {0}/{1}" -f $index, @($Rows).Count))

        if ($BatchDelaySeconds -gt 0 -and $index -lt @($Rows).Count) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 500)) -Phase "batch delay"
        }
    }
}

function Invoke-RemovePlaylistBatch {
    param(
        [string]$PlaylistId,
        [string]$PlaylistTitle,
        $Rows
    )

    $actions = [System.Collections.Generic.List[object]]::new()
    foreach ($row in @($Rows)) {
        if (-not $row.SetVideoId -or -not $row.VideoId) { continue }

        $actions.Add(@{
            action = "ACTION_REMOVE_VIDEO"
            setVideoId = [string]$row.SetVideoId
            removedVideoId = [string]$row.VideoId
        })
    }

    if ($actions.Count -eq 0) { return }

    $index = 0
    while ($index -lt $actions.Count) {
        $take = [Math]::Min($BatchSize, $actions.Count - $index)
        if ($take -eq 1) {
            $batch = @($actions[$index])
        }
        else {
            $batch = @($actions[$index..($index + $take - 1)])
        }

        $response = Invoke-YtmRequest `
            -Endpoint "browse/edit_playlist" `
            -Body @{
                playlistId = $PlaylistId
                actions = $batch
            } `
            -ContextLabel ("removing wrong placements from '{0}'" -f $PlaylistTitle)

        $status = Get-NestedValue $response @("status")
        if ($status -and [string]$status -notmatch 'SUCCEEDED') {
            throw ("Playlist removal failed for '{0}' with status {1}" -f $PlaylistTitle, $status)
        }

        $index += $take

        if ($BatchDelaySeconds -gt 0 -and $index -lt $actions.Count) {
            Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 1000)) -Phase "batch delay"
        }
    }
}


function Get-ManagedPlaylistDesiredDescription {
    param(
        [string]$PlaylistTitle
    )

    # Keep the old Main/Other tone and two-line structure, but make the source
    # wording accurate for the managed playlists and keep everything lowercase.
    $descriptions = @{
        "01 · Drift" = @(
            "split from main/other - calm / drifting / low-energy music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "02 · Night" = @(
            "split from main/other - dark / late-night / moody music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "03 · Melodic" = @(
            "split from main/other - melodic / emotional music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "04 · Bright" = @(
            "split from main/other - bright / upbeat music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "05 · Bass" = @(
            "split from main/other - bass-heavy / dubstep and related music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "06 · Arcade" = @(
            "split from main/other - electronic / game-like / energetic music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "07 · Rock & Alt" = @(
            "split from main/other - rock / alternative music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
        "08 · Pop & Vocal" = @(
            "split from main/other - pop / vocal-focused music",
            "if there is a song that doesn't match the playlist category let me know!"
        )
    }

    if ($descriptions.ContainsKey($PlaylistTitle)) {
        return ($descriptions[$PlaylistTitle] -join "`n")
    }

    $label = [string]$PlaylistTitle
    $label = $label -replace '^\s*\d{1,2}\s*[·\-–—:]\s*', ''
    $label = $label.Trim().ToLowerInvariant()

    if (-not $label) {
        $label = "music"
    }

    return @(
        ("organized by taste - {0}" -f $label),
        "if there is a song that doesn't match the playlist category let me know!"
    ) -join "`n"
}

function Get-PlaylistEditableSettingsSnapshot {
    param(
        [string]$PlaylistId,
        [string]$PlaylistTitle
    )

    $browseId = if ($PlaylistId.StartsWith("VL")) { $PlaylistId } else { "VL$PlaylistId" }

    $response = Invoke-YtmRequest `
        -Endpoint "browse" `
        -Body @{ browseId = $browseId } `
        -ContextLabel ("reading settings for managed playlist '{0}'" -f $PlaylistTitle)

    # Current ytmusicapi get_playlist() path for an owned playlist:
    #
    # contents.twoColumnBrowseResultsRenderer.tabs[0].tabRenderer.content
    #   .sectionListRenderer.contents[0]
    #   .musicEditablePlaylistDetailHeaderRenderer
    #
    # Its responsive header then carries the actual playlist description under:
    # header.musicResponsiveHeaderRenderer.description.musicDescriptionShelfRenderer
    #
    # Use this exact path first instead of a global "first shelf" search.
    $headerData = Get-NestedValue $response @(
        "contents",
        "twoColumnBrowseResultsRenderer",
        "tabs",
        0,
        "tabRenderer",
        "content",
        "sectionListRenderer",
        "contents",
        0
    )

    $editable = Get-NestedValue $headerData @(
        "musicEditablePlaylistDetailHeaderRenderer"
    )

    if ($null -eq $editable) {
        throw ("Managed playlist '{0}' did not expose an editable playlist header. " +
            "Refusing to guess its settings layout.") -f $PlaylistTitle
    }

    $privacy = [string](Get-NestedValue $editable @(
        "editHeader",
        "musicPlaylistEditHeaderRenderer",
        "privacy"
    ))
    if (-not $privacy) {
        $privacy = "UNKNOWN"
    }

    $responsiveHeader = Get-NestedValue $editable @(
        "header",
        "musicResponsiveHeaderRenderer"
    )
    if ($null -eq $responsiveHeader) {
        throw ("Managed playlist '{0}' did not expose musicResponsiveHeaderRenderer. " +
            "Refusing to guess its description/collaboration state.") -f $PlaylistTitle
    }

    $descriptionShelf = Get-NestedValue $responsiveHeader @(
        "description",
        "musicDescriptionShelfRenderer"
    )

    $description = ""
    $descriptionFound = $false
    if ($null -ne $descriptionShelf) {
        $runs = Get-NestedValue $descriptionShelf @("description", "runs")
        if ($null -ne $runs) {
            $description = [string](Get-TextRuns $runs)
            $descriptionFound = $true
        }
    }

    # Current ytmusicapi parse_playlist_header_meta() identifies collaboration
    # by the facepile's PAplaylist_collaborate engagement-panel tag.
    $collaborationTag = [string](Get-NestedValue $responsiveHeader @(
        "facepile",
        "avatarStackViewModel",
        "rendererContext",
        "commandContext",
        "onTap",
        "innertubeCommand",
        "showEngagementPanelEndpoint",
        "identifier",
        "tag"
    ))

    $isCollaborative = ($collaborationTag -eq "PAplaylist_collaborate")

    return [pscustomobject]@{
        PlaylistId = $PlaylistId
        PlaylistTitle = $PlaylistTitle
        Privacy = $privacy
        Description = $description
        DescriptionShelfFound = $descriptionFound
        IsCollaborative = $isCollaborative
        CollaborationTag = $collaborationTag
    }
}

function Get-ManagedPlaylistSettingsAudit {
    param($ManagedPlaylistMap)

    $rows = [System.Collections.Generic.List[object]]::new()

    foreach ($title in $DestinationTitles) {
        $playlistIdKey = ""

        foreach ($candidateId in @($ManagedPlaylistMap.Keys)) {
            if ([string]$ManagedPlaylistMap[$candidateId] -eq $title) {
                $playlistIdKey = [string]$candidateId
                break
            }
        }

        if (-not $playlistIdKey) {
            throw ("Managed playlist ID is missing from migration state: {0}" -f $title)
        }

        $current = Get-PlaylistEditableSettingsSnapshot `
            -PlaylistId $playlistIdKey `
            -PlaylistTitle $title

        $desiredDescription = Get-ManagedPlaylistDesiredDescription -PlaylistTitle $title

        $rows.Add([pscustomobject]@{
            PlaylistTitle = $title
            PlaylistId = $playlistIdKey
            CurrentPrivacy = [string]$current.Privacy
            DesiredPrivacy = "PUBLIC"
            CurrentDescription = [string]$current.Description
            DesiredDescription = $desiredDescription
            DescriptionShelfFound = [bool]$current.DescriptionShelfFound
            DescriptionWillChange = (
                [string]$current.Description -cne [string]$desiredDescription
            )
            DesiredVoting = "EVERYONE"
            VotingRequestValue = 1
            CurrentVoting = "NOT_RELIABLY_EXPOSED"
            CurrentCollaboration = $(if ([bool]$current.IsCollaborative) { "ENABLED" } else { "DISABLED" })
            DesiredCollaboration = "ENABLED"
            CollaborationWillChange = (-not [bool]$current.IsCollaborative)
            SortOrder = "LEAVE_UNCHANGED"
            AddToTop = "LEAVE_UNCHANGED"
        })
    }

    return $rows.ToArray()
}

function Show-ManagedPlaylistSettingsAudit {
    param($Rows)

    Write-Section "Managed playlist settings audit"
    Write-Host "Explicit target policy (does not read Main/Other):" -ForegroundColor Cyan
    Write-Host "  Visibility: PUBLIC"
    Write-Host "  Description: fixed two-line, lowercase text modeled after the old Main/Other descriptions"
    Write-Host "  Voting: EVERYONE"
    Write-Host "  Collaboration: ENABLED"
    Write-Host "  Sort order: unchanged"
    Write-Host "  Add-to-top: unchanged"
    Write-Host ""

    foreach ($row in @($Rows)) {
        Write-Host ("  {0}" -f [string]$row.PlaylistTitle) -ForegroundColor Cyan
        Write-Host ("    privacy: {0} -> PUBLIC" -f [string]$row.CurrentPrivacy)
        Write-Host ("    collaboration: {0} -> ENABLED" -f [string]$row.CurrentCollaboration)
        Write-Host ("    description change: {0}" -f `
            $(if ([bool]$row.DescriptionWillChange) { "YES" } else { "no" }))
        Write-Host ("    current description: {0}" -f `
            $(if ([string]$row.CurrentDescription) { '"' + [string]$row.CurrentDescription + '"' } else { "<empty>" }))
        Write-Host ("    desired description: {0}" -f `
            $(if ([string]$row.DesiredDescription) { '"' + [string]$row.DesiredDescription + '"' } else { "<empty>" }))
        Write-Host "    voting: -> EVERYONE (current permission is not reliably exposed by browse)"
    }

    Write-Host ""
    Write-Host ("Playlist settings report: {0}" -f $PlaylistSettingsReportPath) -ForegroundColor Green
}

function Invoke-ManagedPlaylistSettingsNormalization {
    param($Rows)

    $applied = 0
    $collaborationEnabled = 0

    foreach ($row in @($Rows)) {
        $playlistIdKey = [string]$row.PlaylistId
        $title = [string]$row.PlaylistTitle

        $actions = [System.Collections.Generic.List[object]]::new()

        # Keep the explicit policy idempotent.
        if ([string]$row.CurrentPrivacy -ne "PUBLIC") {
            $actions.Add(@{
                action = "ACTION_SET_PLAYLIST_PRIVACY"
                playlistPrivacy = "PUBLIC"
            })
        }

        if ([bool]$row.DescriptionWillChange) {
            $actions.Add(@{
                action = "ACTION_SET_PLAYLIST_DESCRIPTION"
                playlistDescription = [string]$row.DesiredDescription
            })
        }

        # Current ytmusicapi enables collaboration with
        # ACTION_CREATE_COLLABORATION_INVITE_LINK. Only issue it for playlists
        # that are not already collaborative, so repeat runs do not needlessly
        # create/rotate collaboration invite links.
        if ([bool]$row.CollaborationWillChange) {
            $actions.Add(@{
                action = "ACTION_CREATE_COLLABORATION_INVITE_LINK"
            })
        }

        # Voting isn't reliably exposed by the normal browse response, so set
        # the requested policy every time normalization is explicitly invoked.
        $actions.Add(@{
            action = "ACTION_SET_ALLOW_ITEM_VOTE"
            itemVotePermission = 1
        })

        $response = Invoke-YtmRequest `
            -Endpoint "browse/edit_playlist" `
            -Body @{
                playlistId = $playlistIdKey
                actions = $actions.ToArray()
            } `
            -ContextLabel ("normalizing settings for managed playlist '{0}'" -f $title)

        $status = [string](Get-NestedValue $response @("status"))
        if (-not $status -or $status -notmatch "SUCCEEDED") {
            throw ("Playlist settings update failed for '{0}' (status='{1}')." -f `
                $title, $status)
        }

        if ([bool]$row.CollaborationWillChange) {
            $collaborationEnabled++
        }

        $applied++
        Write-ProgressLine ("    settings accepted: {0}" -f $title) Green
    }

    return [pscustomobject]@{
        Applied = $applied
        CollaborationEnabled = $collaborationEnabled
    }
}

function Test-ManagedPlaylistSettingsVerification {
    param(
        $BeforeRows,
        $AfterRows
    )

    $beforeMap = @{}
    foreach ($row in @($BeforeRows)) {
        $beforeMap[[string]$row.PlaylistId] = $row
    }

    $failures = [System.Collections.Generic.List[string]]::new()

    foreach ($after in @($AfterRows)) {
        $playlistIdKey = [string]$after.PlaylistId
        if (-not $beforeMap.ContainsKey($playlistIdKey)) {
            $failures.Add(("post-write verification returned an unexpected playlist: {0}" -f `
                [string]$after.PlaylistTitle))
            continue
        }

        $before = $beforeMap[$playlistIdKey]

        if ([string]$after.CurrentPrivacy -ne "PUBLIC") {
            $failures.Add(("{0}: privacy is '{1}', expected PUBLIC" -f `
                [string]$after.PlaylistTitle, [string]$after.CurrentPrivacy))
        }

        if ([string]$after.CurrentDescription -cne [string]$before.DesiredDescription) {
            $failures.Add(("{0}: description did not match the normalized value" -f `
                [string]$after.PlaylistTitle))
        }

        if ([string]$after.CurrentCollaboration -ne "ENABLED") {
            $failures.Add(("{0}: collaboration is '{1}', expected ENABLED" -f `
                [string]$after.PlaylistTitle, [string]$after.CurrentCollaboration))
        }
    }

    return $failures.ToArray()
}


function Get-TasteLaneSuggestions {
    param(
        $Cache,
        [int]$MaxCount
    )

    if ($MaxCount -lt 1) { $MaxCount = 8 }

    # This is deliberately metadata-only. It does NOT inspect/listen to audio.
    # Playlist names are weighted strongly because they are direct user labels;
    # song/artist text is only a weak secondary signal.
    $corpus = [System.Collections.Generic.List[object]]::new()

    foreach ($p in @($Cache.PlaylistIndex.Playlists)) {
        if ($p.Title) {
            $corpus.Add([pscustomobject]@{
                Text = ([string]$p.Title).ToLowerInvariant()
                Weight = 8
                Source = "playlist title"
            })
        }
    }

    foreach ($snapshot in @($Cache.Likes, $Cache.Library)) {
        if ($null -eq $snapshot) { continue }

        foreach ($item in @($snapshot.Items)) {
            $combined = ("{0} {1}" -f [string]$item.Title, [string]$item.Artist).ToLowerInvariant()
            if ($combined.Trim()) {
                $corpus.Add([pscustomobject]@{
                    Text = $combined
                    Weight = 1
                    Source = "track metadata"
                })
            }
        }
    }

    $lanes = @(
        [pscustomobject]@{
            Name = "Chill / Drift"
            SuggestedTitle = "01 · Chill"
            Pattern = 'lo[\s-]?fi|ambient|chill|sleep|calm|downtempo|study|relax|dreamy|soft'
            Meaning = "calm, low-energy, ambient, lofi, or study-oriented material"
        },
        [pscustomobject]@{
            Name = "Night / Moody"
            SuggestedTitle = "02 · Night"
            Pattern = 'night|midnight|dark|moody|sad|afterhours|slowed|reverb|dream'
            Meaning = "dark, late-night, slowed, moody, or introspective material"
        },
        [pscustomobject]@{
            Name = "Melodic / Emotional"
            SuggestedTitle = "03 · Melodic"
            Pattern = 'melodic|emotional|progressive|trance|future\s*bass|cinematic|anthem'
            Meaning = "melodic, emotional, progressive, or cinematic electronic material"
        },
        [pscustomobject]@{
            Name = "Bright / Upbeat"
            SuggestedTitle = "04 · Bright"
            Pattern = 'bright|happy|upbeat|summer|kawaii|cute|funk|disco|party'
            Meaning = "bright, cheerful, upbeat, cute, funk, or dance-pop material"
        },
        [pscustomobject]@{
            Name = "Bass / Dubstep"
            SuggestedTitle = "05 · Bass"
            Pattern = 'dubstep|bass|riddim|brostep|drum\s*(?:and|&|n)\s*bass|\bdnb\b|trap'
            Meaning = "bass-heavy, dubstep, riddim, DnB, trap, or related material"
        },
        [pscustomobject]@{
            Name = "Electronic / Arcade"
            SuggestedTitle = "06 · Electronic"
            Pattern = 'electro|electronic|\bedm\b|synth|chiptune|8[\s-]?bit|arcade|game|ost|soundtrack'
            Meaning = "electronic, synth, game/OST, chiptune, or arcade-like material"
        },
        [pscustomobject]@{
            Name = "Rock / Alternative"
            SuggestedTitle = "07 · Rock & Alt"
            Pattern = 'rock|alternative|indie|punk|emo|grunge|metal|post[-\s]?hardcore'
            Meaning = "rock, alternative, indie, punk, emo, grunge, or metal material"
        },
        [pscustomobject]@{
            Name = "Pop / Vocal"
            SuggestedTitle = "08 · Pop & Vocal"
            Pattern = '\bpop\b|vocal|singer|cover|acoustic|ballad'
            Meaning = "pop or vocal-forward material"
        },
        [pscustomobject]@{
            Name = "Hip-Hop / Rap"
            SuggestedTitle = "09 · Hip-Hop"
            Pattern = 'hip[\s-]?hop|\brap\b|phonk|drill|boom\s*bap'
            Meaning = "hip-hop, rap, phonk, drill, or related material"
        },
        [pscustomobject]@{
            Name = "Classical / Instrumental"
            SuggestedTitle = "10 · Instrumental"
            Pattern = 'classical|instrumental|piano|violin|orchestra|orchestral|symphony'
            Meaning = "classical, piano, orchestral, or instrumental material"
        },
        [pscustomobject]@{
            Name = "Jazz / Soul / R&B"
            SuggestedTitle = "11 · Jazz & Soul"
            Pattern = '\bjazz\b|\bsoul\b|r&b|\brnb\b|blues'
            Meaning = "jazz, soul, R&B, or blues material"
        },
        [pscustomobject]@{
            Name = "J-Pop / Anime / Vocaloid"
            SuggestedTitle = "12 · J-Pop & Anime"
            Pattern = 'j[\s-]?pop|vocaloid|anime|city\s*pop|anisong'
            Meaning = "J-pop, anime-song, Vocaloid, or city-pop material"
        }
    )

    $rows = [System.Collections.Generic.List[object]]::new()

    foreach ($lane in $lanes) {
        $score = 0
        $playlistHits = 0
        $trackHits = 0

        foreach ($entry in $corpus) {
            if ([string]$entry.Text -match [string]$lane.Pattern) {
                $score += [int]$entry.Weight
                if ([string]$entry.Source -eq "playlist title") {
                    $playlistHits++
                }
                else {
                    $trackHits++
                }
            }
        }

        if ($score -gt 0) {
            $rows.Add([pscustomobject]@{
                Lane = [string]$lane.Name
                SuggestedPlaylistTitle = [string]$lane.SuggestedTitle
                Score = $score
                PlaylistTitleHits = $playlistHits
                TrackMetadataHits = $trackHits
                Meaning = [string]$lane.Meaning
                Confidence = $(if ($playlistHits -ge 2 -or $score -ge 20) {
                    "MEDIUM"
                }
                elseif ($score -ge 8) {
                    "LOW-MEDIUM"
                }
                else {
                    "LOW"
                })
                Note = "metadata-only suggestion; review before creating or moving anything"
            })
        }
    }

    return @(
        $rows |
        Sort-Object `
            @{ Expression = "Score"; Descending = $true }, `
            @{ Expression = "Lane"; Descending = $false } |
        Select-Object -First $MaxCount
    )
}

function Show-TasteLaneSuggestions {
    param($Rows)

    Write-Section "Taste-lane suggestions (review only)"
    Write-Host "This is metadata-only inference from playlist names + title/artist text; no audio was analyzed." -ForegroundColor DarkYellow

    if (@($Rows).Count -eq 0) {
        Write-Host "No useful lane signals were detected."
        return
    }

    foreach ($row in @($Rows)) {
        Write-Host ("  {0,-24} score={1,-4} confidence={2,-10} -> {3}" -f `
            [string]$row.Lane, [int]$row.Score, [string]$row.Confidence, `
            [string]$row.SuggestedPlaylistTitle)
    }

    Write-Host ""
    Write-Host ("Suggestion CSV: {0}" -f $TasteLaneSuggestionsPath) -ForegroundColor Green
}

function Confirm-BulkAction {
    param(
        [string]$Title,
        [int]$Count,
        [string]$Detail
    )

    if ($Count -le 0) { return $false }

    Write-Host ""
    Write-Host $Title -ForegroundColor Cyan
    Write-Host ("Count: {0}" -f $Count)
    if ($Detail) { Write-Host $Detail }
    $answer = Read-Host "Type YES to perform this bulk action"
    return ($answer -ceq "YES")
}

function Show-ActionSummary {
    param($Actions)

    Write-Section "Proposed bulk actions"

    $groups = @($Actions | Group-Object Action | Sort-Object Name)
    if ($groups.Count -eq 0) {
        Write-Host "No actions proposed."
        return
    }

    foreach ($group in $groups) {
        Write-Host ("  {0}: {1}" -f $group.Name, $group.Count)
    }

    Write-Host ""
    Write-Host "Notes:"
    Write-Host "  - ADD_LIBRARY only appears when YouTube exposed an add-to-library feedback token."
Write-Host "  - ADD_LIBRARY is suppressed when an exact-identity alternate Video ID is already in the fresh Song Library."
    Write-Host ("  - Actual ADD_LIBRARY writes are capped by -AddLibraryWriteLimit (current: {0})." -f `
        $(if ($AddLibraryWriteLimit -eq 0) { "unlimited" } else { [string]$AddLibraryWriteLimit }))
    Write-Host ("  - ADD_LIBRARY feedback requests contain up to {0} token(s) each." -f `
        $AddLibraryFeedbackBatchSize)
    Write-Host "  - v27 refreshes and verifies the complete Song Library around confirmed ADD_LIBRARY writes."
    Write-Host "  - LIKE is optional; LIKE actions are generated only with -IncludeLikeActions."
    Write-Host "  - Playlist moves are offered only for exact historical Video ID mappings."
    Write-Host "  - Inferred artist-history placement is report-only."
    Write-Host "  - Alternate/updated Video ID matches are report-only."
    if ($NormalizeManagedPlaylistSettings) {
        Write-Host "  - Managed playlist settings normalization is handled separately from per-track actions."
    }
}

# ---------------------------- main ----------------------------


# ---------------------------------------------------------------------------
# v27 console UI + cooperative runtime controls
# ---------------------------------------------------------------------------
$script:UserConfig = Load-UserConfig
$script:SetupWizardActive = $false
$script:ConfigureNonMusicAfterIndex = $false
$script:OfferManagedPlaylistCreation = $false
$script:CluiMode = ""

# Explicit command-line identity always wins. Otherwise use saved config.
if (-not $PSBoundParameters.ContainsKey("ExpectedAccountName")) {
    $ExpectedAccountName = [string]$script:UserConfig.ExpectedAccountName
}
if (-not $PSBoundParameters.ContainsKey("ExpectedChannelHandle")) {
    $ExpectedChannelHandle = [string]$script:UserConfig.ExpectedChannelHandle
}

# Launching with no arguments is the user-facing CLUI. Explicit operational
# arguments preserve the traditional script/automation behavior.
$operationalKeys = @(
    $PSBoundParameters.Keys |
    Where-Object {
        $_ -notin @("ConfigPath")
    }
)
$interactiveLaunch = (
    -not $NoInteractive.IsPresent -and
    ($Setup.IsPresent -or $operationalKeys.Count -eq 0)
)

if ($interactiveLaunch) {
    Write-Host ""
    Write-Host "YouTube Music Tidy v27" -ForegroundColor Cyan
    Write-Host "Console setup / maintenance interface" -ForegroundColor DarkCyan

    $isFirstSetup = (
        $Setup.IsPresent -or
        -not (Test-Path -LiteralPath $ConfigPath) -or
        -not [string]$script:UserConfig.ExpectedAccountName
    )

    if ($isFirstSetup) {
        $script:CluiMode = "SETUP"
    }
    else {
        Show-CluiMainMenu
    }

    if ($script:CluiMode -eq "QUIT") {
        Write-Host "No changes made."
        exit 0
    }

    if ($script:CluiMode -eq "AUTH") {
        [void](Invoke-ClipboardBrowserAuthSetup `
            -Config $script:UserConfig `
            -AllowOverwrite)

        $detected = Get-YtmAccountInfo
        Write-Host ""
        Write-Host ("Authenticated as: {0} {1}" -f `
            [string]$detected.AccountName, [string]$detected.ChannelHandle) -ForegroundColor Green

        if (
            [string]$script:UserConfig.ExpectedAccountName -and
            [string]$detected.AccountName -cne [string]$script:UserConfig.ExpectedAccountName
        ) {
            Write-Warning ("Saved target is '{0}', but refreshed auth is '{1}'." -f `
                [string]$script:UserConfig.ExpectedAccountName,
                [string]$detected.AccountName)
            Write-Warning "Config was NOT changed. Run -Setup if you intentionally changed accounts."
        }

        exit 0
    }

    if ($script:CluiMode -eq "SETUP") {
        if (-not (Test-Path -LiteralPath $HeadersPath)) {
            [void](Invoke-ClipboardBrowserAuthSetup -Config $script:UserConfig)
        }
        else {
            Write-Host ""
            Write-Host ("Existing browser auth: {0}" -f $HeadersPath) -ForegroundColor Cyan
            $refresh = (Read-Host "Refresh it from the clipboard now? [y/N]").Trim().ToUpperInvariant()
            if ($refresh -eq "Y" -or $refresh -eq "YES") {
                [void](Invoke-ClipboardBrowserAuthSetup `
                    -Config $script:UserConfig `
                    -AllowOverwrite)
            }
        }

        # Discover/save the expected account when missing, or when -Setup was
        # explicitly requested for reconfiguration.
        if (
            $Setup.IsPresent -or
            -not [string]$script:UserConfig.ExpectedAccountName
        ) {
            [void](Invoke-IdentitySetup -Config $script:UserConfig)
        }

        $ExpectedAccountName = [string]$script:UserConfig.ExpectedAccountName
        $ExpectedChannelHandle = [string]$script:UserConfig.ExpectedChannelHandle

        Set-CluiOperationalMode "SETUP"
    }
    else {
        Set-CluiOperationalMode $script:CluiMode
    }
}

# A non-interactive clean install must explicitly supply an expected account.
# Interactive setup discovers it safely instead.
if (-not [string]$ExpectedAccountName) {
    throw @"
No expected account identity is configured.

Run:
  .\youtube_music_tidy.ps1

for the console setup wizard, or pass:
  -ExpectedAccountName "Your account name"
and optionally:
  -ExpectedChannelHandle "@yourhandle"
"@
}

Write-Section "YouTube Music tidy"
Write-Host ("PowerShell: {0}" -f $PSVersionTable.PSVersion)
Write-Host ("Scanner revision: {0}" -f $ScannerRevision)
Write-Host ("Track metadata revision: {0}" -f $TrackMetadataRevision)
Write-Host ("Head check count: {0}" -f $HeadCheckCount)
Write-Host ("Reuse playlist cache: {0}" -f $ReusePlaylistCache.IsPresent)
Write-Host ("Trace HTTP requests:  {0} (default ON; -QuietRequests disables)" -f [bool]$TraceRequests)
Write-Host ("Include LIKE actions: {0}" -f $IncludeLikeActions.IsPresent)
Write-Host ("Include dynamic playlists: {0}" -f $IncludeDynamicPlaylists.IsPresent)
Write-Host ("Normalize managed settings: {0}" -f $NormalizeManagedPlaylistSettings.IsPresent)
Write-Host ("Suggest taste lanes:       {0}" -f $SuggestTasteLanes.IsPresent)
Write-Host ("Non-music playlist scope:   {0} exclusion(s)" -f $NonMusicPlaylistIdSet.Count)
foreach ($playlistIdKey in @($NonMusicPlaylistIdSet | Sort-Object)) {
    Write-Host ("  excluded from Song-Library resolution when source-only: {0}" -f $playlistIdKey)
}
if (Test-Path -LiteralPath $NonMusicPlaylistConfigPath) {
    Write-Host ("  persistent config: {0}" -f $NonMusicPlaylistConfigPath)
}
Write-Host ("Resolve UNKNOWN Library:   {0} (resolver rev {1})" -f $ResolveUnknownLibrary.IsPresent, $LibraryResolverRevision)
Write-Host ("Enrich Library tokens:     {0} (token rev {1}, limit={2})" -f `
    $EnrichLibraryTokens.IsPresent, $LibraryTokenRevision, $EnrichLibraryTokenLimit)
Write-Host ("Retry unavailable tokens:  {0}" -f $RetryUnavailableLibraryTokens.IsPresent)
Write-Host ("ADD_LIBRARY write limit:     {0}" -f `
    $(if ($AddLibraryWriteLimit -eq 0) { "none" } else { [string]$AddLibraryWriteLimit }))
Write-Host ("ADD_LIBRARY feedback batch:  {0} token(s)/request" -f $AddLibraryFeedbackBatchSize)
Write-Host ("Recent write-preflight reuse: {0} min max age" -f $RecentWritePreflightMaxAgeMinutes)
Write-Host ("ADD_LIBRARY state:           {0}" -f $AddLibraryMutationStatePath)
Write-Host ("Network outage retry: {0} minute(s)" -f $NetworkOutageRetryMinutes)
Write-Host ("Checkpoint interval:  every {0} completed playlist(s)" -f $CheckpointEveryPlaylists)
if ($script:RuntimeControlAvailable) {
    Write-Host "Runtime controls:    PP = pause at next safe point (double-press P within 2s)"
}
else {
    Write-Host "Runtime controls:    PP pause unavailable in this host/input mode"
}

if ($HeadCheckCount -lt 1) { $HeadCheckCount = 5 }
if ($BatchSize -lt 1 -or $BatchSize -gt 50) {
    throw "BatchSize must be between 1 and 50."
}
if ($CheckpointEveryPlaylists -lt 1) {
    $CheckpointEveryPlaylists = 5
}
if ($AddLibraryWriteLimit -lt 0) {
    throw "AddLibraryWriteLimit must be 0 (unlimited) or a positive integer."
}
if ($AddLibraryFeedbackBatchSize -lt 1) {
    throw "AddLibraryFeedbackBatchSize must be at least 1."
}
if ($AddLibraryVerificationDelaySeconds -lt 0) {
    throw "AddLibraryVerificationDelaySeconds cannot be negative."
}
if ($RecentWritePreflightMaxAgeMinutes -lt 0) {
    throw "RecentWritePreflightMaxAgeMinutes cannot be negative."
}

[void](Confirm-YtmAccount)

$oldCache = Load-TidyCache
$oldMetadata = Get-PreviousMetadataIndex $oldCache

$cache = $oldCache
$scannerNeedsRepair = ([int]$cache.ScannerRevision -lt $ScannerRevision)

$dirtyFullPlaylists = $FullScan.IsPresent -or [bool]$oldCache.ForceFullNextRun
$metadataRepairNeeded = (
    $ResolveUnknownLibrary.IsPresent -and
    [int]$cache.TrackMetadataRevision -lt $TrackMetadataRevision -and
    -not $ManagedPlaylistsOnly.IsPresent
)

# Metadata repair needs one full crawl of the normal playlist rows so the cache
# learns musicVideoType and album IDs. It does not force Likes/index full.
$forceFullPlaylists = $dirtyFullPlaylists -or $metadataRepairNeeded
$coreFull = $dirtyFullPlaylists -or $scannerNeedsRepair

if ($metadataRepairNeeded) {
    Write-Host ""
    Write-Host ("One-time track metadata repair required (cache rev {0} -> {1})." -f `
        [int]$cache.TrackMetadataRevision, $TrackMetadataRevision) -ForegroundColor Yellow
    Write-Host "Normal playlists will be fully rescanned once to cache videoType/album metadata." -ForegroundColor DarkYellow
    if ($ReusePlaylistCache) {
        Write-Host "-ReusePlaylistCache is temporarily overridden for this repair pass." -ForegroundColor DarkYellow
    }
}

if ($null -eq $oldCache.Likes -or $null -eq $oldCache.Library -or $null -eq $oldCache.PlaylistIndex) {
    $coreFull = $true
}

if ($FullScan) {
    Write-Host ""
    Write-Host "Scan mode: FULL (all core sets + every playlist)" -ForegroundColor Yellow
    Write-Host "If this run is interrupted, rerun WITHOUT -FullScan to resume from completed cache checkpoints." -ForegroundColor DarkYellow
}
elseif ($scannerNeedsRepair) {
    Write-Host ""
    Write-Host ("Scan mode: REPAIR + INCREMENTAL (cache scanner revision {0} -> {1})" -f `
        $cache.ScannerRevision, $ScannerRevision) -ForegroundColor Yellow
    Write-Host "Old playlist snapshots will be reused unless changed; Likes/Library/index are rebuilt."
}
else {
    Write-Host ""
    Write-Host ("Scan mode: INCREMENTAL (head check = {0})" -f $HeadCheckCount) -ForegroundColor Green
}

if ($ReusePlaylistCache -and $FullScan) {
    Write-Warning "-ReusePlaylistCache is ignored because -FullScan was requested."
}

$cache.ForceFullNextRun = $false

# ----- Playlist index -----
Write-Section "Playlist index"
$playlistIndexFirst = Get-PlaylistIndexFirstPage
$refreshPlaylistIndex = $coreFull

if (-not $refreshPlaylistIndex) {
    $oldHead = @($cache.PlaylistIndex.HeadIds)
    if (-not (Test-SameStringArray $playlistIndexFirst.HeadIds $oldHead)) {
        $refreshPlaylistIndex = $true
        Write-ProgressLine "Playlist-index head changed -> full index refresh." Yellow
    }
}

if ($refreshPlaylistIndex) {
    $cache.PlaylistIndex = Get-PlaylistIndexFull $playlistIndexFirst
    Write-ProgressLine ("Playlist index refreshed: {0} playlist(s), complete={1}" -f `
        @($cache.PlaylistIndex.Playlists).Count, $cache.PlaylistIndex.Complete) Cyan
}
else {
    Write-ProgressLine ("Playlist index unchanged: reusing {0} cached playlist(s)." -f `
        @($cache.PlaylistIndex.Playlists).Count) Green
}
Save-TidyCache $cache


if ($script:ConfigureNonMusicAfterIndex) {
    Invoke-NonMusicPlaylistConsoleSetup `
        -PlaylistIndex $cache.PlaylistIndex `
        -Config $script:UserConfig
}

# v27 resolves managed playlists only after the index is available. Existing
# installs import/cache the legacy state once; new/friend installs can simply
# use numbered playlist titles such as "01 · Drift" or "05 - Bass".
$managedMap = Resolve-ManagedPlaylistMap `
    -Cache $cache `
    -PlaylistIndex $cache.PlaylistIndex

$DestinationTitles = @(
    $managedMap.Values |
    Sort-Object {
        Get-ManagedPlaylistOrdinal ([string]$_)
    }, {
        [string]$_
    }
)

$placementModel = Get-KnownPlacementModel -Cache $cache

# Persist consolidated managed IDs + legacy placement history before continuing.
Save-TidyCache $cache

Write-Host ""
Write-Host ("Managed categories ({0}):" -f $DestinationTitles.Count) -ForegroundColor Cyan
foreach ($title in $DestinationTitles) {
    Write-Host ("  - {0}" -f $title)
}

# Ensure all managed destinations are present even if library-playlist index omitted one.
$playlistIndexMap = @{}
foreach ($p in @($cache.PlaylistIndex.Playlists)) {
    if ($p.PlaylistId) {
        $playlistIndexMap[[string]$p.PlaylistId] = [string]$p.Title
    }
}
foreach ($playlistIdKey in $managedMap.Keys) {
    if (-not $playlistIndexMap.ContainsKey([string]$playlistIdKey)) {
        $playlistIndexMap[[string]$playlistIdKey] = [string]$managedMap[$playlistIdKey]
    }
}

# Liked Music and Saved Episodes are audited separately / not normal user playlists.
foreach ($systemId in @("LM", "SE")) {
    if ($playlistIndexMap.ContainsKey($systemId)) {
        $playlistIndexMap.Remove($systemId)
    }
}

if (-not $IncludeDynamicPlaylists) {
    $dynamicIds = @(
        $playlistIndexMap.Keys |
        Where-Object {
            Test-IsDynamicPlaylist `
                -PlaylistId ([string]$_) `
                -Title ([string]$playlistIndexMap[$_])
        }
    )

    if ($dynamicIds.Count -gt 0) {
        Write-Host ""
        Write-Host ("Skipping {0} dynamic/system playlist(s) from topology:" -f $dynamicIds.Count) -ForegroundColor DarkYellow
        foreach ($dynamicId in $dynamicIds) {
            Write-Host ("  - {0} ({1})" -f [string]$playlistIndexMap[$dynamicId], [string]$dynamicId) -ForegroundColor DarkGray
            $playlistIndexMap.Remove([string]$dynamicId)
        }
        Write-Host "Use -IncludeDynamicPlaylists to include them." -ForegroundColor DarkGray
    }
}

if ($ManagedPlaylistsOnly) {
    $filtered = @{}
    foreach ($playlistIdKey in $managedMap.Keys) {
        $filtered[[string]$playlistIdKey] = [string]$managedMap[$playlistIdKey]
    }
    $playlistIndexMap = $filtered
}

# ----- Likes -----
Write-Section "Liked Music"
$likedFirst = Get-LikedFirstPage
$refreshLikes = $coreFull
$likedDeltaApplied = $false

if (-not $refreshLikes) {
    $cachedComplete = (
        $null -ne $cache.Likes -and
        $cache.Likes.PSObject.Properties.Name -contains "Complete" -and
        [bool]$cache.Likes.Complete
    )

    if (-not $cachedComplete) {
        $refreshLikes = $true
        Write-ProgressLine "Cached Liked snapshot is not marked complete -> refresh." Yellow
    }
    else {
        $countsKnown = (
            $null -ne $likedFirst.Count -and
            $null -ne $cache.Likes.Count
        )

        if ($countsKnown) {
            $oldLikedCount = [int]$cache.Likes.Count
            $newLikedCount = [int]$likedFirst.Count

            if ($newLikedCount -gt $oldLikedCount) {
                $likedDelta = Try-ApplyLikedAdditionDelta `
                    -FirstPage $likedFirst `
                    -CachedSnapshot $cache.Likes

                if ($null -ne $likedDelta) {
                    $addedCount = $newLikedCount - $oldLikedCount
                    $cache.Likes = $likedDelta
                    $likedDeltaApplied = $true

                    Write-ProgressLine (
                        "Liked Music count {0} -> {1}: detected {2} newest addition(s) on page 1; cache patched, full refresh skipped." -f `
                        $oldLikedCount, $newLikedCount, $addedCount
                    ) Green
                }
                else {
                    $refreshLikes = $true
                    Write-ProgressLine (
                        "Liked Music count increased {0} -> {1}, but page-1 head did not prove a pure newest-first addition -> full liked refresh." -f `
                        $oldLikedCount, $newLikedCount
                    ) Yellow
                }
            }
            elseif ($newLikedCount -lt $oldLikedCount) {
                $refreshLikes = $true
                Write-ProgressLine (
                    "Liked Music count decreased {0} -> {1}; removal location is unknown -> full liked refresh." -f `
                    $oldLikedCount, $newLikedCount
                ) Yellow
            }
            elseif (-not (Test-SameStringArray $likedFirst.HeadIds @($cache.Likes.HeadIds))) {
                $refreshLikes = $true
                Write-ProgressLine "Liked Music count unchanged but head changed -> full liked refresh." Yellow
            }
        }
        elseif (-not (Test-SameStringArray $likedFirst.HeadIds @($cache.Likes.HeadIds))) {
            $refreshLikes = $true
            Write-ProgressLine "Liked Music head changed and total count is unavailable -> full liked refresh." Yellow
        }
    }
}

if ($refreshLikes) {
    $cache.Likes = Get-LikedFullSnapshot $likedFirst
}
elseif (-not $likedDeltaApplied) {
    Write-ProgressLine ("Liked Music unchanged: reusing {0} cached item(s)." -f `
        @($cache.Likes.Items).Count) Green
}
Save-TidyCache $cache

# ----- Library -----
Write-Section "Song Library"
$script:LibrarySnapshotValidatedThisRun = $false
$libraryFirst = Get-LibraryFirstPage
$refreshLibrary = $coreFull -or $ResolveUnknownLibrary.IsPresent
$libraryDeltaApplied = $false

if ($ResolveUnknownLibrary) {
    Write-ProgressLine "Deep Library resolution requested -> forcing a fresh complete Song Library snapshot." Yellow
}

if (-not $refreshLibrary) {
    $cachedComplete = (
        $null -ne $cache.Library -and
        $cache.Library.PSObject.Properties.Name -contains "Complete" -and
        [bool]$cache.Library.Complete
    )

    if (-not $cachedComplete) {
        $refreshLibrary = $true
        Write-ProgressLine "Cached Library snapshot is not marked complete -> refresh." Yellow
    }
    elseif (-not (Test-SameStringArray $libraryFirst.HeadIds @($cache.Library.HeadIds))) {
        $libraryDelta = Try-ApplyLibraryFirstPageDelta `
            -FirstPage $libraryFirst `
            -CachedSnapshot $cache.Library

        if ($null -ne $libraryDelta) {
            $cache.Library = $libraryDelta
            $libraryDeltaApplied = $true
            $script:LibrarySnapshotValidatedThisRun = $true

            if ([string]$libraryDelta.IncrementalMode -eq "RECENT_PREFIX_ADD") {
                Write-ProgressLine (
                    "Library recently-added page proves {0} newest addition(s); cache patched from page 1, full refresh skipped." -f `
                    [int]$libraryDelta.IncrementalAdded
                ) Green
            }
            else {
                Write-ProgressLine (
                    "Library page-1 membership is unchanged but recently-added order shifted; cached page-1 order updated, full refresh skipped."
                ) Green
            }
        }
        else {
            $refreshLibrary = $true
            Write-ProgressLine (
                "Recently-added Library head changed, but page 1 did not prove a pure prefix-add or same-membership reorder -> full Library refresh."
            ) Yellow
        }
    }
}

if ($refreshLibrary) {
    $cache.Library = Get-LibraryFullSnapshot $libraryFirst
    $script:LibrarySnapshotValidatedThisRun = [bool]$cache.Library.Complete
}
elseif (-not $libraryDeltaApplied) {
    Write-ProgressLine ("Library unchanged: reusing {0} cached item(s)." -f `
        @($cache.Library.Items).Count) Green
    # The live recently-added head matched the complete cached snapshot.
    $script:LibrarySnapshotValidatedThisRun = $true
}
Save-TidyCache $cache

# ----- Playlists -----
Write-Section "Playlists"
$oldPlaylistMap = Get-CachedPlaylistMap $cache
$newPlaylistSnapshots = [System.Collections.Generic.List[object]]::new()
$playlistIds = @($playlistIndexMap.Keys | Sort-Object)
$playlistTotal = $playlistIds.Count
$playlistOrdinal = 0

foreach ($playlistIdKey in $playlistIds) {
    $playlistOrdinal++
    $title = [string]$playlistIndexMap[$playlistIdKey]
    if (-not $title) { $title = [string]$playlistIdKey }

    Invoke-RuntimeControlCheckpoint -Phase ("playlist {0}/{1}: {2}" -f `
        $playlistOrdinal, $playlistTotal, $title)

    Write-ProgressLine ("[{0}/{1}] {2} ({3})" -f `
        $playlistOrdinal, $playlistTotal, $title, $playlistIdKey) Cyan

    $cachedExists = $oldPlaylistMap.ContainsKey([string]$playlistIdKey)
    $skipProbe = ($ReusePlaylistCache -and -not $forceFullPlaylists -and $cachedExists)

    if ($skipProbe) {
        $cached = $oldPlaylistMap[[string]$playlistIdKey]
        $newPlaylistSnapshots.Add($cached)
        Write-ProgressLine ("    CACHE TRUSTED: {0} item(s), last full scan {1}" -f `
            @($cached.Items).Count, [string]$cached.ScannedAt) DarkGreen
    }
    else {
        $probeSw = [System.Diagnostics.Stopwatch]::StartNew()
        $first = Get-PlaylistFirstPage ([string]$playlistIdKey) $title
        $probeSw.Stop()

        Write-ProgressLine ("    probe: first-page={0}, header-count={1}, continuation={2}, {3:N1}s" -f `
            @($first.Tracks).Count,
            $(if ($null -ne $first.Count) { $first.Count } else { "?" }),
            $(if ($first.Continuation) { "yes" } else { "no" }),
            $probeSw.Elapsed.TotalSeconds) DarkGray

        $refresh = $forceFullPlaylists

        if (-not $refresh) {
            if (-not $cachedExists) {
                $refresh = $true
                Write-ProgressLine "    decision: NEW playlist -> full scan" Yellow
            }
            else {
                $cached = $oldPlaylistMap[[string]$playlistIdKey]
                $headChanged = -not (Test-SameStringArray $first.HeadIds @($cached.HeadIds))
                $countChanged = $false

                if ($null -ne $first.Count -and $null -ne $cached.Count) {
                    $countChanged = ([int]$first.Count -ne [int]$cached.Count)
                }

                $cachedComplete = $true
                if ($cached.PSObject.Properties.Name -contains "Complete") {
                    $cachedComplete = [bool]$cached.Complete
                }

                if ($headChanged -or $countChanged -or -not $cachedComplete) {
                    $refresh = $true
                    Write-ProgressLine ("    decision: full scan (headChanged={0}, countChanged={1}, cachedComplete={2})" -f `
                        $headChanged, $countChanged, $cachedComplete) Yellow
                }
                else {
                    Write-ProgressLine "    decision: unchanged -> reuse cached full snapshot" Green
                }
            }
        }
        else {
            Write-ProgressLine "    decision: full scan required (explicit/dirty-cache/metadata-repair)" Yellow
        }

        if ($refresh) {
            $snap = Get-PlaylistFullSnapshot ([string]$playlistIdKey) $title $first
            $newPlaylistSnapshots.Add($snap)
        }
        else {
            $cached = $oldPlaylistMap[[string]$playlistIdKey]
            $snap = [pscustomobject]@{
                PlaylistId = [string]$playlistIdKey
                PlaylistTitle = $title
                Count = if ($null -ne $first.Count) { $first.Count } else { $cached.Count }
                HeadIds = @($first.HeadIds)
                Items = @($cached.Items)
                Complete = if ($cached.PSObject.Properties.Name -contains "Complete") { [bool]$cached.Complete } else { $true }
                Pages = if ($cached.PSObject.Properties.Name -contains "Pages") { $cached.Pages } else { $null }
                ScannedAt = [string]$cached.ScannedAt
            }
            $newPlaylistSnapshots.Add($snap)
        }
    }

    # Periodic disk checkpoint. This keeps a long run recoverable on bad Wi-Fi.
    if (
        ($playlistOrdinal % $CheckpointEveryPlaylists) -eq 0 -or
        $playlistOrdinal -eq $playlistTotal
    ) {
        $cache.Playlists = @($newPlaylistSnapshots)

        # Preserve cached snapshots for playlists not reached yet in this run.
        if ($playlistOrdinal -lt $playlistIds.Count) {
            for ($remainingIndex = $playlistOrdinal; $remainingIndex -lt $playlistIds.Count; $remainingIndex++) {
                $remainingId = [string]$playlistIds[$remainingIndex]
                if (
                    $remainingId -and
                    $oldPlaylistMap.ContainsKey($remainingId) -and
                    -not (@($cache.Playlists | Where-Object { $_.PlaylistId -eq $remainingId }).Count)
                ) {
                    $cache.Playlists += $oldPlaylistMap[$remainingId]
                }
            }
        }

        Save-TidyCache $cache
        Write-ProgressLine ("    checkpoint saved ({0}/{1} playlists processed)" -f `
            $playlistOrdinal, $playlistTotal) DarkGray

        # Rebuild the working list without duplicated preserved tails.
        $newPlaylistSnapshots = [System.Collections.Generic.List[object]]::new()
        foreach ($doneId in $playlistIds[0..($playlistOrdinal - 1)]) {
            $snapDone = @($cache.Playlists | Where-Object { $_.PlaylistId -eq [string]$doneId } | Select-Object -First 1)
            if ($snapDone.Count -gt 0) { $newPlaylistSnapshots.Add($snapDone[0]) }
        }
    }
}

$cache.Playlists = @($newPlaylistSnapshots)
$cache.ScannerRevision = $ScannerRevision

if (
    $forceFullPlaylists -and
    -not $ManagedPlaylistsOnly.IsPresent
) {
    $cache.TrackMetadataRevision = $TrackMetadataRevision
    $cache.TrackMetadataRepairedAt = Get-NowIso
}

if ($dirtyFullPlaylists) {
    $cache.LastFullScan = Get-NowIso
}
Save-TidyCache $cache

Write-ProgressLine ("Scanning complete. HTTP requests this run: {0}" -f $script:RequestCount) Cyan

if ($metadataRepairNeeded) {
    Write-ProgressLine ("Track metadata repair DONE: cache revision {0}; future resolver runs can reuse it." -f `
        $TrackMetadataRevision) Green
}

# Build report and action plan.
Write-Section "Topology analysis"

$libraryResolutionCache = Load-LibraryResolutionCache
$libraryResolutionMap = Get-LibraryResolutionMap $libraryResolutionCache

$libraryTokenCache = Load-LibraryTokenCache
$libraryTokenMap = Get-LibraryTokenMap $libraryTokenCache

$result = Build-TidyReport `
    -Cache $cache `
    -OldMetadata $oldMetadata `
    -ManagedPlaylistMap $managedMap `
    -PlacementModel $placementModel `
    -LibraryResolutionMap $libraryResolutionMap `
    -LibraryTokenMap $libraryTokenMap `
    -NonMusicPlaylistIdSet $NonMusicPlaylistIdSet

$reportRows = @($result.ReportRows)
$actions = @($result.Actions)

if ($ResolveUnknownLibrary) {
    Resolve-UnknownLibraryRows `
        -Rows $reportRows `
        -ResolutionMap $libraryResolutionMap `
        -LibrarySnapshotComplete ([bool]$cache.Library.Complete)

    $result = Build-TidyReport `
        -Cache $cache `
        -OldMetadata $oldMetadata `
        -ManagedPlaylistMap $managedMap `
        -PlacementModel $placementModel `
        -LibraryResolutionMap $libraryResolutionMap `
        -LibraryTokenMap $libraryTokenMap `
        -NonMusicPlaylistIdSet $NonMusicPlaylistIdSet

    $reportRows = @($result.ReportRows)
    $actions = @($result.Actions)
}


if ($EnrichLibraryTokens) {
    Enrich-LibraryAddTokens `
        -Rows $reportRows `
        -TokenMap $libraryTokenMap `
        -LibrarySnapshotComplete ([bool]$cache.Library.Complete)

    $result = Build-TidyReport `
        -Cache $cache `
        -OldMetadata $oldMetadata `
        -ManagedPlaylistMap $managedMap `
        -PlacementModel $placementModel `
        -LibraryResolutionMap $libraryResolutionMap `
        -LibraryTokenMap $libraryTokenMap `
        -NonMusicPlaylistIdSet $NonMusicPlaylistIdSet

    $reportRows = @($result.ReportRows)
    $actions = @($result.Actions)
}

$reportRows | Export-Csv -LiteralPath $ReportPath -NoTypeInformation -Encoding UTF8
$actions | Export-Csv -LiteralPath $ActionsPath -NoTypeInformation -Encoding UTF8

$likedCount = @($cache.Likes.Items).Count
$libraryCount = @($cache.Library.Items).Count
$playlistSongCount = @(
    foreach ($p in @($cache.Playlists)) { @($p.Items).Count }
) | Measure-Object -Sum | Select-Object -ExpandProperty Sum

$wrongExact = @($reportRows | Where-Object {
    $_.PlacementStatus -in @("WRONG MANAGED PLAYLIST", "EXPECTED + EXTRA MANAGED PLAYLIST")
}).Count
$missingExpected = @($reportRows | Where-Object {
    $_.PlacementStatus -eq "MISSING EXPECTED PLAYLIST"
}).Count
$potentialWrong = @($reportRows | Where-Object {
    $_.PlacementStatus -eq "POTENTIALLY WRONG PLAYLIST"
}).Count
$missingLibrary = @($reportRows | Where-Object { $_.InLibrary -eq "NO" }).Count
$unknownLibrary = @($reportRows | Where-Object { $_.InLibrary -eq "UNKNOWN" }).Count
$notApplicableLibrary = @($reportRows | Where-Object { $_.InLibrary -eq "N/A" }).Count
$excludedNonMusicLibrary = @(
    $reportRows | Where-Object {
        $_.NonMusicOnly -eq "YES" -and
        $_.LibraryStatusBasis -like "excluded non-music playlist only:*"
    }
).Count
$missingLibraryTokenAvailable = @(
    $reportRows | Where-Object {
        $_.InLibrary -eq "NO" -and $_.LibraryAddTokenStatus -eq "AVAILABLE"
    }
).Count
$missingLibraryAlternatePresent = @(
    $reportRows | Where-Object {
        $_.InLibrary -eq "NO" -and
        $_.LibraryAddTokenStatus -eq "ALTERNATE_IN_LIBRARY"
    }
).Count
$missingLibraryTokenUnavailable = @(
    $reportRows | Where-Object {
        $_.InLibrary -eq "NO" -and $_.LibraryAddTokenStatus -eq "UNAVAILABLE"
    }
).Count
$missingLibraryTokenConflict = @(
    $reportRows | Where-Object {
        $_.InLibrary -eq "NO" -and $_.LibraryAddTokenStatus -eq "CONFLICT"
    }
).Count
$missingLibraryTokenNotChecked = @(
    $reportRows | Where-Object {
        $_.InLibrary -eq "NO" -and $_.LibraryAddTokenStatus -eq "NOT_CHECKED"
    }
).Count
$missingLikes = @($reportRows | Where-Object { $_.Liked -eq "NO" }).Count
$unknownLikes = @($reportRows | Where-Object { $_.Liked -eq "UNKNOWN" }).Count
$alternate = @($reportRows | Where-Object { $_.AlternateVideoIds }).Count
$metadataChanged = @($reportRows | Where-Object {
    $_.MetadataStatus -match 'Metadata changed'
}).Count

Write-Host ("Liked Music snapshot:        {0} (complete={1}, pages={2})" -f `
    $likedCount, $cache.Likes.Complete, $cache.Likes.Pages)
Write-Host ("Song Library snapshot:       {0} (complete={1}, pages={2})" -f `
    $libraryCount, $cache.Library.Complete, $cache.Library.Pages)
Write-Host ("Playlist entries scanned:    {0}" -f $playlistSongCount)
Write-Host ("Unique topology tracks:      {0}" -f $reportRows.Count)
Write-Host ""
Write-Host ("Missing Library:             {0}" -f $missingLibrary)
Write-Host ("Library status unknown:      {0}" -f $unknownLibrary)
Write-Host ("Library not applicable:      {0}" -f $notApplicableLibrary)
Write-Host ("  excluded non-music only:   {0}" -f $excludedNonMusicLibrary)
Write-Host ("Library add-token available: {0}" -f $missingLibraryTokenAvailable)
Write-Host ("Library alternate already present:{0}" -f $missingLibraryAlternatePresent)
Write-Host ("Library add-token unavailable:{0}" -f $missingLibraryTokenUnavailable)
Write-Host ("Library token conflicts:     {0}" -f $missingLibraryTokenConflict)
Write-Host ("Library token not checked:   {0}" -f $missingLibraryTokenNotChecked)
Write-Host ("Not liked:                   {0}" -f $missingLikes)
Write-Host ("Liked status unknown:        {0}" -f $unknownLikes)
Write-Host ("Missing exact playlist:      {0}" -f $missingExpected)
Write-Host ("Wrong/extra exact playlist:  {0}" -f $wrongExact)
Write-Host ("Potentially wrong (inferred):{0}" -f $potentialWrong)
Write-Host ("Possible alternate IDs:      {0}" -f $alternate)
Write-Host ("Metadata changed vs cache:   {0}" -f $metadataChanged)
Write-Host ""
Write-Host ("Report:  {0}" -f $ReportPath) -ForegroundColor Green
Write-Host ("Actions: {0}" -f $ActionsPath) -ForegroundColor Green
Write-Host ("Library resolution cache: {0} ({1} cached result(s))" -f `
    $LibraryResolutionCachePath, $libraryResolutionMap.Count) -ForegroundColor Green
Write-Host ("Library token cache:      {0} ({1} cached result(s))" -f `
    $LibraryTokenCachePath, $libraryTokenMap.Count) -ForegroundColor Green

if ($libraryTokenMap.Count -gt 0) {
    $tokenStates = @($libraryTokenMap.Values | Group-Object State | Sort-Object Name)
    $tokenText = @(
        foreach ($g in $tokenStates) {
            "{0}={1}" -f $g.Name, $g.Count
        }
    ) -join ", "
    Write-Host ("Library token cache states (cached): {0}" -f $tokenText) -ForegroundColor Green
}

if ($libraryResolutionMap.Count -gt 0) {
    $resolutionStates = @($libraryResolutionMap.Values | Group-Object State | Sort-Object Name)
    $resolutionText = @(
        foreach ($g in $resolutionStates) {
            "{0}={1}" -f $g.Name, $g.Count
        }
    ) -join ", "
    Write-Host ("Resolver cache states:       {0}" -f $resolutionText) -ForegroundColor Green
}


if (-not [bool]$cache.Likes.Complete) {
    Write-Warning "Liked snapshot is incomplete. Unseen tracks are reported as Liked=UNKNOWN and no LIKE action is generated for them."
}
if (-not [bool]$cache.Library.Complete) {
    Write-Warning "Library snapshot is incomplete. Treat Library topology as provisional."
}
if (-not $IncludeLikeActions) {
    Write-Host ""
    Write-Host "LIKE actions were NOT generated. Add -IncludeLikeActions if you explicitly want them." -ForegroundColor Yellow
}
if ($EnrichLibraryTokens) {
    Write-Host ""
    Write-Host "Token enrichment was READ-ONLY. Any ADD_LIBRARY rows below are proposals only." -ForegroundColor Yellow
}



if ($SuggestTasteLanes) {
    $tasteSuggestions = @(
        Get-TasteLaneSuggestions `
            -Cache $cache `
            -MaxCount $TasteLaneCount
    )

    $tasteSuggestions |
        Export-Csv -LiteralPath $TasteLaneSuggestionsPath -NoTypeInformation -Encoding UTF8

    Show-TasteLaneSuggestions $tasteSuggestions

    if (
        $script:OfferManagedPlaylistCreation -and
        @($cache.ManagedPlaylists).Count -eq 0
    ) {
        $createdManaged = Invoke-ManagedPlaylistCreationWizard `
            -Suggestions $tasteSuggestions `
            -Cache $cache `
            -Config $script:UserConfig

        if ($createdManaged.Count -gt 0) {
            Write-Host ""
            Write-Host ("Setup created {0} empty managed playlist(s)." -f $createdManaged.Count) -ForegroundColor Green
            Write-Host "No songs were moved by setup." -ForegroundColor Green
            Write-Host "Run the script again to audit the new managed layout before any track mutations." -ForegroundColor Cyan
            $script:SetupCreatedManagedPlaylists = $true
        }
    }
}

$managedSettingsAudit = @()
if ($NormalizeManagedPlaylistSettings) {
    $managedSettingsAudit = @(
        Get-ManagedPlaylistSettingsAudit -ManagedPlaylistMap $managedMap
    )

    $managedSettingsAudit |
        Export-Csv -LiteralPath $PlaylistSettingsReportPath -NoTypeInformation -Encoding UTF8

    Show-ManagedPlaylistSettingsAudit $managedSettingsAudit
}

Show-ActionSummary $actions

if ($ReportOnly) {
    Write-Host ""
    if ($script:SetupCreatedManagedPlaylists) {
        Write-Host "Setup discovery complete. Empty managed playlists were created only after explicit YES." -ForegroundColor Green
        Write-Host "No track/library mutations were performed." -ForegroundColor Green
    }
    else {
        Write-Host "Report-only mode: no mutation prompts were shown." -ForegroundColor Green
    }
    exit 0
}

# Mutation categories. Every category requires its own explicit YES.
$didWrite = $false

$didLibraryWrite = $false
$didTopologyWrite = $false
$didPlaylistSettingsWrite = $false

if ($NormalizeManagedPlaylistSettings -and $managedSettingsAudit.Count -gt 0) {
    if (
        Confirm-BulkAction `
            "Bulk action: normalize managed playlist settings" `
            $managedSettingsAudit.Count `
            "Sets PUBLIC visibility, old-style two-line lowercase descriptions, Voting=Everyone, and enables Collaborators. Does not touch sort order or add-to-top."
    ) {
        # Verify the intended Brand account again immediately before writes.
        if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "managed playlist settings normalization")) {
            Write-Host "Managed playlist settings write cancelled." -ForegroundColor Yellow
            exit 0
        }

        $settingsWriteResult = Invoke-ManagedPlaylistSettingsNormalization `
            -Rows $managedSettingsAudit

        # Read all eight playlists back. Privacy, description, and
        # collaboration are directly verifiable from the owned playlist header.
        # Voting permission is not reliably exposed by browse, so its successful
        # edit response is the verification available here.
        $postSettingsAudit = @(
            Get-ManagedPlaylistSettingsAudit -ManagedPlaylistMap $managedMap
        )

        $postSettingsAudit |
            Export-Csv -LiteralPath $PlaylistSettingsReportPath -NoTypeInformation -Encoding UTF8

        $settingsFailures = @(
            Test-ManagedPlaylistSettingsVerification `
                -BeforeRows $managedSettingsAudit `
                -AfterRows $postSettingsAudit
        )

        Write-Host ""
        Write-Host ("Managed playlist settings API accepted: {0}/{1}" -f `
            [int]$settingsWriteResult.Applied, $managedSettingsAudit.Count) -ForegroundColor Green
        Write-Host ("Collaborators newly enabled: {0}" -f `
            [int]$settingsWriteResult.CollaborationEnabled) -ForegroundColor Green
        Write-Host ("Privacy/description/collaboration verification failures: {0}" -f `
            $settingsFailures.Count) -ForegroundColor `
            $(if ($settingsFailures.Count -eq 0) { "Green" } else { "Yellow" })
        Write-Host "Voting=Everyone was accepted by the edit API; browse does not reliably expose the permission for direct read-back verification." -ForegroundColor DarkYellow

        if ($settingsFailures.Count -gt 0) {
            foreach ($failure in $settingsFailures) {
                Write-Warning $failure
            }
            throw "Managed playlist settings post-write verification failed."
        }

        $didWrite = $true
        $didPlaylistSettingsWrite = $true
    }
}

$addLibraryRows = @($actions | Where-Object { $_.Action -eq "ADD_LIBRARY" })
if ($addLibraryRows.Count -gt 0) {
    Write-Section "ADD_LIBRARY write preflight"

    # A full, authoritative Library snapshot is mandatory immediately before
    # selecting writes. This catches:
    #   - songs added manually since the report was built,
    #   - a prior run that succeeded before its terminal/session was lost,
    #   - stale cached report rows.
    # If the snapshot is incomplete, writes abort.
    $preWriteLibrary = $null
    $reuseRecent = $false
    $recentAgeMinutes = $null

    if (
        $RecentWritePreflightMaxAgeMinutes -gt 0 -and
        $script:LibrarySnapshotValidatedThisRun -and
        $null -ne $cache.Library -and
        [bool]$cache.Library.Complete -and
        $cache.Library.ScannedAt
    ) {
        try {
            $scannedAt = [DateTimeOffset]::Parse([string]$cache.Library.ScannedAt)
            $recentAgeMinutes = ([DateTimeOffset]::UtcNow - $scannedAt.ToUniversalTime()).TotalMinutes
        }
        catch {
            $recentAgeMinutes = $null
        }

        if (
            $null -ne $recentAgeMinutes -and
            $recentAgeMinutes -ge 0 -and
            $recentAgeMinutes -le $RecentWritePreflightMaxAgeMinutes
        ) {
            if ($ReuseRecentWritePreflight.IsPresent) {
                $reuseRecent = $true
            }
            elseif (-not $NoInteractive.IsPresent) {
                Write-Host ""
                Write-Host ("A complete Library snapshot from {0:N1} minute(s) ago is available." -f $recentAgeMinutes) -ForegroundColor Cyan
                Write-Host "Its recently-added head was checked live and matched during THIS run."
                Write-Host "This can resume an auth-only failure without rereading the entire Library."
                Write-Warning "Reuse is slightly less strict than taking another full snapshot immediately before the write."
                $reuseAnswer = Read-Host "Type YES to reuse this recent verified snapshot, or press ENTER for a fresh full preflight"
                if ($reuseAnswer -ceq "YES") {
                    $reuseRecent = $true
                }
            }
        }
    }

    if ($reuseRecent) {
        $preWriteLibrary = $cache.Library
        Write-ProgressLine ("Reusing recent complete Song Library snapshot for write preflight ({0:N1} min old)." -f `
            $recentAgeMinutes) Green
    }
    else {
        if ($ReuseRecentWritePreflight.IsPresent) {
            Write-Warning "Recent preflight reuse was requested, but no eligible recent head-validated complete snapshot exists. Performing a fresh full preflight."
        }

        $preWriteLibrary = Get-FreshCompleteLibrarySnapshotForWriteSafety `
            "pre-ADD_LIBRARY verification"

        $cache.Library = $preWriteLibrary
        Save-TidyCache $cache
    }

    $preWriteIds = Get-LibraryVideoIdSet $preWriteLibrary

    $mutationState = Load-AddLibraryMutationState
    $mutationMap = Get-AddLibraryMutationMap $mutationState

    $alreadyPresentRows = [System.Collections.Generic.List[object]]::new()
    $pendingRows = [System.Collections.Generic.List[object]]::new()

    foreach ($row in $addLibraryRows) {
        $videoId = [string]$row.VideoId
        if (-not $videoId -or -not [string]$row.FeedbackToken) {
            continue
        }

        if ($preWriteIds.Contains($videoId)) {
            $alreadyPresentRows.Add($row)
            Set-AddLibraryMutationResult `
                -MutationMap $mutationMap `
                -VideoId $videoId `
                -Title ([string]$row.Title) `
                -Artist ([string]$row.Artist) `
                -Status "VERIFIED_PRESENT" `
                -VerifiedAt (Get-NowIso) `
                -Note "already present in authoritative pre-write Library snapshot"
        }
        else {
            $pendingRows.Add($row)
        }
    }

    Save-AddLibraryMutationMap $mutationMap

    $selectedRows = @($pendingRows)
    if ($AddLibraryWriteLimit -gt 0) {
        $selectedRows = @($selectedRows | Select-Object -First $AddLibraryWriteLimit)
    }

    Write-Host ("Action proposals from report:       {0}" -f $addLibraryRows.Count)
    Write-Host ("Already present at write preflight: {0}" -f $alreadyPresentRows.Count)
    Write-Host ("Currently missing/actionable:       {0}" -f $pendingRows.Count)
    Write-Host ("Selected for this write run:        {0}" -f $selectedRows.Count)
    if ($AddLibraryWriteLimit -gt 0 -and $pendingRows.Count -gt $selectedRows.Count) {
        Write-Host ("Deferred by safety cap:             {0}" -f `
            ($pendingRows.Count - $selectedRows.Count)) -ForegroundColor Yellow
    }

    if (
        Confirm-BulkAction `
            "Bulk action: add selected missing songs to the YouTube Music song Library" `
            $selectedRows.Count `
            (("This run sends up to {0} feedback token(s) per request, checkpoints every API-accepted batch, " +
              "then performs a fresh complete Library snapshot and verifies EVERY selected Video ID.") -f `
                $AddLibraryFeedbackBatchSize)
    ) {
        # Required again immediately before the first write. If browser auth
        # expired after the expensive Library preflight, v24 can refresh it
        # in-place and retry this identity check without repeating the preflight.
        if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "ADD_LIBRARY")) {
            Write-Host "ADD_LIBRARY write cancelled." -ForegroundColor Yellow
            exit 0
        }

        $apiAcceptedRows = [System.Collections.Generic.List[object]]::new()
        $selectedArray = @($selectedRows)
        $selectedCount = $selectedArray.Count
        $batchCount = 0
        if ($selectedCount -gt 0) {
            $batchCount = [int][Math]::Ceiling(
                [double]$selectedCount / [double]$AddLibraryFeedbackBatchSize
            )
        }

        $writeIndex = 0
        $batchOrdinal = 0

        while ($writeIndex -lt $selectedCount) {
            $batchOrdinal++
            $take = [Math]::Min(
                $AddLibraryFeedbackBatchSize,
                ($selectedCount - $writeIndex)
            )

            if ($take -eq 1) {
                $batchRows = @($selectedArray[$writeIndex])
            }
            else {
                $batchRows = @(
                    $selectedArray[$writeIndex..($writeIndex + $take - 1)]
                )
            }

            $tokens = [System.Collections.Generic.List[string]]::new()
            foreach ($row in $batchRows) {
                $token = [string]$row.FeedbackToken
                if (-not $token) {
                    throw ("ADD_LIBRARY batch {0} contains a row with no feedback token: {1}" -f `
                        $batchOrdinal, [string]$row.VideoId)
                }
                $tokens.Add($token)
            }

            Write-ProgressLine ("ADD_LIBRARY batch [{0}/{1}] rows {2}-{3} of {4} ({5} token(s))" -f `
                $batchOrdinal,
                $batchCount,
                ($writeIndex + 1),
                ($writeIndex + $take),
                $selectedCount,
                $tokens.Count) Cyan

            # Persist SUBMITTING_BATCH before the request. If the process dies
            # after YouTube accepts a batch but before we can checkpoint the
            # HTTP success, the next run's authoritative pre-write Library
            # snapshot reconciles which exact Video IDs actually landed.
            foreach ($row in $batchRows) {
                Set-AddLibraryMutationResult `
                    -MutationMap $mutationMap `
                    -VideoId ([string]$row.VideoId) `
                    -Title ([string]$row.Title) `
                    -Artist ([string]$row.Artist) `
                    -Status "SUBMITTING_BATCH" `
                    -BatchOrdinal $batchOrdinal `
                    -BatchSize $batchRows.Count `
                    -Note ("about to submit in ADD_LIBRARY feedback batch {0}/{1}" -f `
                        $batchOrdinal, $batchCount)
            }
            Save-AddLibraryMutationMap $mutationMap

            # Current YouTube Music feedback semantics accept a LIST of feedback
            # tokens in one request. The response does not give a trustworthy
            # per-song success breakdown, so API acceptance is checkpointed for
            # the whole batch and every Video ID is verified afterward against
            # a fresh complete Song Library snapshot.
            [void](Invoke-YtmRequest `
                -Endpoint "feedback" `
                -Body @{ feedbackTokens = $tokens.ToArray() } `
                -ContextLabel ("ADD_LIBRARY batch {0}/{1} ({2} token(s))" -f `
                    $batchOrdinal, $batchCount, $tokens.Count))

            $acceptedAt = Get-NowIso
            foreach ($row in $batchRows) {
                Set-AddLibraryMutationResult `
                    -MutationMap $mutationMap `
                    -VideoId ([string]$row.VideoId) `
                    -Title ([string]$row.Title) `
                    -Artist ([string]$row.Artist) `
                    -Status "API_ACCEPTED" `
                    -ApiAcceptedAt $acceptedAt `
                    -BatchOrdinal $batchOrdinal `
                    -BatchSize $batchRows.Count `
                    -Note ("feedback batch {0}/{1} returned successfully; awaiting exact-ID Library verification" -f `
                        $batchOrdinal, $batchCount)

                $apiAcceptedRows.Add($row)
            }
            Save-AddLibraryMutationMap $mutationMap

            Write-ProgressLine ("    checkpointed API acceptance for {0} row(s)" -f `
                $batchRows.Count) DarkCyan

            $writeIndex += $take

            if ($BatchDelaySeconds -gt 0 -and $writeIndex -lt $selectedCount) {
                Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 1000)) -Phase "batch delay"
            }
        }

        if ($AddLibraryVerificationDelaySeconds -gt 0 -and $apiAcceptedRows.Count -gt 0) {
            Write-ProgressLine ("Waiting {0}s before post-write Library verification..." -f `
                $AddLibraryVerificationDelaySeconds) DarkGray
            Start-PausableSleep -Seconds $AddLibraryVerificationDelaySeconds -Phase "waiting before post-write Library verification"
        }

        $postWriteLibrary = Get-FreshCompleteLibrarySnapshotForWriteSafety `
            "post-ADD_LIBRARY verification"

        $postWriteIds = Get-LibraryVideoIdSet $postWriteLibrary
        $verificationMissing = [System.Collections.Generic.List[object]]::new()
        $verifiedCount = 0

        foreach ($row in $apiAcceptedRows) {
            $videoId = [string]$row.VideoId

            if ($postWriteIds.Contains($videoId)) {
                $verifiedCount++
                Set-AddLibraryMutationResult `
                    -MutationMap $mutationMap `
                    -VideoId $videoId `
                    -Title ([string]$row.Title) `
                    -Artist ([string]$row.Artist) `
                    -Status "VERIFIED_PRESENT" `
                    -VerifiedAt (Get-NowIso) `
                    -Note "confirmed by authoritative post-write Song Library snapshot"
            }
            else {
                $verificationMissing.Add($row)
                Set-AddLibraryMutationResult `
                    -MutationMap $mutationMap `
                    -VideoId $videoId `
                    -Title ([string]$row.Title) `
                    -Artist ([string]$row.Artist) `
                    -Status "VERIFY_MISSING" `
                    -Note "feedback endpoint returned successfully but Video ID was absent from post-write Library snapshot"
            }
        }

        Save-AddLibraryMutationMap $mutationMap

        # The verified Library snapshot is now authoritative, so keep it in the
        # main tidy cache. A Library-only write no longer needs a wasteful full
        # playlist crawl on the next run.
        $cache.Library = $postWriteLibrary
        Save-TidyCache $cache

        $didWrite = ($apiAcceptedRows.Count -gt 0)
        $didLibraryWrite = $didWrite

        Write-Host ""
        Write-Host ("ADD_LIBRARY API accepted: {0}" -f $apiAcceptedRows.Count) -ForegroundColor Green
        Write-Host ("ADD_LIBRARY verified present: {0}" -f $verifiedCount) -ForegroundColor Green
        Write-Host ("ADD_LIBRARY verification missing: {0}" -f $verificationMissing.Count) `
            -ForegroundColor $(if ($verificationMissing.Count -eq 0) { "Green" } else { "Yellow" })
        Write-Host ("Song Library count: {0} -> {1}" -f `
            $preWriteLibrary.Count, $postWriteLibrary.Count) -ForegroundColor Green
        Write-Host ("ADD_LIBRARY state: {0}" -f $AddLibraryMutationStatePath) -ForegroundColor Green

        if ($verificationMissing.Count -gt 0) {
            throw @"
One or more ADD_LIBRARY requests were accepted by the API but did not appear in
the authoritative post-write Song Library snapshot.

The mutation checkpoint has been preserved with status VERIFY_MISSING.
Do NOT continue with a larger write batch yet. Re-run in -ReportOnly mode and
inspect the affected rows first.
"@
        }
    }
}

$addExpectedRows = @($actions | Where-Object { $_.Action -eq "ADD_EXPECTED_PLAYLIST" })
if (
    Confirm-BulkAction `
        "Bulk action: add songs missing from their exact expected managed playlist" `
        $addExpectedRows.Count `
        "This uses only historical exact Video ID mappings, not artist inference."
) {
    if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "add exact expected playlist placements")) {
        Write-Host "Playlist-add write cancelled." -ForegroundColor Yellow
        exit 0
    }

    foreach ($group in @($addExpectedRows | Group-Object ToPlaylistId)) {
        $playlistIdKey = [string]$group.Name
        $title = [string]$group.Group[0].ToPlaylist
        $ids = @($group.Group | ForEach-Object { [string]$_.VideoId })

        $index = 0
        while ($index -lt $ids.Count) {
            $take = [Math]::Min($BatchSize, $ids.Count - $index)
            if ($take -eq 1) {
                $batch = @($ids[$index])
            }
            else {
                $batch = @($ids[$index..($index + $take - 1)])
            }

            $res = Invoke-AddBatch $playlistIdKey $title $batch
            if (-not $res.Success) {
                # Conservative fallback: try one at a time so duplicates do not
                # poison the whole batch.
                foreach ($id in $batch) {
                    [void](Invoke-AddBatch $playlistIdKey $title @($id))
                }
            }

            $index += $take
            if ($BatchDelaySeconds -gt 0 -and $index -lt $ids.Count) {
                Start-PausableSleep -Milliseconds ([int]($BatchDelaySeconds * 1000)) -Phase "batch delay"
            }
        }
    }

    $didWrite = $true
    $didTopologyWrite = $true
    Write-Host ("Processed {0} missing exact playlist placement(s)." -f $addExpectedRows.Count) -ForegroundColor Green
}

$removeWrongRows = @($actions | Where-Object { $_.Action -eq "REMOVE_WRONG_MANAGED_PLAYLIST" })
if (
    Confirm-BulkAction `
        "Bulk action: remove exact-known songs from wrong/extra managed playlists" `
        $removeWrongRows.Count `
        "Main and Other are protected. Only items with setVideoId are removable."
) {
    if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "remove wrong/extra managed playlist placements")) {
        Write-Host "Playlist-removal write cancelled." -ForegroundColor Yellow
        exit 0
    }

    foreach ($group in @($removeWrongRows | Group-Object FromPlaylistId)) {
        $playlistIdKey = [string]$group.Name
        $title = [string]$group.Group[0].FromPlaylist

        if (
            $ProtectedPlaylistIds -contains $playlistIdKey -or
            $ProtectedPlaylistTitles -contains $title
        ) {
            Write-Warning ("Protected playlist skipped: {0}" -f $title)
            continue
        }

        Invoke-RemovePlaylistBatch $playlistIdKey $title @($group.Group)
    }

    $didWrite = $true
    $didTopologyWrite = $true
    Write-Host ("Processed {0} wrong/extra managed playlist removal(s)." -f $removeWrongRows.Count) -ForegroundColor Green
}

$likeRows = @($actions | Where-Object { $_.Action -eq "LIKE" })
if (
    Confirm-BulkAction `
        "OPTIONAL bulk action: like every currently-unliked topology track" `
        $likeRows.Count `
        "This can substantially grow Liked Music. It is intentionally last and optional."
) {
    if (-not (Confirm-YtmAccountWithAuthRecovery -WriteContext "bulk LIKE")) {
        Write-Host "LIKE write cancelled." -ForegroundColor Yellow
        exit 0
    }
    Invoke-LikeBatch $likeRows
    $didWrite = $true
    $didTopologyWrite = $true
    Write-Host ("Liked {0} song(s)." -f $likeRows.Count) -ForegroundColor Green
}

if ($didWrite) {
    if ($didTopologyWrite) {
        # Playlist/Like mutations can invalidate cached topology and menu state.
        $cache.ForceFullNextRun = $true
        Save-TidyCache $cache

        Write-Host ""
        Write-Host "Topology-affecting changes were made. The next tidy run will automatically perform one full rescan." -ForegroundColor Yellow
    }
    elseif ($didLibraryWrite) {
        # v27 already performed a complete post-write Song Library verification
        # and stored that authoritative snapshot. Keep playlist caches reusable.
        $cache.ForceFullNextRun = $false
        Save-TidyCache $cache

        Write-Host ""
        Write-Host "Library-only changes were post-verified. The next tidy run may remain incremental." -ForegroundColor Green
    }
    elseif ($didPlaylistSettingsWrite) {
        # Playlist metadata/settings do not change track topology.
        $cache.ForceFullNextRun = $false
        Save-TidyCache $cache

        Write-Host ""
        Write-Host "Managed playlist settings were post-verified. Track topology cache remains valid." -ForegroundColor Green
    }
}
else {
    Write-Host ""
    Write-Host "No bulk actions were confirmed. Cache remains valid for incremental checks." -ForegroundColor Green
}

Write-Host ""
Write-ProgressLine ("Done. Total runtime {0}; HTTP requests {1}." -f (Format-Elapsed $script:RunStopwatch.Elapsed), $script:RequestCount) Green

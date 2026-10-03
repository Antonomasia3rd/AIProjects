[CmdletBinding()]
param([string]$RepositoryRoot = (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))))

$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'YouTubeMusicMigrateSourceCheck.ps1') -RepositoryRoot $RepositoryRoot
$script:Checks = 0
function Require([bool]$condition, [string]$message) {
    $script:Checks++
    if (-not $condition) { throw $message }
}

# Copy only the thin entry points. The real engines are parsed by the source
# check above, but are NEVER evaluated, copied into the fixture or dot-sourced.
# This fixture needs no browser, auth files, network, clipboard or live account.
$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$fixtureName = 'AIProjects-YtmWrapper-' + [Guid]::NewGuid().ToString('N')
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $temporaryBase $fixtureName))
$productRoot = Join-Path $fixtureRoot 'legacy/YouTubeMusicMigrate'
$dependencyRoot = Join-Path $fixtureRoot 'dependencies/YouTubeMusicMigrate'
$mainPath = Join-Path $productRoot 'youtube_music_tidy.ps1'
$cleanupPath = Join-Path $productRoot 'cleanup_youtube_music_tidy_directory.ps1'
$mainEngine = Join-Path $dependencyRoot 'youtube_music_tidy_app.ps1'
$utf8 = New-Object Text.UTF8Encoding($true)
$pathDefaults = [ordered]@{
    MigrationCsvPath = 'youtube_music_migration.csv'
    AutoAssignmentsPath = 'youtube_music_auto_assignments.csv'
    StatePath = 'youtube_music_migration_state.json'
    HeadersPath = 'raw_headers.txt'
    CachePath = 'youtube_music_tidy_cache.json'
    ReportPath = 'reports/youtube_music_tidy_report.csv'
    ActionsPath = 'reports/youtube_music_tidy_actions.csv'
    ConfigPath = 'youtube_music_tidy_config.json'
    PlaylistSettingsReportPath = 'reports/youtube_music_tidy_playlist_settings.csv'
    TasteLaneSuggestionsPath = 'reports/youtube_music_tidy_taste_lane_suggestions.csv'
    NonMusicPlaylistConfigPath = 'youtube_music_tidy_nonmusic_playlists.txt'
    LibraryResolutionCachePath = 'youtube_music_tidy_library_resolution.json'
    LibraryTokenCachePath = 'youtube_music_tidy_library_tokens.json'
    AddLibraryMutationStatePath = 'youtube_music_tidy_add_library_state.json'
}
$mainStub = @'
param(
    [Parameter(Mandatory = $true)][string]$DataRoot,
    [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.IDictionary]$InvocationParameters
)
function Read-FixtureScriptScope { return $script:ExpectedAccountName }
$paths = @{}
foreach ($name in @('MigrationCsvPath', 'AutoAssignmentsPath', 'StatePath', 'HeadersPath',
    'CachePath', 'ReportPath', 'ActionsPath', 'ConfigPath', 'PlaylistSettingsReportPath',
    'TasteLaneSuggestionsPath', 'NonMusicPlaylistConfigPath', 'LibraryResolutionCachePath',
    'LibraryTokenCachePath', 'AddLibraryMutationStatePath')) {
    $paths[$name] = (Get-Variable -Name $name -ValueOnly)
}
[pscustomobject]@{
    Marker = 'inert-main-engine'
    DataRoot = $DataRoot
    Paths = $paths
    Explicit = $InvocationParameters
    ExpectedAccountName = $ExpectedAccountName
    ScriptAccountName = (Read-FixtureScriptScope)
    ExpectedChannelHandle = $ExpectedChannelHandle
    Setup = $Setup.IsPresent
    ReportOnly = $ReportOnly.IsPresent
    NoInteractive = $NoInteractive.IsPresent
    HeadCheckCount = $HeadCheckCount
    BatchSize = $BatchSize
    BatchDelaySeconds = $BatchDelaySeconds
    AdditionalIds = @($AdditionalNonMusicPlaylistIds)
}
'@
$cleanupStub = @'
param([switch]$Apply, [string]$Root)
[pscustomobject]@{ Marker = 'inert-cleanup-engine'; Apply = $Apply.IsPresent; Root = $Root }
'@
try {
    [IO.Directory]::CreateDirectory($productRoot) | Out-Null
    [IO.Directory]::CreateDirectory($dependencyRoot) | Out-Null
    foreach ($name in @('youtube_music_tidy.ps1', 'cleanup_youtube_music_tidy_directory.ps1')) {
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot ('legacy/YouTubeMusicMigrate/' + $name)) -Destination (Join-Path $productRoot $name)
    }
    [IO.File]::WriteAllText($mainEngine, $mainStub, $utf8)
    [IO.File]::WriteAllText((Join-Path $dependencyRoot 'cleanup_youtube_music_tidy_directory_app.ps1'), $cleanupStub, $utf8)

    $default = & $mainPath
    Require ($default.Marker -ceq 'inert-main-engine') 'The entry point must invoke the inert fixture engine.'
    Require ($default.DataRoot -ceq $productRoot) 'The data root must remain the product directory.'
    Require ($default.Explicit.Count -eq 0) 'Defaulted or internal options must not become explicit arguments.'
    foreach ($name in $pathDefaults.Keys) {
        Require ([IO.Path]::GetFullPath($default.Paths[$name]) -ceq [IO.Path]::GetFullPath((Join-Path $productRoot $pathDefaults[$name]))) "Default data path changed: $name"
    }
    Require ($default.HeadCheckCount -eq 5 -and $default.BatchSize -eq 20 -and $default.BatchDelaySeconds -eq 2.0) 'Numeric defaults changed.'
    Require (-not $default.Setup -and -not $default.ReportOnly -and -not $default.NoInteractive) 'Switch defaults changed.'
    Require ($default.ExpectedAccountName -ceq '' -and $default.ExpectedChannelHandle -ceq '') 'Account defaults must remain empty.'
    Require ($default.AdditionalIds.Count -eq 0) 'Playlist defaults must remain empty.'

    $custom = @{}
    foreach ($name in $pathDefaults.Keys) { $custom[$name] = Join-Path $fixtureRoot ('custom folder/' + $name + '.fixture') }
    $custom.ExpectedAccountName = 'fixture account'
    $custom.ExpectedChannelHandle = ''
    $custom.Setup = $true
    $custom.ReportOnly = $false
    $custom.NoInteractive = $true
    $custom.HeadCheckCount = 0
    $custom.BatchSize = 3
    $custom.BatchDelaySeconds = 0.25
    $custom.AdditionalNonMusicPlaylistIds = @('fixture-a', 'fixture-b')
    $actual = & $mainPath @custom
    Require ($actual.DataRoot -ceq $productRoot) 'Path overrides must not change the debug-output data root.'
    foreach ($name in $pathDefaults.Keys) {
        Require ($actual.Paths[$name] -ceq $custom[$name]) "Custom path was lost or rewritten: $name"
    }
    Require ((@($actual.Explicit.Keys | Sort-Object) -join ',') -ceq (@($custom.Keys | Sort-Object) -join ',')) 'The original explicit-argument key set must survive forwarding.'
    Require ($actual.ExpectedAccountName -ceq 'fixture account' -and $actual.ScriptAccountName -ceq 'fixture account') 'Shared functions must see the product script scope.'
    Require ($actual.Explicit.Contains('ExpectedChannelHandle') -and $actual.ExpectedChannelHandle -ceq '') 'Explicit empty identity differs from an omitted identity.'
    Require ($actual.Setup -and -not $actual.ReportOnly -and $actual.NoInteractive) 'True and explicit-false switches must retain their values.'
    Require ($actual.HeadCheckCount -eq 0 -and $actual.BatchSize -eq 3 -and $actual.BatchDelaySeconds -eq 0.25) 'Zero and fractional numeric options must retain their values.'
    Require (($actual.AdditionalIds -join ',') -ceq 'fixture-a,fixture-b') 'Array options must not become one joined string.'
    $configOnly = & $mainPath -ConfigPath 'relative config.json'
    Require ($configOnly.Explicit.Count -eq 1 -and $configOnly.Explicit.Contains('ConfigPath')) 'Config-only invocation must still be identifiable for interactive startup.'
    Require ($configOnly.Paths.ConfigPath -ceq 'relative config.json') 'Explicit relative paths must retain their existing meaning.'

    $cleanup = & $cleanupPath
    Require ($cleanup.Marker -ceq 'inert-cleanup-engine') 'Cleanup must select its shared implementation.'
    Require ($cleanup.Root -ceq $productRoot -and -not $cleanup.Apply) 'Cleanup must default to the product root and dry run.'
    $cleanup = & $cleanupPath -Root 'relative fixture root' -Apply
    Require ($cleanup.Root -ceq 'relative fixture root' -and $cleanup.Apply) 'Cleanup must forward explicit root and apply options.'
    $cleanup = & $cleanupPath -Apply:$false
    Require (-not $cleanup.Apply) 'Cleanup must preserve an explicit false Apply switch.'

    [IO.File]::WriteAllText($mainEngine, "throw 'inert fixture failure'", $utf8)
    $failed = $false
    try { & $mainPath | Out-Null } catch { $failed = $_.Exception.Message -eq 'inert fixture failure' }
    Require $failed 'The wrapper must propagate an engine failure.'
    Require (-not (Test-Path -LiteralPath (Join-Path $productRoot 'raw_headers.txt'))) 'The fixture must not create or need auth files.'
} finally {
    if (Test-Path -LiteralPath $fixtureRoot) {
        $expected = [IO.Path]::GetFullPath((Join-Path $temporaryBase $fixtureName))
        $actualRoot = Get-Item -LiteralPath $fixtureRoot
        if ($actualRoot.FullName -cne $expected -or
            ($actualRoot.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw 'Refusing cleanup outside the exact temporary fixture directory.'
        }
        Remove-Item -LiteralPath $actualRoot.FullName -Recurse -Force
    }
}
Write-Output ("YouTubeMusicMigrate inert wrapper tests passed: {0} checks." -f $script:Checks)

param([Parameter(Mandatory = $true)][string]$RequestPath)
$ErrorActionPreference = 'Stop'
$script:AipYtmRequest = Get-Content -LiteralPath $RequestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ($script:AipYtmRequest.Version -ne 1) { throw 'Unsupported YouTube Music worker request version.' }
$allowed = @('MigrationCsvPath','AutoAssignmentsPath','StatePath','HeadersPath','CachePath','ReportPath','ActionsPath','ExpectedAccountName','ExpectedChannelHandle','ConfigPath','Setup','NoInteractive','HeadCheckCount','BatchSize','BatchDelaySeconds','MaxRetries','RetryBaseSeconds','FullScan','ReportOnly','ManagedPlaylistsOnly','NormalizeManagedPlaylistSettings','PlaylistSettingsReportPath','SuggestTasteLanes','TasteLaneCount','TasteLaneSuggestionsPath','ReusePlaylistCache','IncludeLikeActions','TraceRequests','QuietRequests','IncludeDynamicPlaylists','AdditionalNonMusicPlaylistIds','NonMusicPlaylistConfigPath','ResolveUnknownLibrary','ResolveLibraryLimit','LibraryResolutionCachePath','EnrichLibraryTokens','EnrichLibraryTokenLimit','RetryUnavailableLibraryTokens','LibraryTokenCachePath','AddLibraryWriteLimit','AddLibraryFeedbackBatchSize','AddLibraryMutationStatePath','AddLibraryVerificationDelaySeconds','ReuseRecentWritePreflight','RecentWritePreflightMaxAgeMinutes','NetworkOutageRetryMinutes','CheckpointEveryPlaylists')
$supplied = @($script:AipYtmRequest.Values.PSObject.Properties.Name)
if ($supplied.Count -ne $allowed.Count -or @($supplied | Where-Object { $_ -notin $allowed }).Count) { throw 'The worker request must contain the complete known parameter set.' }
if ($script:AipYtmRequest.Intent -notin @('legacy','menu','run')) { throw 'Unknown worker launch intent.' }
$userIdentity = [Environment]::UserName
$mutexPrefix = 'AIProjects.YouTubeMusicWorker.'
if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
    $mutexPrefix = 'Global\AIProjects.YouTubeMusicWorker.'
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    try { $userIdentity = $identity.User.Value } finally { $identity.Dispose() }
}
$hasher = [Security.Cryptography.SHA256]::Create()
try {
    $scope = $userIdentity + '|' + [IO.Path]::GetFullPath([string]$script:AipYtmRequest.DataRoot).TrimEnd([IO.Path]::DirectorySeparatorChar).ToUpperInvariant()
    $digest = [BitConverter]::ToString($hasher.ComputeHash([Text.Encoding]::UTF8.GetBytes($scope))).Replace('-', '')
} finally { $hasher.Dispose() }
$workerMutex = New-Object Threading.Mutex($false, ($mutexPrefix + $digest))
$workerLocked = $false
try {
    try { $workerLocked = $workerMutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $workerLocked = $true }
    if (-not $workerLocked) { throw 'Another YouTube Music worker is already using this data directory. Wait for it to finish or cancel it through its runtime controls.' }
$originalArguments = @{}
foreach ($entry in $script:AipYtmRequest.Values.PSObject.Properties) {
    $value = $entry.Value
    if ($entry.Name -in @($script:AipYtmRequest.Switches)) { $value = [Management.Automation.SwitchParameter][bool]$value }
    elseif ($entry.Name -eq 'AdditionalNonMusicPlaylistIds') { $value = [string[]]@($value) }
    elseif ($entry.Name -eq 'BatchDelaySeconds') { $value = [double]$value }
    elseif ($entry.Name -in @('HeadCheckCount','BatchSize','MaxRetries','RetryBaseSeconds','TasteLaneCount','ResolveLibraryLimit','EnrichLibraryTokenLimit','AddLibraryWriteLimit','AddLibraryFeedbackBatchSize','AddLibraryVerificationDelaySeconds','RecentWritePreflightMaxAgeMinutes','NetworkOutageRetryMinutes','CheckpointEveryPlaylists')) { $value = [int]$value }
    Set-Variable -Name $entry.Name -Value $value -Scope Script
    if ($entry.Name -in @($script:AipYtmRequest.Explicit)) { $originalArguments[$entry.Name] = $value }
}
. (Join-Path $PSScriptRoot 'powershell_native_launcher.ps1')
$script:AipIdentityBaseline = @{}
foreach ($key in @('ExpectedAccountName','ExpectedChannelHandle','DataRoot','ConfigPath')) { $script:AipIdentityBaseline[$key] = [string]$script:AipYtmRequest.IdentityBaseline.$key }
function Sync-YtmProfileIdentity {
    param($Config)
    $desired = @{ ExpectedAccountName = [string]$Config.ExpectedAccountName; ExpectedChannelHandle = [string]$Config.ExpectedChannelHandle }
    if ($desired.ExpectedAccountName -ceq $script:AipIdentityBaseline.ExpectedAccountName -and
        $desired.ExpectedChannelHandle -ceq $script:AipIdentityBaseline.ExpectedChannelHandle) { return }
    $request = @{ IniPath = $script:AipYtmRequest.IniPath; DataRoot = $script:AipYtmRequest.ProfileRoot; Baseline = $script:AipIdentityBaseline; Values = $desired }
    $temporary = Join-Path ([IO.Path]::GetTempPath()) ('AIProjects-YtmIdentity-' + [Guid]::NewGuid().ToString('N') + '.json')
    try {
        [IO.File]::WriteAllText($temporary, ($request | ConvertTo-Json -Depth 8), (New-Object Text.UTF8Encoding($false)))
        $code = Invoke-AipNativeExecutable -ExecutablePath $script:AipYtmRequest.HostPath -ArgumentList @('--persist-worker-preferences', $temporary)
        if ($code -ne 0) { throw 'The confirmed account preferences could not be saved to the INI. No parallel identity copy was written; reload preferences and retry setup.' }
        foreach ($key in $desired.Keys) { $script:AipIdentityBaseline[$key] = $desired[$key] }
    } finally { if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) } }
}
. (Join-Path $PSScriptRoot 'youtube_music_tidy_app.ps1') -DataRoot $script:AipYtmRequest.DataRoot -InvocationParameters $originalArguments -LaunchIntent $script:AipYtmRequest.Intent -UseIniIdentity
} finally {
    if ($workerLocked) { $workerMutex.ReleaseMutex() }
    $workerMutex.Dispose()
}

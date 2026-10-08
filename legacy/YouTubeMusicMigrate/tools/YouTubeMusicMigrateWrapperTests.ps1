[CmdletBinding()]
param([string]$RepositoryRoot)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) { $RepositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) }
& (Join-Path $PSScriptRoot 'YouTubeMusicMigrateSourceCheck.ps1') -RepositoryRoot $RepositoryRoot
$script:checks = 0
function Require([bool]$ok, [string]$message) { $script:checks++; if (-not $ok) { throw $message } }
$parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$root = Join-Path $parent ('AIProjects-YtmBridge-' + [Guid]::NewGuid().ToString('N'))
$utf8 = New-Object Text.UTF8Encoding($true)
try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    $product = Join-Path $root 'product'; $build = Join-Path $product 'build'
    [IO.Directory]::CreateDirectory($build) | Out-Null
    Copy-Item -LiteralPath (Join-Path $RepositoryRoot 'legacy/YouTubeMusicMigrate/youtube_music_tidy.ps1') -Destination $product
    foreach ($file in @('youtube_music_legacy_frontend.ps1','youtube_music_worker_bridge.ps1')) {
        Copy-Item -LiteralPath (Join-Path $RepositoryRoot ('dependencies/YouTubeMusicMigrate/' + $file)) -Destination $build
    }
    [IO.File]::WriteAllText((Join-Path $build 'YouTubeMusicMigrate.exe'), 'inert placeholder; never executed')
    $nativeStub = @'
function Invoke-AipNativeExecutable {
    param([string]$ExecutablePath, [string[]]$ArgumentList)
    $global:YtmCapturedRequest = Get-Content -LiteralPath $ArgumentList[1] -Raw -Encoding UTF8 | ConvertFrom-Json
    $global:YtmCapturedRequestPath = $ArgumentList[1]
    return 0
}
'@
    [IO.File]::WriteAllText((Join-Path $build 'powershell_native_launcher.ps1'), $nativeStub, $utf8)
    $entry = Join-Path $product 'youtube_music_tidy.ps1'
    & $entry
    Require (@($global:YtmCapturedRequest.Arguments.PSObject.Properties).Count -eq 0) 'No-argument legacy invocation gained explicit defaults.'
    Require ($global:YtmCapturedRequest.DataRoot -eq $product) 'Legacy data root moved into the dependency/build directory.'
    Require (-not [IO.File]::Exists($global:YtmCapturedRequestPath)) 'Legacy request file leaked.'
    & $entry -ExpectedAccountName '' -ReportOnly:$false -HeadCheckCount 0 -BatchDelaySeconds 0.25 -AdditionalNonMusicPlaylistIds @('a','b') -IniFile 'relative.ini'
    $legacy = $global:YtmCapturedRequest
    Require (@($legacy.Arguments.PSObject.Properties).Count -eq 5) 'Legacy explicit argument set changed.'
    Require ($legacy.Arguments.ReportOnly -eq $false -and $legacy.Arguments.ExpectedAccountName -ceq '') 'Explicit false/empty values were lost.'
    Require ($legacy.Arguments.HeadCheckCount -eq 0 -and $legacy.Arguments.BatchDelaySeconds -eq 0.25) 'Zero/fractional values were changed.'
    Require (($legacy.Arguments.AdditionalNonMusicPlaylistIds -join ',') -eq 'a,b') 'Array arguments became a joined string.'
    Require ($legacy.IniPath -eq 'relative.ini') 'Explicit INI selection was changed.'

    $profileSource = [IO.File]::ReadAllText((Join-Path $RepositoryRoot 'dependencies/YouTubeMusicMigrate/youtube_music_profile.cs'))
    $catalog = @([regex]::Matches($profileSource, 'new Parameter\("([A-Za-z0-9]+)", Kind\.([A-Za-z]+), "([^"]*)"\)'))
    $values = @{}; $switches = @(); $types = @{}
    foreach ($definition in $catalog) {
        $name = $definition.Groups[1].Value; $kind = $definition.Groups[2].Value; $value = $definition.Groups[3].Value
        switch ($kind) {
            'Path' { $value = Join-Path $root $value; $types[$name] = 'String' }
            'Text' { $types[$name] = 'String' }
            'Switch' { $value = $value -eq '1'; $switches += $name; $types[$name] = 'SwitchParameter' }
            'Integer' { $value = [int]$value; $types[$name] = 'Int32' }
            'Number' { $value = [double]::Parse($value, [Globalization.CultureInfo]::InvariantCulture); $types[$name] = 'Double' }
            'List' { $value = @('a','b'); $types[$name] = 'String[]' }
        }
        $values[$name] = $value
    }
    Require ($values.Count -eq 47) 'Fixture does not cover all legacy parameters.'
    $values.ExpectedAccountName = 'one-run fixture override'
    $request = @{ Version = 1; IniPath = (Join-Path $root 'profile.ini'); DataRoot = $root; ProfileRoot = $root; HostPath = (Join-Path $build 'YouTubeMusicMigrate.exe'); Intent = 'legacy'; Values = $values; Switches = $switches; Explicit = @('ExpectedAccountName','ReportOnly'); IdentityBaseline = @{ ExpectedAccountName = 'saved fixture'; ExpectedChannelHandle = '@saved'; DataRoot = $root; ConfigPath = $values.ConfigPath } }
    # Only the small metadata writer function is loaded from the actual worker.
    # Its complete body is never evaluated; all API/clipboard/account code stays inert.
    $tokens = $null; $errors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile((Join-Path $RepositoryRoot 'dependencies/YouTubeMusicMigrate/youtube_music_tidy_app.ps1'), [ref]$tokens, [ref]$errors)
    $saveFunction = $ast.Find({ param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Save-UserConfig' }, $false).Extent.Text
    $traceStatement = $ast.Find({ param($node) $node -is [Management.Automation.Language.AssignmentStatementAst] -and $node.Left.Extent.Text -eq '$TraceRequests' }, $false).Extent.Text
    $evaluateTrace = [scriptblock]::Create('param([switch]$TraceRequests,[switch]$QuietRequests)' + "`n" + $traceStatement + "`n[bool]`$TraceRequests")
    Require ($values.TraceRequests -eq $true -and $values.QuietRequests -eq $false) 'Catalog tracing defaults diverged from the original effective default.'
    foreach ($case in @(@($true,$false,$true),@($false,$false,$false),@($true,$true,$false),@($false,$true,$false))) {
        Require ((& $evaluateTrace -TraceRequests:$case[0] -QuietRequests:$case[1]) -eq $case[2]) 'TraceRequests/QuietRequests precedence is inconsistent.'
    }
    $stub = @'
param([string]$DataRoot, [System.Collections.IDictionary]$InvocationParameters, [string]$LaunchIntent, [switch]$UseIniIdentity)
$global:YtmBridgeTypes = @{}
$global:YtmBridgeValues = @{}
foreach ($name in $script:AipYtmRequest.Values.PSObject.Properties.Name) {
    $value = Get-Variable -Name $name -ValueOnly
    $global:YtmBridgeTypes[$name] = $value.GetType().Name
    $global:YtmBridgeValues[$name] = $value
}
$global:YtmBridgeExplicit = @($InvocationParameters.Keys)
$global:YtmBridgeIntent = $LaunchIntent
$probe = [Management.Automation.PowerShell]::Create()
$probeRunspace = [Management.Automation.Runspaces.RunspaceFactory]::CreateRunspace()
$probeRunspace.ThreadOptions = [Management.Automation.Runspaces.PSThreadOptions]::UseNewThread
$probeRunspace.Open()
$probe.Runspace = $probeRunspace
try {
    [void]$probe.AddScript('param($bridge, $request) & $bridge -RequestPath $request').AddArgument((Join-Path $PSScriptRoot 'youtube_music_worker_bridge.ps1')).AddArgument($RequestPath)
    $caught = ''
    try { $null = $probe.Invoke() } catch { $caught = $_.Exception.Message }
    $global:YtmConcurrentDiagnostic = $caught + (@($probe.Streams.Error | ForEach-Object { $_.ToString() }) -join ' ')
    $global:YtmConcurrentRejected = $global:YtmConcurrentDiagnostic.Contains('already using this data directory')
} finally { $probe.Dispose(); $probeRunspace.Dispose() }
$config = [pscustomobject]@{ Version = 1; ExpectedAccountName = 'confirmed fixture'; ExpectedChannelHandle = '@confirmed'; SetupCompletedAt = 'fixture-time' }
'@
    [IO.File]::WriteAllText((Join-Path $build 'youtube_music_tidy_app.ps1'), $stub + "`n" + $saveFunction + "`nSave-UserConfig -Config `$config`n", $utf8)
    $requestPath = Join-Path $root 'request.json'
    [IO.File]::WriteAllText($requestPath, ($request | ConvertTo-Json -Depth 10), $utf8)
    & (Join-Path $build 'youtube_music_worker_bridge.ps1') -RequestPath $requestPath
    foreach ($name in $types.Keys) { Require ($global:YtmBridgeTypes[$name] -eq $types[$name]) "Worker parameter type changed: $name" }
    Require (($global:YtmBridgeExplicit | Sort-Object) -join ',' -eq 'ExpectedAccountName,ReportOnly') 'Defaults became explicit worker invocation keys.'
    Require ($global:YtmBridgeIntent -eq 'legacy') 'Launch intent was lost.'
    Require $global:YtmConcurrentRejected ('A concurrent worker was not rejected before evaluating its stub: ' + $global:YtmConcurrentDiagnostic)
    Require ($global:YtmCapturedRequest.Baseline.ExpectedAccountName -eq 'saved fixture' -and $global:YtmCapturedRequest.Values.ExpectedAccountName -eq 'confirmed fixture') 'Identity writeback lost its saved baseline or confirmed value.'
    Require (-not [IO.File]::Exists($global:YtmCapturedRequestPath)) 'Identity writeback request leaked.'
    $state = Get-Content -LiteralPath $values.ConfigPath -Raw -Encoding UTF8 | ConvertFrom-Json
    Require ($state.SetupCompletedAt -eq 'fixture-time' -and 'ExpectedAccountName' -notin @($state.PSObject.Properties.Name)) 'State JSON retained a parallel editable identity.'
} finally {
    foreach ($name in @('YtmCapturedRequest','YtmCapturedRequestPath','YtmBridgeTypes','YtmBridgeValues','YtmBridgeExplicit','YtmBridgeIntent','YtmConcurrentRejected','YtmConcurrentDiagnostic')) { Remove-Variable -Name $name -Scope Global -ErrorAction SilentlyContinue }
    $resolved = [IO.Path]::GetFullPath($root)
    if ([IO.Path]::GetDirectoryName($resolved) -ne $parent -or -not [IO.Path]::GetFileName($resolved).StartsWith('AIProjects-YtmBridge-')) { throw 'Unsafe bridge fixture cleanup path.' }
    if ([IO.Directory]::Exists($resolved)) { [IO.Directory]::Delete($resolved, $true) }
}
Write-Output "YouTube Music bridge: $script:checks checks passed using inert worker/host stubs."

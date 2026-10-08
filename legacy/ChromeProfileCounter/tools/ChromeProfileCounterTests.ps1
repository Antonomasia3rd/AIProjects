[CmdletBinding()]
param([string]$RepositoryRoot)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$checks = 0
function Require([bool]$condition, [string]$description) {
    $script:checks++
    if (-not $condition) { throw $description }
}

. (Join-Path $RepositoryRoot 'dependencies/powershell_native_launcher.ps1')
$cases = @(
    @('', '""'),
    @('plain', '"plain"'),
    @('two words', '"two words"'),
    @('C:\path with spaces\', '"C:\path with spaces\\"'),
    @('say"yes', '"say\"yes"'),
    @('a\"b', '"a\\\"b"'),
    @('a\\"b', '"a\\\\\"b"'),
    @('日本語', '"日本語"')
)
foreach ($case in $cases) {
    Require ((ConvertTo-AipNativeArgument $case[0]) -ceq $case[1]) "Argument encoding failed: $($case[0])"
}

$parent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$root = Join-Path $parent ('AIProjects-ChromeWrapper-' + [Guid]::NewGuid().ToString('N'))
$oldLocation = Get-Location
try {
    [IO.Directory]::CreateDirectory($root) | Out-Null
    Set-Location -LiteralPath $root
    $fixtureProcess = New-Object PSObject -Property @{ ExitCode = 23; Waited = $false; Disposed = $false }
    $fixtureProcess | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { $this.Waited = $true }
    $fixtureProcess | Add-Member -MemberType ScriptMethod -Name Dispose -Value { $this.Disposed = $true }
    $expected = @('--show-config', '--ini', 'folder with spaces/profile.ini', '--user-data', 'C:\path with spaces\', '', 'a"b', '$literal; value')
    $script:observedInfo = $null
    $code = Invoke-AipNativeExecutable -ExecutablePath (Join-Path $root 'unused.exe') -ArgumentList $expected -StartProcess {
        param($info)
        $script:observedInfo = $info
        return $fixtureProcess
    }
    Require ($code -eq 23 -and $fixtureProcess.Waited -and $fixtureProcess.Disposed) 'Shared launcher lost the child exit code, wait, or disposal.'
    Require ($script:observedInfo.WorkingDirectory -eq $root -and -not $script:observedInfo.UseShellExecute) 'Shared launcher ignored the current filesystem working directory.'
    Require ($script:observedInfo.Arguments -ceq (($expected | ForEach-Object { ConvertTo-AipNativeArgument $_ }) -join ' ')) 'Shared launcher lost argument order or values.'
    $fixtureProcess.Disposed = $false
    $fixtureProcess | Add-Member -MemberType ScriptMethod -Name WaitForExit -Value { throw 'Injected wait failure.' } -Force
    $failed = $false
    try { Invoke-AipNativeExecutable -ExecutablePath (Join-Path $root 'unused.exe') -StartProcess { return $fixtureProcess } | Out-Null } catch { $failed = $true }
    Require ($failed -and $fixtureProcess.Disposed) 'Wait failure leaked the process wrapper or hid the error.'
    $failed = $false
    try { Invoke-AipNativeExecutable -ExecutablePath (Join-Path $root 'unused.exe') -StartProcess { return $null } | Out-Null } catch { $failed = $true }
    Require $failed 'A failed process start must report an error.'

    # Copy the real wrapper unchanged. Only the selected helper is replaced by
    # an inert fixture; the .exe placeholders below are never executed.
    $wrapper = Join-Path $root 'ChromeProfileCounter.ps1'
    [IO.File]::Copy((Join-Path $RepositoryRoot 'legacy/ChromeProfileCounter/ChromeProfileCounter.ps1'), $wrapper)
    $stub = @'
function Invoke-AipNativeExecutable {
    param([string]$ExecutablePath, [string[]]$ArgumentList)
    $global:ChromeWrapperFixtureArguments = @($ArgumentList)
    $global:ChromeWrapperFixtureExecutable = $ExecutablePath
    return 23
}
'@
    [IO.File]::WriteAllText((Join-Path $root 'powershell_native_launcher.ps1'), $stub)
    $child = Join-Path $root 'ChromeProfileCounter.exe'
    [IO.File]::WriteAllText($child, 'inert placeholder, never executed')
    $global:LASTEXITCODE = 99
    & $wrapper @expected
    Require ($LASTEXITCODE -eq 23) 'Wrapper returned a stale exit code instead of the child result.'
    Require ($global:ChromeWrapperFixtureExecutable -eq $child) 'Wrapper failed to prefer the sibling release executable.'
    Require ($global:ChromeWrapperFixtureArguments.Count -eq $expected.Count) 'Wrapper lost an argument.'
    for ($index = 0; $index -lt $expected.Count; ++$index) {
        Require ($global:ChromeWrapperFixtureArguments[$index] -ceq $expected[$index]) "Forwarded argument $index was changed."
    }
    [IO.File]::Delete($child)
    $build = Join-Path $root 'build'
    [IO.Directory]::CreateDirectory($build) | Out-Null
    [IO.File]::WriteAllText((Join-Path $build 'ChromeProfileCounter.exe'), 'inert placeholder, never executed')
    & $wrapper --version
    Require ($LASTEXITCODE -eq 23 -and $global:ChromeWrapperFixtureExecutable -eq (Join-Path $build 'ChromeProfileCounter.exe')) 'Checkout build fallback was not used.'
    [IO.File]::Move((Join-Path $root 'powershell_native_launcher.ps1'), (Join-Path $build 'powershell_native_launcher.ps1'))
    & $wrapper --help
    Require ($LASTEXITCODE -eq 23 -and $global:ChromeWrapperFixtureArguments[0] -eq '--help') 'Folder-preserving release helper fallback was not used.'
    [IO.File]::Delete((Join-Path $build 'ChromeProfileCounter.exe'))
    $failed = $false
    try { & $wrapper --help } catch { $failed = $_.Exception.Message.Contains('is missing') }
    Require $failed 'Missing executable must fail clearly without compiling or using an old engine.'
} finally {
    Set-Location -LiteralPath $oldLocation.ProviderPath
    Remove-Variable -Name ChromeWrapperFixtureArguments,ChromeWrapperFixtureExecutable -Scope Global -ErrorAction SilentlyContinue
    $resolved = [IO.Path]::GetFullPath($root)
    if ([IO.Path]::GetDirectoryName($resolved) -ne $parent -or
        -not [IO.Path]::GetFileName($resolved).StartsWith('AIProjects-ChromeWrapper-')) { throw 'Unsafe wrapper fixture cleanup path.' }
    if ([IO.Directory]::Exists($resolved)) { [IO.Directory]::Delete($resolved, $true) }
}
Write-Output "ChromeProfileCounter wrapper/helper: $checks checks passed. Only inert process/helper fixtures were used."

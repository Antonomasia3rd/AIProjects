[CmdletBinding()]
param([string]$RepositoryRoot)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
}
$product = Join-Path $RepositoryRoot 'legacy/ChromeProfileCounter'
$wrapper = Join-Path $product 'ChromeProfileCounter.ps1'
$helper = Join-Path $RepositoryRoot 'dependencies/powershell_native_launcher.ps1'
foreach ($sourcePath in @($wrapper, $helper)) {
    $tokens = $null; $errors = $null
    [System.Management.Automation.Language.Parser]::ParseFile($sourcePath, [ref]$tokens, [ref]$errors) | Out-Null
    if ($errors.Count) { throw ($errors | Out-String) }
}
$engine = [IO.File]::ReadAllText((Join-Path $RepositoryRoot 'dependencies/ChromeProfileCounter/chrome_profile_counter_engine.cs'))
$app = [IO.File]::ReadAllText((Join-Path $RepositoryRoot 'dependencies/ChromeProfileCounter/chrome_profile_counter_app.cs'))
$launcher = [IO.File]::ReadAllText($wrapper)
foreach ($contract in @('File.Replace(temporary, destination, backup)', 'Func<bool> isBrowserRunning', 'span.Start', 'span.Length', 'new UTF8Encoding(false, true)')) {
    if (-not $engine.Contains($contract)) { throw "Missing compiled engine contract: $contract" }
}
foreach ($contract in @('ManagedProfile.ResolveSettings', 'ManagedIniFile.SaveSectionBatch', 'ManagedConfigurationTray.Run', 'new ManagedConfigurationStartup(', 'ManagedConfigurationStartup.BuildShortcut')) {
    if (-not $app.Contains($contract)) { throw "Missing shared product wiring: $contract" }
}
if (-not $launcher.Contains('Invoke-AipNativeExecutable -ExecutablePath $implementation -ArgumentList $args') -or
    -not $launcher.Contains('dependencies/powershell_native_launcher.ps1') -or
    -not $launcher.Contains('exit $nativeExitCode') -or
    -not $launcher.Contains('build/ChromeProfileCounter.exe')) {
    throw 'Compatibility launcher must forward arguments, report the current child exit code, and support checkout builds.'
}
if (Test-Path -LiteralPath (Join-Path $RepositoryRoot 'dependencies/ChromeProfileCounter/chrome_profile_counter_app.ps1')) {
    throw 'The duplicate PowerShell runtime engine was not retired.'
}
foreach ($relative in @('ChromeProfileCounter.cs', 'ChromeProfileCounter.example.ini', 'BuildChromeProfileCounter.cmd')) {
    if (-not (Test-Path -LiteralPath (Join-Path $product $relative) -PathType Leaf)) { throw "Missing product overlay: $relative" }
}
Write-Output 'ChromeProfileCounter compiled-source and compatibility launcher checks passed.'

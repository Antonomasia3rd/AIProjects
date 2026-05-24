param(
    [string[]]$SkipProjects = @(),
    [switch]$StopRunningArtifacts,
    [switch]$StopMatchingArtifactNames
)

$ErrorActionPreference = 'Stop'

$cmdArgs = @()
if ($SkipProjects.Count -gt 0) {
    $cmdArgs += '/skip:' + (($SkipProjects -join ',') -replace '\s+', '')
}
if ($StopRunningArtifacts -or $StopMatchingArtifactNames) {
    Write-Warning 'Process-stopping build options are ignored by the CMD build wrapper. Close locked output binaries manually if the compiler reports an overwrite failure.'
}

$script = Join-Path $PSScriptRoot 'build-windows.cmd'
$command = '"' + $script + '"'
if ($cmdArgs.Count -gt 0) {
    $command += ' ' + ($cmdArgs -join ' ')
}

& $env:ComSpec /d /c $command
exit $LASTEXITCODE

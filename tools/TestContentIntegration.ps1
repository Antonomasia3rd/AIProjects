#requires -Version 7.0
param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
$contentBuild = Join-Path $ReviewRoot 'build/content-integration'
$targets = @('HardwareProfileImportTests','NowPlayingProfileImportTests',
    'NotesSourceTests','SmtcSourceTests','WinrtFactoryLifetimeTests','TransparentBackgroundTests','PngCrc32Tests','DiscordProfileEditTests',
    'ContentRuntimeTests','ContentRuntimeNoHardwareTests')
foreach ($target in (@('DiscordService','DiscordEditor','AsusService') + $targets)) {
    $built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',
        (Join-Path $PSScriptRoot 'BuildContentIntegrationTests.cmd'), $target) -Label ($target + '-content-build') -Seconds 60
    Write-Output $built.Output.Trim()
    if ($built.ExitCode -ne 0) { throw "Content fixture compilation failed: $target" }
    if (-not $Run -or $targets -notcontains $target) { continue }
    $fixture = Join-Path $contentBuild ($target + '.exe')
    Assert-ReviewScan -Path $fixture -Label ($target + '-content')
    $tested = Invoke-ReviewProcess -File $fixture -Parameters @() -Label ($target + '-content-tests') -Seconds 60
    Write-Output $tested.Output.Trim()
    if ($tested.ExitCode -ne 0) { throw "Content fixture checks failed: $target" }
}
if (-not $Run) { Write-Output 'Compilation only. Pass -Run to scan and execute bounded inert fixtures.' }

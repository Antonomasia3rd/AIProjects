#requires -Version 7.0
param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
$built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',(Join-Path $PSScriptRoot 'BuildNowPlayingProfileImportTests.cmd')) -Label 'now-playing-profile-import-build' -Seconds 60
Write-Output $built.Output.Trim()
if ($built.ExitCode -ne 0) { throw "NowPlayingTile import fixture compilation failed: $($built.ExitCode)" }
if (-not $Run) { Write-Output 'Compilation only. Pass -Run to scan and execute the inert temporary-file fixture.'; exit 0 }
$fixture = Join-Path $ReviewLogs 'NowPlayingProfileImportTests.exe'
Assert-ReviewScan -Path $fixture -Label 'now-playing-profile-import'
$tested = Invoke-ReviewProcess -File $fixture -Parameters @() -Label 'now-playing-profile-import-tests' -Seconds 60
Write-Output $tested.Output.Trim()
if ($tested.ExitCode -ne 0) { throw "NowPlayingTile import fixture checks failed: $($tested.ExitCode)" }

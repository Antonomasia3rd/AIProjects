param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
$built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',(Join-Path $PSScriptRoot 'BuildManagedIniDocumentTests.cmd')) -Label 'managed-ini-document-build' -Seconds 60
Write-Output $built.Output.Trim()
if ($built.ExitCode -ne 0) { throw "INI fixture compilation failed: $($built.ExitCode)" }
if (-not $Run) {
    Write-Output 'Compilation only. Pass -Run to scan and execute the temporary-file fixture.'
    exit 0
}
$fixture = Join-Path $ReviewLogs 'ManagedIniDocumentTests.exe'
Assert-ReviewScan -Path $fixture -Label 'managed-ini-document'
$tested = Invoke-ReviewProcess -File $fixture -Parameters @() -Label 'managed-ini-document-tests' -Seconds 60
Write-Output $tested.Output.Trim()
if ($tested.ExitCode -ne 0) { throw "INI fixture checks failed: $($tested.ExitCode)" }

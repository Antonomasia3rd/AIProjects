param([switch]$Run)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
$built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',(Join-Path $PSScriptRoot 'BuildManagedIniInteropTests.cmd')) -Label 'managed-native-ini-build' -Seconds 60
Write-Output $built.Output.Trim()
if ($built.ExitCode -ne 0) { throw "Mixed INI fixture compilation failed: $($built.ExitCode)" }
if (-not $Run) { Write-Output 'Compilation only. Pass -Run to scan both workers and execute the temporary-file fixture.'; exit 0 }
$nativeWorker = Join-Path $ReviewLogs 'IniInteropNativeWorker.exe'
$managedWorker = Join-Path $ReviewLogs 'ManagedIniInteropTests.exe'
Assert-ReviewScan -Path $nativeWorker -Label 'managed-native-ini-native'
Assert-ReviewScan -Path $managedWorker -Label 'managed-native-ini-managed'
$tested = Invoke-ReviewProcess -File $managedWorker -Parameters @($nativeWorker) -Label 'managed-native-ini-tests' -Seconds 60
Write-Output $tested.Output.Trim()
if ($tested.ExitCode -ne 0) { throw "Mixed INI fixture checks failed: $($tested.ExitCode)" }

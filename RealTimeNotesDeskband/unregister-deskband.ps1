$ErrorActionPreference = 'Stop'

$Root = (Resolve-Path $PSScriptRoot).Path
$Dll = Join-Path $Root 'build\RealTimeNotesDeskband.dll'

if (-not (Test-Path $Dll)) {
    throw "Deskband DLL was not found. Build it first with: .\BuildDeskband.cmd"
}

$RegSvr32 = Join-Path $env:WINDIR 'System32\regsvr32.exe'
$Process = Start-Process -FilePath $RegSvr32 -ArgumentList @('/u', '/s', $Dll) -Wait -PassThru -WindowStyle Hidden
if ($Process.ExitCode -ne 0) {
    throw "regsvr32 /u failed with exit code $($Process.ExitCode)"
}

Write-Host "Unregistered Real Time Notes Deskband."

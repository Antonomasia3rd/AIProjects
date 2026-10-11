param([switch]$Run, [switch]$PassiveOnly)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
if (-not $PassiveOnly) {
    foreach ($stage in @('portable','asus','discord','runtime','passive','unavailable','source')) {
        $built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',(Join-Path $PSScriptRoot 'BuildHardwareHostIntegrationTests.cmd'),$stage) -Label ('hardware-host-' + $stage + '-build') -Seconds 60
        Write-Output $built.Output.Trim()
        if ($built.ExitCode -ne 0) { throw "Hardware host $stage compilation failed: $($built.ExitCode)" }
    }
}
if (-not $Run) { Write-Output 'Compilation only. Pass -Run for scan-gated inert fixtures.'; exit 0 }
if (-not $PassiveOnly) {
    foreach ($fixture in @('HardwareContentEngineTestsOct11','HardwareContentRuntimeTestsOct11','HardwareUnavailableRuntimeTestsOct11','HardwareDesktopSourceCheckOct11')) {
        $binary = Join-Path $ReviewLogs ($fixture + '.exe')
        Assert-ReviewScan -Path $binary -Label $fixture
        $workingDirectory = if ($fixture -eq 'HardwareDesktopSourceCheckOct11') { Join-Path $ReviewRoot 'DesktopStub' } else { $ReviewRoot }
        $tested = Invoke-ReviewProcess -File $binary -Parameters @() -Label ($fixture + '-tests') -Seconds 60 -Directory $workingDirectory
        if ($fixture -eq 'HardwareDesktopSourceCheckOct11') { Write-Output $tested.Output.Trim().Split("`n")[-1].Trim() }
        else { Write-Output $tested.Output.Trim() }
        if ($tested.ExitCode -ne 0) { throw "Hardware host $fixture failed: $($tested.ExitCode)" }
    }
}
$passive = Join-Path $ReviewLogs 'DesktopStubHardwarePassiveOct11.exe'
Assert-ReviewScan -Path $passive -Label 'hardware-host-passive'
$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $temporaryParent ('AIProjects-HardwareHostPassive-' + [Guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($fixtureRoot) -ne $temporaryParent) { throw 'Fixture path is outside its temporary parent.' }
$null = [IO.Directory]::CreateDirectory($fixtureRoot)
$utf8 = [Text.UTF8Encoding]::new($true, $true)
$caseNumber = 0
function Invoke-HardwarePassiveCase {
    param([string[]]$Arguments, [int]$Expected)
    $script:caseNumber++
    $result = Invoke-ReviewProcess -File $passive -Parameters $Arguments -Label ('hardware-host-passive-case-' + $script:caseNumber) -Seconds 45 -Directory $fixtureRoot
    if ($result.ExitCode -ne $Expected) { throw "Passive case $script:caseNumber returned $($result.ExitCode): $($result.Output)" }
    Write-Output "Passive hardware case $script:caseNumber passed (exit $Expected)."
}
try {
    $capsSource = Join-Path $fixtureRoot 'caps-source.ini'
    $asusSource = Join-Path $fixtureRoot 'asus-source.ini'
    $badSource = Join-Path $fixtureRoot 'invalid-source.ini'
    $lastSource = Join-Path $fixtureRoot 'last-source.ini'
    [IO.File]::WriteAllText($capsSource, "; keep Caps source`r`n[Settings]`r`nKeyboardTargetPath=\Device\KeyboardClass05`r`nBlinkIntervalMs=750`r`nRunAtStartup=true`r`n", $utf8)
    [IO.File]::WriteAllText($asusSource, "; keep ASUS source`r`n[Options]`r`nmic-state=bad`r`nno-tray=`r`n[Settings]`r`nMicState=1`r`nEvent001HddState=128,129`r`nEvent001HddInterval=500`r`n", $utf8)
    [IO.File]::WriteAllText($badSource, "[Settings]`r`nEvent001MicState=2`r`n", $utf8)
    [IO.File]::WriteAllText($lastSource, "[Settings]`r`nKeyboardState=130`r`n", $utf8)
    $capsHash = (Get-FileHash -LiteralPath $capsSource).Hash
    $asusHash = (Get-FileHash -LiteralPath $asusSource).Hash
    $capsTarget = Join-Path $fixtureRoot 'caps-target.ini'
    Invoke-HardwarePassiveCase @('--ini',$capsTarget,'--caps-import-profile',$capsSource,'--set','CapsBlink.BlinkIntervalMs=900') 0
    if (-not [IO.File]::ReadAllText($capsTarget).Contains('"BlinkIntervalMs" = "900"')) { throw 'Explicit Caps setting did not win.' }
    $target = Join-Path $fixtureRoot 'host-target.ini'
    [IO.File]::WriteAllText($target, "; preserve target`r`n[AsusBlink]`r`nHardwareEnabled=0`r`nReadDiskActivity=0`r`nPaused=1`r`nProfilePath=old.ini`r`nEvent2MicState=1`r`nEvent2MicInterval=bad`r`nVendor=keep`r`n", $utf8)
    Invoke-HardwarePassiveCase @('--ini',$target,'--asus-import-profile',$asusSource,'--set','AsusBlink.Event001HddState=140') 0
    $saved = [IO.File]::ReadAllText($target)
    if ($saved.Contains('Event2Mic') -or -not $saved.Contains('"ProfilePath" = ""') -or -not $saved.Contains('"Event001HddState" = "140"') -or
        -not $saved.Contains('HardwareEnabled=0') -or -not $saved.Contains('ReadDiskActivity=0') -or -not $saved.Contains('Paused=1') -or -not $saved.Contains('Vendor=keep')) {
        throw 'ASUS replacement lost independent state or retained stale fields.'
    }
    $before = (Get-FileHash -LiteralPath $target).Hash
    Invoke-HardwarePassiveCase @('--ini',$target,'--asus-import-profile',$badSource) 2
    if ((Get-FileHash -LiteralPath $target).Hash -ne $before) { throw 'Invalid source changed the existing target.' }
    $missingTarget = Join-Path $fixtureRoot 'never-created.ini'
    Invoke-HardwarePassiveCase @('--ini',$missingTarget,'--caps-import-profile',(Join-Path $fixtureRoot 'missing-source.ini')) 2
    if (Test-Path -LiteralPath $missingTarget) { throw 'Missing source created a target.' }
    Invoke-HardwarePassiveCase @('--configure-only','--ini',$target,'--set','AsusBlink.Event01MicState=1') 0
    Invoke-HardwarePassiveCase @('--configure-only','--ini',$target,'--set','AsusBlink.Event01MicState=2') 2
    Invoke-HardwarePassiveCase @('--ini',$target,'--asus-import-profile',$asusSource,'--asus-import-profile',$lastSource) 0
    if ([IO.File]::ReadAllText($target).Contains('Event001HddState')) { throw 'Multiple imports retained earlier source events.' }
    $before = (Get-FileHash -LiteralPath $target).Hash
    Invoke-HardwarePassiveCase @('--ini',$target,'--asus-import-profile',$target) 2
    if ((Get-FileHash -LiteralPath $target).Hash -ne $before) { throw 'Self import changed its source/target.' }
    if ((Get-FileHash -LiteralPath $capsSource).Hash -ne $capsHash -or (Get-FileHash -LiteralPath $asusSource).Hash -ne $asusHash) { throw 'Passive imports changed a donor source.' }
    Write-Output "Hardware passive CLI checks passed: $caseNumber; original source hashes unchanged."
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture) -ne $temporaryParent) { throw 'Refusing cleanup outside the known temporary parent.' }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}

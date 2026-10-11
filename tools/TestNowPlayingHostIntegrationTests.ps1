#requires -Version 7.0
param([switch]$Run, [switch]$SkipBuild, [switch]$PassiveOnly)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
if (-not $SkipBuild) {
    foreach ($stage in @('asus','discord','runtime','passive')) {
        $built = Invoke-ReviewProcess -File $env:ComSpec -Parameters @('/d','/c',(Join-Path $PSScriptRoot 'BuildNowPlayingHostIntegrationTests.cmd'),$stage) -Label ('now-playing-host-' + $stage + '-build') -Seconds 60
        Write-Output $built.Output.Trim()
        if ($built.ExitCode -ne 0) { throw "NowPlaying host $stage compilation failed: $($built.ExitCode)" }
    }
}
if (-not $Run) { Write-Output 'Compilation only. Pass -Run for scan-gated inert host and passive CLI fixtures.'; exit 0 }
if (-not $PassiveOnly) {
    $runtime = Join-Path $ReviewLogs 'NowPlayingContentRuntimeTestsOct11.exe'
    Assert-ReviewScan -Path $runtime -Label 'now-playing-host-runtime'
    $tested = Invoke-ReviewProcess -File $runtime -Parameters @() -Label 'now-playing-host-runtime-tests' -Seconds 60
    Write-Output $tested.Output.Trim()
    if ($tested.ExitCode -ne 0) { throw "Inert NowPlaying host fixture failed: $($tested.ExitCode)" }
}
$passive = Join-Path $ReviewLogs 'DesktopStubNowPlayingPassiveOct11.exe'
Assert-ReviewScan -Path $passive -Label 'now-playing-host-passive'
$temporaryParent = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd([IO.Path]::DirectorySeparatorChar)
$fixtureRoot = [IO.Path]::GetFullPath((Join-Path $temporaryParent ('AIProjects-NowPlayingHostPassive-' + [Guid]::NewGuid().ToString('N'))))
if ([IO.Path]::GetDirectoryName($fixtureRoot) -ne $temporaryParent) { throw 'Fixture path is outside its temporary parent.' }
$null = [IO.Directory]::CreateDirectory($fixtureRoot)
$utf8 = [Text.UTF8Encoding]::new($true, $true)
$caseNumber = 0
function Invoke-NowPlayingPassiveCase {
    param([string[]]$Arguments, [int]$Expected)
    $script:caseNumber++
    $result = Invoke-ReviewProcess -File $passive -Parameters $Arguments -Label ('now-playing-host-passive-case-' + $script:caseNumber) -Seconds 45 -Directory $fixtureRoot
    if ($result.ExitCode -ne $Expected) { throw "Passive case $script:caseNumber returned $($result.ExitCode): $($result.Output)" }
    Write-Output "Passive NowPlaying case $script:caseNumber passed (exit $Expected)."
    return $result
}
try {
    $source = Join-Path $fixtureRoot 'donor.ini'
    $bad = Join-Path $fixtureRoot 'bad.ini'
    $target = Join-Path $fixtureRoot 'target.ini'
    $missing = Join-Path $fixtureRoot 'never-created.ini'
    $base = "; keep host`r`n[Settings]`r`nRunAtStartup=1`r`nRunAtStartupPackaged=0`r`n[Content]`r`nCount=4`r`nCycleSeconds=29`r`n[Content.1]`r`nBackground=Image`r`nImagePath=obsolete.bmp`r`nNotesDetail=invalid-old-detail`r`n[Content.4]`r`nText=stale-entry`r`n[Unrelated]`r`nKeep=yes`r`n"
    $null = Invoke-NowPlayingPassiveCase @('--help','--ini',$missing,'--now-playing-import-profile','absent.ini') 0
    $null = Invoke-NowPlayingPassiveCase @('--version','--ini',$missing,'--now-playing-import-profile','absent.ini') 0
    if (Test-Path -LiteralPath $missing) { throw 'Help/version created an output profile.' }
    foreach ($layout in @('Text','Artwork','Combined','Cycle')) {
        [IO.File]::WriteAllText($source, "; keep donor`r`n[Settings]`r`nTile-Layout=$layout`r`nUpdateIntervalSeconds=6`r`nTileRefreshSeconds=240`r`nRunAtStartup=true`r`nWidgetFont=unsupported`r`n", $utf8)
        [IO.File]::WriteAllText($target, $base, $utf8)
        $sourceHash = (Get-FileHash -LiteralPath $source).Hash
        $result = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$source) 0
        $saved = [IO.File]::ReadAllText($target)
        $count = if ($layout -eq 'Cycle') { '3' } else { '1' }
        $mode = if ($layout -in @('Combined','Cycle')) { 'Overlay' } else { 'Auto' }
        if (-not $saved.Contains('"Count" = "' + $count + '"') -or -not $saved.Contains('"TextMode" = "' + $mode + '"') -or
            $saved.Contains('stale-entry') -or $saved.Contains('invalid-old-detail') -or $saved.Contains('obsolete.bmp') -or
            -not $saved.Contains('RunAtStartup=1') -or -not $saved.Contains('CycleSeconds=29') -or -not $saved.Contains('Keep=yes') -or
            -not $result.Output.Contains('Donor RunAtStartup=true') -or -not $result.Output.Contains('deferred') -or -not $result.Output.Contains('WidgetFont')) {
            throw "The $layout import lost preferences/diagnostics or retained stale entries."
        }
        if ((Get-FileHash -LiteralPath $source).Hash -ne $sourceHash) { throw 'An import changed its donor source.' }
    }
    $null = Invoke-NowPlayingPassiveCase @('--set','Content.RefreshSeconds=9','--ini',$target,'--now-playing-import-profile',$source) 0
    if (-not [IO.File]::ReadAllText($target).Contains('"RefreshSeconds" = "9"')) { throw 'An earlier explicit setting lost precedence.' }
    $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$source,'--set','Content.RefreshSeconds=11') 0
    if (-not [IO.File]::ReadAllText($target).Contains('"RefreshSeconds" = "11"')) { throw 'A later explicit setting lost precedence.' }
    foreach ($invalid in @("[Settings]`nTileLayout=invalid`n", "[Settings]`nUpdateIntervalSeconds=61`n", "[Settings]`nRunAtStartup=perhaps`n", "[Settings`nTileLayout=Text`n")) {
        [IO.File]::WriteAllText($bad, $invalid, $utf8)
        $before = (Get-FileHash -LiteralPath $target).Hash
        $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$bad) 2
        if ((Get-FileHash -LiteralPath $target).Hash -ne $before) { throw 'A malformed source changed the target.' }
    }
    $null = Invoke-NowPlayingPassiveCase @('--ini',$missing,'--now-playing-import-profile',(Join-Path $fixtureRoot 'missing-source.ini')) 2
    if (Test-Path -LiteralPath $missing) { throw 'A missing source created the output profile.' }
    $before = (Get-FileHash -LiteralPath $target).Hash
    $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$target) 2
    $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$source,'--startup') 2
    $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$source,'--render-only') 2
    if ((Get-FileHash -LiteralPath $target).Hash -ne $before) { throw 'Self-import or disallowed integration changed the target.' }
    $last = Join-Path $fixtureRoot 'last.ini'
    [IO.File]::WriteAllText($last, "[Settings]`nTileLayout=Text`nRunAtStartup=false`n", $utf8)
    $null = Invoke-NowPlayingPassiveCase @('--ini',$target,'--now-playing-import-profile',$source,'--now-playing-import-profile',$last) 0
    $saved = [IO.File]::ReadAllText($target)
    if ($saved.Contains('[Content.2]') -or $saved.Contains('[Content.3]') -or -not $saved.Contains('"Count" = "1"')) { throw 'The last full-profile import retained earlier Cycle entries.' }
    $relativeRoot = Join-Path $fixtureRoot 'relative'
    $null = [IO.Directory]::CreateDirectory($relativeRoot)
    $relativeSource = Join-Path $relativeRoot 'relative.ini'
    [IO.File]::WriteAllText($relativeSource, "[Settings]`nTileLayout=Artwork`n", $utf8)
    $relativeTarget = Join-Path $relativeRoot 'target.ini'
    $null = Invoke-NowPlayingPassiveCase @('--ini',$relativeTarget,'--now-playing-import-profile','relative.ini') 0
    if (-not [IO.File]::ReadAllText($relativeTarget).Contains('"Background" = "SMTCArtwork"')) { throw 'A donor path did not resolve relative to the target INI.' }
    if (Test-Path -LiteralPath (Join-Path $fixtureRoot 'Assets')) { throw 'Passive import created package/assets output.' }
    Write-Output "NowPlaying passive CLI checks passed: $caseNumber; malformed/disallowed inputs made zero target changes."
} finally {
    $resolvedFixture = [IO.Path]::GetFullPath($fixtureRoot)
    if ([IO.Path]::GetDirectoryName($resolvedFixture) -ne $temporaryParent) { throw 'Refusing cleanup outside the known temporary parent.' }
    Remove-Item -LiteralPath $resolvedFixture -Recurse -Force
}

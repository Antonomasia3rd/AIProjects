#requires -Version 7.0
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ReviewScanPolicy.ps1')
$checks=0
function Check([bool]$Passed,[string]$Message) {
    if(-not $Passed){throw ('FAIL: '+$Message)}
    $script:checks++
}
function Decision([hashtable]$Override) {
    $arguments=@{Attempt=1;ScanExitCode=0;ScanOutput='Scan finished. No threats.';InspectionSucceeded=$true;
        InspectionExitCode=0;InspectionError='';InputUnchanged=$true;DetectedThreat=$false}
    foreach($key in $Override.Keys){$arguments[$key]=$Override[$key]}
    Get-ReviewScanDecision @arguments
}
foreach($code in @('800106ba','800706ba')) {
    $signed=[BitConverter]::ToInt32([BitConverter]::GetBytes([Convert]::ToUInt32($code,16)),0)
    Check (Test-ReviewScanRpcUnavailable -ExitCode 2 -FailureText ('[Failed][0x'+$code+'] The RPC server is unavailable.')) 'exact native RPC code is recognized'
    Check (Test-ReviewScanRpcUnavailable -ExitCode $signed -FailureText '') 'signed HRESULT is recognized without broad text matching'
    Check (Test-ReviewScanRpcUnavailable -ExitCode 2 -FailureText ('HRESULT 0X'+$code.ToUpperInvariant())) 'RPC code matching is case independent'
    foreach($attempt in @(1,2,3)) {
        $result=Decision @{Attempt=$attempt;ScanExitCode=2;ScanOutput=('[Failed][0x'+$code+']')}
        Check ($result.Action -eq $(if($attempt -lt 3){'Retry'}else{'Block'})) 'each outage allows at most three attempts'
        Check ($result.DelaySeconds -eq $(if($attempt -lt 3){2}else{0})) 'only the first two attempts request a two-second delay'
    }
    Check ((Decision @{InspectionSucceeded=$false;InspectionExitCode=$signed;InspectionError='RPC unavailable'}).Action -eq 'Retry') 'scan success cannot replace a fresh threat inspection'
    Check ((Decision @{ScanExitCode=2;ScanOutput=('[Failed][0x'+$code+']');DetectedThreat=$true}).Action -eq 'Block') 'a matching detection prevents an RPC retry'
    Check ((Decision @{ScanExitCode=2;ScanOutput=('[Failed][0x'+$code+']');InputUnchanged=$false}).Action -eq 'Block') 'missing or changed inputs prevent an RPC retry'
}
Check ((Decision @{}).Action -eq 'Pass') 'zero scan plus successful fresh empty inspection passes'
Check ((Decision @{Attempt=3}).Action -eq 'Pass') 'a clean third attempt can pass'
Check ((Decision @{DetectedThreat=$true}).Action -eq 'Block') 'zero scan cannot override a matching detection'
Check ((Decision @{InputUnchanged=$false}).Action -eq 'Block') 'zero scan cannot override missing or changed input'
Check ((Decision @{InspectionSucceeded=$false;InspectionError='RPC server unavailable'}).Action -eq 'Block') 'generic RPC wording without an allowed code remains blocked'
Check ((Decision @{ScanExitCode=2;ScanOutput='RPC server unavailable'}).Action -eq 'Block') 'unknown scanner failure remains blocked'
Check ((Decision @{ScanExitCode=2;ScanOutput='Unknown scanner error';InspectionSucceeded=$false;InspectionError='HRESULT 0x800706ba'}).Action -eq 'Block') 'inspection RPC failure cannot mask an unknown scanner failure'
Check ((Decision @{ScanExitCode=2;ScanOutput='[Failed][0x800106ba]';InspectionSucceeded=$false;InspectionError='Access denied 0x80070005'}).Action -eq 'Block') 'scanner RPC failure cannot mask unknown inspection failure'
Check ((Decision @{ScanOutput='[Failed][0x800106ba]'}).Action -eq 'Retry') 'an RPC failure marker cannot pass solely because the native exit was zero'
Check ((Decision @{ScanOutput='[Failed][0x80070005]'}).Action -eq 'Block') 'a zero scanner exit cannot mask an unknown reported failure'
Check ((Decision @{ScanOutput='[Failed]'}).Action -eq 'Block') 'an unclassified failure marker remains blocked even with zero exit'
foreach($exit in @(0,2)) {
    foreach($attempt in @(1,2,3)) {
        foreach($output in @('Scanning C:\fixtures\worker.exe was skipped.',
            "Scan starting...`r`nScan finished.`r`nScanning C:\fixtures\worker.exe was skipped.`r`n",
            "[Failed][0x800106ba]`nScanning C:\fixtures\worker.exe was skipped.",
            "Scanning C:\fixtures\worker.exe was skipped.`n[Failed][0x800706ba]",
            '  SCANNING C:\fixtures\worker.exe WAS SKIPPED.  ')) {
            $skipped=Decision @{Attempt=$attempt;ScanExitCode=$exit;ScanOutput=$output}
            Check ($skipped.Action -eq 'Block' -and $skipped.DelaySeconds -eq 0) 'an explicit skipped scan is terminal even with zero exit or mixed RPC output'
        }
    }
}
Check ((Decision @{ScanOutput='Scanning C:\fixtures\worker.exe was skipped.';InspectionSucceeded=$false;
    InspectionError='HRESULT 0x800706ba'}).Action -eq 'Block') 'an inspection RPC outage cannot make a skipped scan retryable'
foreach($output in @('Scanning C:\fixtures\was skipped.exe found no threats.',
    'Scanning C:\fixtures\was skipped.\worker.exe found no threats.',
    'Diagnostic filename: Scanning worker was skipped.',
    'Scan finished.', 'Scananalyse abgeschlossen.')) {
    Check ((Decision @{ScanOutput=$output}).Action -eq 'Pass') 'path spelling and unrelated prose do not force an English positive scan witness'
}
foreach($exit in @(1,4,99,[Convert]::ToInt64('80508023',16))) {
    Check (-not (Test-ReviewScanRpcUnavailable -ExitCode $exit -FailureText '[Failed][0x800106ba]')) 'unknown native exit codes cannot be normalized away by RPC text'
    Check ((Decision @{ScanExitCode=$exit;ScanOutput='[Failed][0x800106ba]'}).Action -eq 'Block') 'an unknown scanner exit remains blocked in the full decision'
}
foreach($hex in @('80131500','80131501','80004005')) {
    $wrapper=[Convert]::ToInt64($hex,16)
    Check ((Decision @{InspectionSucceeded=$false;InspectionExitCode=$wrapper;InspectionError='HRESULT 0x800706ba'}).Action -eq 'Retry') 'known generic inspection wrappers require the exact provider RPC diagnostic'
    Check ((Decision @{ScanExitCode=$wrapper;ScanOutput='HRESULT 0x800706ba'}).Action -eq 'Block') 'inspection wrapper exceptions never apply to native scanner exits'
}
Check ((Decision @{InspectionSucceeded=$false;InspectionExitCode=4;InspectionError='HRESULT 0x800706ba'}).Action -eq 'Block') 'an unknown inspection exit is not a generic CIM wrapper'
foreach($attempt in @(-1,0,4,100)){Check ((Decision @{Attempt=$attempt}).Action -eq 'Block') 'invalid attempt numbers remain blocked'}
foreach($text in @('0x800106baa','0x1800706ba','0x800706b','error 0x80070005',
    '[Failed][0x800106ba] and [Failed][0x80070005]',
    'Detected malware 0x80508007; HRESULT 0x800706ba',
    'Scanning C:\fixtures\0x800106ba.exe', 'https://example.invalid/0x800706ba')) {
    Check (-not (Test-ReviewScanRpcUnavailable -ExitCode 2 -FailureText $text)) 'partial, unrelated, mixed error codes and path spellings do not permit retry'
}
# A sequence never authorizes execution before both gates succeed, and an
# intervening matching detection remains a terminal block.
$outage=Decision @{Attempt=1;ScanExitCode=2;ScanOutput='[Failed][0x800106ba]'}
$probeOutage=Decision @{Attempt=2;InspectionSucceeded=$false;InspectionError='HRESULT 0x800706ba'}
$recovered=Decision @{Attempt=3}
Check ($outage.Action -eq 'Retry' -and $probeOutage.Action -eq 'Retry' -and $recovered.Action -eq 'Pass') 'scanner and threat-inspection RPC outages must recover before pass'
Check ((Decision @{Attempt=2;DetectedThreat=$true;InspectionSucceeded=$true}).Action -eq 'Block') 'a detection after an earlier outage ends the sequence'
Write-Output ($checks.ToString()+' scan retry policy checks passed; no scanner, service, process, delay or file mutation used.')

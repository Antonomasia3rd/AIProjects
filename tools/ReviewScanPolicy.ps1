#requires -Version 7.0
# Pure retry policy. No scanner, service, filesystem, delay or process is used.
function Test-ReviewScanRpcUnavailable {
    param([long]$ExitCode, [AllowEmptyString()][string]$FailureText,
        [ValidateSet('Scanner','Inspection')][string]$Source='Scanner')
    $known = @('800106ba','800706ba')
    $codes = @([regex]::Matches($FailureText, '(?i)(?<![a-z0-9_\\/])0x([0-9a-f]{8})(?![0-9a-f])') |
        ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() })
    # An unknown failure must not become retryable because an RPC code also
    # appeared in its text. Zero is sometimes included as a status field.
    if (@($codes | Where-Object { $_ -ne '00000000' -and $_ -notin $known }).Count) { return $false }
    $exitHex = '{0:x8}' -f ($ExitCode -band 4294967295L)
    # MpCmdRun's generic error exit 2 needs its exact RPC diagnostic. Never
    # normalize another native exit/HRESULT away merely because text mentions
    # RPC. CIM/PowerShell may expose a generic wrapper HRESULT separately from
    # the provider's exact diagnostic; that exception is inspection-only.
    $allowedExit = @('00000000','00000002')+$known
    if($Source -eq 'Inspection') { $allowedExit=@('00000000','80131500','80131501','80004005')+$known }
    if($exitHex -notin $allowedExit) { return $false }
    return $exitHex -in $known -or @($codes | Where-Object { $_ -in $known }).Count -gt 0
}
function Get-ReviewScanDecision {
    param([int]$Attempt, [long]$ScanExitCode,
        [AllowEmptyString()][string]$ScanOutput,
        [bool]$InspectionSucceeded, [long]$InspectionExitCode,
        [AllowEmptyString()][string]$InspectionError,
        [bool]$InputUnchanged, [bool]$DetectedThreat)
    $block = { param($why) [pscustomobject]@{ Action='Block'; Reason=$why; DelaySeconds=0 } }
    if ($Attempt -lt 1 -or $Attempt -gt 3) { return & $block 'Invalid scan-attempt number.' }
    if (-not $InputUnchanged) { return & $block 'The input is missing or changed.' }
    if ($DetectedThreat) { return & $block 'Defender reported a matching detection.' }
    # MpCmdRun can exit zero after explicitly skipping an excluded file. An
    # empty threat query does not turn that unperformed scan into a clean one.
    if ($ScanOutput -match '(?im)^[ \t]*Scanning[ \t]+[^\r\n]+[ \t]+was[ \t]+skipped\.[ \t]*\r?$') {
        return & $block 'Defender explicitly skipped this scan.'
    }
    $scanRpc = Test-ReviewScanRpcUnavailable -ExitCode $ScanExitCode -FailureText $ScanOutput
    $reportedCodes = @([regex]::Matches($ScanOutput, '(?i)(?<![a-z0-9_\\/])0x([0-9a-f]{8})(?![0-9a-f])') |
        ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() })
    $unknownCode = @($reportedCodes | Where-Object { $_ -notin @('00000000','800106ba','800706ba') }).Count -gt 0
    if ($ScanExitCode -eq 0 -and -not $scanRpc -and ($unknownCode -or $ScanOutput -match '(?i)\[Failed\]')) {
        return & $block 'Scanner output reported an unknown failure despite a zero exit code.'
    }
    if ($ScanExitCode -ne 0 -and -not $scanRpc) { return & $block 'Scanner failure is not an allowed RPC outage.' }
    $inspectionRpc = -not $InspectionSucceeded -and
        (Test-ReviewScanRpcUnavailable -ExitCode $InspectionExitCode -FailureText $InspectionError -Source Inspection)
    if (-not $InspectionSucceeded -and -not $inspectionRpc) { return & $block 'Fresh threat inspection failed for an unknown reason.' }
    if ($ScanExitCode -eq 0 -and -not $scanRpc -and $InspectionSucceeded) {
        return [pscustomobject]@{ Action='Pass'; Reason='Successful scan and fresh threat inspection.'; DelaySeconds=0 }
    }
    if ($Attempt -eq 3) { return & $block 'Allowed RPC outage persisted through three attempts.' }
    return [pscustomobject]@{ Action='Retry'; Reason='Defender RPC is temporarily unavailable.'; DelaySeconds=2 }
}

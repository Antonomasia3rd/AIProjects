#requires -Version 7.0
# Bounded local validation helper. It does not install/update Defender, change
# protection settings, or allow/restore detections. Scans precede fixture runs.
# This runner bounds ordinary fixture processes; integration smoke uses the
# repository's dedicated Job Object guard instead.
$ErrorActionPreference = 'Stop'
$ReviewRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ReviewLogs = Join-Path $ReviewRoot 'build/review-validation'
$null = [IO.Directory]::CreateDirectory($ReviewLogs)
. (Join-Path $PSScriptRoot 'ReviewScanPolicy.ps1')
function Invoke-ReviewProcess {
    param([string]$File, [string[]]$Parameters, [string]$Label,
        [int]$Seconds = 120, [string]$Directory = $ReviewRoot)
    $p = [Diagnostics.Process]::new()
    $p.StartInfo.FileName = $File
    $p.StartInfo.WorkingDirectory = $Directory
    $p.StartInfo.UseShellExecute = $false
    $p.StartInfo.CreateNoWindow = $true
    $p.StartInfo.RedirectStandardOutput = $true
    $p.StartInfo.RedirectStandardError = $true
    foreach ($parameter in $Parameters) { $p.StartInfo.ArgumentList.Add($parameter) }
    $started = $false
    try {
        if (-not $p.Start()) { throw "Cannot start stage: $Label" }
        $started = $true
        $stdout = $p.StandardOutput.ReadToEndAsync()
        $stderr = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit($Seconds * 1000)) {
            $p.Kill($true)
            $null = $p.WaitForExit(5000)
            if ($stdout.Wait(3000) -and $stderr.Wait(3000)) {
                [IO.File]::WriteAllText((Join-Path $ReviewLogs ($Label + '.log')),
                    $stdout.Result + $stderr.Result + "`nStage timed out after $Seconds seconds: $Label`n")
            }
            throw "Stage timed out after $Seconds seconds: $Label"
        }
        if (-not $stdout.Wait(3000) -or -not $stderr.Wait(3000)) { throw "Output timeout: $Label" }
        $output = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText((Join-Path $ReviewLogs ($Label + '.log')), $output)
        [pscustomobject]@{ Label=$Label; ExitCode=$p.ExitCode; Output=$output }
    } finally {
        if ($started -and -not $p.HasExited) { $p.Kill($true) }
        $p.Dispose()
    }
}
function Assert-ReviewScan {
    param([string]$Path, [string]$Label)
    $Path = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing scan input: $Path" }
    $before = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    # Retain the earliest start across retries so an intervening detection can
    # never disappear from this operation's inspection window.
    $scanStart = Get-Date
    $platform = Get-ChildItem -LiteralPath 'C:/ProgramData/Microsoft/Windows Defender/Platform' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $scanner = Join-Path $platform.FullName 'MpCmdRun.exe'
    $assertInput = {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf) -or
            (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $before) {
            throw "Changed/missing scan input; stage blocked: $Label"
        }
    }
    $receipt = Join-Path $ReviewLogs ($Label + '-scan.sha256')
    if (Test-Path -LiteralPath $receipt -PathType Leaf) { Remove-Item -LiteralPath $receipt }
    $audit = [Text.StringBuilder]::new()
    $null = $audit.AppendLine('Earliest scan start: ' + $scanStart.ToString('o'))
    $legacyLog = Join-Path $ReviewLogs ($Label + '-defender.log')
    [IO.File]::WriteAllText($legacyLog,$audit.ToString())
    for ($attempt=1; $attempt -le 3; ++$attempt) {
        & $assertInput
        $attemptLabel = $Label + '-defender-attempt-' + $attempt
        try {
            $scan = Invoke-ReviewProcess -File $scanner -Parameters @('-Scan','-ScanType','3','-File',$Path) -Label $attemptLabel -Seconds 180
        } catch { & $assertInput; throw }
        Write-Output $scan.Output.Trim()
        & $assertInput
        $inspectionSucceeded=$false; $inspectionError=''; $inspectionExitCode=0; $detections=@()
        try {
            $detections = @(Get-MpThreatDetection -ErrorAction Stop | Where-Object {
                $_.InitialDetectionTime -ge $scanStart.AddSeconds(-5) -and
                ($_.Resources | Where-Object { $_.IndexOf($Path,[StringComparison]::OrdinalIgnoreCase) -ge 0 })
            })
            $inspectionSucceeded=$true
        } catch {
            $inspectionError=$_.ToString(); $inspectionExitCode=$_.Exception.HResult
        }
        & $assertInput
        $decision = Get-ReviewScanDecision -Attempt $attempt -ScanExitCode $scan.ExitCode -ScanOutput $scan.Output `
            -InspectionSucceeded $inspectionSucceeded -InspectionExitCode $inspectionExitCode -InspectionError $inspectionError `
            -InputUnchanged $true -DetectedThreat ($detections.Count -gt 0)
        $status = 'Attempt=' + $attempt + '; scanExit=' + $scan.ExitCode + '; freshInspection=' + $inspectionSucceeded +
            '; matchingDetections=' + $detections.Count + '; action=' + $decision.Action + '; ' + $decision.Reason
        if ($inspectionError) { $status += "`n" + $inspectionError }
        [IO.File]::WriteAllText((Join-Path $ReviewLogs ($attemptLabel + '-inspection.log')), $status)
        $null = $audit.AppendLine($status).AppendLine($scan.Output)
        [IO.File]::WriteAllText($legacyLog,$audit.ToString())
        if ($decision.Action -eq 'Pass') {
            & $assertInput
            [IO.File]::WriteAllText($receipt, $before + '  ' + $Path)
            return
        }
        if ($decision.Action -ne 'Retry') { throw ($decision.Reason + " Stage blocked: $Label") }
        Write-Output ("Defender RPC unavailable; retrying scan in two seconds (attempt $attempt of 3).")
        Start-Sleep -Seconds $decision.DelaySeconds
        & $assertInput
    }
    throw "Scan did not complete; stage blocked: $Label"
}

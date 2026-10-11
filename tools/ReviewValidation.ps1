#requires -Version 7.0
# Bounded local validation helper. It does not install/update Defender, change
# protection settings, or allow/restore detections. Scans precede fixture runs.
# This runner bounds ordinary fixture processes; integration smoke uses the
# repository's dedicated Job Object guard instead.
$ErrorActionPreference = 'Stop'
$ReviewRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ReviewLogs = Join-Path $ReviewRoot 'build/review-validation'
$null = [IO.Directory]::CreateDirectory($ReviewLogs)
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
    try {
        if (-not $p.Start()) { throw "Cannot start stage: $Label" }
        $stdout = $p.StandardOutput.ReadToEndAsync()
        $stderr = $p.StandardError.ReadToEndAsync()
        if (-not $p.WaitForExit($Seconds * 1000)) {
            $p.Kill($true)
            $null = $p.WaitForExit(5000)
            throw "Stage timed out after $Seconds seconds: $Label"
        }
        if (-not $stdout.Wait(3000) -or -not $stderr.Wait(3000)) { throw "Output timeout: $Label" }
        $output = $stdout.Result + $stderr.Result
        [IO.File]::WriteAllText((Join-Path $ReviewLogs ($Label + '.log')), $output)
        [pscustomobject]@{ Label=$Label; ExitCode=$p.ExitCode; Output=$output }
    } finally {
        if ($p.Id -and -not $p.HasExited) { $p.Kill($true) }
        $p.Dispose()
    }
}
function Assert-ReviewScan {
    param([string]$Path, [string]$Label)
    $Path = [IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing scan input: $Path" }
    $before = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash
    $scanStart = Get-Date
    $platform = Get-ChildItem -LiteralPath 'C:/ProgramData/Microsoft/Windows Defender/Platform' -Directory |
        Sort-Object Name -Descending | Select-Object -First 1
    $scanner = Join-Path $platform.FullName 'MpCmdRun.exe'
    $scan = Invoke-ReviewProcess -File $scanner -Parameters @('-Scan','-ScanType','3','-File',$Path) -Label ($Label + '-defender') -Seconds 180
    Write-Output $scan.Output.Trim()
    if ($scan.ExitCode -ne 0) { throw "Defender returned $($scan.ExitCode); stage blocked: $Label" }
    $detections = @(Get-MpThreatDetection | Where-Object {
        $_.InitialDetectionTime -ge $scanStart.AddSeconds(-5) -and
        ($_.Resources | Where-Object { $_.IndexOf($Path,[StringComparison]::OrdinalIgnoreCase) -ge 0 })
    })
    if ($detections.Count -or -not (Test-Path -LiteralPath $Path) -or
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ne $before) {
        throw "Defender detection or changed/missing input; stage blocked: $Label"
    }
    [IO.File]::WriteAllText((Join-Path $ReviewLogs ($Label + '-scan.sha256')), $before + '  ' + $Path)
}

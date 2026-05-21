$ErrorActionPreference = "Stop"

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) {
    throw "C# compiler not found at $csc"
}

$outDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir "DiscordRPC.exe"
$source = Join-Path $PSScriptRoot "DiscordRPC.cs"

if (Test-Path -LiteralPath $out -PathType Leaf) {
    $resolvedOut = (Resolve-Path -LiteralPath $out).Path
    $lockers = @(Get-Process | Where-Object {
        try {
            $_.Path -and [string]::Equals((Resolve-Path -LiteralPath $_.Path).Path, $resolvedOut, [StringComparison]::OrdinalIgnoreCase)
        } catch {
            $false
        }
    })
    if ($lockers.Count -gt 0) {
        $summary = ($lockers | ForEach-Object { "$($_.ProcessName)[$($_.Id)]" }) -join ', '
        throw "Cannot overwrite $out because it is running: $summary"
    }
}

& $csc /nologo /optimize+ /target:exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /out:$out $source
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built $out"

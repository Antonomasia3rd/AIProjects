$ErrorActionPreference = "Stop"

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) {
    throw "C# compiler not found at $csc"
}

$outDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$out = Join-Path $outDir "DiscordRPC.exe"
$source = Join-Path $PSScriptRoot "DiscordRPC.cs"

& $csc /nologo /optimize+ /target:exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /out:$out $source
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built $out"

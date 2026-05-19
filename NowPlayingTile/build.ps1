$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$outDir = Join-Path $root 'build'
$out = Join-Path $outDir 'NowPlayingTile.exe'
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$systemRuntime = Join-Path $env:WINDIR 'Microsoft.NET\assembly\GAC_MSIL\System.Runtime\v4.0_4.0.0.0__b03f5f7f11d50a3a\System.Runtime.dll'
$winMetadata = Join-Path $env:WINDIR 'System32\WinMetadata'
$foundationWinmd = Join-Path $winMetadata 'Windows.Foundation.winmd'
$dataWinmd = Join-Path $winMetadata 'Windows.Data.winmd'
$mediaWinmd = Join-Path $winMetadata 'Windows.Media.winmd'
$storageWinmd = Join-Path $winMetadata 'Windows.Storage.winmd'
$uiWinmd = Join-Path $winMetadata 'Windows.UI.winmd'

New-Item -ItemType Directory -Force -Path $outDir | Out-Null

& $csc /nologo /target:winexe /platform:x64 /out:$out `
    /r:System.dll `
    /r:System.Core.dll `
    /r:System.Drawing.dll `
    /r:System.Windows.Forms.dll `
    /r:$systemRuntime `
    /r:$foundationWinmd `
    /r:$dataWinmd `
    /r:$mediaWinmd `
    /r:$storageWinmd `
    /r:$uiWinmd `
    (Join-Path $root 'NowPlayingTile.cs')

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

Write-Host "Built $out"

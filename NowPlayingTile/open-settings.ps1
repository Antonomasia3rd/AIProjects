$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root 'build'
$exe = Get-ChildItem -LiteralPath $buildDir -Filter '*.exe' -File -ErrorAction SilentlyContinue | Select-Object -First 1
if ($exe) {
    $settingsDir = $exe.DirectoryName
    $settingsPath = Join-Path $settingsDir "$($exe.BaseName).ini"
} else {
    $settingsDir = $buildDir
    $settingsPath = Join-Path $settingsDir 'NowPlayingTile.ini'
}

if (-not (Test-Path -LiteralPath $settingsPath)) {
    New-Item -ItemType Directory -Force -Path $settingsDir | Out-Null
    @'
# NowPlayingTile settings
# TileLayout: Cycle, Text, Artwork, Combined
TileLayout=Cycle
UpdateIntervalSeconds=2
TileRefreshSeconds=60
ShowTrayIcon=false
'@ | Set-Content -LiteralPath $settingsPath -Encoding ASCII
}

Start-Process notepad.exe $settingsPath

$ErrorActionPreference = 'Stop'

$settingsDir = Join-Path $env:LOCALAPPDATA 'NowPlayingTile'
$settingsPath = Join-Path $settingsDir 'settings.ini'

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

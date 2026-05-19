$ErrorActionPreference = 'Stop'

$shortcutPath = Join-Path ([Environment]::GetFolderPath('Startup')) 'Now Playing Tile.lnk'
if (Test-Path -LiteralPath $shortcutPath) {
    Remove-Item -LiteralPath $shortcutPath -Force
    Write-Host "Removed startup shortcut: $shortcutPath"
} else {
    Write-Host 'Startup shortcut is not installed.'
}

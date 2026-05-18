$ErrorActionPreference = 'Stop'

$package = Get-AppxPackage -Name 'NowPlayingTile.App'
if ($null -eq $package) {
    throw 'NowPlayingTile.App is not registered. Run .\register-dev-package.ps1 first.'
}

$startupDir = [Environment]::GetFolderPath('Startup')
$shortcutPath = Join-Path $startupDir 'Now Playing Tile.lnk'
$shell = New-Object -ComObject WScript.Shell
$shortcut = $shell.CreateShortcut($shortcutPath)
$shortcut.TargetPath = Join-Path $env:WINDIR 'explorer.exe'
$shortcut.Arguments = "shell:AppsFolder\$($package.PackageFamilyName)!App"
$shortcut.WorkingDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
$shortcut.WindowStyle = 7
$shortcut.Description = 'Start Now Playing Tile in background at sign-in'
$shortcut.Save()

Write-Host "Installed startup shortcut: $shortcutPath"

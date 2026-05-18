$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$manifest = Join-Path $root 'AppxManifest.xml'

& (Join-Path $root 'build.ps1')
& (Join-Path $root 'make-assets.ps1')

Write-Host "Registering development package..."
Add-AppxPackage -Register $manifest -ForceApplicationShutdown -ExternalLocation $root

$package = Get-AppxPackage -Name 'Amiya.NowPlayingTile'
if ($null -eq $package) {
    throw 'Package registration completed but Get-AppxPackage could not find Amiya.NowPlayingTile.'
}

Write-Host ''
Write-Host 'Registered package:'
Write-Host "  $($package.PackageFullName)"
Write-Host ''
Write-Host 'Pin this app to Start from:'
Write-Host "  shell:AppsFolder\$($package.PackageFamilyName)!App"
Write-Host ''
Write-Host 'ExplorerPatcher/Windows 10 Start should then be able to show Live Tile updates while the app is running.'

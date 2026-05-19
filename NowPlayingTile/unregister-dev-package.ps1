$ErrorActionPreference = 'Stop'

$package = Get-AppxPackage -Name 'NowPlayingTile.App'
if ($null -eq $package) {
    Write-Host 'NowPlayingTile.App is not registered.'
    exit 0
}

Remove-AppxPackage -Package $package.PackageFullName
Write-Host "Removed $($package.PackageFullName)"

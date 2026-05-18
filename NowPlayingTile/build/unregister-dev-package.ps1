$ErrorActionPreference = 'Stop'

$package = Get-AppxPackage -Name 'Amiya.NowPlayingTile'
if ($null -eq $package) {
    Write-Host 'Amiya.NowPlayingTile is not registered.'
    exit 0
}

Remove-AppxPackage -Package $package.PackageFullName
Write-Host "Removed $($package.PackageFullName)"

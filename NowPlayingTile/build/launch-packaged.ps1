$ErrorActionPreference = 'Stop'

$package = Get-AppxPackage -Name 'Amiya.NowPlayingTile'
if ($null -eq $package) {
    throw 'Amiya.NowPlayingTile is not registered. Run .\register-dev-package.ps1 first.'
}

Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!App"

$ErrorActionPreference = 'Stop'

$package = Get-AppxPackage -Name 'NowPlayingTile.App'
if ($null -eq $package) {
    throw 'NowPlayingTile.App is not registered. Run .\register-dev-package.ps1 first.'
}

Start-Process "shell:AppsFolder\$($package.PackageFamilyName)!App"

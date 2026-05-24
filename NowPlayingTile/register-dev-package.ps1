$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $root 'build'
$manifestTemplate = Join-Path $root 'package\AppxManifest.xml'
$manifest = Join-Path $buildDir 'AppxManifest.xml'

& $env:ComSpec /d /c "`"$root\build.cmd`""
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
& (Join-Path $root 'make-assets.ps1')
Copy-Item -LiteralPath $manifestTemplate -Destination $manifest -Force

Write-Host "Registering development package..."
Add-AppxPackage -Register $manifest -ForceApplicationShutdown -ExternalLocation $buildDir

$package = Get-AppxPackage -Name 'NowPlayingTile.App'
if ($null -eq $package) {
    throw 'Package registration completed but Get-AppxPackage could not find NowPlayingTile.App.'
}

Write-Host ''
Write-Host 'Registered package:'
Write-Host "  $($package.PackageFullName)"
Write-Host ''
Write-Host 'Pin this app to Start from:'
Write-Host "  shell:AppsFolder\$($package.PackageFamilyName)!App"
Write-Host ''
Write-Host 'ExplorerPatcher/Windows 10 Start should then be able to show Live Tile updates while the app is running.'

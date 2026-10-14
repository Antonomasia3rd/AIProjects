#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$Key, [switch]$LegacyMultipleHosts)
# An intentional current-user installation, separate from offline preparation/tests.
# Compatibility only: new setup is DesktopStub tray > Tiles, with one resident.
if (-not $LegacyMultipleHosts) { throw 'Use DesktopStub tray > Tiles for one-process setup. This legacy copied-host tool requires -LegacyMultipleHosts.' }
$ErrorActionPreference='Stop'
$names=@{smtc='AipSmtcTile';rss='AipRssTile';genshin='AipGenshinTile';hsr='AipHsrTile';zzz='AipZzzTile';discord='AipDiscordTile'}
if(-not $names.ContainsKey($Key)){throw 'Unknown managed tile key.'}
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../DesktopStub/build/Tiles'))
$directory=[IO.Path]::GetFullPath((Join-Path $root $Key))
$manifestPath=Join-Path $directory 'AppxManifest.xml'
[xml]$manifest=Get-Content -Raw -LiteralPath $manifestPath
$identity='dev.local.aiprojects.tiles.'+$Key
if($manifest.Package.Identity.Name -ne $identity -or
   $manifest.Package.Applications.Application.Executable -ne ($names[$Key]+'.exe')){throw 'Tile manifest does not match its managed identity.'}
$existing=@(Get-AppxPackage -Name $identity)
if($existing.Count -and @($existing | Where-Object {$_.InstallLocation -ne $directory}).Count){throw 'This identity already belongs to another location; no package changed.'}
Add-AppxPackage -Register $manifestPath -ForceUpdateFromAnyVersion -ErrorAction Stop
$package=Get-AppxPackage -Name $identity
if(-not $package -or $package.InstallLocation -ne $directory){throw 'Registration did not produce the intended user package.'}
[pscustomobject]@{Name=$package.Name;Family=$package.PackageFamilyName;AUMID=$package.PackageFamilyName+'!App';Location=$package.InstallLocation} | ConvertTo-Json -Compress

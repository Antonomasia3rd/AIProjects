#requires -Version 7.0
[CmdletBinding()]
param([string]$Tag='DesktopStub-v27-beta1',[string]$Version='27.0.0.1',[string]$HostBinary='DesktopStubBetaOct26.exe',[string]$BrokerBinary='DesktopStubBetaOct26LiveTileBroker.exe',[string]$LauncherBinary='DesktopStubBetaOct26TileLauncher.exe')
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if($Tag-notmatch '^[A-Za-z0-9][A-Za-z0-9.-]{0,63}$'){throw 'Use a plain release tag.'}
$taskOutput=Join-Path $taskRoot ('DesktopStub\releases\'+$Tag)
$taskZip=Join-Path $taskRoot ('DesktopStub\releases\'+$Tag+'-x64.zip')
if(Test-Path -LiteralPath $taskOutput){throw 'Release directory already exists; preserve it or select a new tag.'}
$taskFiles=@(@($HostBinary,'DesktopStub.exe'),@($BrokerBinary,'DesktopStubLiveTileBroker.exe'),@($LauncherBinary,'DesktopStubTileLauncher.exe'))
foreach($pair in $taskFiles){
 if([IO.Path]::GetFileName($pair[0])-ne $pair[0]){throw 'Build inputs must be plain filenames.'}
 $path=Join-Path $taskRoot ('DesktopStub\build\'+$pair[0]);$item=Get-Item -LiteralPath $path
 if($item.Attributes-band[IO.FileAttributes]::ReparsePoint){throw 'Linked build input.'}
 if($item.VersionInfo.FileVersion-ne $Version){throw ('Mismatched build version: '+$pair[0])}
}
$null=[IO.Directory]::CreateDirectory($taskOutput)
foreach($pair in $taskFiles){Copy-Item -LiteralPath (Join-Path $taskRoot ('DesktopStub\build\'+$pair[0])) -Destination (Join-Path $taskOutput $pair[1])}
Copy-Item -LiteralPath (Join-Path $taskRoot 'LICENSE') -Destination (Join-Path $taskOutput 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $taskRoot 'tools\InspectDesktopStubTileImages.ps1') -Destination (Join-Path $taskOutput 'InspectDesktopStubTileImages.ps1')
$readme=@"
$Tag ($Version), x64 beta

FRESH PC/VM: Extract to a permanent writable local folder. Run DesktopStub.exe
normally as your Windows user. Desktop is the only initial tile, uses the main
Registration path, and generates/registers its wallpaper on startup. Then pin
Desktop from Start > All apps. Keep the utility running for wallpaper changes.
Optional modules and both sign-in startup methods default off.

Keep all three EXEs together for profiles, independent Registration and broker
features. Do not copy another PC's generated INI, manifest, profiles or caches
for a fresh installation. Windows must allow development/sideload registration.
A Start implementation supporting tiles is required for the tile experience.

UPGRADE: Close the old resident. Back up its folder, retain its INI, and replace
the matching binaries in that folder. v1-v25 released defaults and representative
customizations are tested; arbitrary old INIs/accounts still need beta feedback.
The converter preserves complex legacy compositions rather than flatten them.

MEDIA: Source settings can follow Windows-current or a selected application;
session occurrence 0 means current/first, positive values select an occurrence.
Use the same selection for an Information tile and Album art only tile.

ICONS: Artwork.IconFont supports Auto/MDL2/Fluent/Vector. Auto follows the Windows
generation and checks actual glyph support. Independent Registration now uses
an owned activation binding rather than uap10 parameters, with a 15063 baseline.
1809 Start image display and real account/hardware flows still need acceptance.

Optional LiveTile host setup: DesktopStub.exe --prepare-live-tile-host (the old
--setup-desktop alias also works). It keeps every tile's delivery choice;
ordinary Registration first startup needs no command. The GUI action is under
Advanced > Profile package identity and setup.

PROPERTIES: Profiles now opens inside the same settings window. The unwanted
filled row highlight is removed; Application > Interface > Properties window
offers Windows color sources and readable contrast, including custom colors.

WUWA: Source settings > Data freshness and failures keeps last-good values,
shows their age and configures independent tile/popup failure thresholds.

Read-only 1809 report (PowerShell 5.1+):
  .\InspectDesktopStubTileImages.ps1 -Directory . -OutputPath .\tile-report.json
Choose a new report filename. Review public paths/metadata before sharing it.

Source and current beta notes:
https://github.com/Antonomasia3rd/AIProjects
docs/desktopstub-beta-2026-10-26.md
"@
[IO.File]::WriteAllText((Join-Path $taskOutput 'README-beta.txt'),$readme,[Text.UTF8Encoding]::new($false))
$taskManifest=@(Get-ChildItem -LiteralPath $taskOutput -File | ForEach-Object {[pscustomobject]@{Name=$_.Name;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash;Bytes=$_.Length}})
$taskCommit=(& git -C $taskRoot rev-parse HEAD).Trim()
[IO.File]::WriteAllText((Join-Path $taskOutput 'beta-manifest.json'),([pscustomobject]@{Tag=$Tag;Version=$Version;SourceCommit=$taskCommit;Files=$taskManifest}|ConvertTo-Json -Depth 4))
if(Test-Path -LiteralPath $taskZip){throw 'Release ZIP already exists.'}
Compress-Archive -LiteralPath $taskOutput -DestinationPath $taskZip -CompressionLevel Optimal
[pscustomobject]@{Directory=$taskOutput;Zip=$taskZip;SHA256=(Get-FileHash -LiteralPath $taskZip).Hash;SourceCommit=$taskCommit;Files=$taskManifest.Count}|ConvertTo-Json -Compress

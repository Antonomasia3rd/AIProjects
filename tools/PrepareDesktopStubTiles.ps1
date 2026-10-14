#requires -Version 7.0
param([string]$HostBinary, [string]$BrokerBinary)
# Prepare only: no package registration, resident, Startup, account or device action.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReviewValidation.ps1')
if (-not $HostBinary) { $HostBinary=Join-Path $ReviewRoot 'DesktopStub/build/DesktopStub.exe' }
if (-not $BrokerBinary) { $BrokerBinary=Join-Path $ReviewRoot 'DesktopStub/build/DesktopStubLiveTileBroker.exe' }
$HostBinary=[IO.Path]::GetFullPath($HostBinary); $BrokerBinary=[IO.Path]::GetFullPath($BrokerBinary)
foreach($binary in @($HostBinary,$BrokerBinary)) { if(-not (Test-Path -LiteralPath $binary -PathType Leaf)){throw 'A reviewed host and broker are required.'} }
$tileRoot=Join-Path $ReviewRoot 'DesktopStub/build/Tiles'
$plans=@(
 @{Key='smtc'; Base='AipSmtcTile'; Name='Now playing'; Source='SMTC'; Count=1; Resource='Resin'},
 @{Key='rss'; Base='AipRssTile'; Name='BBC Technology'; Source='RssFeed'; Count=5; Resource='Resin'},
 @{Key='genshin'; Base='AipGenshinTile'; Name='Genshin Impact'; Source='Notes'; Count=1; Resource='Resin'},
 @{Key='hsr'; Base='AipHsrTile'; Name='Honkai Star Rail'; Source='Notes'; Count=1; Resource='Stamina'},
 @{Key='zzz'; Base='AipZzzTile'; Name='Zenless Zone Zero'; Source='Notes'; Count=1; Resource='Charge'},
 @{Key='discord'; Base='AipDiscordTile'; Name='Discord preview'; Source='DiscordRPC'; Count=1; Resource='Resin'}
)
foreach($plan in $plans) {
    $directory=Join-Path $tileRoot $plan.Key
    $ini=Join-Path $directory ($plan.Base+'.ini')
    if(Test-Path -LiteralPath $ini){Write-Output ('Preserved existing '+$plan.Name+' profile: '+$directory); continue}
    $null=[IO.Directory]::CreateDirectory($directory)
    $exe=Join-Path $directory ($plan.Base+'.exe')
    Copy-Item -LiteralPath $HostBinary -Destination $exe
    Copy-Item -LiteralPath $BrokerBinary -Destination (Join-Path $directory ($plan.Base+'LiveTileBroker.exe'))
    if((Get-FileHash $HostBinary).Hash -ne (Get-FileHash $exe).Hash){throw 'Host copy mismatch.'}
    Assert-ReviewScan $exe ('tile-'+$plan.Key+'-prepare')
    $text=@"
[Settings]
ManifestIdentityName=dev.local.aiprojects.tiles.$($plan.Key)
ManifestDisplayName=$($plan.Name)
ManifestDescription=$($plan.Name) live tile
ManifestPublisher=CN=AIProjectsTileProfiles
ManifestPublisherDisplayName=AIProjects
ExperimentalLiveTileUpdate=Registration
LiveTileTemplateStyle=Adaptive
LiveTileBranding=Name
GenerateOnStartup=1
ListenWallpaper=0
ListenFit=0
LiveWallpaperCapture=0
DisableFitting=1
GeneratedAssetPrecache=0
NotificationsEnabled=0
TrayIcon=1
Logging=1
RunAtStartup=0
RunAtStartupPackaged=0
[Content]
Enabled=0
Count=$($plan.Count)
CycleEnabled=$(if($plan.Count -gt 1){1}else{0})
CycleDriver=Auto
CycleSeconds=15
RefreshSeconds=5
TextMode=Auto
[RssFeed]
FeedUrl=https://feeds.bbci.co.uk/news/technology/rss.xml
UpdateIntervalSeconds=900
MaxItems=5
[Discord]
SendPresence=0
Name=DesktopStub Discord preview
Details=Configure Details and State under Sources and accounts
State=Preview only
[CapsBlink]
HardwareEnabled=0
[AsusBlink]
HardwareEnabled=0
[Strings]
TrayTip=$($plan.Name)
NotificationTitle=$($plan.Name)
"@
    for($index=1;$index -le $plan.Count;$index++) {
        $text+="`r`n[Content.$index]`r`nEnabled=1`r`nName=$($plan.Name)`r`nBackground=Transparent`r`nTextSources=$($plan.Source)`r`nRssItem=$index`r`nNotesResource=$($plan.Resource)`r`nNotesDetail=Summary`r`n"
    }
    [IO.File]::WriteAllText($ini,$text)
    $seed=Join-Path $directory 'bootstrap.bmp'
    $bytes=[byte[]]::new(58); $bytes[0]=66; $bytes[1]=77
    foreach($field in @(@(2,58),@(10,54),@(14,40),@(18,1),@(22,1),@(34,4))) {
        [Array]::Copy([BitConverter]::GetBytes([int]$field[1]),0,$bytes,[int]$field[0],4)
    }
    $bytes[26]=1; $bytes[28]=24; $bytes[54]=215; $bytes[55]=120
    [IO.File]::WriteAllBytes($seed,$bytes)
    $arguments=@('--ini',$ini,'--render-only','--wallpaper',$seed,'--no-live-tile','--set','TileText.Enabled=1','--set',('TileText.Text='+$plan.Name),'--set','TileText.SecondaryText=Start this app to update the live tile')
    foreach($asset in @('StoreLogo','MediumTile','Square44x44Logo','Square30x30Logo','SmallTile','WideTile','LargeTile','SplashScreen')) { $arguments+=@('--asset',($asset+'=1')) }
    $result=Invoke-ReviewProcess -File $exe -Parameters $arguments -Label ('tile-'+$plan.Key+'-assets') -Seconds 45 -Directory $directory
    if($result.ExitCode -ne 0){throw ('Asset preparation failed for '+$plan.Key+'; inspect its private review log.')}
    $result=Invoke-ReviewProcess -File $exe -Parameters @('--ini',$ini,'--configure-only','--set','Content.Enabled=1','--live-tile') -Label ('tile-'+$plan.Key+'-profile') -Seconds 30 -Directory $directory
    if($result.ExitCode -ne 0){throw ('Profile validation failed for '+$plan.Key)}
    [xml]$manifest=Get-Content -Raw -LiteralPath (Join-Path $directory 'AppxManifest.xml')
    if($manifest.Package.Identity.Name -ne ('dev.local.aiprojects.tiles.'+$plan.Key) -or
        $manifest.Package.Applications.Application.Executable -ne ($plan.Base+'.exe')){throw 'Prepared tile identity/executable mismatch.'}
    Write-Output ('Prepared '+$plan.Name+'; identity='+$manifest.Package.Identity.Name+'; '+$directory)
}
Write-Output 'Preparation complete. Register each AppxManifest.xml and start its host only when intentionally installing these tiles.'

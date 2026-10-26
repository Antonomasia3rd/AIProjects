#requires -Version 5.1
<#
.SYNOPSIS
Collect read-only tile image/identity evidence on the affected Windows machine.
.DESCRIPTION
Reads only public manifest metadata, selected non-secret INI fields, local PNG
headers/hashes/ACLs and the matching current-user package registration. It does
not launch DesktopStub, read accounts, install/uninstall packages or change ACLs.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Directory,
    [ValidateRange(0,64)][int]$RecentImages=12,
    [string]$OutputPath
)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath($Directory).TrimEnd('\','/')
if($taskRoot.Length -lt 4){throw 'Select the DesktopStub folder, not a drive root.'}
if($taskRoot.StartsWith('\\')){throw 'Run the collector on the affected PC against a local folder, not a network share.'}
$taskRootItem=Get-Item -LiteralPath $taskRoot
if(!$taskRootItem.PSIsContainer -or ($taskRootItem.Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Select an ordinary local DesktopStub folder.'}
function Test-LocalEvidencePath([string]$Path) {
    $candidate=[IO.Path]::GetFullPath($Path)
    if(!$candidate.Equals($taskRoot,[StringComparison]::OrdinalIgnoreCase)-and !$candidate.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase)){return $false}
    while($candidate -and !$candidate.Equals($taskRoot,[StringComparison]::OrdinalIgnoreCase)){
        $part=Get-Item -LiteralPath $candidate -ErrorAction SilentlyContinue
        if($part -and ($part.Attributes -band [IO.FileAttributes]::ReparsePoint)){return $false}
        $candidate=[IO.Path]::GetDirectoryName($candidate)
    }
    return $true
}
function Read-TileFileEvidence([string]$Path) {
    if(!(Test-LocalEvidencePath $Path)){return [pscustomobject]@{Path=$Path;Skipped='Linked or outside selected folder'}}
    $item=Get-Item -LiteralPath $Path -ErrorAction SilentlyContinue
    if(!$item){return [pscustomobject]@{Path=$Path;Exists=$false}}
    if($item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)){return [pscustomobject]@{Path=$Path;Exists=$true;Skipped='Directory or linked path'}}
    $png=$null
    if($item.Extension -ieq '.png'){
        $stream=[IO.File]::Open($Path,'Open','Read','ReadWrite')
        try{
            $header=New-Object byte[] 24
            if($stream.Read($header,0,24)-eq 24 -and [BitConverter]::ToString($header,0,8)-eq '89-50-4E-47-0D-0A-1A-0A'){
                [byte[]]$width=$header[16..19];[Array]::Reverse($width)
                [byte[]]$height=$header[20..23];[Array]::Reverse($height)
                $png=[pscustomobject]@{Width=[BitConverter]::ToUInt32($width,0);Height=[BitConverter]::ToUInt32($height,0)}
            }
        }finally{$stream.Dispose()}
    }
    $acl=$null
    try{
        $security=Get-Acl -LiteralPath $Path
        $acl=[pscustomobject]@{Owner=$security.Owner;Rules=@($security.Access|ForEach-Object {
            [pscustomobject]@{Identity=$_.IdentityReference.Value;Rights=$_.FileSystemRights.ToString();Type=$_.AccessControlType.ToString();Inherited=$_.IsInherited}
        })}
    }catch{$acl=[pscustomobject]@{Error=$_.Exception.Message}}
    $hash=if($item.Length -le 16MB){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}else{'Not hashed: over 16 MiB'}
    [pscustomobject]@{Path=$Path;Exists=$true;Bytes=$item.Length;ModifiedUtc=$item.LastWriteTimeUtc;SHA256=$hash;PNG=$png;ACL=$acl}
}
$manifestPath=Join-Path $taskRoot 'AppxManifest.xml'
$identity=$null;$manifestAssets=@();$manifestError=$null
if((Test-LocalEvidencePath $manifestPath)-and (Test-Path -LiteralPath $manifestPath -PathType Leaf)){
    try{
    if((Get-Item -LiteralPath $manifestPath).Length -gt 1MB){throw 'Manifest is over the diagnostic size limit.'}
    $readerSettings=[Xml.XmlReaderSettings]::new();$readerSettings.DtdProcessing=[Xml.DtdProcessing]::Prohibit
    $readerSettings.XmlResolver=$null;$readerSettings.MaxCharactersInDocument=1MB
    $reader=[Xml.XmlReader]::Create($manifestPath,$readerSettings)
    try{$manifest=[Xml.XmlDocument]::new();$manifest.XmlResolver=$null;$manifest.Load($reader)}finally{$reader.Dispose()}
    $node=$manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Identity']")
    $app=$manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Applications']/*[local-name()='Application']")
    $family=$manifest.SelectSingleNode("/*[local-name()='Package']/*[local-name()='Dependencies']/*[local-name()='TargetDeviceFamily']")
    if(!$node-or !$app){throw 'Missing manifest identity or application.'}
    $identity=[pscustomobject]@{Name=$node.GetAttribute('Name');Version=$node.GetAttribute('Version');ApplicationId=$app.GetAttribute('Id');Executable=$app.GetAttribute('Executable');EntryPoint=$app.GetAttribute('EntryPoint');MinVersion=$(if($family){$family.GetAttribute('MinVersion')}else{$null});MaxVersionTested=$(if($family){$family.GetAttribute('MaxVersionTested')}else{$null});HasUap10Parameters=$app.HasAttribute('Parameters','http://schemas.microsoft.com/appx/manifest/uap/windows10/10')}
    foreach($element in @($manifest.SelectNodes("//*[local-name()='VisualElements' or local-name()='DefaultTile' or local-name()='SplashScreen']"))){
        foreach($attribute in @($element.Attributes)){
            if($attribute.LocalName -notmatch '^(Square150x150Logo|Square44x44Logo|Square30x30Logo|Square71x71Logo|Wide310x150Logo|Square310x310Logo|Logo|SmallLogo|WideLogo|Image)$'){continue}
            $relative=$attribute.Value
            if([IO.Path]::IsPathRooted($relative)-or $relative -match '(^|[\\/])\.\.([\\/]|$)' -or $relative.Contains(':')){continue}
            $path=[IO.Path]::GetFullPath((Join-Path $taskRoot $relative))
            if($path.StartsWith($taskRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$manifestAssets+=[pscustomobject]@{Attribute=$attribute.LocalName;Relative=$relative;File=(Read-TileFileEvidence $path)}}
        }
    }
    }catch{$identity=$null;$manifestAssets=@();$manifestError='Manifest XML or required identity/application structure is invalid or unreadable.'}
}
$settings=@();$iniPath=Join-Path $taskRoot 'DesktopStub.ini'
if((Test-LocalEvidencePath $iniPath)-and (Test-Path -LiteralPath $iniPath -PathType Leaf)){
    $section=''
    foreach($line in [IO.File]::ReadLines($iniPath)){
        if($line -match '^\s*\[([^\]]+)\]\s*$'){$section=$matches[1];continue}
        if($line -notmatch '^\s*"?([^"=]+)"?\s*=\s*(.*)$'){continue}
        $key=$matches[1].Trim();$value=$matches[2].Trim()
        $allowed=($section -ieq 'Settings' -and $key -match '^(ExperimentalLiveTileUpdate|AppxManifestTarget|LiveTileTemplateStyle|UsePackageRegistration|GenerateDesktopIconForDisabledEntries|GenerateScale\d+|GenerateScaleAuto)$') -or
            ($section -ieq 'Assets' -and $key -match '^(MediumTile|WideTile|LargeTile|SmallTile|Square44x44Logo|Square30x30Logo)$') -or
            ($section -ieq 'Tiles' -and $key -ieq 'Instances') -or
            ($section -match '^Tile\.[a-z0-9_-]+$' -and $key -match '^(Type|Primary|Branding|Background|Content\.Background|Delivery\.(Mode|Template|ManifestTarget)|Assets\.(MediumTile|WideTile|LargeTile|SmallTile|Square44x44Logo|Square30x30Logo)|TextMode)$')
        if($allowed){$settings+=[pscustomobject]@{Section=$section;Key=$key;Value=$value}}
    }
}
$packages=@();$packageError=$null
if($identity -and $identity.Name -match '^[A-Za-z0-9.-]{3,50}$' -and (Get-Command Get-AppxPackage -ErrorAction SilentlyContinue)){
    try{$packages=@(Get-AppxPackage -Name $identity.Name | Select-Object Name,Version,PackageFamilyName,InstallLocation,Status)}catch{$packageError=$_.Exception.Message}
}else{$packageError='Package lookup unavailable; run this collector in Windows PowerShell on the affected user session.'}
$recent=@();$assetsRoot=Join-Path $taskRoot 'Assets'
if($RecentImages -gt 0 -and (Test-LocalEvidencePath $assetsRoot)-and (Test-Path -LiteralPath $assetsRoot -PathType Container)){
    $recent=@(Get-ChildItem -LiteralPath $assetsRoot -File -Filter '*.png' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First $RecentImages | ForEach-Object {Read-TileFileEvidence $_.FullName})
}
$topicAssets=@()
foreach($style in @('fluent','mdl2','vector')){
    foreach($relative in @("Assets\TopicTiles\v3\$style\transparent\prayer\Medium.png","Assets\TopicTiles\v3\$style\transparent\prayer\Small.png","Assets\TopicCorners\v1\$style\transparent\prayer\Logo44.png","Assets\TopicCorners\v1\$style\transparent\prayer\Logo30.png")){
        $path=Join-Path $taskRoot $relative
        if((Test-LocalEvidencePath $path)-and (Test-Path -LiteralPath $path -PathType Leaf)){$topicAssets+=Read-TileFileEvidence $path}
    }
}
$osBuild=$null
try{$os=Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion';$osBuild=[pscustomobject]@{ProductName=$os.ProductName;Build=$os.CurrentBuild;Revision=$os.UBR;ReleaseId=$os.ReleaseId}}catch{}
$report=[pscustomobject]@{CollectedUtc=[DateTime]::UtcNow;OSVersion=[Environment]::OSVersion.Version.ToString();WindowsBuild=$osBuild;Directory=$taskRoot;Identity=$identity;ManifestError=$manifestError;Packages=$packages;PackageLookupError=$packageError;Settings=$settings;ManifestAssets=$manifestAssets;RecentTopLevelAssets=$recent;PrayerTopicAssets=$topicAssets;Note='Read-only metadata, no account values, no image contents, no package/pin/ACL changes. Recent images are bounded and not recursive. Readability by this user does not prove readability by Start.'}
$json=$report|ConvertTo-Json -Depth 8
if($OutputPath){
    $reportPath=[IO.Path]::GetFullPath($OutputPath)
    if([IO.Path]::GetExtension($reportPath)-ne '.json'-or (Test-Path -LiteralPath $reportPath)){throw 'Choose a new .json report file; existing files are never overwritten.'}
    [IO.File]::WriteAllText($reportPath,$json,[Text.UTF8Encoding]::new($false))
}else{$json}

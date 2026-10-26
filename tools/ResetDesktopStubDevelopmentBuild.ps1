#requires -Version 7.0
<#
.SYNOPSIS
Archive this checkout's development build and reset only its current-user state.
.DESCRIPTION
Dry-run by default. -Apply stops this installation, removes its current-user
packages, archives settings/generated files, and copies a verified beta payload.
-ValidateColdStart tests ordinary first launch, then resets that trial as well.
Source photos, external browser accounts and unrelated packages are not changed.
#>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$BetaDirectory,[switch]$Apply,[switch]$ValidateColdStart,[switch]$ValidateSingleExe,[switch]$ArchiveValidationBuild)
$ErrorActionPreference='Stop'
$taskRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..')).TrimEnd('\')
$taskBuild=Join-Path $taskRoot 'DesktopStub\build'
$taskBeta=[IO.Path]::GetFullPath($BetaDirectory).TrimEnd('\')
$taskReleaseRoot=Join-Path $taskRoot 'DesktopStub\releases'
if(!$taskBeta.StartsWith($taskReleaseRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Beta payload must be inside this checkout releases directory.'}
$taskSid=[Security.Principal.WindowsIdentity]::GetCurrent().User.Value
function Full([string]$Value){return [IO.Path]::GetFullPath($Value.Replace('/','\')).TrimEnd('\')}
function Within([string]$Path,[string]$Parent){$p=Full $Path;$r=Full $Parent;return $p.Equals($r,[StringComparison]::OrdinalIgnoreCase)-or $p.StartsWith($r+'\',[StringComparison]::OrdinalIgnoreCase)}
function ScopeHash([string]$Text){$sha=[Security.Cryptography.SHA256]::Create();try{return [Convert]::ToHexString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($Text))).Substring(0,16)}finally{$sha.Dispose()}}
$taskInstall=ScopeHash ((Full $taskBuild).ToUpperInvariant())
$taskShared=Join-Path ([Environment]::GetFolderPath('UserProfile')) ('.DesktopStub\Profiles\'+$taskInstall)
$taskLegacy=Join-Path $env:LOCALAPPDATA ('DesktopStub\Profiles\'+$taskInstall)
$taskProfiles=@($taskShared,$taskLegacy)
$taskAllowed=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
foreach($p in @($taskBuild)+$taskProfiles){$null=$taskAllowed.Add((Full $p))}
function OrdinaryTree([string]$Path){
 if(!(Test-Path -LiteralPath $Path)){return}
 $item=Get-Item -LiteralPath $Path -Force
 if(!$item.PSIsContainer-or ($item.Attributes-band[IO.FileAttributes]::ReparsePoint)){throw ('Not an ordinary directory: '+$Path)}
 if(Get-ChildItem -LiteralPath $Path -Force -Recurse | Where-Object {$_.Attributes-band[IO.FileAttributes]::ReparsePoint} | Select-Object -First 1){throw ('Linked entry in reset directory: '+$Path)}
}
function OwnedProcesses{
 return ,@(Get-CimInstance Win32_Process -Filter "Name='DesktopStub.exe' OR Name='DesktopStubAppxStub.exe' OR Name='DesktopStubLiveTileBroker.exe' OR Name='DesktopStubTileLauncher.exe'" | Where-Object {$_.ExecutablePath-and ((Within $_.ExecutablePath $taskBuild)-or (Within $_.ExecutablePath $taskShared))} | Where-Object {(Invoke-CimMethod -InputObject $_ -MethodName GetOwnerSid).Sid-eq $taskSid})
}
$taskStamp=[TimeZoneInfo]::ConvertTimeFromUtc([DateTime]::UtcNow,[TimeZoneInfo]::FindSystemTimeZoneById('SE Asia Standard Time')).ToString('yyyyMMdd-HHmmss')
$taskArchive=Join-Path $taskRoot ('artifacts\desktopstub-reset-'+$taskStamp+'-'+[guid]::NewGuid().ToString('N').Substring(0,6))
$taskManifest=Get-Content -LiteralPath (Join-Path $taskBeta 'beta-manifest.json') -Raw | ConvertFrom-Json
foreach($file in $taskManifest.Files){$path=Join-Path $taskBeta $file.Name;if([IO.Path]::GetFileName($file.Name)-ne $file.Name-or !(Test-Path -LiteralPath $path -PathType Leaf)-or (Get-FileHash -LiteralPath $path).Hash-ne $file.SHA256){throw 'Beta manifest verification failed.'}}
OrdinaryTree $taskBeta;foreach($p in @($taskBuild)+$taskProfiles){OrdinaryTree $p}
Write-Output ('Scope: '+$taskBuild+'; installation '+$taskInstall+'; user '+$taskSid)
Write-Output ('Archive: '+$taskArchive)
if(!$Apply){Write-Output 'Dry run. Use -Apply to perform this app-specific reset.';return}
$null=[IO.Directory]::CreateDirectory($taskArchive)
function Run([string]$File,[string[]]$Arguments,[string]$Label,[int]$Seconds=120){
 $p=[Diagnostics.Process]::new();$p.StartInfo.FileName=$File;$p.StartInfo.UseShellExecute=$false;$p.StartInfo.CreateNoWindow=$true
 $p.StartInfo.RedirectStandardOutput=$true;$p.StartInfo.RedirectStandardError=$true
 $p.StartInfo.StandardOutputEncoding=[Text.Encoding]::UTF8;$p.StartInfo.StandardErrorEncoding=[Text.Encoding]::UTF8
 for($i=0;$i-lt $Arguments.Count;$i++){$a=$Arguments[$i];if($i-and $Arguments[$i-1]-eq '-Command'){$a='[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false);'+$a};$p.StartInfo.ArgumentList.Add($a)}
 try{if(!$p.Start()){throw ('Could not start '+$Label)};$out=$p.StandardOutput.ReadToEndAsync();$err=$p.StandardError.ReadToEndAsync()
  if(!$p.WaitForExit($Seconds*1000)){$p.Kill($true);$p.WaitForExit();throw ('Timed out: '+$Label)}
  $text=$out.GetAwaiter().GetResult()+$err.GetAwaiter().GetResult();[IO.File]::WriteAllText((Join-Path $taskArchive ($Label+'.log')),$text)
  if($p.ExitCode-ne 0){throw ($Label+' exited '+$p.ExitCode+'; see archive log.')};return $text
 }finally{$p.Dispose()}
}
function StopOwned{
 $rows=OwnedProcesses;if(!$rows.Count){return}
 try{$null=Run (Join-Path $taskBuild 'DesktopStub.exe') @('--exit') 'graceful-exit' 30}catch{Write-Output 'Default exit request failed; waiting for owned resident shutdown.'}
 $end=[DateTime]::UtcNow.AddSeconds(90)
 while((OwnedProcesses).Count-and [DateTime]::UtcNow-lt $end){Start-Sleep -Milliseconds 250}
 foreach($row in (OwnedProcesses)){
  $current=Get-CimInstance Win32_Process -Filter ('ProcessId='+$row.ProcessId)
  if($current-and $current.CreationDate-eq $row.CreationDate-and $current.ExecutablePath-eq $row.ExecutablePath){Stop-Process -Id $row.ProcessId -ErrorAction Stop;Write-Output ('Stopped unresponsive owned DesktopStub process '+$row.ProcessId)}
 }
}
function ProfileKeys{
 $keys=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);$null=$keys.Add((Full (Join-Path $taskBuild 'DesktopStub.ini')))
 foreach($dir in $taskProfiles){$ini=Join-Path $dir 'profiles.ini';if(Test-Path -LiteralPath $ini){
  $text=Get-Content -LiteralPath $ini -Raw;if($text-notmatch [regex]::Escape($taskSid)){throw 'Coordinator owner SID differs; reset stopped.'}
  foreach($line in $text.Split("`n")){if($line-match '^\s*IniPath\s*=\s*"(.*)"\s*$'){$path=$Matches[1].Replace('\\','\');if($path-match '^[A-Za-z]:\\'){$null=$keys.Add((Full $path))}}}
 }}
 return ,$keys
}
function RegistrationDirectories{
 $result=@();$base=Join-Path $env:LOCALAPPDATA 'DesktopStub\RegistrationTiles';$userScope=ScopeHash ('SID:'+$taskSid)
 foreach($key in (ProfileKeys)){
  $profileScope=ScopeHash ((Full $key).ToUpperInvariant());$path=Join-Path $base ($userScope+'\'+$profileScope)
  if(Test-Path -LiteralPath $path){if(!(Within $path $base)){throw 'Unexpected Registration state directory.'};$null=$taskAllowed.Add((Full $path));$result+=$path}
 }
 return ,$result
}
function ArchiveStartupLinks{
 $startup=[Environment]::GetFolderPath('Startup');if(!$startup-or !(Test-Path -LiteralPath $startup)){return}
 $shell=New-Object -ComObject WScript.Shell;$destination=Join-Path $taskArchive 'startup-shortcuts'
 try{foreach($file in Get-ChildItem -LiteralPath $startup -File -Filter '*.lnk'){
   $link=$shell.CreateShortcut($file.FullName)
   try{$target=$link.TargetPath;if($target-and ((Within $target $taskBuild)-or (Within $target $taskShared))){
     $null=[IO.Directory]::CreateDirectory($destination);Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $destination $file.Name)
     if((Get-FileHash -LiteralPath $file.FullName).Hash-ne (Get-FileHash -LiteralPath (Join-Path $destination $file.Name)).Hash){throw 'Startup backup mismatch.'}
     Remove-Item -LiteralPath $file.FullName;Write-Output ('Archived owned Startup shortcut '+$file.Name)
   }}finally{$null=[Runtime.InteropServices.Marshal]::FinalReleaseComObject($link)}
 }}finally{$null=[Runtime.InteropServices.Marshal]::FinalReleaseComObject($shell)}
}
$taskPowerShell=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
function RemoveOwnedPackages([string]$Label){
 $keys=ProfileKeys
 $json=Run $taskPowerShell @('-NoProfile','-NonInteractive','-Command','$ErrorActionPreference="Stop";$ProgressPreference="SilentlyContinue";ConvertTo-Json -InputObject @(Get-AppxPackage | Select-Object Name,PackageFullName,PackageFamilyName,InstallLocation,IsFramework,IsResourcePackage) -Compress') ($Label+'-inventory')
 $removed=@()
 foreach($package in @($json|ConvertFrom-Json | Sort-Object {$_.InstallLocation.Length} -Descending)){
  if(!$package.InstallLocation-or $package.IsFramework-or $package.IsResourcePackage){continue}
  $location=Full $package.InstallLocation;$owned=(Within $location $taskBuild)-or (Within $location $taskShared)-or (Within $location $taskLegacy)
  if(!$owned){$marker=Join-Path $location 'registration.owner';if(Test-Path -LiteralPath $marker -PathType Leaf){
    $lines=@(Get-Content -LiteralPath $marker);if($lines.Count-ge 6-and $lines[0]-eq 'AIP_TILE_REGISTRATION_V1'-and $keys.Contains($lines[4])){$owned=$true}
  }}
  if(!$owned){continue}
  $env:DESKTOPSTUB_RESET_PACKAGE=$package.PackageFullName
  try{$null=Run $taskPowerShell @('-NoProfile','-NonInteractive','-Command','$ErrorActionPreference="Stop";$ProgressPreference="SilentlyContinue";Remove-AppxPackage -Package $env:DESKTOPSTUB_RESET_PACKAGE -ErrorAction Stop') ($Label+'-remove-'+$removed.Count) 180}
  finally{Remove-Item Env:DESKTOPSTUB_RESET_PACKAGE -ErrorAction SilentlyContinue}
  $removed+=$package.PackageFullName;Write-Output ('Removed current-user DesktopStub package '+$package.Name)
 }
 [IO.File]::WriteAllText((Join-Path $taskArchive ($Label+'-removed.json')), (ConvertTo-Json -InputObject $removed))
}
function ArchiveDirectory([string]$Source,[string]$Name){
 $sourcePath=Full $Source;if(!$taskAllowed.Contains($sourcePath)){throw 'Unapproved reset source path.'};if(!(Test-Path -LiteralPath $sourcePath)){return}
 OrdinaryTree $sourcePath;$destination=Join-Path $taskArchive $Name
 if(!(Within $destination $taskArchive)-or (Test-Path -LiteralPath $destination)){throw 'Unsafe archive destination.'}
 if([IO.Path]::GetPathRoot($sourcePath)-eq [IO.Path]::GetPathRoot($destination)){Move-Item -LiteralPath $sourcePath -Destination $destination}
 else{
  Copy-Item -LiteralPath $sourcePath -Destination $destination -Recurse
  foreach($file in Get-ChildItem -LiteralPath $sourcePath -File -Recurse -Force){$copy=Join-Path $destination ([IO.Path]::GetRelativePath($sourcePath,$file.FullName));if(!(Test-Path -LiteralPath $copy)-or (Get-FileHash -LiteralPath $file.FullName).Hash-ne (Get-FileHash -LiteralPath $copy).Hash){throw 'Archive copy verification failed.'}}
  OrdinaryTree $sourcePath;if(!$taskAllowed.Contains((Full $sourcePath))){throw 'Reset source changed.'};Remove-Item -LiteralPath $sourcePath -Recurse
 }
 Write-Output ('Archived '+$sourcePath)
}
StopOwned
ArchiveStartupLinks
$taskRegistrationDirs=RegistrationDirectories
foreach($p in @($taskBuild)+$taskProfiles){if(Test-Path -LiteralPath (Join-Path $p 'profiles.ini')){Copy-Item -LiteralPath (Join-Path $p 'profiles.ini') -Destination (Join-Path $taskArchive ('coordinator-before-'+[IO.Path]::GetFileName((Split-Path $p -Parent))+'.ini'))}}
RemoveOwnedPackages 'previous'
for($index=0;$index-lt $taskRegistrationDirs.Count;$index++){ArchiveDirectory $taskRegistrationDirs[$index] ('previous-registration-'+$index)}
ArchiveDirectory $taskBuild 'previous-development-build'
ArchiveDirectory $taskShared 'previous-shared-profiles';ArchiveDirectory $taskLegacy 'previous-legacy-profiles'
$null=[IO.Directory]::CreateDirectory($taskBuild)
foreach($file in $taskManifest.Files){Copy-Item -LiteralPath (Join-Path $taskBeta $file.Name) -Destination (Join-Path $taskBuild $file.Name)}
Copy-Item -LiteralPath (Join-Path $taskBeta 'beta-manifest.json') -Destination (Join-Path $taskBuild 'beta-manifest.json')
$taskColdPassed=$false
if($ValidateColdStart){
 if($ValidateSingleExe){$held=Join-Path $taskArchive 'trial-held-helpers';$null=[IO.Directory]::CreateDirectory($held);foreach($name in @('DesktopStubLiveTileBroker.exe','DesktopStubTileLauncher.exe')){Move-Item -LiteralPath (Join-Path $taskBuild $name) -Destination (Join-Path $held $name)}}
 try{
 $trial=Start-Process -FilePath (Join-Path $taskBuild 'DesktopStub.exe') -WorkingDirectory $taskBuild -WindowStyle Hidden -PassThru
 Start-Sleep -Seconds 8
 $end=[DateTime]::UtcNow.AddSeconds(67);while([DateTime]::UtcNow-lt $end){if($trial.HasExited){throw 'Ordinary beta first launch exited unexpectedly.'};Start-Sleep -Seconds 2}
 $ini=Get-Content -LiteralPath (Join-Path $taskBuild 'DesktopStub.ini') -Raw
 if($ini-notmatch '"?Delivery.Mode"?\s*=\s*"?Registration"?'-or $ini-notmatch '"?Delivery.PrimaryRegistration"?\s*=\s*"?Main"?'){throw 'First launch did not retain requested primary Registration defaults.'}
 $log=Get-Content -LiteralPath (Join-Path $taskBuild 'DesktopStub.log') -Raw
 $env:DESKTOPSTUB_BETA_CHECK_DIRECTORY=$taskBuild
 try{$registered=Run $taskPowerShell @('-NoProfile','-NonInteractive','-Command','$ErrorActionPreference="Stop";$ProgressPreference="SilentlyContinue";$d=[IO.Path]::GetFullPath($env:DESKTOPSTUB_BETA_CHECK_DIRECTORY).TrimEnd("\");ConvertTo-Json -InputObject @(Get-AppxPackage | Where-Object {$_.InstallLocation -and [IO.Path]::GetFullPath($_.InstallLocation).TrimEnd("\") -eq $d} | Select-Object Name,PackageFullName,InstallLocation) -Compress') 'trial-registered-main'}finally{Remove-Item Env:DESKTOPSTUB_BETA_CHECK_DIRECTORY -ErrorAction SilentlyContinue}
 if(@($registered|ConvertFrom-Json).Count-ne 1){throw 'First launch did not register exactly one main package at its own directory.'}
 foreach($asset in @(@('MediumTile.png',150,150),@('WideTile.png',310,150),@('LargeTile.png',310,310))){
  $path=Join-Path $taskBuild ('Assets\'+$asset[0]);if(!(Test-Path -LiteralPath $path -PathType Leaf)){throw 'First launch did not generate all Desktop wallpaper assets.'}
  $bytes=[IO.File]::ReadAllBytes($path);if($bytes.Length-lt 24){throw 'Invalid Desktop PNG.'}
  $width=[uint32]$bytes[16]*16777216+[uint32]$bytes[17]*65536+[uint32]$bytes[18]*256+$bytes[19]
  $height=[uint32]$bytes[20]*16777216+[uint32]$bytes[21]*65536+[uint32]$bytes[22]*256+$bytes[23]
  if($width-ne $asset[1]-or $height-ne $asset[2]){throw 'First Desktop asset dimensions differ from the requested tile size.'}
 }
 [IO.File]::WriteAllText((Join-Path $taskArchive 'first-run-validation.log'),$log)
 $null=Run (Join-Path $taskBuild 'DesktopStub.exe') @('--profile-list') 'fresh-profile-list' 30
 $taskColdPassed=$true
 }finally{
 StopOwned;RemoveOwnedPackages 'trial'
 $keep=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase);foreach($file in $taskManifest.Files){$null=$keep.Add($file.Name)};$null=$keep.Add('beta-manifest.json')
 $generated=Join-Path $taskArchive 'first-run-generated';$null=[IO.Directory]::CreateDirectory($generated)
 foreach($item in Get-ChildItem -LiteralPath $taskBuild -Force){if(!$keep.Contains($item.Name)){Move-Item -LiteralPath $item.FullName -Destination (Join-Path $generated $item.Name)}}
 ArchiveDirectory $taskShared 'trial-shared-profiles';ArchiveDirectory $taskLegacy 'trial-legacy-profiles'
 foreach($file in $taskManifest.Files){if(!(Test-Path -LiteralPath (Join-Path $taskBuild $file.Name))){Copy-Item -LiteralPath (Join-Path $taskBeta $file.Name) -Destination (Join-Path $taskBuild $file.Name)}}
 }
}
foreach($file in $taskManifest.Files){if((Get-FileHash -LiteralPath (Join-Path $taskBuild $file.Name)).Hash-ne $file.SHA256){throw 'Installed beta payload changed.'}}
if($ArchiveValidationBuild){$validation=Join-Path $taskRoot 'build';$null=$taskAllowed.Add((Full $validation));ArchiveDirectory $validation 'validation-build'}
$receipt=[pscustomobject]@{Archive=$taskArchive;Beta=$taskBeta;Installed=$taskBuild;ColdStartValidated=$taskColdPassed;SingleExeTrial=[bool]$ValidateSingleExe;FreshSettingsAbsent=!(Test-Path -LiteralPath (Join-Path $taskBuild 'DesktopStub.ini'));ResidentsRemaining=(OwnedProcesses).Count;InstallationScope=$taskInstall}
[IO.File]::WriteAllText((Join-Path $taskArchive 'reset-result.json'),($receipt|ConvertTo-Json));$receipt|ConvertTo-Json -Compress

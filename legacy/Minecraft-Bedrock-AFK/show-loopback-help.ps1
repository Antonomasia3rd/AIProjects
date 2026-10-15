$ErrorActionPreference = 'Stop'

Write-Host ""
Write-Host "Minecraft Bedrock localhost/loopback helper"
Write-Host "-------------------------------------------"
Write-Host ""
Write-Host "This is only needed if official Minecraft cannot see/join 127.0.0.1."
Write-Host "The classic UWP package exemption is:"
Write-Host ""
Write-Host '  CheckNetIsolation LoopbackExempt -a -n="Microsoft.MinecraftUWP_8wekyb3d8bbwe"'
Write-Host ""
Write-Host "Installed Minecraft-ish AppX packages on this PC:"
Get-AppxPackage -AllUsers -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match 'Minecraft' } |
    Select-Object Name, PackageFamilyName, PackageFullName |
    Format-Table -AutoSize

Write-Host ""
Write-Host "If this Minecraft build is unpackaged/GDK desktop and localhost already works,"
Write-Host "you do NOT need an AppContainer loopback exemption."
Write-Host ""
Write-Host "Alternative: connect Minecraft to this PC's LAN IPv4 address instead of 127.0.0.1."
Write-Host "Candidate IPv4 addresses:"
Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
    Where-Object {
        $_.IPAddress -ne '127.0.0.1' -and
        $_.IPAddress -notlike '169.254.*' -and
        $_.PrefixOrigin -ne 'WellKnown'
    } |
    Select-Object InterfaceAlias, IPAddress |
    Format-Table -AutoSize

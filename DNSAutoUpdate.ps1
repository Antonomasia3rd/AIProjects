[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$ZoneName = "server.local",
    [string[]]$SubFolder,
    [string[]]$ManagedRecordName = @(),
    [switch]$NoRootRecord,
    [string]$LogFile = ".\DNSAutoUpdate.log",
    [ValidateRange(0, 1048576)]
    [int]$MaxLogMegabytes = 10,
    [ValidateRange(0, 100)]
    [int]$LogRetentionCount = 5,
    [ValidateRange(1, 86400)]
    [int]$SleepSeconds = 20,
    [string[]]$IncludeInterfaceAlias = @(),
    [string[]]$ExcludeInterfaceAlias = @("Loopback*", "vEthernet*", "VMware*", "VirtualBox*", "Bluetooth*"),
    [string[]]$IncludeIPAddress = @(),
    [switch]$IncludeUnpreferred
)

$scriptPath = Join-Path $PSScriptRoot 'DNSAutoUpdate\DNSAutoUpdate.ps1'
if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
    throw "DNSAutoUpdate implementation was not found: $scriptPath"
}

& $scriptPath @PSBoundParameters

[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$ZoneName = "server.local",
    [string[]]$SubFolder,                         # no default here
    [string]$LogFile = ".\DNSAutoUpdate.log",
    [ValidateRange(1, 86400)]
    [int]$SleepSeconds = 20,
    [string[]]$IncludeInterfaceAlias = @(),
    [string[]]$ExcludeInterfaceAlias = @("Loopback*", "vEthernet*", "VMware*", "VirtualBox*", "Bluetooth*"),
    [string[]]$IncludeIPAddress = @(),
    [switch]$IncludeUnpreferred
)

# Apply default **only if the user did not provide -SubFolder**
if (-not $PSBoundParameters.ContainsKey('SubFolder')) {
    $SubFolder = @("")
}

function Write-Log {
    param([string]$msg)
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $line = "$timestamp  $msg"
    Add-Content -Path $LogFile -Value $line
    Write-Output $line
}

function Normalize-IP {
    param([string]$ip)
    return ($ip -replace '/\d+$','').Trim()
}

function Test-WildcardAny {
    param(
        [string]$Value,
        [string[]]$Patterns
    )

    foreach ($pattern in $Patterns) {
        if ($pattern -and $Value -like $pattern) {
            return $true
        }
    }

    return $false
}

function Get-EligibleServerIPv4 {
    if ($IncludeIPAddress.Count -gt 0) {
        return @(
            $IncludeIPAddress |
                ForEach-Object { Normalize-IP $_ } |
                Where-Object { $_ -and $_ -notmatch '^(127\.|169\.254\.)' -and $_ -ne '0.0.0.0' } |
                Sort-Object -Unique
        )
    }

    $addresses = Get-NetIPAddress -AddressFamily IPv4
    $eligible = foreach ($addr in $addresses) {
        $ip = Normalize-IP $addr.IPAddress
        if (-not $ip -or $ip -match '^(127\.|169\.254\.)' -or $ip -eq '0.0.0.0') {
            continue
        }

        if (-not $IncludeUnpreferred -and $addr.AddressState -and $addr.AddressState.ToString() -ne 'Preferred') {
            continue
        }

        $alias = [string]$addr.InterfaceAlias
        if ($IncludeInterfaceAlias.Count -gt 0 -and -not (Test-WildcardAny -Value $alias -Patterns $IncludeInterfaceAlias)) {
            continue
        }

        if ($ExcludeInterfaceAlias.Count -gt 0 -and (Test-WildcardAny -Value $alias -Patterns $ExcludeInterfaceAlias)) {
            continue
        }

        $ip
    }

    return @($eligible | Sort-Object -Unique)
}

Write-Log "============================================="
Write-Log " DNS Auto-Update Background Service Starting "
Write-Log " Zone: $ZoneName"
if ($SubFolder.Count -gt 0) {
    Write-Log " Subfolders enabled: $($SubFolder -join ', ')"
} else {
    Write-Log " Subfolders: <none>"
}
Write-Log "============================================="

while ($true) {

    Write-Log "Starting scan cycle..."

    $serverIPs = @(Get-EligibleServerIPv4)
    if ($serverIPs.Count -eq 0) {
        Write-Log "No eligible server IPv4 addresses detected; skipping DNS changes this cycle."
        Write-Log "Cycle complete. Sleeping for $SleepSeconds seconds."
        Write-Log "---------------------------------------------"
        Start-Sleep -Seconds $SleepSeconds
        continue
    }

    Write-Log "Eligible server IPs detected: $($serverIPs -join ', ')"

    # ===========================================================
    # FUNCTION - apply sync logic to one exact DNS owner name
    # ===========================================================
    function Sync-ARecords {
        param([string]$RecName)

        function Get-ExactARecords {
            try {
                @(Get-DnsServerResourceRecord -ZoneName $ZoneName -Name $RecName -RRType A -ErrorAction Stop)
            } catch [Microsoft.Management.Infrastructure.CimException] {
                @()
            }
        }

        $records = Get-ExactARecords
        Write-Log "Found $($records.Count) A records for '$RecName'"

        foreach ($rec in $records) {
            $existingIP = Normalize-IP $rec.RecordData.IPv4Address.ToString()
            Write-Log " Checking '$RecName' → $existingIP"

            if ($serverIPs -notcontains $existingIP) {
                Write-Log "   OUTDATED IP detected, removing $existingIP"
                try {
                    if ($PSCmdlet.ShouldProcess("$ZoneName/$RecName $existingIP", "Remove stale A record")) {
                        Remove-DnsServerResourceRecord `
                            -ZoneName $ZoneName `
                            -RRType A `
                            -Name $RecName `
                            -RecordData $existingIP `
                            -Force
                        Write-Log "   Removed successfully."
                    } else {
                        Write-Log "   Removal skipped."
                    }
                } catch {
                    Write-Log "   ERROR removing: $_"
                }
            } else {
                Write-Log "   IP is valid."
            }
        }

        # Re-evaluate remaining IPs for this exact owner name.
        $records = Get-ExactARecords
        $existingIPs = $records | ForEach-Object {
            Normalize-IP $_.RecordData.IPv4Address.ToString()
        }

        Write-Log "Ensuring correct IP set for '$RecName'"

        foreach ($ip in $serverIPs) {
            if ($existingIPs -notcontains $ip) {
                Write-Log "   Adding missing IP $ip"
                try {
                    if ($PSCmdlet.ShouldProcess("$ZoneName/$RecName $ip", "Add missing A record")) {
                        Add-DnsServerResourceRecordA `
                            -ZoneName $ZoneName `
                            -Name $RecName `
                            -IPv4Address $ip
                        Write-Log "   Added successfully."
                    } else {
                        Write-Log "   Add skipped."
                    }
                } catch {
                    Write-Log "   ERROR adding: $_"
                }
            }
        }
    }

    # ===========================================================
    # ALWAYS process root "@"
    # ===========================================================
    Sync-ARecords -RecName "@"

    # ===========================================================
    # Process each subfolder (if any)
    # ===========================================================
    foreach ($folder in $SubFolder) {
        if ($folder -and $folder.Trim() -ne "") {
            Sync-ARecords -RecName $folder
        }
    }

    Write-Log "Cycle complete. Sleeping for $SleepSeconds seconds."
    Write-Log "---------------------------------------------"
    Start-Sleep -Seconds $SleepSeconds
}

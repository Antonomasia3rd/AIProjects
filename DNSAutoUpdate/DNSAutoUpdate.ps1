[CmdletBinding(SupportsShouldProcess = $true, ConfirmImpact = 'Medium')]
param(
    [string]$ZoneName = "server.local",
    [string[]]$SubFolder,                         # no default here
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

# Apply default **only if the user did not provide -SubFolder**
if (-not $PSBoundParameters.ContainsKey('SubFolder')) {
    $SubFolder = @("")
}

function Write-Log {
    param([string]$msg)
    $timestamp = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss")
    $line = "$timestamp  $msg"
    try {
        Rotate-LogIfNeeded
        $logDir = Split-Path -Parent $LogFile
        if ($logDir) {
            New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        }
        Add-Content -Path $LogFile -Value $line
    } catch {
        Write-Warning "Could not write DNSAutoUpdate log '$LogFile': $($_.Exception.Message)"
    }
    Write-Output $line
}

function Rotate-LogIfNeeded {
    if ($MaxLogMegabytes -le 0 -or [string]::IsNullOrWhiteSpace($LogFile)) {
        return
    }

    $logItem = Get-Item -LiteralPath $LogFile -ErrorAction SilentlyContinue
    if ($null -eq $logItem) {
        return
    }

    $maxBytes = [int64]$MaxLogMegabytes * 1MB
    if ($logItem.Length -lt $maxBytes) {
        return
    }

    if ($LogRetentionCount -le 0) {
        Remove-Item -LiteralPath $LogFile -Force -ErrorAction SilentlyContinue
        return
    }

    for ($i = $LogRetentionCount; $i -ge 1; $i--) {
        $source = if ($i -eq 1) { $LogFile } else { "$LogFile.$($i - 1)" }
        $destination = "$LogFile.$i"
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            continue
        }
        if ($i -eq $LogRetentionCount) {
            Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        }
        Move-Item -LiteralPath $source -Destination $destination -Force
    }
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

function Normalize-RecordName {
    param([string]$Name)

    $normalized = ([string]$Name).Trim()
    if (-not $normalized) {
        return "@"
    }
    return $normalized
}

function Test-DnsRecordNotFoundException {
    param([Microsoft.Management.Infrastructure.CimException]$Exception)

    # Common DNS "no such owner/record" codes. Other CIM failures usually mean
    # the DNS lookup itself failed and should not be treated as an empty set.
    $notFoundCodes = @(9003, 9701, 9714)
    if ($null -ne $Exception.NativeErrorCode -and $notFoundCodes -contains [int]$Exception.NativeErrorCode) {
        return $true
    }

    $message = [string]$Exception.Message
    return $message -match '(?i)DNS_ERROR_(RECORD|NAME)_DOES_NOT_EXIST|resource record.*(not found|does not exist)|record.*does not exist'
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

$managedRecordNames = @()
if ($ManagedRecordName.Count -gt 0) {
    $managedRecordNames = @(
        $ManagedRecordName |
            ForEach-Object { Normalize-RecordName $_ } |
            Where-Object { $_ } |
            Sort-Object -Unique
    )
} else {
    if (-not $NoRootRecord) {
        $managedRecordNames += "@"
    }

    $managedRecordNames += @(
        $SubFolder |
            ForEach-Object { Normalize-RecordName $_ } |
            Where-Object { $_ -and $_ -ne "@" } |
            Sort-Object -Unique
    )
    $managedRecordNames = @($managedRecordNames | Sort-Object -Unique)
}

if ($managedRecordNames.Count -eq 0) {
    throw "No managed DNS owner names were selected. Remove -NoRootRecord, pass -SubFolder, or pass -ManagedRecordName."
}

Write-Log "============================================="
Write-Log " DNS Auto-Update Background Service Starting "
Write-Log " Zone: $ZoneName"
Write-Log " Managed exact A record owner names: $($managedRecordNames -join ', ')"
Write-Log " Records outside this allowlist are ignored."
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
                if (Test-DnsRecordNotFoundException -Exception $_.Exception) {
                    @()
                    return
                }
                throw
            }
        }

        try {
            $records = @(Get-ExactARecords)
        } catch {
            Write-Log "ERROR reading A records for '$RecName'; skipping this owner name this cycle: $($_.Exception.Message)"
            return
        }
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
        try {
            $records = @(Get-ExactARecords)
        } catch {
            Write-Log "ERROR re-reading A records for '$RecName' after removals; skipping additions this cycle: $($_.Exception.Message)"
            return
        }
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

    foreach ($recordName in $managedRecordNames) {
        Sync-ARecords -RecName $recordName
    }

    Write-Log "Cycle complete. Sleeping for $SleepSeconds seconds."
    Write-Log "---------------------------------------------"
    Start-Sleep -Seconds $SleepSeconds
}

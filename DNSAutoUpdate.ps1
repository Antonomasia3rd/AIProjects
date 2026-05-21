param(
    [string]$ZoneName = "server.local",
    [string[]]$SubFolder,                         # no default here
    [string]$LogFile = ".\DNSAutoUpdate.log",
    [int]$SleepSeconds = 20
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

    # Get local server IPv4s
    $rawIPs = Get-NetIPAddress -AddressFamily IPv4 | Select-Object -ExpandProperty IPAddress
    $serverIPs = $rawIPs | ForEach-Object { Normalize-IP $_ }
    Write-Log "Server IPs detected: $($serverIPs -join ', ')"

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
                    Remove-DnsServerResourceRecord `
                        -ZoneName $ZoneName `
                        -RRType A `
                        -Name $RecName `
                        -RecordData $existingIP `
                        -Force
                    Write-Log "   Removed successfully."
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
                    Add-DnsServerResourceRecordA `
                        -ZoneName $ZoneName `
                        -Name $RecName `
                        -IPv4Address $ip
                    Write-Log "   Added successfully."
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

# DNSAutoUpdate

PowerShell loop that keeps Windows DNS Server A records synchronized with the server's current IPv4 addresses.

It processes the zone root record (`@`) and optional subfolder/node records. Outdated IPs are removed and missing current IPs are added each cycle.

## Requirements

- Windows Server or Windows installation with the DNS Server PowerShell module.
- Administrator rights to modify DNS records.
- A DNS zone that the machine is allowed to update.

## Run

From this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local"
```

With subfolders/nodes:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -SubFolder "app","files" -SleepSeconds 60
```

Preview DNS changes without applying them:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -WhatIf
```

## Parameters

- `-ZoneName`: DNS zone to maintain. Default: `server.local`.
- `-SubFolder`: optional DNS nodes to maintain in addition to `@`.
- `-LogFile`: log file path. Default: `.\DNSAutoUpdate.log` from the process working directory.
- `-SleepSeconds`: delay between scan cycles. Default: `20`.
- `-IncludeInterfaceAlias`: optional wildcard allowlist for network interface aliases.
- `-ExcludeInterfaceAlias`: wildcard denylist for network interface aliases. Defaults exclude loopback and common virtual adapters.
- `-IncludeIPAddress`: explicit IP allowlist. When supplied, interface discovery is bypassed.
- `-IncludeUnpreferred`: include IPv4 addresses whose `AddressState` is not `Preferred`.
- `-WhatIf`: log and preview DNS add/remove operations without changing records.

## Notes

- The script runs forever until the PowerShell process is stopped.
- Loopback, APIPA, `0.0.0.0`, unpreferred addresses, and excluded virtual adapters are ignored by default. If no eligible IPv4 address remains, the cycle skips DNS changes instead of deleting records.
- The repository root also contains a duplicate `DNSAutoUpdate.ps1` convenience copy with the same content.
- Generated logs/build folders are ignored by git.

# DNSAutoUpdate

PowerShell loop that keeps selected Windows DNS Server A records synchronized with the server's current IPv4 addresses.

The script only manages an explicit set of exact DNS owner names. For each managed name, stale A records are removed and missing current A records are added each cycle. Records outside the managed allowlist are ignored.

## Requirements

- Windows Server or a Windows installation with the DNS Server PowerShell module.
- Administrator rights to modify DNS records.
- A DNS zone that the machine is allowed to update.

## Run

From this folder:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local"
```

Legacy/default behavior manages the zone root owner name `@`.

Manage root plus subfolder/node records:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -SubFolder "app","files" -SleepSeconds 60
```

Use an explicit managed allowlist instead of the legacy root-plus-subfolder selection:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -ManagedRecordName "@","app","files"
```

Manage only non-root names:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -NoRootRecord -SubFolder "app","files"
```

Preview DNS changes without applying them:

```powershell
powershell -ExecutionPolicy Bypass -File .\DNSAutoUpdate.ps1 -ZoneName "server.local" -ManagedRecordName "@","app" -WhatIf
```

## Parameters

- `-ZoneName`: DNS zone to maintain. Default: `server.local`.
- `-SubFolder`: optional DNS owner names to maintain in addition to `@` when `-ManagedRecordName` is not supplied.
- `-ManagedRecordName`: explicit exact owner-name allowlist. When supplied, `-SubFolder` and `-NoRootRecord` are ignored for owner-name selection.
- `-NoRootRecord`: do not manage `@` when using legacy `-SubFolder` selection.
- `-LogFile`: log file path. Default: `.\DNSAutoUpdate.log` from the process working directory.
- `-SleepSeconds`: delay between scan cycles. Default: `20`.
- `-IncludeInterfaceAlias`: optional wildcard allowlist for network interface aliases.
- `-ExcludeInterfaceAlias`: wildcard denylist for network interface aliases. Defaults exclude loopback and common virtual adapters.
- `-IncludeIPAddress`: explicit IP allowlist. When supplied, interface discovery is bypassed.
- `-IncludeUnpreferred`: include IPv4 addresses whose `AddressState` is not `Preferred`.
- `-WhatIf`: log and preview DNS add/remove operations without changing records.
- `-Confirm`: prompt before each DNS add/remove operation.

## Safety Notes

- This script is destructive for managed owner names by design: A records under those exact names that are not in the current eligible IP set are removed.
- Use `-ManagedRecordName` for production jobs so the owner-name allowlist is obvious in the scheduled command.
- If no eligible IPv4 address remains, the cycle skips DNS changes instead of deleting records.
- Loopback, APIPA, `0.0.0.0`, unpreferred addresses, and excluded virtual adapters are ignored by default.
- The repository root also contains a `DNSAutoUpdate.ps1` convenience wrapper that forwards to this script with the same parameters.
- The script runs forever until the PowerShell process is stopped.
- Generated logs are ignored by git.

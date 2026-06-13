# DNSAutoUpdate

C# console loop that keeps selected Windows DNS Server A records synchronized with the server's current IPv4 addresses.

The script only manages an explicit set of exact DNS owner names. For each managed name, stale A records are removed and missing current A records are added each cycle. Records outside the managed allowlist are ignored.

## Requirements

- Windows Server or a Windows installation with `dnscmd.exe` available.
- Administrator rights to modify DNS records.
- A DNS zone that the machine is allowed to update.

## Build

From this folder:

```cmd
BuildDNSAutoUpdate.cmd
```

Output:

```text
build\DNSAutoUpdate.exe
```

## Run

From this folder:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local"
```

Legacy/default behavior manages the zone root owner name `@`.

Manage root plus subfolder/node records:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local" -SubFolder "app,files" -SleepSeconds 60
```

Use an explicit managed allowlist instead of the legacy root-plus-subfolder selection:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local" -ManagedRecordName "@,app,files"
```

Manage only non-root names:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local" -NoRootRecord -SubFolder "app,files"
```

Preview DNS changes without applying them:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local" -ManagedRecordName "@,app" -WhatIf
```

Run exactly one cycle, suitable for Task Scheduler:

```cmd
DNSAutoUpdate.cmd -ZoneName "server.local" -ManagedRecordName "@,app" -Once
```

## Parameters

- `-ZoneName`: DNS zone to maintain. Default: `server.local`.
- `-SubFolder`: optional DNS owner names to maintain in addition to `@` when `-ManagedRecordName` is not supplied.
- `-ManagedRecordName`: explicit exact owner-name allowlist. When supplied, `-SubFolder` and `-NoRootRecord` are ignored for owner-name selection.
- `-NoRootRecord`: do not manage `@` when using legacy `-SubFolder` selection.
- `-LogFile`: log file path. Default: `DNSAutoUpdate.log` beside the compiled helper executable. Relative paths resolve from the helper directory.
- `-MaxLogMegabytes`: rotate the log when it reaches this size. Default: `10`; `0` disables rotation.
- `-LogRetentionCount`: number of rotated logs to keep. Default: `5`; `0` deletes the current log when the size cap is reached.
- `-SleepSeconds`: delay between scan cycles. Default: `20`.
- `-IncludeInterfaceAlias`: optional wildcard allowlist for network interface aliases.
- `-ExcludeInterfaceAlias`: wildcard denylist for network interface aliases. Defaults exclude loopback and common virtual adapters.
- `-IncludeIPAddress`: explicit IP allowlist. When supplied, interface discovery is bypassed.
- `-IncludeUnpreferred`: include IPv4 addresses whose `AddressState` is not `Preferred`.
- `-WhatIf`: log and preview DNS add/remove operations without changing records.
- `-Confirm`: prompt before each DNS add/remove operation.
- `-Once`: run one complete scan/update cycle and exit. A successful cycle returns `0`; no eligible address or any read/write failure returns `3`.

## Safety Notes

- This script is destructive for managed owner names by design: A records under those exact names that are not in the current eligible IP set are removed.
- Use `-ManagedRecordName` for production jobs so the owner-name allowlist is obvious in the scheduled command.
- If no eligible IPv4 address remains, the cycle skips DNS changes instead of deleting records.
- Loopback, APIPA, `0.0.0.0`, unpreferred addresses, and excluded virtual adapters are ignored by default.
- The repository root also contains a `DNSAutoUpdate.cmd` convenience wrapper that forwards to this utility with the same parameters.
- Without `-Once`, the utility runs until the process is stopped.
- `dnscmd` output is accepted only from lines that contain an exact IPv4 `A` record; unrelated server/noise addresses are ignored.
- Timed-out `dnscmd` processes are terminated and their output drain is bounded.
- Generated logs are ignored by git. Long-running jobs should keep log rotation enabled or send `-LogFile` to a managed logging location.

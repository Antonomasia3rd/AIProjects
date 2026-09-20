# DNSAutoUpdate

Resident Windows utility that keeps an explicit set of Windows DNS Server A
record owner names synchronized with selected local IPv4 addresses. It uses the
shared AIProjects INI, logging, tray, and per-user `shell:startup` dependencies.

DNS changes are destructive by design, so a new profile is inert:

- `Enabled=false`
- `WhatIf=true`
- `NoRootRecord=true`
- no managed owner names

Launching a new copy with no arguments therefore creates its sidecar INI and
tray UI but does not read or change DNS. Enabling the updater requires an exact
owner selection. Apply mode additionally requires either an explicit IP list or
an interface-alias allowlist; use `*` only if every eligible interface is
intentionally authoritative.

## Requirements

- Windows Server or a Windows installation with `dnscmd.exe`.
- Permission to read and update the selected DNS zone.
- One authoritative deployment for each managed owner set across all hosts.

The optional Startup entry is an ordinary current-user shortcut in
`shell:startup`. It cannot and must not bypass UAC. For unattended startup, give
that user appropriately scoped DNS permissions; do not replace the shortcut
with a Run registry value or scheduled task.

## Build

```cmd
BuildDNSAutoUpdate.cmd
```

The output is `build\DNSAutoUpdate.exe`. The INI and default log use the
executable's current base name beside that executable. Append and configured
rotation are serialized through the shared managed logger.

`DNSAutoUpdate.cs` is intentionally only the product assembly-metadata overlay.
The product-owned resident implementation is compiled from
`dependencies\DNSAutoUpdate\dns_auto_update_app.cs`, where it composes the
root-level managed INI, named-object, logging, Startup, and tray dependencies.

## Safe first configuration

Save a preview profile without contacting DNS:

```cmd
DNSAutoUpdate.exe --managed-record-name "@,app,files" --include-interface-alias "Ethernet*" --enabled --what-if --configure-only
```

Launch it with no arguments to inspect preview cycles from the tray and log.
After verifying the selected addresses and owner names, enable changes through
the tray or persist apply mode:

```cmd
DNSAutoUpdate.exe --apply --configure-only
```

`--configure-only` never enumerates interfaces, starts `dnscmd.exe`, creates a
tray icon, or enters the resident loop. A valid setting command without
`--configure-only` saves the complete batch and starts the resident process; if
that profile is already running, it is asked to reload.

## Configuration surfaces

Every persistent setting is available in `[Settings]`, through a direct CLI
option or `--set Settings.Key=Value`, and in the tray menu:

- `Enabled`: master DNS-operation switch.
- `ZoneName`: DNS zone to maintain.
- `ManagedRecordName`: comma-separated exact owner-name allowlist. `@` means
  the zone root. When nonempty, it supersedes `SubFolder`/`NoRootRecord`.
- `SubFolder`: legacy comma-separated owner names used only when
  `ManagedRecordName` is empty.
- `NoRootRecord`: excludes `@` from legacy `SubFolder` selection.
- `IncludeIPAddress`: explicit comma-separated usable dotted-decimal IPv4
  allowlist. When nonempty, interface discovery is bypassed.
- `IncludeInterfaceAlias`: wildcard interface-name allowlist.
- `ExcludeInterfaceAlias`: wildcard denylist applied after the allowlist.
- `IncludeUnpreferred`: includes addresses not in the Preferred DAD state.
- `SleepSeconds`: resident polling interval, 1 through 86400.
- `WhatIf`: previews planned additions/removals without applying them.
- `Confirm`: requires approval for each change. Resident mode uses a visible
  Windows confirmation dialog; one-shot console mode reads the console.
- `LogFile`, `MaxLogMegabytes`, `LogRetentionCount`: sidecar logging and
  rotation policy. Relative log paths resolve beside the executable.
- `RunAtStartup`: exact empty-argument, INI-scoped current-user Startup link.
  The shared direction-safe commit self-reconciles an interrupted enable or
  disable on the next launch.
- `ShowTrayIcon`, `ShowMenuAsDropdown`: tray visibility/layout.

Boolean aliases include `--enabled`/`--disabled`, `--what-if`/`--apply`,
`--confirm`/`--no-confirm`, `--startup`/`--no-startup`,
`--tray`/`--no-tray`, and `--dropdown`/`--flat-menu`. Existing single-dash
PascalCase option names remain accepted and now persist through the same INI.
Use `--help` for the complete direct-option list.

Actions are invocation-only:

- `--once`: run one cycle in the invoking process and exit (`0` complete,
  `3` incomplete/failed). It still uses the persisted profile and any setting
  batch supplied on that invocation.
- `--run-now`: wake an existing resident instance for an immediate cycle.
- `--reload`: ask an existing resident instance to reload its INI.
- `--exit`: ask an existing resident instance to stop cooperatively.
- `--help`, `--version`: side-effect-free information commands.

## DNS safety behavior

- Missing desired records are added and re-read before stale records are
  removed. If no desired replacement is verified, deletion fails closed.
- Automatically discovered addresses must be identical for two consecutive
  cycles before any stale-record deletion. Explicit `IncludeIPAddress` values
  are already an operator-declared set and do not need that observation delay.
- No eligible address means no DNS mutation.
- Every mutation requires a writable serialized audit log. A log failure blocks
  the change.
- One resident local profile owns a zone for its lifetime. A second local
  profile cannot alternate conflicting changes. This cannot coordinate separate
  machines, so deployments must not claim the same owner set from multiple
  hosts.
- DNS reads recognize only explicit missing-record/name error codes; generic or
  localized error text fails closed.
- IPv4 input is strict four-octet dotted decimal. Short, hexadecimal, loopback,
  APIPA, unspecified, multicast, and reserved values are rejected as desired
  addresses.
- `dnscmd` execution, output draining, polling delays, reload, and exit waits
  are bounded or cancellable. A timed-out mutation is treated as uncertain and
  followed by authoritative re-reads before later decisions.

The tray shows the current profile/status in its hover text and menu, exposes
Run now, Pause/Resume, every persistent setting, configuration/log opening,
reload, and cooperative exit.

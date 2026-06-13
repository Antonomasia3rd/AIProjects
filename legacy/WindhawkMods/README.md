# WindhawkMods

Source-only local Windhawk mods copied from `C:\ProgramData\Windhawk\ModsSource`.

| File | Mod ID | Name | Purpose |
| --- | --- | --- | --- |
| `local@always-uiaccess.wh.cpp` | `always-uiaccess` | Always UIAccess | Relaunches included processes with `TokenUIAccess` set by the Windhawk service. |
| `local@appsfolder-unhide-hidden-apps.wh.cpp` | `appsfolder-unhide-hidden-apps` | AppsFolder Unhide Hidden Apps | Adds selected hidden AppUserModelIDs to `shell:AppsFolder` enumeration. |
| `local@snipping-tool-border-fix.wh.cpp` | `snipping-tool-border-fix` | Snipping Tool Border Fix | Hooks Snipping Tool DWM frame-bound queries to avoid the border/crop issue. |

## Usage

Import the `.wh.cpp` source in Windhawk or copy the files back into Windhawk's
local mod source folder:

```text
C:\ProgramData\Windhawk\ModsSource
```

The filenames preserve Windhawk's local source naming convention,
`local@<mod-id>.wh.cpp`.

## Build

These files are compiled and loaded by Windhawk. They are not part of
`.github\scripts\build-windows.cmd` and are intentionally absent from
`.github\project-map.json`.

When Windhawk is installed, run the local x64 and x86 syntax checks with:

```cmd
TestWindhawkMods.cmd
```

Set `WINDHAWK_COMPILER` to the full path of Windhawk's `clang++.exe` when it is not installed under a standard location.

## Safety

These mods hook Windows or application process behavior. Enable them one at a
time and verify the target process list in Windhawk before broad use.

`always-uiaccess` expects Windhawk to be running as a service so the service
process can use `SeTcbPrivilege` to set `TokenUIAccess`. Requests are bound to
the requesting process and actual child image, and unsupported CreateProcess
semantics fall back to suspended in-process creation before token patching.
Its service-broker path still cannot perfectly reproduce every caller-owned
console, affinity, or job relationship, so test each allowlisted application.

`appsfolder-unhide-hidden-apps` hooks shell AppsFolder enumeration in Explorer
and shell hosts. `snipping-tool-border-fix` limits itself to `SnippingTool.exe`
and uses extended frame bounds when available.

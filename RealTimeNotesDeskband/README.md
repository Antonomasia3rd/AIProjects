# RealTimeNotesDeskband

Classic Windows taskbar Deskband for HoYoLAB Real-Time Notes. It shows a compact ExplorerPatcher-weather-style widget on the taskbar:

- icon/fallback marker
- `current/max` amount
- time until full

Supported resources:

- Genshin Impact Resin
- Honkai: Star Rail Trailblaze Power
- Zenless Zone Zero Battery Charge

## Layout

```text
RealTimeNotesDeskband\
  build\                         generated DLLs, ignored
  references\                    local upstream/reference source, ignored
  BuildDeskband.cmd
  ConfigureDeskband.cmd
  RegisterDeskband.cmd
  UnregisterDeskband.cmd
  RealTimeNotesDeskband.cpp
```

The `references\` folder may contain local copies of ExplorerPatcher and the original Real-Time Notes tray app, but those copies are not part of this publishable project.

## Requirements

- Windows with a classic taskbar toolbar host, such as Windows 10 taskbar mode in ExplorerPatcher.
- MinGW-w64 `g++` on `PATH`. Strawberry Perl's bundled MinGW works.

The stock Windows 11 taskbar does not expose classic taskbar toolbars.

## Build

```cmd
BuildDeskband.cmd
```

Syntax check only:

```cmd
BuildDeskband.cmd check
```

Side-by-side build for a locked installed DLL:

```cmd
BuildDeskband.cmd new
```

Outputs are written to `build\`.

## Install

```cmd
RegisterDeskband.cmd
```

To register a side-by-side DLL built with `BuildDeskband.cmd new`:

```cmd
RegisterDeskband.cmd build\RealTimeNotesDeskband.12345.dll
```

Then right-click the taskbar and enable:

```text
Toolbars > Real Time Notes
```

If it does not appear immediately, restart File Explorer from ExplorerPatcher Properties.

## Configure Credentials

No PowerShell is required. Configure credentials from the deskband context menu:

- right-click the deskband and choose `Configure selected account...`
- choose the game resource
- enter `UID`, `ltoken_v2`, and `ltuid_v2`
- optionally set a per-account refresh interval in seconds
- choose `Import cookie JSON for selected...` if you already have a compatible cookie JSON file

You can also open the same native configuration dialog without Explorer loaded:

```cmd
ConfigureDeskband.cmd
```

The imported JSON fields are `uid`, `ltoken_v2`, `ltuid_v2`, and optional `refresh_interval`.

## Context Menu

The deskband menu includes:

- current game detail rows matching the original tray menu where the API exposes them
- `Refresh now`
- selected account configuration/import/clear actions
- `Open HoYoLAB login page`
- `Resource` selection, including automatic selection
- per-resource account configuration and import commands
- config/asset directory open and change commands under `Advanced`

The deskband asks Explorer to resize when status text changes, so the toolbar width follows the current content instead of staying at a fixed size.

## Uninstall

```cmd
UnregisterDeskband.cmd
```

If the registered DLL was a side-by-side build, the unregister script detects the registered path automatically. You can also pass it explicitly:

```cmd
UnregisterDeskband.cmd build\RealTimeNotesDeskband.12345.dll
```

## Settings

Settings are stored under:

```text
HKCU\Software\RealTimeNotesDeskband
```

Values:

- `Resource`: `auto`, `resin`, `stamina`, or `charge`.
- `Accounts\<resource>\UID`: game account UID.
- `Accounts\<resource>\LTokenV2Protected`: DPAPI-protected HoYoLAB `ltoken_v2`.
- `Accounts\<resource>\LTuidV2Protected`: DPAPI-protected HoYoLAB `ltuid_v2`.
- Legacy plaintext `LTokenV2` / `LTuidV2` values are still read when protected values are not present, and are kept by default so older releases can still read credentials after rollback.
- `KeepLegacyPlaintextSecrets`: optional DWORD. Set to `0` before saving credentials if you want new saves to remove legacy plaintext token values.
- `Accounts\<resource>\RefreshIntervalSeconds`: optional DWORD refresh override for one resource.
- `ConfigDir`: legacy fallback directory containing cookie JSON files.
- `AssetDir`: optional directory containing icon resources.
- `RefreshIntervalSeconds`: optional DWORD override. Values below 30 seconds are clamped.
- `InstallDir`: directory containing the registered DLL.

Credential registry values are user-local and are not written to the repository. If no icon resource is found, the Deskband draws a small built-in fallback marker. Local references can supply icons from `references\genshin-real-time-notes-0.0.8\embedded\assets`.

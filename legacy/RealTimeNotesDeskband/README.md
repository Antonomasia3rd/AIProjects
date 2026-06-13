# RealTimeNotesDeskband

Classic Windows taskbar Deskband for HoYoLAB Real-Time Notes. It shows a compact ExplorerPatcher-weather-style widget on the taskbar with:

- icon or built-in fallback marker;
- current/max resource amount;
- time until full;
- context-menu details for API fields exposed by HoYoLAB.

Supported resources:

- Genshin Impact Resin
- Honkai: Star Rail Trailblaze Power
- Zenless Zone Zero Battery Charge

## Requirements

- Windows with a classic taskbar toolbar host, such as Windows 10 taskbar mode in ExplorerPatcher.
- MinGW-w64 `g++` on `PATH`. Strawberry Perl's bundled MinGW works.

The stock Windows 11 taskbar does not expose classic taskbar toolbars.

## Build

```cmd
BuildDeskband.cmd
```

Syntax and source-regression checks:

```cmd
BuildDeskband.cmd check
```

The source checks cover shared/atomic INI persistence, strict parsing and UTF conversion, modal dialog teardown, deskband site/window lifetime, refresh timer setup, and refresh-worker generation checks.

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

- right-click the deskband and choose `Configure selected account...`;
- choose the game resource;
- enter `UID`, `ltoken_v2`, and `ltuid_v2`;
- optionally set a per-account refresh interval in seconds;
- choose `Import cookie JSON for selected...` if you already have a compatible cookie JSON file.

You can also open the same native configuration dialog without Explorer loaded:

```cmd
ConfigureDeskband.cmd
```

The imported JSON fields are `uid`, `ltoken_v2`, `ltuid_v2`, and optional `refresh_interval`.

## Context Menu

The deskband menu includes:

- current game detail rows;
- `Refresh now`;
- selected-account configure/import/clear actions;
- `Open HoYoLAB login page`;
- `Resource` selection, including automatic selection;
- per-resource account configuration and import commands;
- config/asset directory open and change commands under `Advanced`;
- `About`.

The deskband asks Explorer to resize when status text changes, so the toolbar width follows current content instead of staying fixed.

## Runtime Behavior

- Refreshes use WinHTTP with connect/send/receive timeouts.
- HTTP responses are capped at 1 MiB before parsing so Explorer does not retain an unexpectedly large API body.
- Settings are copied under a lock before refresh workers use them, so changing config/asset directories from the menu cannot race with a background refresh.
- Account save and removal operations are serialized and atomically replace the INI, so failed writes do not leave partially updated credentials.
- Refresh workers are tied to the exact deskband window generation that started them, so Explorer teardown or recreation cannot receive a stale worker completion.
- If no icon resource is found, the deskband draws a built-in fallback marker.
- HoYoLAB request signing, headers, and response fields intentionally mirror the original Real-Time Notes upstream behavior. Local upstream/reference copies may be kept in an ignored `references\` folder for research, but those copies are not part of the publishable source package. Treat compatibility changes here as HoYoLAB compatibility updates, not generic API refactors.

## Settings

Settings are stored beside the registered DLL using the module base name:

```text
<dll folder>\<dll name>.ini
<dll folder>\<dll name>.log
```

For the default DLL name, those files are `RealTimeNotesDeskband.ini` and `RealTimeNotesDeskband.log`. COM/deskband registration still uses `HKCU\Software\Classes` because Explorer requires that registration state.

INI values:

- `Resource`: `auto`, `resin`, `stamina`, or `charge`.
- `[Account.<resource>] UID`: game account UID.
- `[Account.<resource>] LTokenV2Protected`: DPAPI-protected HoYoLAB `ltoken_v2`.
- `[Account.<resource>] LTuidV2Protected`: DPAPI-protected HoYoLAB `ltuid_v2`.
- `[Account.<resource>] RefreshIntervalSeconds`: optional per-resource refresh override.
- `ConfigDir`: legacy fallback directory containing cookie JSON files.
- `AssetDir`: optional directory containing icon resources.
- `InstallDir`: directory containing the registered DLL.
- `RefreshIntervalSeconds`: optional global refresh override. Values below 30 seconds are clamped.
- `LoggingEnabled`: optional `0`/`1` log toggle.
- `KeepLegacyPlaintextSecrets`: optional `0`/`1`. Set to `1` before saving credentials only if older releases must keep reading plaintext token values after rollback.

Legacy registry settings under `HKCU\Software\RealTimeNotesDeskband` are migrated to the module-local INI when the deskband loads. Legacy plaintext `LTokenV2` and `LTuidV2` values are still read during migration when protected values are absent. New saves are DPAPI-only and remove legacy plaintext values unless `KeepLegacyPlaintextSecrets` is enabled.

## Source Layout

```text
RealTimeNotesDeskband\
  build\                         generated DLLs, ignored
  references\                    optional local upstream/reference source, ignored and not vendored
  BuildDeskband.cmd
  ConfigureDeskband.cmd
  RegisterDeskband.cmd
  UnregisterDeskband.cmd
  RealTimeNotesDeskband.cpp
```

The optional `references\` folder may contain local copies of ExplorerPatcher and the original Real-Time Notes tray app while developing. It is ignored intentionally so large or license-sensitive reference material is not accidentally shipped in this repository.

## Uninstall

```cmd
UnregisterDeskband.cmd
```

If the registered DLL was a side-by-side build, the unregister script detects the registered path automatically. You can also pass it explicitly:

```cmd
UnregisterDeskband.cmd build\RealTimeNotesDeskband.12345.dll
```

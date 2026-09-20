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

## Shared data engine

The data engine now lives in `dependencies/content_sources/notes.inc` and is
shared with DesktopStub's Notes source. The deskband keeps its account-loading,
taskbar UI, and registration behavior, then passes explicit account values into
that engine. Requests use the shared HTTP total deadline and cancel when the
deskband window is closed or replaced. Data parsing no longer accepts unrelated
nested fields or displays a partial resource with unknown recovery as Full.

`tools\TestNotesSource.cmd` runs synthetic response and fake-transport tests
without reading accounts, decrypting credentials, or accessing the network.
The existing service endpoints, region mappings, and authentication literals
were preserved; current live API compatibility has not been verified. See
[the Notes provider audit](../../docs/audit-notes-source.md) for the API,
validation results, and retained compatibility limits. The standalone deskband
remains available while its full functionality is consolidated.

## Requirements

- Windows with a classic taskbar toolbar host, such as Windows 10 taskbar mode in ExplorerPatcher.
- Visual Studio 2019/2022 C++ Build Tools, or MinGW-w64 `g++` on `PATH`. The build script prefers MSVC, matching DesktopStub, and falls back to MinGW when MSVC is unavailable.

The stock Windows 11 taskbar does not expose classic taskbar toolbars.

## Build

```cmd
BuildDeskband.cmd
```

Syntax and source-regression checks:

```cmd
BuildDeskband.cmd check
```

The source checks run with MSVC or MinGW and cover shared/atomic INI persistence, rollback-safe COM registration, strict parsing and UTF conversion, the real hover tooltip, modal dialog teardown, deskband site/window lifetime, refresh timer setup, and refresh-worker generation checks.

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

Registration snapshots every registry value and INI value it may replace. If a later step fails, it rolls back to the prior state; the COM activation path is written last so an interrupted registration cannot activate a half-configured server.

## Configure Credentials and Settings

No PowerShell is required. Configure credentials from the deskband context menu:

- right-click the deskband and choose `Configure selected account...`;
- choose the game resource;
- enter `UID`, `ltoken_v2`, and `ltuid_v2`;
- optionally set a per-account refresh interval in seconds;
- choose `Import cookie JSON for selected...` if you already have a compatible cookie JSON file.

You can open the general settings dialog without Explorer loaded:

```cmd
ConfigureDeskband.cmd
```

Open an account dialog directly, or update all general settings from the command line:

```cmd
ConfigureDeskband.cmd --account resin
ConfigureDeskband.cmd --set Resource=auto --set RefreshIntervalSeconds=300 --set LoggingEnabled=true
ConfigureDeskband.cmd --set KeepLegacyPlaintextSecrets=false --set InstallDir="C:\Tools\RealTimeNotes"
```

`--set Name=Value` accepts `Resource`, `RefreshIntervalSeconds`, `LoggingEnabled`, `KeepLegacyPlaintextSecrets`, `InstallDir`, `ConfigDir`, and `AssetDir`. The complete batch is validated before one atomic INI update. Syntax/validation errors return exit code 2, persistence errors return 1, and success/help return 0. `ConfigureDeskband.cmd --help` prints the same contract and supports redirected output.

The imported JSON fields are `uid`, `ltoken_v2`, `ltuid_v2`, and optional `refresh_interval`.

## Context Menu

The deskband menu includes:

- current game detail rows;
- a state-bearing hover tooltip matching the current resource/status;
- `Refresh now`;
- selected-account configure/import/clear actions;
- `Open HoYoLAB login page`;
- `Resource` selection, including automatic selection;
- per-resource account configuration and import commands;
- a general `Settings...` editor for resource, refresh, logging, rollback compatibility, install, config, and asset values;
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
- `[Account.<resource>] LTokenV2Protected`: DPAPI-protected HoYoLAB `ltoken_v2`. New saves use the shared versioned UTF-8 serialization; legacy unversioned UTF-16LE values remain readable.
- `[Account.<resource>] LTuidV2Protected`: DPAPI-protected HoYoLAB `ltuid_v2`, with the same versioned-write/legacy-read behavior.
- `[Account.<resource>] RefreshIntervalSeconds`: optional per-resource refresh override.
- `ConfigDir`: legacy fallback directory containing cookie JSON files.
- `AssetDir`: optional directory containing icon resources.
- `InstallDir`: directory containing the registered DLL.
- `RefreshIntervalSeconds`: optional global refresh override. Use `default`/blank or 30 through 86400 seconds; typed UI/CLI input outside that range is rejected.
- `LoggingEnabled`: optional `0`/`1` log toggle.
- `KeepLegacyPlaintextSecrets`: optional `0`/`1`. Set to `1` before saving credentials only if older releases must keep reading plaintext token values after rollback.

Legacy registry settings under `HKCU\Software\RealTimeNotesDeskband` are migrated to the module-local INI when the deskband loads. Legacy plaintext `LTokenV2` and `LTuidV2` values are read only as migration input when protected values are absent. After the protected INI account is re-read successfully, the corresponding legacy registry account key is removed; a failed cleanup is logged and retried on the next load. The current code saves DPAPI values and removes plaintext INI values unless `KeepLegacyPlaintextSecrets` is enabled for rollback compatibility.

That mandatory encryption behavior is a remaining implementation gap: the
repository's current rule prioritizes convenient setup and makes additional
security opt-in after functional repairs. `KeepLegacyPlaintextSecrets` keeps a
second plaintext copy; it does not disable DPAPI or make an account portable to
another Windows user. Anyone who reads a plaintext token can use the associated
session, so keep INI files and cookie exports private and out of Git. Current
DPAPI storage binds secrets to the Windows user; it does not protect them from
other code already running as that user. See [the shared audit](../../docs/audit-shared.md).

## Source Layout

```text
RealTimeNotesDeskband\
  build\                         generated DLLs, ignored
  references\                    optional local upstream/reference source, ignored and not vendored
  BuildDeskband.cmd
  ConfigureDeskband.cmd
  RegisterDeskband.cmd
  TestRealTimeNotesDeskbandSource.cmd
  UnregisterDeskband.cmd
  RealTimeNotesDeskband.cpp         thin dependency-overlay entry point
dependencies\RealTimeNotesDeskband\
  deskband_app.inc                 product-owned COM/deskband implementation
```

The project-local `.cpp` intentionally contains only the ordered include for
the product-owned dependency fragment. That fragment composes the root-level
desktop baseline and DPAPI modules; other products must not include the
deskband fragment directly.

The optional `references\` folder may contain local copies of ExplorerPatcher and the original Real-Time Notes tray app while developing. It is ignored intentionally so large or license-sensitive reference material is not accidentally shipped in this repository.

## Uninstall

```cmd
UnregisterDeskband.cmd
```

If the registered DLL was a side-by-side build, the unregister script detects the registered path automatically. You can also pass it explicitly:

```cmd
UnregisterDeskband.cmd build\RealTimeNotesDeskband.12345.dll
```

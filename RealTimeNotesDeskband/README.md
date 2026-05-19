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
  RealTimeNotesDeskband.cpp
  configure-deskband.ps1
  register-deskband.ps1
  unregister-deskband.ps1
```

The `references\` folder may contain local copies of ExplorerPatcher and the original Real-Time Notes tray app, but those copies are not part of this publishable project.

## Requirements

- Windows with a classic taskbar toolbar host, such as Windows 10 taskbar mode in ExplorerPatcher.
- MinGW-w64 `g++` on `PATH`. Strawberry Perl's bundled MinGW works.

The stock Windows 11 taskbar does not expose classic taskbar toolbars.

## Configure Credentials

The Deskband reads HoYoLAB credentials from its own per-user registry keys. It does not need the old tray program to be running.

Manual setup:

```powershell
.\configure-deskband.ps1 -Resource resin -UID <uid> -LTokenV2 <ltoken_v2> -LTuidV2 <ltuid_v2>
```

Import from one existing cookie JSON:

```powershell
.\configure-deskband.ps1 -Resource resin -ImportFromJson "D:\path\to\genshin_cookie.json"
```

Import every supported cookie JSON from a directory:

```powershell
.\configure-deskband.ps1 -Resource all -ImportFromDir "D:\path\to\Real-Time Notes" -NoSelect
```

The imported fields are `uid`, `ltoken_v2`, `ltuid_v2`, and optional `refresh_interval`.

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

```powershell
.\register-deskband.ps1
```

To register a side-by-side DLL built with `BuildDeskband.cmd new`:

```powershell
.\register-deskband.ps1 -DllPath .\build\RealTimeNotesDeskband.12345.dll
```

Then right-click the taskbar and enable:

```text
Toolbars > Real Time Notes
```

If it does not appear immediately, restart File Explorer from ExplorerPatcher Properties.

## Uninstall

```powershell
.\unregister-deskband.ps1
```

## Settings

Settings are stored under:

```text
HKCU\Software\RealTimeNotesDeskband
```

Values:

- `Resource`: `auto`, `resin`, `stamina`, or `charge`.
- `Accounts\<resource>\UID`: game account UID.
- `Accounts\<resource>\LTokenV2`: HoYoLAB `ltoken_v2`.
- `Accounts\<resource>\LTuidV2`: HoYoLAB `ltuid_v2`.
- `Accounts\<resource>\RefreshIntervalSeconds`: optional DWORD refresh override for one resource.
- `ConfigDir`: legacy fallback directory containing cookie JSON files.
- `AssetDir`: optional directory containing icon resources.
- `RefreshIntervalSeconds`: optional DWORD override. Values below 30 seconds are clamped.
- `InstallDir`: directory containing the registered DLL.

Credential registry values are user-local and are not written to the repository. If no icon resource is found, the Deskband draws a small built-in fallback marker. Local references can supply icons from `references\genshin-real-time-notes-0.0.8\embedded\assets`.

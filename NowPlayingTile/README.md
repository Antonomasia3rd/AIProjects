# NowPlayingTile

Small background app that reads the current System Media Transport Controls (SMTC) session and updates a Windows Start Live Tile.

It is intended for Windows 10 Start, including ExplorerPatcher's Windows 10 Start menu on Windows 11. Direct `NowPlayingTile.exe` launches are supported for diagnostics/widget mode, but tile updates require package identity through the registered development package.

## Requirements

- Windows 10, or Windows 11 with a Start implementation that still displays live tiles.
- .NET Framework compiler at `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe`.
- PowerShell for build/register helper scripts.
- Developer Mode or sideloading support for loose Appx registration.

## Build

```powershell
pwsh .\build.ps1
```

Output:

```text
build\NowPlayingTile.exe
```

The build also prepares generated manifest/assets under `build\`.

## Register And Run

Register the dev package once:

```powershell
pwsh .\register-dev-package.ps1
```

Launch the packaged background updater:

```powershell
pwsh .\launch-packaged.ps1
```

Pin the packaged app entry to Start:

```text
shell:AppsFolder\NowPlayingTile.App_<package-family-suffix>!App
```

If Windows changes the package family suffix after re-registration, the register script prints the current `shell:AppsFolder\...!App` target.

Start automatically at sign-in:

```powershell
pwsh .\install-startup.ps1
```

Disable sign-in startup:

```powershell
pwsh .\uninstall-startup.ps1
```

Unregister the development package:

```powershell
pwsh .\unregister-dev-package.ps1
```

## Optional Widget

The old visible Win32 widget is still available, but opt-in:

```powershell
pwsh .\launch-widget.ps1
```

Or directly:

```powershell
.\build\NowPlayingTile.exe --widget
```

Right-click the widget for refresh, always-on-top, and exit. Drag the window by holding the left mouse button anywhere on the widget.

## Settings

Open settings:

```powershell
pwsh .\open-settings.ps1
```

Settings are stored at:

```text
<exe folder>\<exe name>.ini
```

Diagnostics are written to:

```text
<exe folder>\<exe name>.log
```

For the default build output, those files are `build\NowPlayingTile.ini` and `build\NowPlayingTile.log`. If the executable is renamed, the default INI/log names follow the renamed executable.

Available settings:

```ini
# TileLayout: Cycle, Text, Artwork, Combined
TileLayout=Cycle
UpdateIntervalSeconds=2
TileRefreshSeconds=60
ShowTrayIcon=false
```

`Cycle` is the default. It sends two live tile notifications: a text-only view and an artwork view. This avoids the artwork-only tile problem while still showing album/site artwork when Windows rotates the live tile queue.

## Notes

SMTC access uses:

```text
Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager
```

Windows 11 blocks these UWP/WinRT APIs in non-interactive service-like sessions. If the app displays `The specified service does not exist as an installed service`, launch it normally from Explorer, Start, or an interactive terminal.

Tile updates use:

```text
Windows.UI.Notifications.TileUpdateManager
```

Running `NowPlayingTile.exe` directly does not provide package identity, so Start Live Tile updates are ignored. Launch through the registered `shell:AppsFolder\...!App` entry or `launch-packaged.ps1` for tile updates.

## Generated Files

The `build` folder contains generated files:

- `NowPlayingTile.exe`
- `NowPlayingTile.ini`
- `NowPlayingTile.log`
- copied/generated Appx manifest output
- generated tile assets

The source manifest template is tracked at `package\AppxManifest.xml`.

## Release

Prebuilt binary: [NowPlayingTile v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/NowPlayingTile-v1).

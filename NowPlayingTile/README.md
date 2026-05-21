# NowPlayingTile

Small background app that reads the current System Media Transport Controls
(SMTC) session and updates a Windows Start Live Tile.

It also updates the app's Windows Start Live Tile when launched through the
registered development package. This is intended for Windows 10 Start, including
ExplorerPatcher's Windows 10 Start menu on Windows 11.

## Build

```powershell
pwsh .\build.ps1
```

The build uses the .NET Framework C# compiler already installed at:

```text
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe
```

Output is written to:

```text
build\NowPlayingTile.exe
```

## Run

For Start Live Tile updates, register the dev package once:

```powershell
pwsh .\register-dev-package.ps1
```

Then launch the background updater:

```powershell
pwsh .\launch-packaged.ps1
```

Pin the packaged app entry to Start:

```text
shell:AppsFolder\NowPlayingTile.App_<package-family-suffix>!App
```

By default, launching the app opens no window. It runs in the background and
updates the tile.

To start it automatically when you sign in:

```powershell
pwsh .\install-startup.ps1
```

If Windows changes the package family suffix after re-registration, the register
script prints the current `shell:AppsFolder\...!App` target.

## Optional Widget

The old visible Win32 widget is still available, but it is opt-in:

```powershell
pwsh .\launch-widget.ps1
```

Or directly:

```powershell
.\build\NowPlayingTile.exe --widget
```

Right-click the widget for refresh, always-on-top, and exit. Drag the window by
holding the left mouse button anywhere on the widget.

## Settings

Edit settings with:

```powershell
pwsh .\open-settings.ps1
```

Settings are stored at:

```text
%LOCALAPPDATA%\NowPlayingTile\settings.ini
```

Diagnostics are written to:

```text
%LOCALAPPDATA%\NowPlayingTile\NowPlayingTile.log
```

Available settings:

```ini
# TileLayout: Cycle, Text, Artwork, Combined
TileLayout=Cycle
UpdateIntervalSeconds=2
TileRefreshSeconds=60
ShowTrayIcon=false
```

`Cycle` is the default. It sends two live tile notifications: a text-only view
and an artwork view. This avoids the artwork-only tile problem while still
showing album/site artwork when Windows rotates the live tile queue.

To remove the registered development package:

```powershell
pwsh .\unregister-dev-package.ps1
```

To disable sign-in startup:

```powershell
pwsh .\uninstall-startup.ps1
```

## Notes

SMTC access uses:

```text
Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager
```

Windows 11 blocks these UWP/WinRT APIs in non-interactive service-like sessions.
If the app displays `The specified service does not exist as an installed
service`, launch it normally from Explorer, Start, or an interactive terminal.

Tile updates use:

```text
Windows.UI.Notifications.TileUpdateManager
```

Running `NowPlayingTile.exe` directly is still supported, but direct launches do
not have package identity, so Start Live Tile updates are ignored. Launch via the
registered `shell:AppsFolder\...!App` entry, or use `launch-packaged.ps1`, for
tile updates.

## Generated Files

The `build` folder contains generated files: `NowPlayingTile.exe`, copied manifest output, and generated tile assets. The manifest template is tracked at `package\AppxManifest.xml`.

## Release

Prebuilt binary: [NowPlayingTile v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/NowPlayingTile-v1).

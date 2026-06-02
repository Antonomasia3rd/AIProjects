# NowPlayingTile

`NowPlayingTile.exe` is a native Win32/C++ background app that reads the current System Media Transport Controls (SMTC) session and updates a Windows Start Live Tile.

It is intended for Windows 10 Start, including ExplorerPatcher's Windows 10 Start menu on Windows 11. The project is structured like `DesktopStub`: one native entry point, ordered `src\*.inc` fragments, one Visual Studio build command, and generated runtime/package files under `build`.

## Requirements

- Windows 10, or Windows 11 with a Start implementation that still displays Live Tiles.
- Visual Studio Build Tools with the C++ workload.
- Windows 10/11 SDK with C++/WinRT headers and WinRT metadata.
- Developer Mode or sideloading support for loose Appx registration.

## Build

From the repository root:

```cmd
NowPlayingTile\BuildNowPlayingTile.cmd
```

Syntax-only check:

```cmd
NowPlayingTile\BuildNowPlayingTile.cmd check
```

Output:

```text
NowPlayingTile\build\NowPlayingTile.exe
```

If `build\NowPlayingTile.exe` is running, close it before rebuilding so the compiler can overwrite the output.

## Run

```cmd
NowPlayingTile\build\NowPlayingTile.exe
```

On first launch the app creates runtime files next to the executable:

- `NowPlayingTile.ini`
- `NowPlayingTile.log`
- `AppxManifest.xml`
- `Assets\*`

The manifest and assets are generated files. They are not tracked in the source tree, matching the way `DesktopStub` treats generated Appx files.

## Register the Start Tile

Live Tile updates need package identity. Normal launch works as the DesktopStub-style bootstrap path:

```cmd
NowPlayingTile\build\NowPlayingTile.exe
```

If the executable is started directly from `build`, it generates `AppxManifest.xml` and `Assets`, registers the loose Appx package, launches the packaged `shell:AppsFolder\...!App` identity, then exits the unpackaged bootstrap process.

Manual registration is still available for troubleshooting:

```cmd
NowPlayingTile\build\NowPlayingTile.exe --register
NowPlayingTile\build\NowPlayingTile.exe --launch-packaged
```

The registration action prints the current `shell:AppsFolder\...!App` target. Pin that packaged entry to Start if you want the tile visible.

To remove the registered package:

```cmd
NowPlayingTile\build\NowPlayingTile.exe --unregister
```

To rewrite the generated manifest and default logo assets:

```cmd
NowPlayingTile\build\NowPlayingTile.exe --regenerate-manifest
```

## Optional Widget

The visible Win32 widget is opt-in:

```cmd
NowPlayingTile\build\NowPlayingTile.exe --widget
```

Right-click the widget for refresh, always-on-top, and exit. Drag the window by holding the left mouse button anywhere on the widget.

## Command Line

```cmd
NowPlayingTile.exe --once
NowPlayingTile.exe --widget --allow-multiple
NowPlayingTile.exe --tray
NowPlayingTile.exe --no-tray
NowPlayingTile.exe --register
NowPlayingTile.exe --launch-packaged
NowPlayingTile.exe --unregister
NowPlayingTile.exe --exit
```

Supported options:

- `--help`, `-h`, `/?`: show command-line help.
- `--once`: read SMTC once, update the Live Tile, then exit.
- `--widget` / `--show`: open the optional visible diagnostic widget.
- `--tray` / `--no-tray`: override `ShowTrayIcon` for this invocation.
- `--allow-multiple`: skip the single-instance guard.
- `--register`: generate `AppxManifest.xml` and `Assets`, then register the loose package.
- `--unregister`: remove the registered loose package.
- `--launch-packaged` / `--launch`: launch the registered `shell:AppsFolder\...!App` entry.
- `--regenerate-manifest`: rewrite the generated manifest and default logo assets.
- `--exit` / `--quit`: ask the running background/widget instance to exit.

## Settings

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
# NowPlayingTile settings
# TileLayout: Cycle, Text, Artwork, Combined
[Settings]
TileLayout=Cycle
UpdateIntervalSeconds=2
TileRefreshSeconds=60
ShowTrayIcon=false
```

`Cycle` is the default. It sends two Live Tile notifications: a text-only view and an artwork view. This avoids the artwork-only tile problem while still showing album/site artwork when Windows rotates the Live Tile queue.

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

Running `NowPlayingTile.exe` directly from `build` starts as an unpackaged process, so it bootstraps by registering the loose package and relaunching through the packaged identity. Live Tile updates are only attempted after that packaged relaunch.

## Source Layout

`NowPlayingTile.cpp` is the single translation-unit entry point. Most implementation code is split into ordered fragments under `NowPlayingTile\src`:

- `npt_core.inc`: path helpers, logging, string helpers, XML escaping, and file URI helpers.
- `npt_config_defaults.inc`: generated INI defaults and settings parsing.
- `npt_command_line.inc`: command-line parsing and single-instance exit signaling.
- `npt_manifest.inc`: generated Appx manifest/default logo assets and loose-package register/launch helpers.
- `npt_media.inc`: SMTC media/session reading and artwork extraction.
- `npt_live_tile.inc`: Live Tile XML payload generation and notification updates.
- `npt_tray.inc`: hidden background window, timer loop, tray menu, and update scheduling.
- `npt_widget.inc`: optional visible diagnostic widget.
- `npt_app.inc`: `wWinMain`, process initialization, single-instance guard, and mode dispatch.

## Generated Files

Generated/runtime files live under `NowPlayingTile\build` and are ignored by git:

- `NowPlayingTile.exe`
- `NowPlayingTile.ini`
- `NowPlayingTile.log`
- `AppxManifest.xml`
- `Assets\*`
- `tile-artwork-*.jpg`
- compiler object files under `obj\`

## Release

Prebuilt binaries are published through the repository's Windows build workflow and tagged GitHub releases when available.

Normal launch behavior:

- If the executable is started directly from `build`, it first generates `AppxManifest.xml` and `Assets`, registers the loose package, launches the packaged Start-menu identity, then exits the unpackaged bootstrap process.
- If automatic registration fails, the unpackaged bootstrap process exits after logging the PowerShell/Appx deployment error instead of continuing to spam Live Tile update failures.
- Live Tile updates only work from the packaged identity. The direct/unpackaged process cannot update the tile because Windows gives it no package identity.
- `build\obj` contains MSVC intermediate `.obj` files. This matches `DesktopStub/BuildDesktopStub.cmd`, which creates `build\obj\DesktopStub.obj`; it is not source and can be deleted safely after a build.

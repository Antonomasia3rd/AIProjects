# DesktopStub / GenerateAssets

`GenerateAssets.exe` is a tray utility that generates Windows Start tile assets from the current desktop wallpaper and registers a loose Appx manifest for a desktop tile entry.

The app can monitor wallpaper changes, wallpaper fit mode changes, and DPI scale settings, then regenerate assets and re-register the manifest automatically. Most behavior is configurable through the tray menu and generated INI file.

## Requirements

- Windows 10/11 for the running utility and normal Desktop Bridge registration. The manifest generator can also emit Windows 8 or Windows 8.1-style AppX XML for Start Screen/Live Tile simulator compatibility.
- Visual Studio Build Tools with the C++ workload.
- Developer Mode or sideloading support may be required for Appx registration, depending on Windows policy.

## Build

Run the build script once:

```cmd
DesktopStub\BuildGenerateAssets.cmd
```

The script now ignores all command-line arguments for compatibility with older habits such as `win8`, `win81`, `broker`, `helpers`, `background`, `experiments`, `all`, or `check`. Every invocation builds the same stable target set:

```text
DesktopStub\build\GenerateAssets.exe
DesktopStub\build\GenerateAssetsLiveTileBroker.exe
```

This avoids the old split where a Windows 8/8.1 manifest could be selected at runtime while the broker helper was missing because the build was run without the right argument.

The experimental background-task DLL remains in the source tree for research, but it is intentionally not part of the normal one-command build.

If `build\GenerateAssets.exe` or `build\GenerateAssetsLiveTileBroker.exe` is running, close it before rebuilding so the compiler can overwrite the output.

## Developer Checks

`TestGenerateAssetsSource.ps1` is a maintainer regression guard. It does not build or launch `GenerateAssets.exe`; it scans the source for safety fixes that should not be accidentally removed.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File DesktopStub\TestGenerateAssetsSource.ps1
```

Use `-ListChecks` to print the guardrails without running assertions.

## Run

For first-time/default Windows 10 usage:

```cmd
DesktopStub\build\GenerateAssets.exe
```

You do not need to run `--manifest-win8` or `--manifest-win81` unless you are deliberately switching this build into the Windows 8/8.1 compatibility experiment. The generated manifest target defaults to Windows 10.

For Windows 8/8.1 Start Screen simulator testing, switch the generated manifest target after the normal build. The broker is already produced by `BuildGenerateAssets.cmd`; extra build arguments are ignored.

```cmd
DesktopStub\BuildGenerateAssets.cmd
DesktopStub\build\GenerateAssets.exe --manifest-win8
DesktopStub\build\GenerateAssets.exe
```

On first launch the app creates `GenerateAssets.ini` next to the executable. The tray menu exposes:

- general settings;
- notifications and logging;
- wallpaper fitting and detection methods;
- asset generation targets and DPI scales;
- manifest target selection, status, and one-shot regeneration;
- registration mode and fallback behavior;
- Live Tile update mode;
- advanced timing/error options;
- startup/cleanup actions.

Command-line settings are saved to `GenerateAssets.ini`, the same configuration file used by the tray menu. Action-only commands such as `--once`, `--generate`, `--no-monitor`, and `--exit` affect only that invocation.

Live wallpaper capture is configurable from the tray menu, from command-line flags, or from `GenerateAssets.ini`:

```ini
[Settings]
"LiveWallpaperCapture" = "1"
"LiveWallpaperCaptureDelayMs" = "2500"
"LiveWallpaperCaptureRefreshMs" = "10000"
"LiveWallpaperCaptureScreenFallback" = "0"
"LiveWallpaperCaptureStartupRefreshMs" = "1000"
"LiveWallpaperCaptureStartupRefreshDurationMs" = "0"
```

When enabled, `GenerateAssets.exe` scans the desktop WorkerW live-wallpaper host tree. If a live-wallpaper host window is actually present, it captures a still frame from that host and uses the temporary BMP snapshot as the wallpaper source for asset generation. This is intentionally different from older external AHK bridge scripts: the built-in implementation does **not** call `SystemParametersInfo` or `IDesktopWallpaper::SetWallpaper`, so it does not replace or restore the user's actual wallpaper.

`LiveWallpaperCaptureRefreshMs=10000` is the default, so an active live-wallpaper host is recaptured about every 10 seconds. Set it to `0`/`once` if you only want one capture per host lifetime. The startup recapture duration defaults to `0`/`off`; set `LiveWallpaperCaptureStartupRefreshDurationMs` to a positive value only if you want DesktopStub to collect warm-up snapshots briefly while a provider starts. Warm-up snapshots are not published one by one; the latest valid snapshot is published after the startup settle window ends.

The live-wallpaper capture path does not look for a specific process name. It scans only non-icon `WorkerW` desktop-host windows and accepts visible monitor-sized child windows inside those `WorkerW` trees as candidates. This covers Lively, Wallpaper Engine, N0va Desktop, and arbitrary app-wallpaper modes that embed their renderer under the desktop. It intentionally does not enumerate arbitrary top-level windows, because a normal foreground app can also be large and visible. Captured windows that are tiny relative to the primary monitor are rejected, so small UI/helper fragments do not get stretched into the tile. If a WorkerW-hosted child renderer such as Wallpaper Engine's `WPEDesktopDX11Window` or `WPEDesktopCEFWindow` returns a black frame from `PrintWindow`, the capture path keeps the same non-icon `WorkerW` parent as a later fallback while still staying WorkerW-only. If every candidate returns a black GPU/WebView frame, the capture path rejects the blank/black frame instead of generating a black tile. The old screen-DC fallback is disabled by default because it can capture the visible desktop, taskbar, icons, or the user's static wallpaper; enable `LiveWallpaperCaptureScreenFallback=1` only for debugging or as an explicit unsafe workaround.

```cmd
GenerateAssets.exe --once
GenerateAssets.exe --no-tray --console
GenerateAssets.exe --ini D:\Temp\GenerateAssets.ini --set Settings.PollIntervalMs=5000
GenerateAssets.exe --regenerate-manifest
GenerateAssets.exe --wallpaper D:\Pictures\wallpaper.jpg --scales 100,200
GenerateAssets.exe --exit
```

Supported options:

- `--help`, `-h`, `/?`: show command-line help.
- `--ini <path>`: use an alternate INI file; alternate INI instances have separate single-instance scope.
- `--set Section.Key=Value`: set and save an INI value used by the app. Manifest fields are intentionally not controlled by the INI; edit `AppxManifest.xml` directly or regenerate the built-in default manifest.
- `--exit` / `--quit`: ask the running instance to exit gracefully.
- `--once`: generate once and exit.
- `--regenerate-manifest`: rewrite `AppxManifest.xml` once from the built-in default manifest template.
- `--generate` / `--generate-now`: force startup generation and keep running.
- `--wallpaper <path>` or a bare wallpaper path: generate from that image.
- `--no-monitor`: skip wallpaper/fit/DPI monitoring.
- `--tray` / `--no-tray`, `--console` / `--no-console`, `--logging` / `--no-logging`, `--notifications` / `--no-notifications`.
- `--powershell` / `--com-registration`: set and save registration command mode.
- `--live-tile` / `--no-live-tile`: set and save Live Tile update mode.
- `--live-tile-auto`: set and save automatic Live Tile update mode.
- `--live-tile-mode Auto|Registration|LiveTile`: set and save Live Tile update mode.
- `--manifest-target Windows10|Windows81|Windows8`: set the generated AppX manifest dialect and regenerate `AppxManifest.xml`. Windows 10 remains the default.
- `--manifest-win10`, `--manifest-win81` / `--manifest-win8.1`, `--manifest-win8` / `--manifest-win8.0`: shortcuts for `--manifest-target`.
- `--win8-broker` / `--no-win8-broker`: set and save `Win8LiveTileBrokerApp`.
- `--win8-background-task` / `--no-win8-background-task`: set and save `Win8LiveTileBackgroundTask`.
- `--win8-oop-helper` / `--no-win8-oop-helper`: set and save `Win8LiveTileOopHelper`.
- `--detect <method>`: set and save `WallpaperDetectionMethod`.
- `--live-wallpaper-capture` / `--no-live-wallpaper-capture`: set and save live wallpaper snapshot capture.
- `--live-wallpaper-screen-fallback` / `--no-live-wallpaper-screen-fallback`: set and save unsafe screen-copy fallback. It is off by default.
- `--live-wallpaper-delay <ms>`: set and save `LiveWallpaperCaptureDelayMs` from 0 to 30000.
- `--live-wallpaper-refresh <ms|once>`: set and save `LiveWallpaperCaptureRefreshMs` from 0 to 3600000. `once` maps to 0.
- `--live-wallpaper-startup-refresh <ms|off>`: set and save `LiveWallpaperCaptureStartupRefreshMs` from 0 to 3600000. `off` maps to 0.
- `--live-wallpaper-startup-refresh-duration <ms|off>`: set and save `LiveWallpaperCaptureStartupRefreshDurationMs` from 0 to 3600000. `off` maps to 0.
- `--scales auto|all|100,125,150,200,400`: set and save generated DPI scales. `auto` ignores manual scale toggles while preserving their previous checkbox state for later.
- `--asset Name=0|1`: set and save one asset toggle, such as `MediumTile=1`.

## Important Features

- Generates Store logo, medium tile, square 44 logo, square 30 logo, small tile, wide tile, large tile, and splash screen assets.
- Supports selected DPI scales plus automatic current-DPI scale generation.
- Detects wallpaper through configurable methods, including slideshow-compatible methods.
- Can internally capture a still frame from WorkerW-hosted live wallpaper apps and use that snapshot as the generation source, without overwriting the user's real Windows wallpaper.
- Uses COM Appx registration by default with optional PowerShell-only mode and fallback behavior.
- Can automatically use Live Tile notification updates when launched with package identity, with manual registration/Live Tile overrides.
- Can dynamically create or regenerate `AppxManifest.xml` from built-in manifest defaults.
- Supports quoted INI values and inline comments.
- Keeps detailed logs and exposes registration output from the tray.
- Records forced-shutdown cleanup state and warns on the next startup.

## Source Layout

`GenerateAssets.cpp` is the main host translation-unit entry point. Most implementation code is split into ordered fragments under `DesktopStub\src`:

- `ga_core.inc`: low-level file, text, INI, and process-output helpers.
- `ga_config_defaults.inc`: runtime globals and generated INI/string defaults.
- `ga_command_line.inc`: command-line parsing and saved INI setting changes.
- `ga_ui_logging.inc`: ordered UI/logging aggregator for smaller fragments.
- `ga_ui_state.inc`: UI string state, logging/tray globals, and shared state labels.
- `ga_logging_core.inc`: logging, console, INI access wrappers, and runtime logging settings.
- `ga_manifest.inc`: built-in manifest defaults, XML helpers, and generated manifest output.
- `ga_ui_strings.inc`: localized string loading, defaults validation, and format-token checks.
- `ga_runtime_helpers.inc`: runtime option parsing, DPI scale helpers, cleanup policy, and rename dialog.
- `ga_wallpaper.inc`: wallpaper and fit/DPI detection.
- `ga_image.inc`: GDI+ image generation and PNG saving.
- `ga_registration.inc`: Appx registration and PowerShell fallback handling.
- `ga_generation.inc`: asset generation, polling, and shutdown coordination.
- `ga_live_tile.inc`: Live Tile notification update handling.
- `ga_tray.inc`: tray menu and tray notifications.
- `ga_app.inc`: window procedure and application startup/shutdown.
- `ga_livetile_broker_app.inc`: implementation of the optional packaged WinRT Live Tile broker. `LiveTileBroker.cpp` is only a tiny wrapper because the broker must build as a separate executable.
- `ga_livetile_background_task_dll.inc`: implementation of the disabled experimental background-task DLL. `LiveTileBackgroundTask.cpp` is only a tiny wrapper because the task must build as a separate DLL.

## Generated Files

Generated/runtime files live under `DesktopStub\build` and are ignored by git:

- `GenerateAssets.exe`
- `GenerateAssetsLiveTileBroker.exe`
- `GenerateAssetsAppxStub.exe` when legacy fallback mode is selected
- `GenerateAssetsLiveTileTask.dll` only if manually built for the disabled background-task experiment
- `GenerateAssets.ini`
- `GenerateAssets.log`
- `GenerateAssets.appxactivation.log`
- `GenerateAssets.livetile.pending.xml`
- `AppxManifest.xml`
- `Assets\*`
- compiler object files under `obj\`

## Manifest Target Compatibility

`AppxManifestTarget=Windows10` is the default. It keeps the existing Windows 10 Desktop Bridge-style manifest with `TargetDeviceFamily`, `Windows.FullTrustApplication`, and `runFullTrust`.

For Start Screen / Live Tile simulator experiments, the generator can instead emit legacy AppX manifest shapes:

- `Windows81`: uses the Windows 8.1-era base namespace plus `m2` 2013 extensions, `<Prerequisites>`, `m2:VisualElements`, `Square150x150Logo`, `Square30x30Logo`, `m2:DefaultTile`, 70/150/310 tile names, splash screen, and Live Tile XML with `TileSquare150x150Image`, `TileWide310x150Image`, and `TileSquare310x310Image`.
- `Windows8`: uses the Windows 8 base namespace, `<Prerequisites>`, unprefixed `VisualElements`, `Logo`, `SmallLogo`, `DefaultTile WideLogo`, and Live Tile XML with the older `TileSquareImage` / `TileWideImage` templates. There is no 310x310 large-tile notification binding for this target.

For Windows 8/8.1 targets, the default compatibility helper is now `GenerateAssetsLiveTileBroker.exe`, a tiny CoreApplication-based WinRT broker app. The normal `GenerateAssets.exe` remains the unpackaged tray/wallpaper monitor; the broker only exists so the registered package can update the Live Tile under package identity. The standard build script always builds this broker, regardless of arguments, before `--manifest-win8` or `--manifest-win81` are used. Set `[Settings] Win8LiveTileBrokerApp=0` and regenerate the manifest to fall back to the older `GenerateAssetsAppxStub.exe` behavior.

Experimental helper paths remain in the source for later testing, but they are disabled by default: `[Settings] Win8LiveTileBackgroundTask=0` and `[Settings] Win8LiveTileOopHelper=0`. The background-task path currently requires package identity for the caller; the OOP-server path did not register reliably with the loose Windows 8-style package.

## Live Tile Update

`ExperimentalLiveTileUpdate=Auto` is the default Live Tile update mode in `GenerateAssets.ini`.

In `Auto`, Windows 10 Desktop Bridge mode updates the tile directly when `GenerateAssets.exe` is running with package identity. In Windows 8/8.1 compatibility mode, the normal unpackaged host writes `GenerateAssets.livetile.pending.xml`, mirrors it into the package `LocalState` folder, then activates the packaged WinRT broker (`GenerateAssetsLiveTileBroker.exe`) so the broker can apply the Live Tile update under package identity.

The mode is user-configurable from the tray menu, command line, or INI:

- `Auto`: Windows 10 uses direct Live Tile updates only when package identity is present; Windows 8/8.1 compatibility targets use the packaged broker.
- `LiveTile` or `1`: always try the Live Tile path. On Windows 8/8.1 targets this means broker activation from the normal unpackaged host.
- `Registration` or `0`: refresh by re-registering `AppxManifest.xml` instead of using the Live Tile notification path.

For Windows 8/8.1 targets, generated manifests point at `GenerateAssetsLiveTileBroker.exe` by default, not the resident tray app. This avoids the earlier fake-RT activation/MoAppHang behavior. Existing `AppxManifest.xml` files are kept unless you explicitly use `--regenerate-manifest`, `--manifest-win8`, `--manifest-win81`, or the tray regeneration action.

When Live Tile update is active, static manifest logo assets are treated as disabled so stale registered assets are not refreshed with wallpaper images. If **Generate Desktop Icon for disabled entries** is enabled, those static assets become desktop-icon placeholders; otherwise they are deleted. The Live Tile notification itself uses separate generated files under `Assets\Live*.png`.

Changing the Live Tile update mode queues one asset regeneration and one Appx re-registration before normal Live Tile updates resume. This refreshes Windows' cached static assets after switching modes.

## Release

Prebuilt binaries are published through the repository's Windows build workflow and tagged GitHub releases when available.

## Notes for fix28

`BuildGenerateAssets.cmd` no longer has argument-selected build modes. It always builds the main host and the stable packaged Live Tile broker; any supplied arguments are accepted but ignored.

Live Tile menu additions from the previous fix remain:

- **Relaunch on switch**: when enabled, switching Live Tile to **Enabled** from an unpackaged Windows 10 instance queues a relaunch through the registered package entry; switching to **Disabled** from a packaged instance queues a relaunch as the normal unpackaged desktop app.
- **Clear Live Tile on shutdown**: enabled by default. Packaged instances clear the live tile during graceful shutdown. Win8/8.1 broker mode can request a package-side clear through the broker.

The wallpaper poller updates its internal baseline after a failed poll-triggered generation attempt. This prevents repeated regeneration of the same wallpaper when the tile/app registration stage fails after assets were generated successfully.

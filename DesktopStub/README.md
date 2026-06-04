# DesktopStub

`DesktopStub.exe` is a tray utility that generates Windows Start tile assets from the current desktop wallpaper and registers a loose Appx manifest for a desktop tile entry.

The app can monitor wallpaper changes, wallpaper fit mode changes, and DPI scale settings, then regenerate assets and re-register the manifest automatically. Most behavior is configurable through the tray menu and generated INI file.

## Requirements

- Windows 10/11 for the running utility and normal Desktop Bridge registration. The manifest generator can also emit Windows 8 or Windows 8.1-style AppX XML for Start Screen/Live Tile simulator compatibility.
- Visual Studio Build Tools with the C++ workload.
- Developer Mode or sideloading support may be required for Appx registration, depending on Windows policy.

## Build

Run the build script once:

```cmd
DesktopStub\BuildDesktopStub.cmd
```

The script now ignores all command-line arguments for compatibility with older habits such as `win8`, `win81`, `broker`, `helpers`, `background`, `experiments`, `all`, or `check`. Every invocation builds the same stable target set:

```text
DesktopStub\build\DesktopStub.exe
DesktopStub\build\DesktopStubLiveTileBroker.exe
```

This avoids the old split where a Windows 8/8.1 manifest could be selected at runtime while the broker helper was missing because the build was run without the right argument.

The build embeds one version into both EXEs, the generated AppX manifest, startup diagnostics, the tray menu, and `--version`. By default the script reads the `DesktopStub-vN` Git tag family and builds the next package version as `N.0.0.0`; for example, after `DesktopStub-v17`, an untagged local/CI build reports `DesktopStub-v18 (18.0.0.0)`. Set `DESKTOPSTUB_VERSION=18.0.0.0` or `DESKTOPSTUB_RELEASE_TAG=DesktopStub-v18` before running the script to override this for a custom build.

For template reuse, the build names are parameterized without changing the default outputs. Set `DESKTOPSTUB_PRODUCT_NAME`, `DESKTOPSTUB_HOST_EXE_NAME`, `DESKTOPSTUB_BROKER_EXE_NAME`, or `DESKTOPSTUB_RELEASE_TAG_PREFIX` before running the script to reuse the baseline for another project while keeping the same source layout. Ordinary spaces are supported in the product/output names; the default release tag prefix removes spaces from `DESKTOPSTUB_PRODUCT_NAME`, and an explicitly supplied `DESKTOPSTUB_RELEASE_TAG_PREFIX` must not contain whitespace. Command-shell metacharacters, `%`, `!`, and quotes are rejected so the generated compiler/resource commands stay deterministic. Host and broker executable names must be plain file names, not paths.

The experimental background-task DLL remains in the source tree for research, but it is intentionally not part of the normal one-command build.

If `build\DesktopStub.exe` or `build\DesktopStubLiveTileBroker.exe` is running, close it before rebuilding so the compiler can overwrite the output.

## Developer Checks

`TestDesktopStubSource.cmd` builds and runs a small C++ maintainer regression guard. It does not build or launch `DesktopStub.exe`; it scans the source for safety fixes that should not be accidentally removed.

```cmd
DesktopStub\TestDesktopStubSource.cmd
```

Use `--list` to print the guardrails without running assertions. The helper binary is emitted under `DesktopStub\build` and is not part of the DesktopStub runtime.

## Run

For first-time/default Windows 10 usage:

```cmd
DesktopStub\build\DesktopStub.exe
```

You do not need to run `--manifest-win8` or `--manifest-win81` unless you are deliberately switching this build into the Windows 8/8.1 compatibility experiment. The generated manifest target defaults to Windows 10.

For Windows 8/8.1 Start Screen simulator testing, switch the generated manifest target after the normal build. The broker is already produced by `BuildDesktopStub.cmd`; extra build arguments are ignored.

```cmd
DesktopStub\BuildDesktopStub.cmd
DesktopStub\build\DesktopStub.exe --manifest-win8
DesktopStub\build\DesktopStub.exe
```

On first launch the app creates `DesktopStub.ini` next to the executable. The tray menu exposes:

- general settings;
- notifications and logging;
- wallpaper fitting and detection methods;
- asset generation targets and DPI scales;
- manifest target selection, status, and one-shot regeneration;
- registration mode and fallback behavior;
- Live Tile update mode;
- advanced timing/error options;
- startup/cleanup actions.

Command-line settings are saved to `DesktopStub.ini`, the same configuration file used by the tray menu. Action-only commands such as `--once`, `--generate`, `--no-monitor`, and `--exit` affect only that invocation.

Live wallpaper capture is configurable from the tray menu, from command-line flags, or from `DesktopStub.ini`:

```ini
[Settings]
"LiveWallpaperCapture" = "0"
"LiveWallpaperCaptureDelayMs" = "2500"
"LiveWallpaperCaptureRefreshMs" = "10000"
"LiveWallpaperCaptureScreenFallback" = "0"
"LiveWallpaperCaptureStartupRefreshMs" = "1000"
"LiveWallpaperCaptureStartupRefreshDurationMs" = "0"
```

When enabled, `DesktopStub.exe` scans the desktop WorkerW live-wallpaper host tree. If a live-wallpaper host window is actually present, it captures a still frame from that host and uses the temporary BMP snapshot as the wallpaper source for asset generation. This is intentionally different from older external AHK bridge scripts: the built-in implementation does **not** call `SystemParametersInfo` or `IDesktopWallpaper::SetWallpaper`, so it does not replace or restore the user's actual wallpaper.

Live wallpaper capture is off by default to keep the resident tray process lean. When `LiveWallpaperCapture=1`, `LiveWallpaperCaptureRefreshMs=10000` means an active live-wallpaper host is recaptured about every 10 seconds. Set it to `0`/`once` if you only want one capture per host lifetime. The startup recapture duration defaults to `0`/`off`; set `LiveWallpaperCaptureStartupRefreshDurationMs` to a positive value only if you want DesktopStub to collect warm-up snapshots briefly while a provider starts. Warm-up snapshots are not published one by one; the latest valid snapshot is published after the startup settle window ends. Snapshot BMPs are written below `%TEMP%\<current-exe-base>\`, so copied projects and renamed EXEs do not overwrite each other's live-wallpaper temp files.

Large static wallpapers are decoded through WIC's scaler by default before being handed to GDI+. This avoids decoding an 8K/very large wallpaper into a full native-size 32bpp GDI+ bitmap when the generated tile assets only need desktop/asset-sized pixels. Configure this from `DesktopStub.ini`, the tray menu under **Methods**, or command-line flags. Set `[Settings] WallpaperDecodeLowMemory=0` or pass `--no-wallpaper-decode-low-memory` to force the old full-size GDI+ decode path. Leave `[Settings] WallpaperDecodeMaxLongEdge=0` / `--wallpaper-decode-max-long-edge auto` for automatic monitor/asset-based sizing, or set a positive pixel value to force a specific maximum long edge. Automatic low-memory decode keeps the old full-size path for Center/Tile wallpaper modes unless `WallpaperDecodeMaxLongEdge` is explicitly set.

Idle memory reduction is enabled by default. DesktopStub releases GDI+ after image work, compacts the CRT heap, trims the process working set when it returns to idle, and periodically re-trims while the wallpaper poller is idle. Use `[Settings] TrimWorkingSetOnIdle`, `[Settings] CompactCrtHeapOnIdle`, `[Settings] KeepGdiPlusLoaded`, and `[Settings] IdleTrimIntervalMs`, or the matching command-line flags, to trade lower idle memory against warmer image-generation startup.

Generated asset caching is enabled by default, but slideshow sibling pre-cache is off by default to avoid background RAM/CPU churn in the resident tray process. Repeated wallpaper states can still reuse validated PNG assets without loading the wallpaper again. Cache files are verified as complete PNGs with the exact expected tile dimensions, including chunk CRCs and IDAT/IEND presence, before they can suppress regeneration. Use the tray **Caching** menu or the `--generated-asset-*` command-line flags to enable pre-cache or tune this behavior.

The live-wallpaper capture path does not look for a specific process name. It scans only non-icon `WorkerW` desktop-host windows and accepts visible monitor-sized child windows inside those `WorkerW` trees as candidates. This covers Lively, Wallpaper Engine, N0va Desktop, and arbitrary app-wallpaper modes that embed their renderer under the desktop. It intentionally does not enumerate arbitrary top-level windows, because a normal foreground app can also be large and visible. Captured windows that are tiny relative to the primary monitor are rejected, so small UI/helper fragments do not get stretched into the tile. If a WorkerW-hosted child renderer such as Wallpaper Engine's `WPEDesktopDX11Window` or `WPEDesktopCEFWindow` returns a black frame from `PrintWindow`, the capture path keeps the same non-icon `WorkerW` parent as a later fallback while still staying WorkerW-only. If every candidate returns a black GPU/WebView frame, the capture path rejects the blank/black frame instead of generating a black tile. The old screen-DC fallback is disabled by default because it can capture the visible desktop, taskbar, icons, or the user's static wallpaper; enable `LiveWallpaperCaptureScreenFallback=1` only for debugging or as an explicit unsafe workaround.

```cmd
DesktopStub.exe --once
DesktopStub.exe --no-tray --console
DesktopStub.exe --ini D:\Temp\DesktopStub.ini --set Settings.PollIntervalMs=5000
DesktopStub.exe --regenerate-manifest
DesktopStub.exe --wallpaper D:\Pictures\wallpaper.jpg --scales 100,200
DesktopStub.exe --exit
```

Supported options:

- The `--help` output is configurable through `[CommandLineHelp] Template` in `DesktopStub.ini`; use `\r\n` for line breaks and `{exe}` / `{iniExampleName}` placeholders where needed.
- `--help`, `-h`, `/?`: show command-line help.
- `--version`, `-v`: show the embedded release tag and AppX/Win32 package version.
- `--ini <path>`: use an alternate INI file; alternate INI instances have separate single-instance scope.
- `--set Section.Key=Value`: set and save an INI value used by the app. Manifest fields are controlled through `[Settings] Manifest*` keys; changing them by command line regenerates `AppxManifest.xml`.
- `--exit` / `--quit`: ask the running instance to exit gracefully.
- `--once`: generate once and exit.
- `--regenerate-manifest`: rewrite `AppxManifest.xml` once from the configured manifest defaults.
- `--generate` / `--generate-now`: force startup generation and keep running.
- `--wallpaper <path>` or a bare wallpaper path: generate from that image.
- `--no-monitor`: skip wallpaper/fit/DPI monitoring.
- `--tray` / `--no-tray`, `--console` / `--no-console`, `--logging` / `--no-logging`, `--notifications` / `--no-notifications`.
- `--trim-working-set-on-idle` / `--no-trim-working-set-on-idle`: lower idle Task Manager memory by trimming the working set.
- `--compact-crt-heap-on-idle` / `--no-compact-crt-heap-on-idle`: release free CRT heap pages after idle work.
- `--keep-gdiplus-loaded` / `--no-keep-gdiplus-loaded`: keep or release GDI+ after image work for speed vs lower idle memory.
- `--low-idle-memory` / `--no-low-idle-memory`: convenience profile for lower idle memory vs warmer image runtime.
- `--idle-trim-interval <ms|off>`: set and save periodic idle working-set trim interval from 0 to 3600000 ms. `off` maps to 0.
- `--powershell` / `--com-registration`: set and save registration command mode.
- `--live-tile` / `--no-live-tile`: set and save Live Tile update mode.
- `--live-tile-auto`: set and save automatic Live Tile update mode.
- `--live-tile-mode Auto|Registration|LiveTile`: set and save Live Tile update mode.
- `--tile-text <text>`, `--tile-text-secondary <text>` / `--tile-subtext <text>`, and `--tile-text-badge <text>` / `--tile-badge <text>`: configure optional primary, secondary, and badge text overlays.
- `--tile-text-enable`, `--tile-text-disable`, `--tile-text-clear`, `--tile-text-secondary-clear`, and `--tile-text-badge-clear`: enable/disable or clear overlay parts.
- `--tile-text-align left|center|right`, `--tile-text-valign top|center|bottom`, `--tile-text-badge-align left|center|right`, `--tile-text-badge-valign top|center|bottom`: configure text/badge placement.
- `--tile-text-font <name>`, `--tile-text-font-size <px>`, `--tile-text-secondary-font-size <px>`, `--tile-text-badge-font-size <px>`, `--tile-text-bold`, `--no-tile-text-bold`, `--tile-text-secondary-bold`, `--no-tile-text-secondary-bold`, `--tile-text-badge-bold`, `--no-tile-text-badge-bold`, `--tile-text-italic`, `--no-tile-text-italic`, `--tile-text-color <#AARRGGBB>`, `--tile-text-secondary-color <#AARRGGBB>`, `--tile-text-badge-color <#AARRGGBB>`, `--tile-text-shadow`, `--no-tile-text-shadow`, `--tile-text-shadow-color <#AARRGGBB>`, `--tile-text-margin-x <px>`, `--tile-text-margin-y <px>`, `--tile-text-line-gap <px>`, `--tile-text-max-secondary-lines <n>`: configure text style and layout.
- `--manifest-target Windows10|Windows81|Windows8`: set the generated AppX manifest dialect and regenerate `AppxManifest.xml`. Windows 10 remains the default.
- `--manifest-win10`, `--manifest-win81` / `--manifest-win8.1`, `--manifest-win8` / `--manifest-win8.0`: shortcuts for `--manifest-target`.
- `--win8-broker` / `--no-win8-broker`: set and save `Win8LiveTileBrokerApp`.
- `--win8-background-task` / `--no-win8-background-task`: set and save `Win8LiveTileBackgroundTask`.
- `--win8-oop-helper` / `--no-win8-oop-helper`: set and save `Win8LiveTileOopHelper`.
- `--detect <method>`: set and save `WallpaperDetectionMethod`.
- `--live-wallpaper-capture` / `--no-live-wallpaper-capture`: set and save live wallpaper snapshot capture.
- `--wallpaper-decode-low-memory` / `--no-wallpaper-decode-low-memory`: set and save `WallpaperDecodeLowMemory`. It is on by default.
- `--wallpaper-decode-max-long-edge <px|auto>`: set and save `WallpaperDecodeMaxLongEdge` from 0 to 32768. `auto`, `off`, and `none` map to 0.
- `--live-wallpaper-screen-fallback` / `--no-live-wallpaper-screen-fallback`: set and save unsafe screen-copy fallback. It is off by default.
- `--live-wallpaper-delay <ms>`: set and save `LiveWallpaperCaptureDelayMs` from 0 to 30000.
- `--live-wallpaper-refresh <ms|once>`: set and save `LiveWallpaperCaptureRefreshMs` from 0 to 3600000. `once` maps to 0.
- `--live-wallpaper-startup-refresh <ms|off>`: set and save `LiveWallpaperCaptureStartupRefreshMs` from 0 to 3600000. `off` maps to 0.
- `--live-wallpaper-startup-refresh-duration <ms|off>`: set and save `LiveWallpaperCaptureStartupRefreshDurationMs` from 0 to 3600000. `off` maps to 0.
- `--scales auto|all|100,125,150,200,400`: set and save generated DPI scales. `auto` ignores manual scale toggles while preserving their previous checkbox state for later.
- `--asset Name=0|1`: set and save one asset toggle, such as `MediumTile=1`.
- `--generated-asset-cache` / `--no-generated-asset-cache`: enable or disable generated asset cache restore/save.
- `--generated-asset-precache` / `--no-generated-asset-precache`: enable or disable slideshow sibling pre-cache.
- `--generated-asset-cache-max <n>`: set cache max entries from 0 to 4096.
- `--generated-asset-precache-max <n>`: set pre-cache max files from 0 to 256.

## Important Features

- Generates Store logo, medium tile, square 44 logo, square 30 logo, small tile, wide tile, large tile, and splash screen assets.
- Supports selected DPI scales plus automatic current-DPI scale generation.
- Detects wallpaper through configurable methods, including slideshow-compatible methods.
- Can internally capture a still frame from WorkerW-hosted live wallpaper apps and use that snapshot as the generation source, without overwriting the user's real Windows wallpaper.
- Uses COM Appx registration by default with optional PowerShell-only mode and fallback behavior; the COM isolation helper is disabled by default to avoid spawning an extra helper process on normal registration.
- Can automatically use Live Tile notification updates when launched with package identity, with manual registration/Live Tile overrides.
- Supports optional tile text overlays. In Windows 10 Live Tile mode the text is emitted into the Live Tile XML; in registration/static-image modes it is baked into generated tile PNGs to simulate the same appearance.
- Uses low-memory wallpaper decode, generated asset caching, lazy GDI+, and idle working-set trimming to keep large-wallpaper generation from permanently inflating resident memory.
- Can dynamically create or regenerate `AppxManifest.xml` from `[Settings] Manifest*` defaults.
- Supports quoted INI values and inline comments.
- Keeps detailed logs and exposes registration output from the tray.
- Records forced-shutdown cleanup state and warns on the next startup.

## Source Layout

`DesktopStub.cpp` is the main host translation-unit entry point. Shared baseline helpers come from the repository-level `dependencies\*.inc` files, and DesktopStub-specific implementation code is split into ordered fragments under `DesktopStub\src`:

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

Generated/runtime files live under `DesktopStub\build` and are ignored by git. The default file names below use `DesktopStub`; when the host EXE or build product name changes, runtime sidecars are derived from the current product/runtime base name instead:

- `{ProductRuntimeBaseName}.exe` (`DesktopStub.exe` by default)
- `{ProductRuntimeBaseName}LiveTileBroker.exe` (`DesktopStubLiveTileBroker.exe` by default)
- `{ProductRuntimeBaseName}AppxStub.exe` when legacy fallback mode is selected
- `{ProductRuntimeBaseName}LiveTileTask.dll` only if manually built for the disabled background-task experiment
- `{ProductRuntimeBaseName}.ini`
- `{ProductRuntimeBaseName}.log`
- `{ProductRuntimeBaseName}.appxactivation.log`
- `{ProductRuntimeBaseName}.livetile.pending.xml`
- `{ProductRuntimeBaseName}.livetile.clear`
- `AppxManifest.xml`
- `Assets\*`
- compiler object files under `obj\`

## Manifest Target Compatibility

`AppxManifestTarget=Windows10` is the default. It keeps the existing Windows 10 Desktop Bridge-style manifest with `TargetDeviceFamily`, `Windows.FullTrustApplication`, and `runFullTrust`.

For Start Screen / Live Tile simulator experiments, the generator can instead emit legacy AppX manifest shapes:

- `Windows81`: uses the Windows 8.1-era base namespace plus `m2` 2013 extensions, `<Prerequisites>`, `m2:VisualElements`, `Square150x150Logo`, `Square30x30Logo`, `m2:DefaultTile`, 70/150/310 tile names, splash screen, and Live Tile XML with `TileSquare150x150Image`, `TileWide310x150Image`, and `TileSquare310x310Image`.
- `Windows8`: uses the Windows 8 base namespace, `<Prerequisites>`, unprefixed `VisualElements`, `Logo`, `SmallLogo`, `DefaultTile WideLogo`, and Live Tile XML with the older `TileSquareImage` / `TileWideImage` templates. There is no 310x310 large-tile notification binding for this target.

For Windows 8/8.1 targets, the default compatibility helper is now `DesktopStubLiveTileBroker.exe`, a tiny CoreApplication-based WinRT broker app. The normal `DesktopStub.exe` remains the unpackaged tray/wallpaper monitor; the broker only exists so the registered package can update the Live Tile under package identity. The standard build script always builds this broker, regardless of arguments, before `--manifest-win8` or `--manifest-win81` are used. Set `[Settings] ManifestLiveTileBrokerExecutable` when reusing the baseline under a different broker filename. Set `[Settings] Win8LiveTileBrokerApp=0` and regenerate the manifest to fall back to the older `DesktopStubAppxStub.exe` behavior.

Experimental helper paths remain in the source for later testing, but they are disabled by default: `[Settings] Win8LiveTileBackgroundTask=0` and `[Settings] Win8LiveTileOopHelper=0`. The background-task extension is only emitted into a generated manifest when `Win8LiveTileBackgroundTask=1` and the matching `{ProductRuntimeBaseName}LiveTileTask.dll` exists next to the host. Its default runtime class ID is derived from the product manifest token, so copied projects do not inherit `DesktopStub.LiveTileBackgroundTask`. The background-task path currently requires package identity for the caller; the OOP-server path did not register reliably with the loose Windows 8-style package.

## Live Tile Update

`ExperimentalLiveTileUpdate=Auto` is the default Live Tile update mode in `DesktopStub.ini`.

In `Auto`, Windows 10 Desktop Bridge mode updates the tile directly when `DesktopStub.exe` is running with package identity. In Windows 8/8.1 compatibility mode, the normal unpackaged host writes `{ProductRuntimeBaseName}.livetile.pending.xml`, mirrors it into the package `LocalState` folder, then activates the packaged WinRT broker (`DesktopStubLiveTileBroker.exe` by default) so the broker can apply the Live Tile update under package identity.

The mode is user-configurable from the tray menu, command line, or INI:

- `Auto`: Windows 10 uses direct Live Tile updates only when package identity is present; Windows 8/8.1 compatibility targets use the packaged broker.
- `LiveTile` or `1`: always try the Live Tile path. On Windows 8/8.1 targets this means broker activation from the normal unpackaged host.
- `Registration` or `0`: refresh by re-registering `AppxManifest.xml` instead of using the Live Tile notification path.

For Windows 8/8.1 targets, generated manifests point at `[Settings] ManifestLiveTileBrokerExecutable` by default, not the resident tray app. This avoids the earlier fake-RT activation/MoAppHang behavior. Existing `AppxManifest.xml` files are kept unless you explicitly use `--regenerate-manifest`, `--manifest-win8`, `--manifest-win81`, change a `Settings.Manifest*` value, or use the tray regeneration/editing actions.

When Live Tile update is active, static manifest logo assets are treated as disabled so stale registered assets are not refreshed with wallpaper images. If **Generate Desktop Icon for disabled entries** is enabled, those static assets become desktop-icon placeholders; otherwise they are deleted. The Live Tile notification itself uses separate generated files under `Assets\Live*.png`.

Changing the Live Tile update mode queues one asset regeneration and one Appx re-registration before normal Live Tile updates resume. This refreshes Windows' cached static assets after switching modes. If the app is cold-started from the Start tile while forced `LiveTile` mode is configured and that one-time static icon refresh has not successfully completed yet, the packaged instance relaunches through the unpackaged process first so the same re-registration path still runs.

The mismatch relaunch guard is enabled by default and can be changed from the command line with `--live-tile-relaunch-on-mismatch` or `--no-live-tile-relaunch-on-mismatch`. The INI key remains `LiveTileRelaunchOnSwitch` for backward compatibility with existing configs.

## Tile Text Overlay

`[TileText]` controls an optional text overlay for generated tiles. It is disabled by default. In Windows 10 Live Tile mode, DesktopStub adds the configured text to the Live Tile XML notification so Windows renders it as a tile text layer. In registration/static-image mode, DesktopStub draws the text directly onto the generated PNG assets so non-LiveTile mode can visually simulate the same feature. Windows 8/8.1 compatibility Live Tile targets also bake the text into the notification images because those legacy XML templates are image-focused.

Default configuration:

```ini
[TileText]
Enabled=0
Text=
SecondaryText=
BadgeText=
Font=Segoe UI
FontSize=18
SecondaryFontSize=12
BadgeFontSize=40
Bold=0
SecondaryBold=0
BadgeBold=0
Italic=0
Color=#FFFFFFFF
SecondaryColor=#CCFFFFFF
BadgeColor=#FFFFFFFF
Shadow=1
ShadowColor=#AA000000
HorizontalAlign=Center
VerticalAlign=Top
BadgeHorizontalAlign=Right
BadgeVerticalAlign=Top
MarginX=8
MarginY=10
LineGap=2
MaxSecondaryLines=2
ApplyToMediumTile=1
ApplyToWideTile=1
ApplyToLargeTile=1
ApplyToSmallTile=0
ApplyToLogos=0
```

When Live Tile mode disables static manifest assets, DesktopStub does not bake text into the generated desktop-icon placeholder assets. This keeps the static icon behind the Live Tile clean.

## Release

Prebuilt binaries are published through the repository's Windows build workflow and tagged GitHub releases when available. The DesktopStub Windows file/product version and default `AppxManifest.xml` package version are derived from the same `DesktopStub-vN` family used by CI release publishing, so the binary and manifest versions match the release tag. Reused projects can override the local build tag family with `DESKTOPSTUB_RELEASE_TAG_PREFIX`. The repository workflow still publishes the default `DesktopStub` artifact paths; a copied baseline project that changes output names should update `.github/project-map.json`, `.github/workflows/build-windows.yml`, and `.github/scripts/build-windows.cmd` together so CI artifacts and release assets follow the new product names.

## Notes for fix28

`BuildDesktopStub.cmd` no longer has argument-selected build modes. It always builds the main host and the stable packaged Live Tile broker; any supplied arguments are accepted but ignored.

Live Tile menu additions from the previous fix remain:

- **Relaunch when runtime mismatches**: when enabled, startup and mode changes compare the configured Live Tile mode with the current process state. `LiveTile` mode relaunches an unpackaged Windows 10 process through the registered package entry; `Registration`/disabled mode relaunches a packaged process as the normal unpackaged desktop app before re-registering the manifest. This also repairs the case where a user clicks the tile while no resident instance is running after Live Tile was force-disabled.
- **Clear Live Tile on shutdown**: enabled by default. Packaged instances clear the live tile during graceful shutdown. Win8/8.1 broker mode can request a package-side clear through the broker.

The wallpaper poller updates its internal baseline after a failed poll-triggered generation attempt. This prevents repeated regeneration of the same wallpaper when the tile/app registration stage fails after assets were generated successfully.

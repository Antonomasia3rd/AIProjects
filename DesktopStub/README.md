# DesktopStub

Battery tiles show charge/status/runtime, capacity and wear, with a configurable
critical-action view. See the [Battery guide](../docs/battery-live-tiles-2026-10-25.md)
for per-tile GUI, INI and CLI controls and the meaning of the critical margin.

See the [current topology](../docs/DESKTOPSTUB_TOPOLOGY.md) for what is compiled
into the host, which helper executables are separate, and the status of source
plugins versus shared source code.

`DesktopStub.exe` supplies Windows Start tile content. A fresh configuration starts with one **Desktop** wallpaper tile. **Tiles > Add tile...** creates additional independent tiles by type. Each tile owns its source, appearance, text layout, click action and update interval; five RSS tiles can use five feeds and five schedules in one resident process. **Plugins** contains independent CapsBlink and ASUS indicator utilities. See the [independent tile guide](../docs/tile-instances-2026-10-20.md) for the current schema, setup and command-line examples.

Open **Settings...** in the tray menu, or run `DesktopStub.exe --settings`, for
the native Properties window. It exposes the same tile, plugin and application
settings while retaining the tray interface. **All tile contents...** provides
one central paged view of content and source diagnostics. See the
[settings and UX follow-up](../docs/desktopstub-settings-ux-2026-10-24.md).

Calendar tiles support full Google sign-in through your own Google Cloud Desktop
OAuth client as well as local ICS files. WuWa tiles provide guided Kuro
email/password sign-in and character selection. Prayer/RSS desktop notifications
are in each tile's direct **Notifications for this tile** menu; technical source
controls are under **Source settings > Advanced**. See the
[account and notification setup guide](../docs/calendar-accounts-prayer-notifications-2026-10-24.md).

**Application > Profiles...** manages independent residents with separate INIs,
accounts, persistent package identities and runtime folders. Separate tray icons
are the default; fresh profiles include their name in the tooltip. Startup of
selected profiles, unified tray presentation and crash notices are opt-in.
Use `--profile-manager` or `--profile-list` for the corresponding command-line
entry points. See the [profile architecture and CLI guide](../docs/desktopstub-profile-architecture-2026-10-25.md).

**Add tile > Mail** supports Gmail / Google Workspace through a separate Google
consent. Headers are the default; snippets require read-only Gmail permission.
**Add tile > App launcher** creates app, document, folder and URL shortcuts with
custom artwork in Small, Medium, Wide and Large sizes. The [October 25 update](../docs/desktopstub-profiles-mail-ux-2026-10-25.md)
records setup, settings/layout changes, validation and remaining acceptance work.

## Fresh VM quick start

Live Tiles need **Windows 10's Start menu/screen**, or an already working legacy
Start implementation on Windows 11 that supports Live Tiles. The standard
Windows 11 Start menu [does not display them](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/chaseable-tile-notifications). Selecting a Windows 8/8.1 manifest
does not add Live Tile support to that menu.

1. Keep the extracted release/build files together in a permanent, writable
   folder. Open a terminal in the folder containing `DesktopStub.exe` and run:

   ```powershell
   .\DesktopStub.exe --setup-desktop
   ```

2. Wait for the setup result. It prepares the wallpaper assets, registers the
   main app identity once, then restarts DesktopStub with that identity. If you
   already opened the EXE normally, **Tiles > Set up Desktop tile...** performs
   the same explicit setup. Check Windows Developer Mode/sideloading policy if
   Windows refuses package registration; setup reports the error and can be
   retried from that menu.
3. Open Start's **All apps** list and pin **Desktop** to Start. This is the
   default app display name, not `DesktopStub`; a customized manifest/display
   name can change it. Select Medium, Wide or Large and leave its Live Tile
   enabled. Keep DesktopStub running for wallpaper updates.

Fresh defaults use **LiveTile** delivery and **NameAndLogo** branding, with no
optional source tiles pinned or enabled automatically. Native updates avoid
repeated static Registration updates; the one-time identity registration above
is still required. **Launch at sign-in is off by default** for both startup
methods. Enable one later under **Application > Launch at sign-in** if wanted.
After setup, normal launches can use `DesktopStub.exe` without repeating setup.
See the [fresh Desktop defaults and setup details](../docs/desktop-factory-defaults-2026-10-23.md).

The app monitors configured wallpaper, fitting and DPI changes and publishes
updates through the selected tile delivery method. Static Registration delivery
is a separate option. Most behavior is configurable through the tray menu and
generated INI file.

**Tiles** manages an ordered list of independently configured instances from one resident.
The list has no fixed seven/eight-source or 32-content-slot limit; large lists
use **Previous tiles...** and **Next tiles...** menu pages. Existing profiles are
converted with a pre-conversion backup while retaining source pin identities,
accounts and gallery history. The old global content/source/display/cycling
menus remain relevant to legacy profiles, rather than the normal tile workflow.
Delivery mode, native template, shutdown cleanup, generated assets/scales,
image-cache budget and relevant image-fitting/decoder settings belong to each
tile. Conversion captures existing global defaults once; later global edits do
not alter saved tile policies. See the guide's [policy keys and delivery
migration](../docs/tile-instances-2026-10-20.md#per-tile-rendering-cache-and-delivery).
Existing pins receive their own static topic logos automatically after an upgrade.
**Quick Actions** offers direct per-action pinning without an enable switch.
Source tiles and Quick Actions default to transparent fallback icons and
name-only branding, hiding the small corner logo while live content is shown.
Use **Tiles > your source > Appearance** or **Quick Actions > Appearance** to
choose a solid background, a custom color, or different name/logo branding.
Each instance saves these choices under its `Tile.<id>` INI section; existing
Quick Actions share `MediaActions`, while newly added QuickAction instances
have their own settings. The same settings accept `--set`, for example
`--set Tile.smtc.Branding=None`. See the
[appearance settings](../docs/tile-appearance-2026-10-15.md) for all values.
Now playing appearance can use current playback status, the player icon, or no
icon, with optional repeat/shuffle indicators. Game tiles stack the available
resource/activity fields. For sign-in, open the game-specific embedded window,
sign in, choose **Find game accounts**, then save the selected UID/server.
Fresh private browser sessions are the default; remembering a session is optional
for each saved account in instance mode. Clearing local browser data keeps imported Notes credentials and
does not call HoYoLAB Logout. Each game tile chooses an explicit saved account
and can retain its own game UID/server. See the [current tile guide](../docs/tile-instances-2026-10-20.md)
and [sign-in details](../docs/notes-signin-ux-2026-10-15.md).

**Tiles > Photo galleries > Set up gallery...** creates a named set of 1–1,024 photo
tiles from one folder. Each tile owns a separate image pool, so its photos are
not assigned to another gallery tile. Setup queues the individual Windows pin
confirmations automatically; **Pin all...** is also available afterward.
Windows rotates up to five queued photos per tile; DesktopStub replaces the
queues at the configured interval, rather than rendering each animation.
Fit keeps the whole photo with transparent letterboxing; Fill crops its edges.
Keep the INI and its `.gallery-ledger` together so image ownership survives a
restart or folder change. See the [gallery setup and limits](../docs/gallery-setup-2026-10-15.md)
and [current review status](../docs/CURRENT_STATUS.md).
Use **Add another gallery folder...** for another album, and each gallery's
**Number of tiles...** to expand it. Setup and **Pin all...** queue native
confirmations sequentially. Pinned tiles update automatically; Start controls
whether Live Tile content is displayed. See the
[current gallery/menu/game changes](../docs/tile-feedback-followup-2026-10-15.md).

## Requirements

- Windows 10/11 for the running utility and normal Desktop Bridge registration. Native Live Tiles need the compatible Start interface described above. The manifest generator can also emit Windows 8 or Windows 8.1-style AppX XML for Start Screen/Live Tile simulator compatibility.
- Visual Studio Build Tools with the C++ workload when building from source; prebuilt releases do not require the compiler.
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

Normal builds optimize the host, broker and built-in provider objects for speed
with `/O2 /fp:precise`. Set `DESKTOPSTUB_OPTIMIZE=0` for an unoptimized diagnostic
build. Named builds keep generated headers/resources and objects in their own
`build/obj/<host-name>` directory. `/bigobj` accommodates the template-heavy
Windows headers; it adds no runtime file or provider DLL.

The build embeds one version into both EXEs, the generated AppX manifest, startup diagnostics, the tray menu, and `--version`. By default the script reads the local `DesktopStub-vN` Git tag family and builds the next package version as `N.0.0.0`; for example, after `DesktopStub-v17`, an untagged local/CI build reports `DesktopStub-v18 (18.0.0.0)`. CI uses a full checkout, so release builds already have the repository tags. Set `DESKTOPSTUB_REFRESH_TAGS=1` to explicitly fetch tags from `origin` before a local build; this fetch does not force-update or prune local tags. Set `DESKTOPSTUB_VERSION=18.0.0.0` / `DESKTOPSTUB_RELEASE_TAG=DesktopStub-v18` to use an explicit custom version without fetching.

For template reuse, the build names are parameterized without changing the default outputs. Set `DESKTOPSTUB_PRODUCT_NAME`, `DESKTOPSTUB_HOST_EXE_NAME`, `DESKTOPSTUB_BROKER_EXE_NAME`, or `DESKTOPSTUB_RELEASE_TAG_PREFIX` before running the script to reuse the baseline for another project while keeping the same source layout. Ordinary spaces are supported in the product/output names; the default release tag prefix removes spaces from `DESKTOPSTUB_PRODUCT_NAME`, and an explicitly supplied `DESKTOPSTUB_RELEASE_TAG_PREFIX` must not contain whitespace. Command-shell metacharacters, `%`, `!`, and quotes are rejected so the generated compiler/resource commands stay deterministic. Host and broker executable names must be plain file names, not paths.

The normal build includes the optional `CapsBlink` and `AsusBlink` source providers. A developer can make a host that excludes those raw-hardware providers entirely by setting a build flag before compiling:

~~~cmd
set DESKTOPSTUB_ENABLE_HARDWARE_SOURCES=0
DesktopStub\BuildDesktopStub.cmd
~~~

That host keeps the same INI, CLI, and tray source choices, but reports a clear unavailable-source status if either hardware source is selected. No INI value can turn the omitted code back on; build with `DESKTOPSTUB_ENABLE_HARDWARE_SOURCES=1` (the default) to restore the full provider set.

The experimental background-task DLL remains in the source tree for research, but it is intentionally not part of the normal one-command build.

If `build\DesktopStub.exe` or `build\DesktopStubLiveTileBroker.exe` is running, close it before rebuilding so the compiler can overwrite the output.

## Developer Checks

`TestDesktopStubSource.cmd` runs source contracts, portable content scheduling/configuration tests, geometry tests, actual preset XML tests, and the production GDI+ renderer harness. `tools/TestContentRuntime.cmd` also exercises the actual host's content snapshots and Win32 menu dispatch using a temporary INI; it never calls the application entry point or registers a package. It first compiles an inert host flavor with `DESKTOPSTUB_ENABLE_HARDWARE_SOURCES=0` and checks that omitted CapsBlink/AsusBlink providers retain tray compatibility and give a clear status. The renderer writes a PNG contact sheet under `DesktopStub/build/tile-render-smoke`.

```cmd
DesktopStub\TestDesktopStubSource.cmd
tools\TestContentRuntime.cmd
```

Portable C++ tests run with CMake on Windows/Linux/macOS without WSL. Windows UI and shell behavior still require Windows. Commands and measured outcomes are in [the audit](../docs/REWORK_AUDIT.md); passing source checks alone is not proof of runtime/UI correctness.

The default binary smoke copies its inputs into a temporary directory and uses offline commands. It does not register packages, publish or clear native tiles, capture the desktop, or start live content providers. A Windows job terminates child processes if the runner is interrupted; temporary diagnostic files can remain. Real package/resident integration is excluded unless `--allow-package-integration` is explicitly supplied, and belongs in a disposable Windows test environment. See [smoke isolation and validation](../docs/audit-smoke-safety.md).

## Run

For a first installation, use the [fresh VM quick start](#fresh-vm-quick-start)
and perform explicit main Desktop setup:

```cmd
DesktopStub\build\DesktopStub.exe --setup-desktop
```

Once setup succeeds, launch `DesktopStub\build\DesktopStub.exe` normally, or
open the registered **Desktop** app from Start. Starting an unpackaged EXE on a
fresh VM by itself does not install the identity needed for native Live Tile
updates. Neither setup nor a normal launch enables sign-in startup by default.

You do not need to run `--manifest-win8` or `--manifest-win81` unless you are deliberately switching this build into the Windows 8/8.1 compatibility experiment. The generated manifest target defaults to Windows 10, which the Desktop setup action requires.

For Windows 8/8.1 Start Screen simulator testing, switch the generated manifest target after the normal build. The broker is already produced by `BuildDesktopStub.cmd`; extra build arguments are ignored.

```cmd
DesktopStub\BuildDesktopStub.cmd
DesktopStub\build\DesktopStub.exe --manifest-win8
DesktopStub\build\DesktopStub.exe
```

On first launch the app creates `DesktopStub.ini` next to the executable. The tray menu exposes:

- tile creation and each tile's type-specific source/display/click settings;
- independent indicator plugins;
- general settings;
- notifications and logging;
- wallpaper fitting and detection methods;
- asset generation targets and DPI scales;
- manifest target selection, status, and one-shot regeneration;
- registration mode and fallback behavior;
- Live Tile update mode;
- advanced timing/error options;
- startup/cleanup actions.

Command-line settings are saved as one all-or-nothing batch to `DesktopStub.ini`, the same configuration file used by the tray menu. A Startup shortcut change is rolled back if the matching INI commit fails. Action-only commands such as `--once`, `--generate`, `--no-monitor`, and `--exit` affect only that invocation.

Ordinary second launches offer a fresh independent profile when
`Settings.AlreadyRunningProfilePrompt=1` (the default). Disable that offer to use
the legacy `Settings.AlreadyRunningAction` choice. Explicit Generate, Console
and Ignore choices remain effective, and explicit command-line requests such as
`--generate`, `--exit` and persistent `--set` changes take precedence. Use the
Profile Manager or `--profile ID` for another resident with its own package
files. An explicit owned `--ini` remains supported; normal residents are routed
to that profile's isolated runtime. Offline `--configure-only` and `--render-only`
retain their explicit-file behavior and do not launch residents or set up packages.

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
- `--ini <path>`: use an alternate INI file; alternate INI instances have separate single-instance scope. DesktopStub also keeps the runtime product/exe base in the mutex/message/window identity, so copied or renamed baseline projects do not accidentally signal each other even if they are pointed at the same explicit INI.
- `--set Section.Key=Value`: set and save an INI value used by the app. Instance keys keep their source prefix: `--set "Tile.releases.RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom"` writes `RssFeed.FeedUrl` within `[Tile.releases]`. Manifest fields are controlled through `[Settings] Manifest*` keys; changing them by command line regenerates `AppxManifest.xml`.
- `--exit` / `--quit`: ask the running instance to exit gracefully.
- `--once`: generate once and exit.
- `--render-only`: render local tile assets and exit before package registration, activation, native tile updates, Startup handling or resident signaling. Supply `--wallpaper`, or use an enabled Image/None content entry containing only CustomText. With `--regenerate-manifest` and no image, it only writes the local manifest/configuration. External providers are rejected.
- `--configure-only`: validate and save the requested INI settings without starting or signaling the resident. Explicit Startup changes are rejected; existing Startup preferences are preserved without applying them.
- `--regenerate-manifest`: rewrite `AppxManifest.xml` once from the configured manifest defaults.
- `--generate` / `--generate-now`: force startup generation and keep running.
- `--wallpaper <path>` or a bare wallpaper path: generate from that image.
- `--content-source Wallpaper|RssFeed`: select the legacy wallpaper/RSS preset. It also writes `Content.Enabled=0`, matching the tray preset action; numbered entries stay saved and can be re-enabled later. The RSS preset renders headlines over wallpaper. A later explicit `--set Content.Enabled=1` keeps layered content instead.
- `--rss-feed-url <http(s)://...>` and `--rss-user-agent <text>`: set the RSS endpoint and HTTP user agent.
- `--rss-update-interval <60-86400>`, `--rss-max-items <1-20>`, `--rss-http-timeout <5-120>`, and `--rss-max-feed-bytes <65536-16777216>`: set the bounded RSS fetch controls.
- `--no-monitor`: skip wallpaper/fit/DPI monitoring.
- `--startup` / `--no-startup`: add or remove this INI profile's shortcut in the current user's `shell:startup` folder. The INI, command line, and tray use the same ownership-checked shared shortcut lifecycle; no Run-registry or scheduled-task entry is used. Startup and the full INI batch commit under shared locks: enable installs before saving `true`, while disable saves `false` before removal, so an interrupted change self-reconciles on the next launch.
- `--packaged-startup` / `--no-packaged-startup`: save `[Settings] RunAtStartupPackaged=1/0` independently of the Startup-folder shortcut. The tray calls this **Run at sign-in (Windows startup task)**. Windows applies it asynchronously when DesktopStub runs with package identity and the Windows10 manifest target; **Windows startup task status...** reports actual OS state and errors separately from the requested checkbox. The default is off. Custom `--ini` profiles must use the Startup-folder option because a package-wide task launches the default profile. Windows8/Windows81 keep using the Startup-folder option.

Generated Windows10 manifests declare the packaged task initially disabled. Existing custom manifests are preserved. If an older registered package lacks the task, regenerate its Windows10 manifest using the existing manifest command/menu, register the updated package, and relaunch its packaged entry. Save any manual manifest customizations first. Enabling can be refused by Windows if you disabled the app in Task Manager or an administrator policy controls it; DesktopStub reports this and does not override that decision. The Windows task does not create a shortcut or a Run-registry entry. If both startup methods are enabled, Windows can issue two launches; normally choose the one that matches how you run the app.
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
- `--live-tile-template Adaptive|Windows81Preset`: choose adaptive Windows 10 XML or the native Windows 8.1 preset-template catalog while keeping the Windows 10 manifest target.
- `--live-tile-adaptive` / `--live-tile-windows81-preset`: shortcuts for `--live-tile-template`.
- `--live-tile-branding Auto|None|Logo|Name|NameAndLogo`: choose the Windows 10 Live Tile notification branding.
- `--tile-text <text>`, `--tile-text-secondary <text>` / `--tile-subtext <text>`, and `--tile-text-badge <text>` / `--tile-badge <text>`: configure optional primary, secondary, and badge text overlays.
- `--tile-text-enable`, `--tile-text-disable`, `--tile-text-clear`, `--tile-text-secondary-clear`, and `--tile-text-badge-clear`: enable/disable or clear overlay parts.
- `--manifest-target Windows10|Windows81|Windows8`: set the generated AppX manifest dialect and regenerate `AppxManifest.xml`. Windows 10 remains the default.
- `--manifest-win10`, `--manifest-win81` / `--manifest-win8.1`, `--manifest-win8` / `--manifest-win8.0`: shortcuts for `--manifest-target`.
- `--win8-broker` / `--no-win8-broker`: set and save `Win8LiveTileBrokerApp`, then regenerate `AppxManifest.xml`.
- `--win8-background-task` / `--no-win8-background-task`: set and save `Win8LiveTileBackgroundTask`, then regenerate `AppxManifest.xml`.
- `--win8-oop-helper` / `--no-win8-oop-helper`: set and save `Win8LiveTileOopHelper`, then regenerate `AppxManifest.xml`.
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
- Supports optional tile text overlays. Windows renders the text from adaptive or Windows 8.1 preset XML in native Windows 10 Live Tile mode; registration/static-image modes use fixed Windows 8/8.1 template typography and geometry.
- Uses low-memory wallpaper decode, generated asset caching, lazy GDI+, and idle working-set trimming to keep large-wallpaper generation from permanently inflating resident memory.
- Can dynamically create or regenerate `AppxManifest.xml` from `[Settings] Manifest*` defaults.
- Supports quoted INI values and inline comments.
- Keeps detailed logs and exposes registration output from the tray.
- Records forced-shutdown cleanup state and warns on the next startup.
- Provides independent RSS/Atom, Now Playing, Prayer Times, game, Discord and wallpaper tiles in one resident, with their own settings. Legacy content composition remains documented for older profiles; indicator plugins run independently from tile text.

## Source Layout

`DesktopStub.cpp` is the main host translation-unit entry point. Shared baseline helpers come through the repository-level `dependencies\desktop_app_baseline.h` aggregate, and DesktopStub-specific implementation code is split into ordered fragments under `dependencies\DesktopStub` (relocated from the old `DesktopStub\src` so all product source lives under the repository's `dependencies` folder; these fragments remain DesktopStub-owned, not shared with other products):

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
- `tile_text_layout.h`: pure region-selection math for the `[TileText]` overlay (which rectangles get used for which tile size/field combination). Unlike every other file in this list, this one deliberately has zero Windows or GDI+ dependency, so it's testable without a Windows machine -- see "Tile Text Overlay" below and `DesktopStub/tools/TileTextLayoutTests.cpp`. `ga_image.inc` includes it and does the actual GDI+ drawing against the rectangles it returns.
- `ga_registration.inc`: Appx registration and PowerShell fallback handling.
- `ga_generation.inc`: asset generation, polling, and shutdown coordination.
- `ga_live_tile.inc`: Live Tile notification update handling.
- `ga_live_tile_templates.inc`: Windows 8.1 preset-catalog binding selection and XML fragments.
- `ga_rss_feed.inc`: RSS/Atom feed data provider. Rendering and delivery belong to the common content pipeline, not to the feed module.
- `ga_tray.inc`: tray wrapper that includes smaller helper/menu/dispatch fragments.
- `ga_tray_helpers.inc`, `ga_tray_menu.inc`, `ga_tray_dispatch.inc`: tray helpers, menu construction, and command dispatch.
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
- an optional profile-scoped `.lnk` in the current user's `shell:startup` folder
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

For Windows 8/8.1 targets, the default compatibility helper is now `DesktopStubLiveTileBroker.exe`, a tiny CoreApplication-based WinRT broker app. The normal `DesktopStub.exe` remains the unpackaged tray/wallpaper monitor; the broker only exists so the registered package can update the Live Tile under package identity. The standard build script always builds this broker, regardless of arguments, before `--manifest-win8` or `--manifest-win81` are used. Set `[Settings] ManifestLiveTileBrokerExecutable` when reusing the baseline under a different broker filename. Set `[Settings] Win8LiveTileBrokerApp=0` to fall back to the older `DesktopStubAppxStub.exe` behavior; the tray and dedicated command-line switches regenerate the manifest automatically.

Experimental helper paths remain in the source for later testing, but they are disabled by default: `[Settings] Win8LiveTileBackgroundTask=0` and `[Settings] Win8LiveTileOopHelper=0`. The background-task extension is only emitted into a generated manifest when `Win8LiveTileBackgroundTask=1` and the matching `{ProductRuntimeBaseName}LiveTileTask.dll` exists next to the host. Its default runtime class ID is derived from the product manifest token, so copied projects do not inherit `DesktopStub.LiveTileBackgroundTask`. The background-task path currently requires package identity for the caller; its access/trigger requests use a five-second bounded WinRT wait and cancel on timeout. The OOP-server path did not register reliably with the loose Windows 8-style package.

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

For a Windows 10 manifest target, `LiveTileTemplateStyle` selects the notification XML family without changing package registration:

- `Adaptive` is the default and preserves DesktopStub's existing adaptive bindings.
- `Windows81Preset` uses Microsoft's native Windows 8.1 preset-template catalog on Windows 10. Medium, wide, and large bindings select image, text, image-and-text, or block layouts from the enabled tile-text content.

The preset style emits version-4 Windows 10 notification XML with Windows 8 fallback names where the catalog defines them. It does not affect `AppxManifestTarget=Windows81` or `Windows8`; those compatibility targets keep their existing legacy XML and packaged broker path.

`LiveTileBranding` controls the branding rendered by Windows for either Windows 10 XML style:

- `Auto` is the default. It resolves to `NameAndLogo` for Adaptive and `Name` for the Windows 8.1 preset catalog.
- `None`, `Logo`, `Name`, and `NameAndLogo` request that exact Windows 10 branding value.

The displayed name comes from `ManifestDisplayName` (`Desktop` by default). Branding does not change the primary, secondary, or badge content configured under `[TileText]`. Windows 8/8.1 manifest targets retain their legacy branding behavior.

## RSS Feed Content Source

Use **Tiles > Add tile... > RSS**, then the tile's **Source settings > RSS or Atom feed URL**. Each instance stores its own `RssFeed.FeedUrl` and `RssFeed.UpdateIntervalSeconds`. The new tile schema uses `[Tiles] Instances=...` and `[Tile.<id>] Type=RSS`, without numbered content entries. See the [five-feed example](../docs/tile-instances-2026-10-20.md#five-independent-rss-tiles).

For an existing instance named `releases`, change the feed through the same typed CLI/INI contract:

```cmd
DesktopStub.exe --configure-only --set "Tile.releases.RssFeed.FeedUrl=https://mods.windhawk.net/releases.atom" --set "Tile.releases.RssFeed.UpdateIntervalSeconds=900"
```

The following provider keys live under the tile section with a `RssFeed.` prefix:

| RSS key | Default | Meaning |
|---|---|---|
| `FeedUrl` | empty | Required when RSS is enabled; HTTP or HTTPS. |
| `UserAgent` | DesktopStub RSS user agent | Header sent with the request. |
| `UpdateIntervalSeconds` | `900` | Fetch interval, 60–86400 seconds; independent of content cycling. |
| `MaxItems` | `5` | Maximum parsed entries, 1–20. |
| `HttpTimeoutSeconds` | `20` | Total HTTP request budget, 5–120 seconds. |
| `MaxFeedBytes` | `2097152` | Response limit, 64 KiB–16 MiB. |

RSS is data-only: it uses the common rendering and delivery pipeline. `ClickAction=source` opens the featured article when a valid link is available; `setup` opens its own settings. Choose **Text size, wrapping and layout > Layout > Headlines with summaries** for a larger wrapped headline and summaries. Legacy `--content-source`, `--rss-*`, and numbered content composition apply to older profiles; use instance-prefixed settings after conversion.

## Legacy Tile Text Overlay

The following global overlay settings describe older profiles. Independent tiles use their own **Text size, wrapping and layout** menu and `[Tile.<id>]` fields instead. In Windows 10 Live Tile mode, DesktopStub sends adaptive XML or native Windows 8.1 preset bindings for Windows to render. Registration/static-image mode and Windows 8/8.1 compatibility targets bake text into generated PNG assets using the corresponding template layouts:

- Medium tiles use a `TileSquareText02`-style heading/body layout, or a `TileSquareBlock`-style layout (badge number over a single caption line) when badge text is present.
- Wide tiles use a `TileWideImageAndText02`-style image with a bottom text band, or `TileWideBlockAndText02` when badge text is present.
- Large tiles use an `ImageAndTextOverlay02`-style darkened image with top heading and bottom body text.

Small tiles and logo assets do not support text, matching the native tile templates. Adaptive text supports named Windows sizes/weights, wrapping, alignment and line limits through the presentation controls below. Arbitrary font-family/pixel-size selection is not exposed. Legacy preset templates retain their documented field structure and native geometry; native Start placement still requires Windows visual acceptance testing.

**Wide badge mapping**: `TileWideBlockAndText02` has three text slots: a wrapped left text field, the numeric block value, and a short caption beneath it. DesktopStub maps `Text`, `BadgeText`, and `SecondaryText` to those slots respectively, both in native Windows 10 preset XML and in the baked static overlay. No fourth field is needed for this template. [Microsoft's tile-template reference](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tiletemplatetype?view=winrt-26100) documents the same field structure.

**Medium tile badge fix**: earlier versions drew both `Text` and `SecondaryText` as two stacked caption lines under the badge number. The real `TileSquareBlock` template only ever has one caption slot, so this now shows just one line -- `Text` if set, falling back to `SecondaryText` if only that was configured, matching the template's actual capacity instead of silently exceeding it.

Default configuration:

```ini
[TileText]
Enabled=0
Text=
SecondaryText=
BadgeText=
ApplyToMediumTile=1
ApplyToWideTile=1
ApplyToLargeTile=1
```

When Live Tile mode disables static manifest assets, DesktopStub does not bake text into the generated desktop-icon placeholder assets. This keeps the static icon behind the Live Tile clean.

**Testing**: the region-selection logic (which rectangles get used for which tile size/field combination) lives in `dependencies/DesktopStub/tile_text_layout.h`, deliberately with no Windows or GDI+ dependency, so it can be tested without a Windows machine. `DesktopStub/tools/TileTextLayoutTests.cpp` exercises every primary/secondary/badge combination for all three tile sizes and asserts regions stay in-bounds, never overlap, and that every configured field actually gets drawn somewhere. It compiles and runs the same way on any C++17 compiler:

```sh
g++ -std=c++17 -Wall -Wextra -I dependencies/DesktopStub -o /tmp/TileTextLayoutTests DesktopStub/tools/TileTextLayoutTests.cpp
/tmp/TileTextLayoutTests
```

`TestDesktopStubSource.cmd` also builds and runs it with `cl.exe` as part of the normal Windows check suite. Separately, the Windows smoke test suite (`RepoTools.cs`) generates a tile once with `[TileText]` disabled and once with it enabled against the same wallpaper, then asserts the resulting `Assets\MediumTile.png` actually differs -- confirming the overlay is really drawn by GDI+, not just that the layout math computes sensible rectangles.

## Release

Prebuilt binaries are published through the repository's Windows build workflow and tagged GitHub releases when available. The DesktopStub Windows file/product version and default `AppxManifest.xml` package version are derived from the same `DesktopStub-vN` family used by CI release publishing, so the binary and manifest versions match the release tag. Reused projects can override the local build tag family with `DESKTOPSTUB_RELEASE_TAG_PREFIX`. The repository workflow still publishes the default `DesktopStub` artifact paths; a copied baseline project that changes output names should update `.github/project-map.json`, `.github/workflows/build-windows.yml`, and `.github/scripts/build-windows.cmd` together so CI artifacts and release assets follow the new product names.

## Tile text size, wrapping and layout

Use **Tiles > your tile > Text size, wrapping and layout**. Choosing a style normally
activates Custom presentation; RSS retains its readable layout when editing its
Default or Headlines with summaries settings. Each instance owns its values.
Readable RSS (`Default` or `RssHeadlines`) uses a semibold 15-pixel headline and
subtle summary, with one rich item on Medium, two on Wide and three on Large.
Named Windows styles control size
and weight; larger text leaves less room for content. Wrapping, line limits,
alignment, stacking and blank-line gaps are also configurable.

The keys live in `[Tile.rss]`, `[Tile.prayer]`, or another configured instance section. For
example, `--configure-only --set Tile.rss.TextPreset=RssHeadlines` or
`--set Tile.prayer.TextPreset=Custom --set Tile.prayer.TextStyle=subtitle`.
Additional keys are TextSecondaryStyle, TextBadgeStyle, TextWrap, TextMaxLines,
TextSecondaryMaxLines, TextBadgeMaxLines, TextAlign, TextStacking and TextGapLines.

## Prayer Times

Prayer Times is an optional tile type sharing the same resident process. Use
**Tiles > your prayer tile > Source settings** to enter a city/country and
choose a named calculation method, Asr convention and optional minute
adjustments. Location starts blank; there is no automatic location lookup.
Select the convention and adjustments used by your local prayer timetable.

Tiles show the next prayer time in large text, followed by its name and location.
Wide/Large tiles have a separate right-hand minutes/countdown column; compact
tiles put the countdown below the main text. Large tiles can also show all five
obligatory prayers with `PrayerTimes.ShowTimetable=1`; optional sunrise is a sixth
chronological row between Fajr and Dhuhr. `PrayerTimes.NextIncludesSunrise=0`
keeps the next-prayer countdown separate unless explicitly enabled.
Both Windows text and image-overlay delivery are supported. The source uses
[AlAdhan's calculated schedules](https://aladhan.com/prayer-times-api), rather
than a mosque's Iqamah timetable. Network retrieval runs in the background and
cached today/tomorrow schedules supply ordinary countdown updates. The selected
city/country and calculation options are sent to AlAdhan when fetching; no
account or API key is required.

Settings use a `PrayerTimes.` prefix in the instance section. For a tile named
`prayers`, use `--configure-only --set "Tile.prayers.PrayerTimes.City=Your city" --set "Tile.prayers.PrayerTimes.Country=Your country"`.
`Method=Auto`, `School=0`, `ShowSunrise=1`, `ShowTimetable=1` and `TwentyFourHour=0` (AM/PM) are the defaults.
Set that tile's `PrayerTimes.TwentyFourHour=0` for AM/PM formatting.
The private `<profile>.prayer-cache` sidecar is bound to the requested location
and calculation settings. A valid cached schedule remains visible during a
network failure with a stale/error indication. User data and caches should
stay private; the cache is ignored by Git. Additional PrayerTimes instances have
their own locations, calculation options and update deadlines.

## Independent Clock, Calendar, WuWa and source alerts

**Tiles > Add tile...** includes Clock/world clocks, a date-only or explicit local
`.ics` Calendar preview, and overseas Wuthering Waves notes with an explicitly
supplied SDK game access token. Prayer and RSS tiles have independent optional
notifications, thresholds/lead times, types and sounds. Sunrise is a separate
chronological timetable entry; next-event countdown and sunrise alerts are
independent opt-ins. No new source or notification is enabled automatically.
See [setup, settings and limits](../docs/tile-sources-alerts-2026-10-23.md).

## Notifications and bulk pin progress

Use **Application > Notifications > Notification style** to select `Auto`,
`Balloon`, or `Toast`. Auto uses Windows notifications when DesktopStub already
has a package identity and otherwise uses the classic tray balloon API. Balloon
always uses the Windows 7-compatible tray API, including ExplorerPatcher's
configured balloon presentation. Toast falls back to a balloon if the existing
identity or notification API is unavailable; selecting a notification style does
not create a package or shortcut.

Bulk pin/unpin operations update one progress notification. Windows notifications
can display a progress bar; classic balloons show numeric counts and are limited
to at most one intermediate update every three seconds. Counts advance after
Windows answers each request. Rejected/canceled requests are recorded separately
from successfully applied changes. Windows still owns each pin/unpin confirmation.

The **Enable notifications** master switch suppresses both APIs, including forced
warnings. **Notify about operations and tile source changes** gates discrete
operation/source notices. Pin progress, source failure/recovery, and gallery changes
also have their own switches. Healthy refreshes, prayer countdown ticks, and photo
rotations do not display notifications. A completed source failure is reported
once until recovery, rather than on every polling attempt. Generation start/success
notices remain disabled by default; generation failure notices remain enabled.

All settings can also be set with `--configure-only --set`, for example:

```powershell
DesktopStub.exe --configure-only --set Settings.NotificationApi=Balloon
DesktopStub.exe --configure-only --set Settings.NotifyOnPinProgress=1
```

The corresponding `[Settings]` keys are `NotificationApi=Auto`,
`NotifyOnOperations=1`, `NotifyOnPinProgress=1`, `NotifySourceErrors=1`,
`NotifyGalleryChanges=1`, and `NotificationProgressIntervalMs=750` (250–60000).
Notification wording is configurable in `[Strings]`. The new operation/source
message templates substitute `%s` literally with the tile/game name; other percent
sequences are treated as text.

**Photo galleries > Gallery settings** controls removal behavior:
`AutoUnpinRemovedGalleryTiles=1` requests removal of obsolete owned native pins
after a saved gallery resize. `ConfirmUnpinRemovedGalleryTiles=0` disables the
additional DesktopStub confirmation by default; Windows may still ask. Gallery
change notices are enabled by default. Photos and ownership history are retained;
separate Registration pins must be removed manually from Start.

## Additional Behavior Notes

- **Clear Live Tile on shutdown**: enabled by default. Graceful shutdown clears the primary notification and owned secondary queues, including Gallery content, while preserving pins, images and ownership history. Win8/8.1 broker mode can request a package-side clear through the broker. Forced process termination cannot guarantee cleanup.
- The wallpaper poller updates its internal baseline after a failed poll-triggered generation attempt. This prevents repeated regeneration of the same wallpaper when the tile/app registration stage fails after assets were generated successfully.
- DesktopStub intentionally keeps the default log beside the executable even when `--ini` points at another directory. This preserves the pre-shared-baseline portable behavior; an explicit `[Settings] LogPath` still overrides it.

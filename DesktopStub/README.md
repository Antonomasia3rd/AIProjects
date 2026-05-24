# DesktopStub / GenerateAssets

`GenerateAssets.exe` is a tray utility that generates Windows Start tile assets from the current desktop wallpaper and registers a loose Appx manifest for a desktop tile entry.

The app can monitor wallpaper changes, wallpaper fit mode changes, and DPI scale settings, then regenerate assets and re-register the manifest automatically. Most behavior is configurable through the tray menu and generated INI file.

## Requirements

- Windows 10/11.
- Visual Studio Build Tools with the C++ workload.
- Developer Mode or sideloading support may be required for Appx registration, depending on Windows policy.

## Build

From the repository root:

```cmd
DesktopStub\BuildGenerateAssets.cmd
```

If `build\GenerateAssets.exe` is running, close it before rebuilding so the compiler can overwrite the output.

Syntax-only check:

```cmd
DesktopStub\BuildGenerateAssets.cmd check
```

Output:

```text
DesktopStub\build\GenerateAssets.exe
```

## Developer Checks

`TestGenerateAssetsSource.ps1` is a maintainer regression guard. It does not build or launch `GenerateAssets.exe`; it scans the source for safety fixes that should not be accidentally removed.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File DesktopStub\TestGenerateAssetsSource.ps1
```

Use `-ListChecks` to print the guardrails without running assertions.

## Run

```cmd
DesktopStub\build\GenerateAssets.exe
```

On first launch the app creates `GenerateAssets.ini` next to the executable. The tray menu exposes:

- general settings;
- notifications and logging;
- wallpaper fitting and detection methods;
- asset generation targets and DPI scales;
- manifest settings;
- registration mode and fallback behavior;
- experimental Live Tile update mode;
- advanced timing/error options;
- startup/cleanup actions.

## Important Features

- Generates Store logo, medium tile, square 44 logo, small tile, wide tile, and large tile assets.
- Supports selected DPI scales plus automatic current-DPI scale generation.
- Detects wallpaper through configurable methods, including slideshow-compatible methods.
- Uses COM Appx registration by default with optional PowerShell-only mode and fallback behavior.
- Can optionally try an experimental Live Tile notification update instead of re-registering after each asset regeneration.
- Can dynamically create `AppxManifest.xml` from configurable manifest defaults.
- Supports quoted INI values and inline comments.
- Keeps detailed logs and exposes registration output from the tray.
- Records forced-shutdown cleanup state and warns on the next startup.

## Source Layout

`GenerateAssets.cpp` is the single translation-unit entry point. Most implementation code is split into ordered fragments under `DesktopStub\src`:

- `ga_core.inc`: low-level file, text, INI, and process-output helpers.
- `ga_config_defaults.inc`: runtime globals and generated INI/string defaults.
- `ga_ui_logging.inc`: ordered UI/logging aggregator for smaller fragments.
- `ga_ui_state.inc`: UI string state, logging/tray globals, and shared state labels.
- `ga_logging_core.inc`: logging, console, INI access wrappers, and runtime logging settings.
- `ga_manifest.inc`: manifest settings, XML helpers, and generated manifest output.
- `ga_ui_strings.inc`: localized string loading, defaults validation, and format-token checks.
- `ga_runtime_helpers.inc`: runtime option parsing, DPI scale helpers, cleanup policy, and rename dialog.
- `ga_wallpaper.inc`: wallpaper and fit/DPI detection.
- `ga_image.inc`: GDI+ image generation and PNG saving.
- `ga_registration.inc`: Appx registration and PowerShell fallback handling.
- `ga_generation.inc`: asset generation, polling, and shutdown coordination.
- `ga_live_tile.inc`: experimental Live Tile notification update handling.
- `ga_tray.inc`: tray menu and tray notifications.
- `ga_app.inc`: window procedure and application startup/shutdown.

## Generated Files

Generated/runtime files live under `DesktopStub\build` and are ignored by git:

- `GenerateAssets.exe`
- `GenerateAssets.ini`
- `GenerateAssets.log`
- `AppxManifest.xml`
- `Assets\*`
- compiler object files under `obj\`

## Experimental Live Tile Update

By default, each successful asset regeneration refreshes the Start entry by re-registering `AppxManifest.xml`.

Set `ExperimentalLiveTileUpdate=1` in `GenerateAssets.ini`, or enable **Experimental Live Tile update** from the tray menu, to try updating the tile through `Windows.UI.Notifications.TileUpdateManager` instead. New default manifests point at `GenerateAssets.exe`, so launching the registered `shell:AppsFolder\<PackageFamilyName>!App` entry starts the updater with package identity. Existing manifests and explicit `Manifest.Executable` settings are not rewritten; keep `rundll32.exe` if you want the tile launch to do nothing. Direct `GenerateAssets.exe` launches usually do not have package identity, so the app logs a clear failure instead of falling back silently. The registration path remains the default.

In this mode, static manifest logo assets are treated as disabled so stale registered assets are not refreshed with wallpaper images. If **Generate Desktop Icon for disabled entries** is enabled, those static assets become desktop-icon placeholders; otherwise they are deleted. The Live Tile notification itself uses separate generated files under `Assets\Live*.png`.

Changing the **Experimental Live Tile update** tray checkbox queues one asset regeneration and one Appx re-registration before normal Live Tile updates resume. This refreshes Windows' cached static assets after switching modes.

## Release

Prebuilt binary: [GenerateAssets v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/GenerateAssets-v1).

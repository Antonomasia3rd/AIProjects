# DesktopStub / GenerateAssets

`GenerateAssets.exe` is a tray utility that generates Windows Start tile assets from the current desktop wallpaper and registers a loose Appx manifest for a desktop tile entry.

The app can monitor wallpaper changes, fitting mode changes, and DPI scale settings, then regenerate assets and re-register the manifest automatically. Most behavior is configurable through the tray menu and the generated INI file.

## Requirements

- Windows 10/11.
- Visual Studio Build Tools with the C++ workload.
- Developer Mode or sideloading support may be required for Appx registration, depending on Windows policy.

## Build

From the repository root:

```cmd
DesktopStub\BuildGenerateAssets.cmd
```

Output:

```text
DesktopStub\build\GenerateAssets.exe
```

## Source Layout

`GenerateAssets.cpp` is the single translation-unit entry point. Most implementation code is split into ordered fragments under `DesktopStub\src` to keep the codebase reviewable without changing linker behavior:

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
- `ga_registration.inc`: AppX registration and PowerShell fallback handling.
- `ga_generation.inc`: asset generation, polling, and shutdown coordination.
- `ga_tray.inc`: tray menu and tray notifications.
- `ga_app.inc`: window procedure and application startup/shutdown.

## Run

```cmd
DesktopStub\build\GenerateAssets.exe
```

On first launch the app creates `GenerateAssets.ini` next to the executable. The tray menu exposes general settings, notifications, logging, wallpaper fitting, detection methods, assets, DPI scales, manifest settings, and advanced timing/error options.

## Important Features

- Generates tile assets for Store logo, medium tile, square 44 logo, small tile, wide tile, and large tile.
- Supports selected DPI scales plus automatic current-DPI scale generation.
- Detects wallpaper through configurable methods, including slideshow-compatible methods.
- Uses COM Appx registration by default with optional PowerShell-only mode/fallback behavior.
- Can dynamically create `AppxManifest.xml` from configurable manifest defaults.
- Supports quoted INI values and inline comments.
- Keeps detailed logs and exposes PowerShell registration output from the tray.

## Generated Files

Generated/runtime files live under `DesktopStub\build` and are ignored by git:

- `GenerateAssets.ini`
- `GenerateAssets.log`
- `AppxManifest.xml`
- `Assets\*`
- compiler object files under `obj\`

## Release

Prebuilt binary: [GenerateAssets v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/GenerateAssets-v1).

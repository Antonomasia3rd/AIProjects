# capsblink

`capsblink` is a resident Windows utility that blinks the physical Caps Lock LED while Caps Lock is logically off. It provides the same persistent INI, command-line, tray, and per-user Startup-folder surfaces as the other resident AIProjects apps.

The app creates a private DOS device mapping for the configured keyboard class device. Exit through the tray, `--exit`, or Ctrl+C. Cleanup restores the physical LED to the logical Caps Lock state, closes the device handle, and removes the mapping.

## Native integration

DesktopStub's native integration uses the C++ port under
`dependencies/hardware/caps_blink_engine.h` with the separate Windows adapter
`caps_blink_windows.inc`. Its physical actions default off and are independent
of tile/source preview. The port preserves this app's Caps-only pattern,
target identity, and cleanup semantics; the C# standalone remains until host
feature parity is complete.

`tools\TestCapsBlinkEngine.cmd` runs portable fake-device lifecycle/pattern tests
and only compiles the Windows adapter. It never opens a keyboard, changes LEDs,
reads real logical key state, creates a DOS mapping, or registers a hotkey.
Cancellation keeps target ownership until pending writes settle, then restores
the indicator before release. A nonresponsive driver can delay final cleanup;
see [the native Caps audit](../../docs/audit-caps-source.md) for the exact API,
test results, and host shutdown requirements.

## Requirements

- Windows with .NET Framework 4.x.
- Access to the keyboard class device. An elevated console may be required.

## Build

From this folder:

Use the product build so the shared managed INI, logging, Startup, and tray dependencies are compiled into the executable:

```cmd
BuildCapsBlink.cmd
```

The repository-wide build delegates to the same script, packages
`build\capsblink.exe`, and publishes it through the `capsblink-vN` project
release family when capsblink is selected for release.

`capsblink.cs` is intentionally only the product assembly-metadata overlay.
The product-owned resident implementation is compiled from
`dependencies\capsblink\capsblink_app.cs`, where it composes the root-level
managed INI, named-object, logging, Startup, and tray dependencies.

## Run

```cmd
build\capsblink.exe
```

On first launch, the app creates `capsblink.ini` and `capsblink.log` beside the executable. Renaming the executable also changes the default sidecar names and profile identity. Log writes use the shared cross-process UTF-8 writer; failures are reported instead of silently discarded.

```ini
[Settings]
KeyboardTargetPath=\Device\KeyboardClass0
BlinkIntervalMs=500
RunAtStartup=false
ShowTrayIcon=true
ShowMenuAsDropdown=true
```

Every setting is available from the tray. The keyboard target and interval open validated editors; the three common resident settings are checkable menu items. `ShowMenuAsDropdown=false` flattens those items into the top-level menu.

Persistent command-line examples:

```cmd
capsblink.exe --blink-interval-ms 750 --startup --flat-menu
capsblink.exe --set Settings.KeyboardTargetPath=\Device\KeyboardClass1
capsblink.exe --tray
capsblink.exe --configure-only --blink-interval-ms 750 --no-startup
```

Aliases are `--startup`/`--no-startup`, `--tray`/`--no-tray`, and `--dropdown`/`--flat-menu`. Direct long options and `--set Settings.Key=Value` write the same canonical INI values in one batch. If the profile is already resident, the running process reloads after the commit. Add `--configure-only` to save a batch without opening the keyboard device.

Resident control is available through `--reload` and `--exit`. `--help`, `-h`, `/?`, and `--version` are side-effect-free.

`RunAtStartup` uses only a validated shortcut in the current user's `shell:startup` folder. The shortcut has empty arguments so the INI remains authoritative. The shared commit installs before saving `true` and saves `false` before removal, allowing an interrupted change to self-reconcile on the next launch. No registry Run key or scheduled task is used.

The process is single-instance per INI profile and also holds a target-scoped mutex, preventing differently named profiles from racing the same physical indicator.

## Limitations

- The default keyboard class device is `KeyboardClass0`; systems with different keyboard device ordering can select another numbered `KeyboardClass` path.
- `BlinkIntervalMs` must be an integer from `50` through `86400000`; malformed settings are rejected instead of silently falling back.
- Direct keyboard class access may fail under normal user permissions or different keyboard drivers.

## Generated Files

- `build\capsblink.exe`
- `build\capsblink.ini`
- `build\capsblink.log`

Generated build output is ignored by git.

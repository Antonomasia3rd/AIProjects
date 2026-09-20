# CharmTray

Win32 tray launcher for the Windows 8/8.1 immersive shell charm flyouts:

- Search
- Share
- Start
- Devices
- Settings

The implementation uses undocumented COM interfaces and GUID/vtable offsets derived from `CharmBar.exe`. It is not expected to work on Windows 10 or newer because the Windows 8 immersive shell charm infrastructure is no longer present.

`CharmTray.cpp` is a thin product overlay that includes the implementation from
`dependencies\CharmTray\charm_app.inc`. Cross-product tray, INI, logging,
single-instance, and Startup behavior remains in the root shared dependencies.

## Requirements

- Windows 8 or Windows 8.1.
- Visual Studio Build Tools with the C++ workload.

## Build

From the repository root:

```cmd
legacy\CharmTray\BuildCharmTray.cmd
```

From this folder, run `BuildCharmTray.cmd`.

The build script locates MSVC, writes `build\CharmTray.exe`, and supports a syntax-only check:

```cmd
legacy\CharmTray\BuildCharmTray.cmd check
```

## Run

```cmd
build\CharmTray.exe
```

The app creates a tray icon. Click or right-click the icon and choose a charm flyout. Keyboard tray activation is also supported. The same menu can open/reload the effective INI, toggle file logging, and add/remove the app's per-user Startup-folder shortcut. The shortcut is a `.lnk` under `shell:startup`; CharmTray does not use the registry or Task Scheduler for startup.

On first launch, the app atomically creates `CharmTray.ini`. A UTF-8 `CharmTray.log` is written beside the executable when file logging is enabled. If the executable is renamed, the default INI/log names follow the renamed executable. An explicit `--ini` can put the configuration elsewhere, while the default log intentionally remains beside the executable. Invalid boolean values are rejected instead of silently changing behavior.

```ini
[Settings]
LoggingEnabled=1
RunAtStartup=0
```

Both settings accept `0/1`, `true/false`, `yes/no`, or `on/off`. The INI, command-line aliases, and tray checkmarks all use these same persisted values. A manual INI edit is applied by **Reload settings** or the next launch. Startup and the INI batch commit under shared locks: enable installs before saving `1`, disable saves `0` before removal, and an interrupted change self-reconciles on the next launch. Startup reconciliation failures are reported but do not hide an otherwise usable tray app; an explicit CLI/tray change reports failure directly.

## Command line

Help and version output are side-effect-free and work even on newer Windows versions:

```cmd
build\CharmTray.exe --help
build\CharmTray.exe --version
```

Configuration and resident-instance commands are:

```text
--ini <path>                   Use an alternate INI file.
--set Settings.Key=Value       Save LoggingEnabled or RunAtStartup.
--bool Settings.Key=Value      Typed alias for --set.
--logging / --no-logging       Toggle Settings.LoggingEnabled.
--startup / --no-startup       Toggle Settings.RunAtStartup and its .lnk.
--reload                       Ask the running profile instance to reload.
--exit / --quit                Ask the running profile instance to exit.
```

The effective absolute INI path scopes both the single-instance identity and Startup shortcut ownership. Different `--ini` profiles can run independently. A second invocation for the same profile applies persistent settings atomically and sends an acknowledged reload/exit request to the resident instance.

Startup failures for the supported-Windows check, settings file, singleton mutex, COM apartment, message window, or tray icon are reported and cause the process to exit instead of leaving a hidden unusable instance running. Undocumented shell COM failures are logged and shown as a tray notification. The tray icon keeps its hover tooltip and is restored after Explorer restarts.

## Limitations

- Windows 10/11 are out of scope.
- This depends on undocumented Windows 8 shell internals and can break across shell updates.
- The shell COM paths cannot be exercised by current Windows 10/11 CI. Builds and source invariants are checked there; flyout behavior still requires a Windows 8/8.1 runtime test.
- `IShellLinkW` cannot reliably round-trip target/working-directory paths at or above `MAX_PATH`; the shared Startup helper rejects them explicitly.

## Generated Files

- `build\CharmTray.exe`
- `build\CharmTray.ini`
- `build\CharmTray.log`
- compiler object files under `build\obj\` when built by the repository build script

Generated build output is ignored by git.

## Release

Prebuilt binary: [CharmTray v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/CharmTray-v1).

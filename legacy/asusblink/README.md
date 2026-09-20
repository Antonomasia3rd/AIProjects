# asusblink

ASUS ACPI LED/task controller. It talks to the ASUS `ATKACPI` device and can drive:

- mic mute LED states;
- keyboard brightness/backlight states;
- keyboard patterns derived from HDD activity levels;
- tray-controlled pause/resume, startup shortcut, log viewing, and exit.

This is hardware-specific and intended for ASUS laptops with the required ACPI interface available at `\\.\ATKACPI`.

## Requirements

- Windows with .NET Framework 4.x.
- Supported ASUS ACPI driver/interface.
- Administrator rights may be required depending on device access policy.

## Build

From this folder:

```cmd
BuildAsusBlink.cmd
```

The repository build delegates to the same script and writes
`build\asusblink.exe`. `asusblink.cs` is intentionally only the assembly
metadata overlay; the product-owned resident implementation is compiled from
`dependencies\asusblink\asusblink_app.cs`, where it composes the root-level
managed INI, named-object, logging, Startup, and tray dependencies.

## Run

Show built-in help without creating an INI or opening hardware:

```cmd
build\asusblink.exe --help
```

On first operational or persistent-setting launch, the app creates `asusblink.ini` beside the executable. Canonical values live in `[Settings]`; legacy `[Options]` values are validated and migrated. Direct long options, aliases, tray changes, and typed `--set Settings.Key=Value` assignments all update the same INI values atomically before hardware is opened. With no configured events, the default tray remains available for configuration; if the tray is disabled too, the process exits without opening ACPI. The default log path is `asusblink.log` beside the executable; if the executable is renamed, the default INI/log names follow the renamed executable. Log appends use the shared cross-process UTF-8 writer and report persistence failures. Unknown options, missing values, malformed times/states, and unsupported state ranges are rejected before settings or hardware are touched.

Examples:

```cmd
build\asusblink.exe --mic-state 0,1 --mic-interval 200,5000 --mic-duration 60s
build\asusblink.exe --keyboard-state 128,129,130,131 --keyboard-interval 200,100,50,2000 --keyboard-duration 5s
build\asusblink.exe --mic-state 1 --keyboard-state 130
```

HDD activity mapping example:

```cmd
build\asusblink.exe --event1-hdd-state 128,129,130,131,131 --event1-hdd-interval 500ms --event1-hdd-duration 0
```

## Options

- `--mic-state <csv>`: mic LED states, usually `0` or `1`.
- `--mic-interval <csv>`: per-state intervals. Supports `ms`, `s`, `m`, `h`, and `d` suffixes.
- `--mic-duration <time|once>`: total duration. `0` means infinite and `once` means one cycle.
- `--keyboard-state <csv>`: keyboard state values, commonly `128..131`.
- `--keyboard-interval <csv>`: per-state intervals.
- `--keyboard-duration <time|once>`: total duration. `0` means infinite and `once` means one cycle.
- `--eventN-mic-*`, `--eventN-keyboard-*`, `--eventN-hdd-*`: named custom events. The numeric `N` is used as priority.
- `--error-log <path|off>`: write operation logs to a file or disable file logging. Relative paths resolve beside the executable.
- `--error-retry <times>`: retry failed device writes.
- `--error-action <exit,continue,pause,crash,log>`: behavior after repeated errors.
- `--startup` / `--no-startup`: persist `run-at-startup` and synchronize the owned per-user `shell:startup` shortcut.
- `--tray` / `--no-tray`: persist `show-tray`.
- `--dropdown` / `--flat-menu`: persist `show-menu-as-dropdown`.
- `--set Settings.Key=Value`: validate and atomically persist any known option (`Options.Key` remains accepted for migration compatibility).
- `--configure-only`: save and validate changes without opening ACPI or starting the resident UI.
- `--reload` / `--exit`: ask the resident instance for this INI profile to reload or exit.
- `--version`: report the build version without creating an INI or touching hardware.

## Event Scheduling

Events are grouped by physical target device. Same-device events now run through one serialized scheduler ordered by priority instead of writing concurrently. HDD activity events target the keyboard device and participate in the same keyboard schedule.

If a high-priority event has infinite duration, lower-priority events for the same target will not run until it ends or the app exits.

## Tray And Startup

By default the app creates a tray icon. Its shared baseline supplies a product/version header and a length-safe, state-bearing hover tooltip. The menu shows running task details, pause/resume, reload/open-configuration actions, persistent Startup/tray/dropdown settings, log path controls, and exit. The canonical `RunAtStartup`, `ShowTrayIcon`, `ShowMenuAsDropdown`, and `ErrorLog` values are shared by `[Settings]`, direct command-line options, `--set`, and the tray.

Startup is deliberately INI-driven: the shortcut launches the executable with no captured invocation arguments, so later INI edits remain authoritative. The shared managed dependency scopes shortcut ownership to the effective INI, serializes cross-process changes, validates target/arguments/working-directory metadata, atomically replaces owned links, and refuses same-name links that point elsewhere. It installs before saving `RunAtStartup=true` and saves `false` before removal, allowing an interrupted change to self-reconcile on the next launch. Both the original unscoped name and the earlier executable-hash name are migrated or removed only when they target this executable. No registry Run key or scheduled task is used.

Shutdown is cooperative: worker tasks are cancelled and drained before synchronized ACPI cleanup. Device writes are serialized by target, firmware return codes are checked, and worker faults cause a nonzero process result.

## Generated Files

- `build\asusblink.exe`
- `build\asusblink.ini`
- `build\asusblink.log`

Generated files are ignored by git.

## Release

Prebuilt binary: [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1).

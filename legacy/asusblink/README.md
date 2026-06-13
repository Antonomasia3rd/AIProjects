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
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe /optimize+ /out:build\asusblink.exe /r:System.Core.dll /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll /r:System.Runtime.Serialization.dll asusblink.cs
```

The repository build script uses the same compiler and writes `build\asusblink.exe`.

## Run

Show built-in help without creating an INI or opening hardware:

```cmd
build\asusblink.exe --help
```

Running with no configured events also prints help. On first operational launch, the app creates `asusblink.ini` beside the executable. Option names in `[Options]` match the command-line names without the leading `--`, and command-line arguments override INI values. The default log path is `asusblink.log` beside the executable; if the executable is renamed, the default INI/log names follow the renamed executable. Unknown options, missing values, malformed times/states, and unsupported state ranges are rejected.

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
- `--mic-duration <time>`: total duration. `0` means infinite; omitted means one cycle.
- `--keyboard-state <csv>`: keyboard state values, commonly `128..131`.
- `--keyboard-interval <csv>`: per-state intervals.
- `--keyboard-duration <time>`: total duration. `0` means infinite; omitted means one cycle.
- `--eventN-mic-*`, `--eventN-keyboard-*`, `--eventN-hdd-*`: named custom events. The numeric `N` is used as priority.
- `--error-log <path|off>`: write operation logs to a file or disable file logging. Relative paths resolve beside the executable.
- `--error-retry <times>`: retry failed device writes.
- `--error-action <exit,continue,pause,crash,log>`: behavior after repeated errors.
- `--no-tray`: run without creating the tray icon.

## Event Scheduling

Events are grouped by physical target device. Same-device events now run through one serialized scheduler ordered by priority instead of writing concurrently. HDD activity events target the keyboard device and participate in the same keyboard schedule.

If a high-priority event has infinite duration, lower-priority events for the same target will not run until it ends or the app exits.

## Tray And Startup

By default the app creates a tray icon. The tray menu shows running task details, pause/resume, startup shortcut toggle, log path controls, and exit.

The startup shortcut preserves the command-line arguments of the running tray instance. A startup shortcut created from `build\asusblink.exe --keyboard-state 130 --keyboard-duration 0` will relaunch with those arguments at sign-in instead of starting with no work to do. Shortcut names are scoped to the executable path and their actual target is checked, so stale or separately installed copies are not reported as the current app's startup entry. A legacy same-name shortcut is migrated or removed only when it points to the current executable.

Shutdown is cooperative: worker tasks are cancelled and drained before synchronized ACPI cleanup. Device writes are serialized by target, firmware return codes are checked, and worker faults cause a nonzero process result.

## Generated Files

- `build\asusblink.exe`
- `build\asusblink.ini`
- `build\asusblink.log`

Generated files are ignored by git.

## Release

Prebuilt binary: [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1).

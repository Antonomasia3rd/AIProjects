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

Show built-in help:

```cmd
build\asusblink.exe
```

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
- `--mic-check`: check current state and apply only when different.
- `--keyboard-state <csv>`: keyboard state values, commonly `128..131`.
- `--keyboard-interval <csv>`: per-state intervals.
- `--keyboard-duration <time>`: total duration. `0` means infinite; omitted means one cycle.
- `--eventN-mic-*`, `--eventN-keyboard-*`, `--eventN-hdd-*`: named custom events. The numeric `N` is used as priority.
- `--error-log <path|off>`: write operation errors to a file or disable file logging.
- `--error-retry <times>`: retry failed device writes.
- `--error-action <exit,continue,pause,crash,log>`: behavior after repeated errors.
- `--no-tray`: run without creating the tray icon.

## Event Scheduling

Events are grouped by physical target device. Same-device events now run through one serialized scheduler ordered by priority instead of writing concurrently. HDD activity events target the keyboard device and participate in the same keyboard schedule.

If a high-priority event has infinite duration, lower-priority events for the same target will not run until it ends or the app exits.

## Tray And Startup

By default the app creates a tray icon. The tray menu shows running task details, pause/resume, startup shortcut toggle, log path controls, and exit.

The startup shortcut preserves the command-line arguments of the running tray instance. A startup shortcut created from `build\asusblink.exe --keyboard-state 130 --keyboard-duration 0` will relaunch with those arguments at sign-in instead of starting with no work to do.

## Generated Files

- `build\asusblink.exe`
- optional error log path passed through `--error-log`

Generated files are ignored by git.

## Release

Prebuilt binary: [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1).

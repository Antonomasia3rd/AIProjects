# asusblink

ASUS ACPI LED/task controller. It talks to the ASUS `ATKACPI` device and can drive mic mute LED, keyboard brightness LED states, and optional HDD/activity-derived blink patterns.

This is hardware-specific. It is intended for supported ASUS laptops with the required ACPI interface/driver available at `\\.\ATKACPI`.

## Requirements

- Windows with .NET Framework 4.x.
- Supported ASUS ACPI driver/interface.
- Administrator rights may be required depending on device access policy.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:winexe /optimize+ /out:build\asusblink.exe /r:System.Windows.Forms.dll /r:System.Drawing.dll /r:System.Management.dll /r:System.Runtime.Serialization.dll asusblink.cs
```

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

Useful options include:

- `--mic-state`, `--mic-interval`, `--mic-duration`, `--mic-check`
- `--keyboard-state`, `--keyboard-interval`, `--keyboard-duration`
- `--event1-*`, `--event2-*`, etc. for custom events
- `--error-log <path|off>`
- `--error-retry <times>`
- `--error-action <exit,continue,pause,crash,log>`
- `--no-tray`

## Tray

By default the app creates a tray icon. The tray menu shows running task details, pause/resume, startup shortcut toggle, log path controls, and exit.

## Release

Prebuilt binary: [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1).

# capsblink

Small console experiment that blinks the physical Caps Lock LED while Caps Lock is off.

It creates a per-process temporary DOS device mapping to `\Device\KeyboardClass0`, opens the keyboard class device, and toggles the Caps Lock indicator every 500 ms by default. Ctrl+C interrupts the current wait and exits through a cleanup path that restores the physical LED to the logical Caps Lock state, closes the device handle, and removes the DOS device mapping.

## Requirements

- Windows with .NET Framework 4.x.
- Access to the keyboard class device. An elevated console may be required.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:exe /optimize+ /out:build\capsblink.exe capsblink.cs
```

The repository build script also packages `build\capsblink.exe`, but no GitHub release is currently published for this experiment.

## Run

```cmd
build\capsblink.exe
```

Stop with Ctrl+C so the cleanup handler can close the device handle and remove the temporary DOS device mapping.

On first launch, the app creates `capsblink.ini` and `capsblink.log` beside the executable. If the executable is renamed, the default INI/log names follow the renamed executable.

The process is single-instance per keyboard target, preventing two copies from racing the same physical indicator. Unknown command-line arguments are rejected before the INI or hardware is touched; `--help`, `-h`, and `/?` show usage without side effects.

## Limitations

- The default keyboard class device is `KeyboardClass0`; systems with different keyboard device ordering can set `[Settings] KeyboardTargetPath` in the local INI.
- `BlinkIntervalMs` must be an integer from `50` through `86400000`; malformed settings are rejected instead of silently falling back.
- Direct keyboard class access may fail under normal user permissions or different keyboard drivers.

## Generated Files

- `build\capsblink.exe`
- `build\capsblink.ini`
- `build\capsblink.log`

Generated build output is ignored by git.

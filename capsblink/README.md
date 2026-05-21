# capsblink

Small console experiment that blinks the physical Caps Lock LED while Caps Lock is off.

It creates a per-process temporary DOS device mapping to `\Device\KeyboardClass0`, opens the keyboard class device, and toggles the Caps Lock indicator every 500 ms. Ctrl+C exits through a cleanup path that closes the device handle and removes the DOS device mapping.

## Requirements

- Windows with .NET Framework 4.x.
- Access to the keyboard class device. An elevated console may be required.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:exe /optimize+ /out:build\capsblink.exe capsblink.cs
```

## Run

```cmd
build\capsblink.exe
```

Stop with Ctrl+C so the cleanup handler can close the device handle and remove the temporary DOS device mapping.

## Notes

- This project currently has no GitHub release asset.
- The target keyboard class device is hardcoded to `KeyboardClass0`; systems with different keyboard device ordering may need code changes.

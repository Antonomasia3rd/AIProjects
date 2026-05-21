# YourPhoneHideBanner

Windows service that watches Phone Link notification registry entries for loaded users and suppresses matching notification banners/sounds.

It targets notification setting keys whose names start with:

```text
Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_
```

For matching keys it sets:

- `ShowBanner` to `0`;
- `SoundFile` to an empty string.

## Requirements

- Windows with .NET Framework 4.x.
- Administrator rights to install/start the service and create the Event Log source.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:exe /optimize+ /out:build\YourPhoneHideBanner.exe /r:System.ServiceProcess.dll YourPhoneHideBanner.cs
```

The service class is `YourPhoneHideBannerService`; the installed Windows service name is also `YourPhoneHideBannerService`.

## Install

Run from an elevated command prompt:

```cmd
sc.exe create YourPhoneHideBannerService binPath= "%CD%\build\YourPhoneHideBanner.exe" start= auto
sc.exe start YourPhoneHideBannerService
```

## Uninstall

Run from an elevated command prompt:

```cmd
sc.exe stop YourPhoneHideBannerService
sc.exe delete YourPhoneHideBannerService
```

## Runtime Behavior

- Watches `HKEY_USERS` for newly loaded user hives.
- Attaches to loaded `S-1-5-21-*` user hives.
- Watches `HKU\<SID>\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings` for child-key and value changes.
- Reapplies the banner/sound values when matching keys are created or changed.
- Logs to the Windows Application Event Log using source `YourPhoneHideBannerService`.

## Generated Files

- `build\YourPhoneHideBanner.exe`

Generated build output is ignored by git.

## Release

Prebuilt binary: [YourPhoneHideBanner v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/YourPhoneHideBanner-v1).

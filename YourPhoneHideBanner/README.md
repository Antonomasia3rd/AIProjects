# YourPhoneHideBanner

Windows service that watches Phone Link notification registry entries and suppresses banners/sounds for matching notification keys.

It targets notification settings whose key starts with:

```text
Microsoft.YourPhone_8wekyb3d8bbwe!YourPhoneNotifications_
```

For those keys it sets:

- `ShowBanner` to `0`
- `SoundFile` to an empty string

## Requirements

- Windows with .NET Framework 4.x.
- Administrator rights to install/start the service and create the Event Log source.

## Build

From this folder:

```cmd
mkdir build 2>nul
C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe /nologo /target:exe /optimize+ /out:build\YourPhoneHideBanner.exe /r:System.ServiceProcess.dll YourPhoneHideBanner.cs
```

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

## Notes

- Logs are written to the Windows Application Event Log using source `YourPhoneHideBannerService`.
- The service watches loaded user hives under `HKU\<SID>\Software\Microsoft\Windows\CurrentVersion\Notifications\Settings`.
- The release build is available at [YourPhoneHideBanner v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/YourPhoneHideBanner-v1).

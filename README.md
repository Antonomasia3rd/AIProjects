# AIProjects

A collection of small Windows utility projects and experiments. Most projects are standalone: each folder contains its source, optional build script, and a project-level `README.md`.

## Prebuilt Releases

Prebuilt executables are published as GitHub releases when a project has a compiled binary:

| Project | Release | Purpose |
| --- | --- | --- |
| AllowContentAboveLock | [AllowContentAboveLock v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/AllowContentAboveLock-v1) | Service that keeps notification `AllowContentAboveLock` registry values enabled for loaded users. |
| asusblink | [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1) | ASUS ACPI LED/task controller for mic, keyboard, and activity blink patterns. |
| CharmTray | [CharmTray v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/CharmTray-v1) | Windows 8/8.1 tray launcher for charm flyouts. |
| GenerateAssets | [GenerateAssets v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/GenerateAssets-v1) | Desktop tile asset generator and Appx manifest registrar. |
| DiscordRPC | [DiscordRPC v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/DiscordRPC-v1) | Discord Rich Presence tray app with IPC and Gateway modes. |
| NowPlayingTile | [NowPlayingTile v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/NowPlayingTile-v1) | SMTC-based Windows Start live tile updater. |
| SecureDesktopLauncher | [SecureDesktopLauncher v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/SecureDesktopLauncher-v1) | Secure desktop service and password-gated launcher. |
| YourPhoneHideBanner | [YourPhoneHideBanner v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/YourPhoneHideBanner-v1) | Service that suppresses Phone Link notification banners/sounds. |

## Projects

| Folder | Type | Notes |
| --- | --- | --- |
| `AllowContentAboveLock` | C# Windows service | Requires administrator rights to install and write Event Log sources. |
| `asusblink` | C# tray/console app | Hardware-specific ASUS ACPI tool. |
| `capsblink` | C# console app | Raw keyboard LED experiment; no release is currently published. |
| `CharmTray` | C++ Win32 tray app | Intended only for Windows 8/8.1. |
| `DesktopStub` | C++ Win32 tray app | Builds `GenerateAssets.exe`. |
| `DiscordRPC` | C# tray/console app | Builds to `DiscordRPC\build`. |
| `DNSAutoUpdate` | PowerShell script | Requires Windows DNS Server PowerShell cmdlets. |
| `NowPlayingTile` | C# app plus loose Appx registration scripts | Builds generated files under `NowPlayingTile\build`. |
| `PhotoCollage` | PowerShell script | Creates a simple image grid/collage. |
| `RealTimeNotesDeskband` | C++ Win32 Deskband DLL | Classic taskbar toolbar for HoYoLAB Real-Time Notes; local upstream/reference dumps are ignored. |
| `SecureDesktopLauncher` | C++ Windows service/tools | Detailed docs are in the project folder. |
| `TaskSchedulerMigration` | PowerShell script | Re-registers scheduled tasks from an old SID to a new user. |
| `YourPhoneHideBanner` | C# Windows service | Requires administrator rights to install and write Event Log sources. |

## Build Policy

Generated outputs belong in project `build` folders and are ignored by git. Source files, scripts, manifests/templates, and READMEs are tracked. Prebuilt binaries should be distributed through GitHub Releases rather than committed to the repository.

GitHub Actions builds Windows binaries on GitHub-hosted runners. Each run uploads zipped workflow artifacts, and tag/release builds attach those same ZIP files to the matching GitHub Release. This keeps published binaries tied to a public workflow run and commit instead of a local machine build.

Common prerequisites:

- Windows 10/11 for most projects, except `CharmTray`, which targets Windows 8/8.1 shell internals.
- .NET Framework compiler at `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe` for C# projects.
- Visual Studio Build Tools with the C++ workload for C++ projects.
- PowerShell 5.1 or newer for script projects.

Some tools modify system settings, services, scheduled tasks, DNS records, or secure-desktop processes. Read the project README before running them, and use an elevated shell where documented.

## License

This repository is released under the [0BSD license](LICENSE). You can use, copy, modify, and distribute it for any purpose, with or without fee.

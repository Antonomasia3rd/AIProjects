# AIProjects

Small Windows utility projects and experiments. Most folders are standalone and contain the source, build scripts, and a project-level `README.md`.

## Projects

| Folder | Runtime | Purpose |
| --- | --- | --- |
| `AllowContentAboveLock` | C# Windows service | Keeps notification `AllowContentAboveLock` registry values enabled for loaded users. |
| `asusblink` | C# tray/console app | ASUS ACPI LED controller for mic LED, keyboard backlight states, and HDD-activity keyboard patterns. |
| `capsblink` | C# console app | Raw keyboard class-device experiment that blinks the physical Caps Lock indicator. |
| `CharmTray` | C++ Win32 tray app | Windows 8/8.1 tray launcher for Search, Share, Start, Devices, and Settings charms. |
| `DesktopStub` | C++ Win32 tray app | Builds `GenerateAssets.exe`, a desktop wallpaper tile-asset generator and loose Appx registrar. |
| `DiscordRPC` | C# tray/console app | Discord Rich Presence app with IPC, optional Gateway mode, dynamic placeholders, and a tray config UI. |
| `DNSAutoUpdate` | PowerShell loop | Keeps selected Windows DNS Server A records aligned with current server IPv4 addresses. |
| `NowPlayingTile` | C# app plus Appx scripts | SMTC-based Windows Start live tile updater with optional widget mode. |
| `PhotoCollage` | PowerShell script | Creates a simple JPEG grid/collage from images in a folder. |
| `RealTimeNotesDeskband` | C++ Deskband DLL | Classic taskbar toolbar for HoYoLAB Real-Time Notes resources. |
| `SecureDesktopLauncher` | C++ service/tools | Launches trusted configured programs on secure desktops, with an optional password-gated launcher. |
| `TaskSchedulerMigration` | PowerShell script | Re-registers scheduled tasks from an old SID to a new user/account. |
| `YourPhoneHideBanner` | C# Windows service | Suppresses Phone Link notification banners and sounds for loaded users. |

## Prebuilt Releases

Prebuilt executables are published through GitHub Releases for projects that produce binaries:

| Project | Release |
| --- | --- |
| AllowContentAboveLock | [AllowContentAboveLock v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/AllowContentAboveLock-v1) |
| asusblink | [asusblink v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/asusblink-v1) |
| CharmTray | [CharmTray v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/CharmTray-v1) |
| GenerateAssets | [GenerateAssets v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/GenerateAssets-v1) |
| DiscordRPC | [DiscordRPC v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/DiscordRPC-v1) |
| NowPlayingTile | [NowPlayingTile v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/NowPlayingTile-v1) |
| SecureDesktopLauncher | [SecureDesktopLauncher v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/SecureDesktopLauncher-v1) |
| YourPhoneHideBanner | [YourPhoneHideBanner v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/YourPhoneHideBanner-v1) |

Projects without a release entry are source/script utilities or experiments.

## Build

Common prerequisites:

- Windows 10/11 for most projects.
- Windows 8 or 8.1 for `CharmTray`.
- .NET Framework compiler at `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe` for C# projects.
- Visual Studio Build Tools with the C++ workload for MSVC projects.
- MinGW-w64 `g++` for `RealTimeNotesDeskband`.
- PowerShell 5.1 or newer for script projects and build wrappers.

Build all packaged Windows artifacts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\.github\scripts\build-windows.ps1
```

Useful build options:

```powershell
# Skip one or more projects.
powershell -NoProfile -ExecutionPolicy Bypass -File .\.github\scripts\build-windows.ps1 -SkipProjects asusblink,RealTimeNotesDeskband

# Stop running output EXEs if they block overwrite.
powershell -NoProfile -ExecutionPolicy Bypass -File .\.github\scripts\build-windows.ps1 -StopRunningArtifacts

# Also allow name-based process stopping when exact executable paths cannot be resolved.
powershell -NoProfile -ExecutionPolicy Bypass -File .\.github\scripts\build-windows.ps1 -StopRunningArtifacts -StopMatchingArtifactNames
```

Each project README also lists direct build commands for that project. Generated outputs belong in project `build` folders and are ignored by git.

## Safety

Several tools intentionally modify system state:

- services write user-hive notification settings and local `.log` files beside their executables;
- `DNSAutoUpdate` adds and removes exact DNS A records in its managed allowlist;
- `SecureDesktopLauncher` can launch processes as `LocalSystem` on secure desktops;
- `TaskSchedulerMigration` re-registers matching scheduled tasks;
- Appx scripts register or unregister loose development packages.

Read the project README before running a tool, use an elevated shell where documented, and use `-WhatIf` for PowerShell scripts that support it.

## Repository Policy

Tracked files are source, build scripts, templates/manifests, and documentation. Local configs, logs, generated assets, binaries, object files, and release ZIPs are ignored.

Runtime configuration and logs must stay local to each program. By default, every binary must use files beside itself with the same base name as the binary:

```text
<binary directory>\<binary name>.ini
<binary directory>\<binary name>.log
```

If a non-INI configuration format is unavoidable, it must still default to the same directory and base name as the binary. Script-only tools should use the script directory and script base name by default. Do not use the registry, `%APPDATA%`, `%LOCALAPPDATA%`, `%PROGRAMDATA%`, the process working directory, or other global/user profile locations for app-owned configuration or logs. Registry writes are acceptable only for OS integration that is the explicit purpose of the tool, such as service registration, COM/deskband registration, scheduled-task migration, or Windows settings the tool is designed to manage.

Use `DesktopStub\GenerateAssets` as the reference pattern for new fixes: create and normalize the INI next to the executable, preserve user-edited values, write the log next to the executable by default, expose path changes through the INI/UI when needed, and report write failures clearly. When changing an existing program that already has registry/profile-based state, preserve or migrate existing user values where practical and stop for maintainer input before choosing a compatibility-breaking migration.

GitHub Actions builds Windows binaries on hosted runners. Workflow artifacts and release attachments are generated from the workflow run so published binaries are tied to a commit.

## License

This repository is released under the [0BSD license](LICENSE). You can use, copy, modify, and distribute it for any purpose, with or without fee.

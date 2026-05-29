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

Prebuilt Windows binaries are published automatically through GitHub Releases for projects that produce binaries.

Release tag families:

| Change scope | Release tag family |
| --- | --- |
| Any built project | `<Project>-vN`, for example `DesktopStub-v1`, `DiscordRPC-v1`, or `asusblink-v1` |
| Shared workflow/repository files changed | one release per built project, each in that project's `<Project>-vN` family |
| Manual workflow run with `All` selected | one release per built project, each in that project's `<Project>-vN` family |

The `DesktopStub` release contains `GenerateAssets.exe` and `GenerateAssetsLiveTileBroker.exe`; the release family is still `DesktopStub-vN` so it matches the folder and repository project name.

GitHub Actions workflow artifacts are also available from each workflow run. GitHub downloads each workflow artifact as an archive, but the artifact payload and release assets are direct project files rather than project-created release ZIPs. SHA256 checksums are written to the workflow summary and to release notes instead of being uploaded as separate `.sha256` files.

Published binaries are unsigned. Windows SmartScreen or antivirus tools may warn on first run; verify the release-note SHA256 hash or build from source if preferred.

## Build

Common prerequisites:

- Windows 10/11 for most projects.
- Windows 8 or 8.1 for `CharmTray`.
- .NET Framework compiler at `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe` for C# projects.
- Visual Studio Build Tools with the C++ workload for MSVC projects.
- MinGW-w64 `g++` for `RealTimeNotesDeskband`.
- PowerShell 5.1 or newer for script-only utilities and Appx helper scripts.

Build all Windows binary artifacts:

```cmd
.github\scripts\build-windows.cmd
```

Useful build options:

```cmd
# Skip one or more projects.
.github\scripts\build-windows.cmd /skip:asusblink,RealTimeNotesDeskband

# Skip DesktopStub / GenerateAssets.
.github\scripts\build-windows.cmd /skip:DesktopStub
```

Run repository validation and smoke checks:

```powershell
.github\scripts\Validate-ProjectMap.ps1
.github\scripts\Test-WorkflowProjectSelection.ps1
.github\scripts\Invoke-PolicyWarnings.ps1
.github\scripts\Test-ReadmeConsistency.ps1
.github\scripts\Smoke-WindowsBuild.ps1
```

The Windows workflow project metadata lives in `.github/project-map.json`. Keep that map, `.github/workflows/build-windows.yml`, `.github/scripts/build-windows.cmd`, and this README in sync when adding or removing projects that produce Windows artifacts. The workflow validates the project map before building.

Each project README also lists direct build commands for that project. Generated outputs belong in project `build` folders and are ignored by git. If a compiler cannot overwrite a running EXE, close that program and rerun the build.

## Safety

Several tools intentionally modify system state:

- services write user-hive notification settings and local `.log` files beside their executables;
- `DNSAutoUpdate` adds and removes exact DNS A records in its managed allowlist;
- `SecureDesktopLauncher` can launch processes as `LocalSystem` on secure desktops;
- `TaskSchedulerMigration` re-registers matching scheduled tasks;
- Appx scripts register or unregister loose development packages.

Read the project README before running a tool, use an elevated shell where documented, and use `-WhatIf` for PowerShell scripts that support it.

## Repository Policy

Tracked files are source, build scripts, templates/manifests, and documentation. Local configs, logs, generated assets, binaries, object files, and generated release archives are ignored.

Runtime configuration and logs must stay local to each program. By default, every binary must use `.ini` and `.log` files beside itself with the same base name as the binary:

```text
<binary directory>\<binary name>.ini
<binary directory>\<binary name>.log
```

If a non-INI configuration format is unavoidable, it must still default to the same directory and base name as the binary. Script-only tools should use the script directory and script base name by default. Do not use the registry, `%APPDATA%`, `%LOCALAPPDATA%`, `%PROGRAMDATA%`, the process working directory, or other global/user profile locations for app-owned configuration or logs. Registry writes are acceptable only for OS integration that is the explicit purpose of the tool, such as service registration, COM/deskband registration, scheduled-task migration, or Windows settings the tool is designed to manage.

Do not change file or directory ACLs from these tools, installers, build scripts, or migration helpers. Past ACL-hardening attempts caused Windows integration failures in specific placements, including Start Menu related cases. Security checks may detect and warn about risky writable locations, but they must not modify ACLs, ownership, inheritance, integrity labels, or other access-control state.

Use `DesktopStub` / `GenerateAssets.exe` as the reference pattern for new fixes: create and normalize the INI next to the executable, preserve user-edited values, write the log next to the executable by default, expose path changes through the INI/UI when needed, and report write failures clearly. When changing another program, follow that implementation style for local config/log handling unless the maintainer explicitly approves a different pattern.

Stop for maintainer input before making a decision that changes storage location, config/log format, migration behavior, compatibility guarantees, ACL/security enforcement, or OS integration behavior. When changing an existing program that already has registry/profile-based state, preserve or migrate existing user values where practical and do not choose a compatibility-breaking migration without maintainer approval.

GitHub Actions builds Windows binaries on hosted runners. Workflow artifacts and release attachments are generated from the workflow run so published binaries are tied to a commit.

## License

This repository is released under the [0BSD license](LICENSE). You can use, copy, modify, and distribute it for any purpose, with or without fee.

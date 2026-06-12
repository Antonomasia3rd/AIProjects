# AIProjects

Small Windows utility projects and experiments. Active baseline projects live at the repository root and share the reusable C++ entry point `dependencies/desktop_app_baseline.h`. Older projects that have not moved onto that shared baseline live under `legacy/` but keep their original project/release names.

## Projects

| Folder | Runtime | Purpose |
| --- | --- | --- |
| `legacy/AllowContentAboveLock` | C# Windows service | Keeps notification `AllowContentAboveLock` registry values enabled for loaded users. |
| `legacy/asusblink` | C# tray/console app | ASUS ACPI LED controller for mic LED, keyboard backlight states, and HDD-activity keyboard patterns. |
| `legacy/capsblink` | C# console app | Raw keyboard class-device experiment that blinks the physical Caps Lock indicator. |
| `legacy/CharmTray` | C++ Win32 tray app | Windows 8/8.1 tray launcher for Search, Share, Start, Devices, and Settings charms. |
| `DesktopStub` | C++ Win32 tray app | Builds `DesktopStub.exe`, a desktop wallpaper tile-asset generator and loose Appx registrar. |
| `DiscordRPC` | C++ Win32 tray/console app | Discord Rich Presence app with Discord IPC, Gateway transport, DPAPI token storage, dynamic placeholders, and a tray config UI. |
| `legacy/DNSAutoUpdate` | C# DNS updater | Keeps selected Windows DNS Server A records aligned with current server IPv4 addresses. |
| `legacy/NowPlayingTile` | C++ app plus Appx helpers | SMTC-based Windows Start live tile updater with optional widget mode. |
| `legacy/PhotoCollage` | C# console app | Creates a simple JPEG grid/collage from images in a folder. |
| `legacy/RealTimeNotesDeskband` | C++ Deskband DLL | Classic taskbar toolbar for HoYoLAB Real-Time Notes resources. |
| `RssLiveTile` | C++ Win32 resident app | RSS/Atom feed reader that periodically queues Windows 10 Live Tile entries through a loose Desktop Bridge package. |
| `legacy/SecureDesktopLauncher` | C++ service/tools | Launches trusted configured programs on secure desktops, with an optional password-gated launcher. |
| `legacy/TaskSchedulerMigration` | C# Task Scheduler utility | Re-registers scheduled tasks from an old SID to a new user/account. |
| `legacy/WindhawkMods` | Windhawk C++ mods | Source-only local Windhawk mods: Always UIAccess, AppsFolder Unhide Hidden Apps, and Snipping Tool Border Fix. |
| `legacy/YourPhoneHideBanner` | C# Windows service | Suppresses Phone Link notification banners and sounds for loaded users. |

## Prebuilt Releases

Prebuilt Windows binaries are published automatically through GitHub Releases for projects that produce binaries.

Release tag families:

| Change scope | Release tag family |
| --- | --- |
| Any built project | `<Project>-vN`, for example `DesktopStub-v1`, `DiscordRPC-v1`, or `asusblink-v1` |
| Shared workflow/repository files changed | one release per built project, each in that project's `<Project>-vN` family |
| Manual workflow run with `All` selected | one release per built project, each in that project's `<Project>-vN` family |

The `DesktopStub` release contains `DesktopStub.exe` and `DesktopStubLiveTileBroker.exe`; the release family is still `DesktopStub-vN` so it matches the folder and repository project name.

GitHub Actions workflow artifacts are also available from each workflow run. GitHub downloads each workflow artifact as an archive, but the artifact payload and release assets are direct project files rather than project-created release ZIPs. SHA256 checksums are written to the workflow summary and to release notes instead of being uploaded as separate `.sha256` files.

Published binaries are unsigned. Windows SmartScreen or antivirus tools may warn on first run; verify the release-note SHA256 hash or build from source if preferred.

## Build

Common prerequisites:

- Windows 10/11 for most projects.
- Windows 8 or 8.1 for `legacy/CharmTray`.
- .NET Framework compiler at `C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe` for C# projects.
- Visual Studio Build Tools with the C++ workload for MSVC projects.
- MinGW-w64 `g++` for `legacy/RealTimeNotesDeskband`.
- `dnscmd.exe` for `legacy/DNSAutoUpdate` on Windows DNS Server systems.

Build all Windows binary artifacts:

```cmd
.github\scripts\build-windows.cmd
```

`legacy/WindhawkMods` contains source-only `.wh.cpp` files that are imported, compiled,
and loaded by Windhawk. They are not built by the repository Windows workflow.

Useful build options:

```cmd
# Skip one or more projects.
.github\scripts\build-windows.cmd /skip:asusblink,RealTimeNotesDeskband

# Skip DesktopStub.
.github\scripts\build-windows.cmd /skip:DesktopStub

# Skip RssLiveTile.
.github\scripts\build-windows.cmd /skip:RssLiveTile
```

Run repository validation and smoke checks:

```cmd
tools\TestSharedBaseline.cmd
.github\scripts\Validate-ProjectMap.cmd
.github\scripts\Test-WorkflowProjectSelection.cmd
.github\scripts\Invoke-PolicyWarnings.cmd
.github\scripts\Test-ReadmeConsistency.cmd
.github\scripts\Smoke-WindowsBuild.cmd
```

The Windows workflow project metadata lives in `.github/project-map.json`. Keep that map, `.github/workflows/build-windows.yml`, `.github/scripts/build-windows.cmd`, and this README in sync when adding or removing projects that produce Windows artifacts. Validation checks selector options, upload conditions, artifact names, declared artifact payload paths, and build-script artifact recording before building.

Each project README also lists direct build commands for that project. Generated outputs belong in project `build` folders and are ignored by git. If a compiler cannot overwrite a running EXE, close that program and rerun the build.

## Safety

Several tools intentionally modify system state:

- services write user-hive notification settings and local `.log` files beside their executables;
- `legacy/DNSAutoUpdate` adds and removes exact DNS A records in its managed allowlist;
- `legacy/SecureDesktopLauncher` can launch processes as `LocalSystem` on secure desktops;
- `legacy/TaskSchedulerMigration` re-registers matching scheduled tasks;
- Appx helpers register or unregister loose development packages.

Read the project README before running a tool, use an elevated shell where documented, and use `-WhatIf` or `--what-if` for tools that support preview mode.

## Repository Policy

Tracked files are source, build scripts, templates/manifests, and documentation. Local configs, logs, generated assets, binaries, object files, and generated release archives are ignored.

Shared C++ projects must use the repository baseline INI dialect from `dependencies\config_ini.inc`: UTF-8 with BOM, quoted assignments like `"Name" = "Value"`, comment/order preservation where practical, and raw Windows path backslashes preserved on read. DesktopStub's INI style is the compatibility standard; new shared helpers must not drift to a different config dialect.

Runtime configuration and logs must stay local to each program. By default, every binary must use `.ini` and `.log` files beside itself with the same base name as the binary:

```text
<binary directory>\<binary name>.ini
<binary directory>\<binary name>.log
```

If a non-INI configuration format is unavoidable, it must still default to the same directory and base name as the binary. Helper utilities should keep their generated logs and state beside the helper executable or project wrapper by default. Do not use the registry, `%APPDATA%`, `%LOCALAPPDATA%`, `%PROGRAMDATA%`, the process working directory, or other global/user profile locations for app-owned configuration or logs. Registry writes are acceptable only for OS integration that is the explicit purpose of the tool, such as service registration, COM/deskband registration, scheduled-task migration, or Windows settings the tool is designed to manage.

Do not change file or directory ACLs from these tools, installers, build scripts, or migration helpers. Past ACL-hardening attempts caused Windows integration failures in specific placements, including Start Menu related cases. Security checks may detect and warn about risky writable locations, but they must not modify ACLs, ownership, inheritance, integrity labels, or other access-control state.

Use `DesktopStub` / `DesktopStub.exe` as the reference behavior pattern for new fixes: create and normalize the INI next to the executable, preserve user-edited values/comments/order where practical, write INI assignments in the DesktopStub style (`"Name" = "Value"`), write the log next to the executable by default, expose path changes through the INI/UI when needed, and report write failures clearly. Shared helpers in `dependencies/` should be preferred for new projects so DesktopStub-specific code does not get copied as a second framework. When changing another program, follow that implementation style for local config/log handling unless the maintainer explicitly approves a different pattern.

Stop for maintainer input before making a decision that changes storage location, config/log format, migration behavior, compatibility guarantees, ACL/security enforcement, or OS integration behavior. When changing an existing program that already has registry/profile-based state, preserve or migrate existing user values where practical and do not choose a compatibility-breaking migration without maintainer approval.

GitHub Actions builds Windows binaries on hosted runners. Workflow artifacts and release attachments are generated from the workflow run so published binaries are tied to a commit.

## License

This repository is released under the [0BSD license](LICENSE). You can use, copy, modify, and distribute it for any purpose, with or without fee.

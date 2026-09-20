# AIProjects

Small Windows utility projects and experiments. Stable project/build folders live
at the repository root and under `legacy/`, preserving their existing product
and release names. Reusable modules and product implementation bodies live under
`dependencies/`; project-local source composes them through includes, metadata,
or declarative policy.

## Projects

| Folder | Runtime | Purpose |
| --- | --- | --- |
| `legacy/AllowContentAboveLock` | C# Windows service | Keeps notification `AllowContentAboveLock` registry values enabled for loaded users. |
| `legacy/ADBController` | C++ Win32 GUI app | Direct ADB-over-TCP TV controller that keeps the ADB server running while switching selected TVs. |
| `legacy/asusblink` | C# tray/console app | ASUS ACPI LED controller for mic LED, keyboard backlight states, and HDD-activity keyboard patterns. |
| `legacy/capsblink` | C# tray app | Raw keyboard class-device experiment that blinks the physical Caps Lock indicator. |
| `legacy/CharmTray` | C++ Win32 tray app | Windows 8/8.1 tray launcher for Search, Share, Start, Devices, and Settings charms. |
| `legacy/ChromeProfileCounter` | PowerShell utility | Inspects and repairs Chrome's local profile counter. |
| `DesktopStub` | C++ Win32 tray app | Builds `DesktopStub.exe`, a Live Tile generator: wallpaper by default, or ordered content with image/wallpaper backgrounds and RSS/custom/SMTC/Caps Lock text; see `docs/content-engine.md`. Also a loose Appx registrar. |
| `DiscordRPC` | C++ Win32 tray/console app | Discord Rich Presence app with Discord IPC, Gateway transport, DPAPI token storage, dynamic placeholders, and a tray config UI. |
| `legacy/DNSAutoUpdate` | C# DNS updater | Keeps selected Windows DNS Server A records aligned with current server IPv4 addresses. |
| `legacy/NowPlayingTile` | C++ app plus Appx helpers | SMTC-based Windows Start live tile updater with optional widget mode. |
| `legacy/PhotoCollage` | C# console app | Creates a simple JPEG grid/collage from images in a folder. |
| `legacy/RealTimeNotesDeskband` | C++ Deskband DLL | Classic taskbar toolbar for HoYoLAB Real-Time Notes resources. |
| `legacy/SecureDesktopLauncher` | C++ service/tools | Launches trusted configured programs on secure desktops, with an optional password-gated launcher. |
| `legacy/TaskSchedulerMigration` | C# Task Scheduler utility | Re-registers scheduled tasks from an old SID to a new user/account. |
| `legacy/WindhawkMods` | Windhawk C++ mods | Source-only local Windhawk mods: Always UIAccess, AppsFolder Unhide Hidden Apps, and Snipping Tool Border Fix. |
| `legacy/WindhawkMods/LockScreenWin10` | Windhawk research mod | Windows 10 lock-screen styling and XAML investigation sources. |
| `legacy/YourPhoneHideBanner` | C# Windows service | Suppresses Phone Link notification banners and sounds for loaded users. |
| `legacy/YouTubeMusicMigrate` | PowerShell utility | Local YouTube Music library and playlist tidy/migration tooling. |

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
- Android Platform Tools for `legacy/ADBController` at runtime (`adb.exe` is not bundled).
- Visual Studio 2019/2022 Build Tools or MinGW-w64 `g++` for `legacy/RealTimeNotesDeskband`.
- `dnscmd.exe` for `legacy/DNSAutoUpdate` on Windows DNS Server systems.

Build all Windows binary artifacts:

```cmd
.github\scripts\build-windows.cmd
```

`legacy/WindhawkMods` contains source-only `.wh.cpp` files that are imported, compiled,
and loaded by Windhawk. They are not built by the repository Windows workflow.
An installed Windhawk compiler can validate both x64 and x86 sources with
`legacy\WindhawkMods\TestWindhawkMods.cmd`.

`legacy/RssLiveTile` no longer exists -- it was retired and removed once its
capability landed as DesktopStub's `ContentSource=RssFeed` (see
`dependencies/README.md`); its history is in git, not in the working tree.

Useful build options:

```cmd
rem Skip one or more projects.
.github\scripts\build-windows.cmd /skip:asusblink,RealTimeNotesDeskband

rem Skip DesktopStub.
.github\scripts\build-windows.cmd /skip:DesktopStub

rem Skip DiscordRPC.
.github\scripts\build-windows.cmd /skip:DiscordRPC
```

Run repository validation and smoke checks:

```cmd
tools\TestSharedBaseline.cmd
tools\TestLegacyUtilities.cmd
tools\TestRegistryNotificationServices.cmd
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

- registry-notification services write user-hive settings and report diagnostics through the Windows Event Log/debug output; privileged services do not create sidecar logs;
- `legacy/DNSAutoUpdate` adds and removes exact DNS A records in its managed allowlist;
- `legacy/SecureDesktopLauncher` can launch processes as `LocalSystem` on secure desktops;
- `legacy/TaskSchedulerMigration` re-registers matching scheduled tasks;
- Appx helpers register or unregister loose development packages.

Read the project README before running a tool, use an elevated shell where documented, and use `-WhatIf` or `--what-if` for tools that support preview mode.

## Repository direction and current gaps

Implementation belongs in `dependencies/`; project folders should supply entry points, resources and values. DesktopStub is the reference implementation, with defects fixed before its behavior is shared. Moving entire app bodies into product-named includes is only an intermediate migration step.

Every app should expose consistent INI, CLI and tray settings. Ordinary startup must use the current user's `shell:startup` folder. Packaged startup may use Windows StartupTask and needs its own clearly named control. These requirements are not yet met by every service, deskband, foreground utility and mod; the [persistent audit](docs/REWORK_AUDIT.md) lists concrete gaps and validation evidence.

Ease of setup takes priority. Additional DPAPI, path/hash checks and other security enforcement should be opt-in, after functional issues are resolved. Some existing programs still enforce protection automatically; their READMEs identify that current behavior instead of presenting it as the desired rule. Review privileged tools carefully: writable executable/configuration locations can let another process control code run with elevated rights, and portable plaintext credentials can be read by anyone with file access. Use existing protection where suitable, restrict access to sensitive files, and avoid running privileged features you do not need.

DesktopStub-style quoted INI assignments are the shared compatibility format, and native/managed parsers now share dialect fixtures. Sidecar configuration/logs remain common defaults; this is not a blanket prohibition on other explicitly configured storage. Preserve existing data and explain migration behavior. Earlier generated rules requiring a maintainer pause for every format/storage/OS integration change were retired by the maintainer on 2026-09-08.

Tracked files are source, build scripts, templates/manifests and documentation. Generated files remain ignored. GitHub Actions builds Windows binaries and portable C++ tests; local results and OS-specific test limitations are in the audit.

## License

This repository is released under the [0BSD license](LICENSE). You can use, copy, modify, and distribute it for any purpose, with or without fee.

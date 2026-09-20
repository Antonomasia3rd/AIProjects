# ADB TV Controller

ADB TV Controller provides a foreground window and an optional tray icon for controlling Android/Google TVs with ADB. Select a TV, connect it, then use the remote controls, screenshot action, device list, or scoped interactive shell. Closing the window exits the application. Startup opens the same foreground UI.

`ADBController.cpp` is an include overlay over `dependencies/ADBController/adb_controller_app.inc`. Shared dependencies own INI parsing, paths, logging, CLI primitives, tray registration/menu/hover behavior, and per-user Startup transactions. `TCL ADB.cmd` remains a compatibility launcher and forwards arguments.

## Setup and configuration

Windows 10 or newer and Android Platform Tools are required. Put `adb.exe` and its DLLs beside the controller, use an absolute `PATH` entry, or select **Choose ADB executable...** from the foreground **...** or tray menu. Configured relative paths resolve beside the controller. The working directory is not searched implicitly.

Enable debugging on the TV and accept its first-use authorization prompt. Device entries accept a hostname or IPv4 address and optional port; port 5555 is used when omitted. ADB is not bundled. The application creates its executable-local INI with the five original script entries on first use; edits and removals are preserved thereafter. `ADBController.example.ini` is a copyable template.

The foreground and tray menus use the same configuration commands: choose ADB, add/edit/remove TVs, select appearance, open/reload the INI, and control these `[Settings]` values:

| Setting | Default | Command line |
| --- | --- | --- |
| `RunAtStartup` | `0` | `--startup` / `--no-startup` |
| `ShowTrayIcon` | `1` | `--tray` / `--no-tray` |
| `ShowMenuAsDropdown` | `1` | `--menu-dropdown` / `--menu-flat` |
| `LoggingEnabled` | `1` | `--logging` / `--no-logging` |
| `Theme` | `Auto` | `--theme auto|light|dark` |
| `AdbPath` | `adb.exe` | `--adb-path <path>` |

Booleans accept `0/1`, `true/false`, `yes/no`, or `on/off`; invalid values fail validation. The foreground **...** menu remains available when the tray is disabled. Hover text shows the product, busy/ready state, selection, and Startup failure. Explorer restart restores an enabled icon. Keyboard Enter/Space opens the tray menu; left click restores the controller.

Startup defaults off. Its profile-scoped shortcut lives only in the current user's `shell:startup` folder and launches `--ini <effective path>`. It captures no TV command or transient invocation flags. Explicit changes couple the shortcut with the complete INI batch: enabling installs first, disabling persists false first, and ordinary failures roll back. Manual INI changes reconcile on launch/reload; failures appear in activity/logging and hover text while the controller remains usable. No Run registry key, scheduled task, or per-machine Startup folder is used.

## Command line

```cmd
ADBController.exe --help
ADBController.exe --theme dark --device "Bedroom TV=192.168.1.20:5557" --configure-only
ADBController.exe --startup --configure-only
ADBController.exe --set Settings.ShowMenuAsDropdown=false --configure-only
ADBController.exe --list-configured
ADBController.exe --target "Bedroom TV" --connect
ADBController.exe --target "Bedroom TV" --home
ADBController.exe --target "Bedroom TV" --reboot --yes
```

`--ini <path>` selects an independent profile. `--configure-only` saves without opening the controller or running ADB. Explicit `--startup` still changes that profile's Startup shortcut. `--help` and `--version` short-circuit other arguments before filesystem or application activity. Generic `--set` accepts the Settings keys above and Devices entries. `--remove-device <name>` requires at least one valid device to remain.

Saved CLI changes notify an open controller. If a command is active, it finishes before the deferred reload applies. `--reload` and `--exit` target only that profile. Device actions include `--devices`, `--connect`, `--refresh`, `--power`, `--home`, `--back`, `--volume-down`, `--mute`, `--volume-up`, `--screenshot`, `--shell`, `--disconnect`, `--reboot`, and `--disconnect-all`. Selected-device actions require `--target`; reboot/disconnect-all require `--yes`, matching GUI confirmation.

## Build and inert tests

Use MSVC Build Tools with the C++ workload:

```cmd
legacy\ADBController\BuildADBController.cmd build-only
legacy\ADBController\BuildADBController.cmd check
legacy\ADBController\TestADBControllerRuntime.cmd
```

`build-only` produces `legacy/ADBController/build/ADBController.exe` without launching it. `check` performs warning-as-error syntax and source checks. The runtime harness compiles the product under an uncalled entry point, injects a fake Startup backend, and uses temporary INI and invisible menu objects. It never starts ADB, contacts TVs, displays/registers a tray icon, or changes a real Startup entry. The default build also only compiles. The old CLI binary smoke requires explicit `binary-smoke`; it uses an unbounded process wait and is not part of the default or inert validation path.

## Behavior and risks

Switching TVs reuses the ADB server; the controller never kills/restarts it. Selected-device commands include `adb -s <endpoint>`, and ADB supplies the connection test without a ping pre-check. Worker commands have a 30-second timeout. Screenshots use `exec-out screencap -p`, commit atomically, and are saved beside the program under `Screenshots`. The interactive shell uses the explicit Windows system `cmd.exe` and the ADB directory for its DLLs.

When enabled, logging writes beside the effective INI. Setup convenience comes first; this consolidation adds no credential protection or security policy. ADB can control a TV, launch commands, and capture its screen, so authorize only controllers you trust and keep debugging access off untrusted networks. See [the ADB audit](../../docs/audit-adb-controller.md) for checks and limitations.

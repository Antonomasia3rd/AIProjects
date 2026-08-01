# ADB TV Controller

ADB TV Controller is a normal foreground Windows program for controlling Android/Google TVs over ADB TCP port `5555`. It is not a tray app. Select a TV in the window, connect it, then use the remote-control buttons, screenshot action, device list, or a scoped interactive ADB shell.

`TCL ADB.cmd` is a compatibility launcher for the built GUI, so users can keep their existing shortcut. `ADBController.exe` is the maintained application.

## What changed from the batch script

- Switching TVs runs `adb connect <selected-ip>:5555`; it never kills or restarts the ADB server.
- Every TV command uses `adb -s <selected-ip>:5555 ...`. Multiple connected TVs therefore do not cause an ambiguous-device error or receive each other's commands.
- There is no `ping` pre-check. ICMP may be blocked even though ADB works, so ADB itself is the connection test.
- ADB commands run in a worker thread, keeping the GUI responsive. They time out after 30 seconds rather than indefinitely locking the program.
- `Disconnect all` is explicit, keeps the ADB server running, and has a confirmation prompt. `Disconnect selected` only removes the chosen TCP endpoint.
- Screenshots use `adb exec-out`, are written atomically, and are saved under `Screenshots` beside the program.

## Prerequisites

- Windows 10 or newer.
- Android Platform Tools' `adb.exe` and its required DLLs. Put them beside `ADBController.exe`, add the platform-tools folder to `PATH`, or set `[Settings] AdbPath` in the local INI file.
- Each TV must have Developer options, USB/Wireless debugging as applicable, and ADB TCP port `5555` enabled. First-time authorization still needs to be accepted on the TV.

The program does not include or redistribute Android Platform Tools.

## Configuration

On first launch, the application creates `ADBController.ini` beside the executable with the five entries from the original script. It uses the repository's standard UTF-8-with-BOM quoted INI format. Use the GUI's **Add TV**, **Edit selected TV**, and **Remove selected TV** buttons to manage the list; each change is saved immediately. **Open configuration** remains available for direct editing, followed by **Reload configuration**.

The defaults are only first-run seeds: removing or changing one in the GUI will not cause it to be restored on the next launch.

## Appearance

The app uses a Fluent-inspired Windows 10/11 desktop surface: card-style sections, rounded flat controls, hover/focus states, Common Controls v6, and Per-Monitor V2 DPI awareness. Use the **...** command in the app header to choose **Follow Windows** (the default), **Light**, or **Dark**. The selected mode is saved as `[Settings] "Theme"` in `ADBController.ini`; **Follow Windows** updates when the Windows app theme changes.

`ADBController.example.ini` is a copyable template. Device host values deliberately accept only DNS hostnames and IPv4-style names (letters, digits, `.` and `-`), and the program always appends `:5555`.

```ini
[Settings]
"AdbPath" = "adb.exe"

[Devices]
"TV Kasir" = "192.168.103.28"
"TV Billing 2" = "192.168.103.29"
```

## Build

Install Visual Studio Build Tools with the C++ desktop workload, then run:

```cmd
legacy\ADBController\BuildADBController.cmd
```

The executable is written to `legacy\ADBController\build\ADBController.exe`. For a compile-only check:

```cmd
legacy\ADBController\BuildADBController.cmd check
```

The repository-wide build also includes this project:

```cmd
.github\scripts\build-windows.cmd
```

## Safety and behavior

Power, mute, volume, Home, Back, screenshots, and reboot are sent to the selected TV only. Reboot and disconnect-all ask for confirmation. The program writes `ADBController.log` beside its executable; both the log and the generated `ADBController.ini` are local runtime files and are not tracked by Git.

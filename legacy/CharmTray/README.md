# CharmTray

Win32 tray launcher for the Windows 8/8.1 immersive shell charm flyouts:

- Search
- Share
- Start
- Devices
- Settings

The implementation uses undocumented COM interfaces and GUID/vtable offsets derived from `CharmBar.exe`. It is not expected to work on Windows 10 or newer because the Windows 8 immersive shell charm infrastructure is no longer present.

## Requirements

- Windows 8 or Windows 8.1.
- Visual Studio Build Tools with the C++ workload.

## Build

From the repository root:

```cmd
CharmTray\BuildCharmTray.cmd
```

From this folder, run `BuildCharmTray.cmd`.

The build script locates MSVC, writes `build\CharmTray.exe`, and supports a syntax-only check:

```cmd
CharmTray\BuildCharmTray.cmd check
```

## Run

```cmd
build\CharmTray.exe
```

The app creates a tray icon. Click or right-click the icon and choose a charm flyout. Keyboard tray activation is also supported. The app is single-instance guarded per executable path, so renamed or separately installed copies do not block each other.

On first launch, the app atomically creates `CharmTray.ini` and writes a UTF-8 `CharmTray.log` beside the executable. If the executable is renamed, the default INI/log names follow the renamed executable. Set `[Settings] LoggingEnabled=0` in the local INI to disable file logging. Invalid boolean values are rejected instead of silently changing behavior.

Startup failures for the supported-Windows check, settings file, singleton mutex, COM apartment, message window, or tray icon are reported and cause the process to exit instead of leaving a hidden unusable instance running. Undocumented shell COM failures are logged and shown as a tray notification. The tray icon is restored after Explorer restarts.

## Limitations

- Windows 10/11 are out of scope.
- This depends on undocumented Windows 8 shell internals and can break across shell updates.
- The shell COM paths cannot be exercised by current Windows 10/11 CI. Builds and source invariants are checked there; flyout behavior still requires a Windows 8/8.1 runtime test.

## Generated Files

- `build\CharmTray.exe`
- `build\CharmTray.ini`
- `build\CharmTray.log`
- compiler object files under `build\obj\` when built by the repository build script

Generated build output is ignored by git.

## Release

Prebuilt binary: [CharmTray v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/CharmTray-v1).

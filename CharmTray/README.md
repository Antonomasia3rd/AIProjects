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

From a Visual Studio x64 developer command prompt:

```cmd
mkdir build 2>nul
cl /nologo /std:c++17 /EHsc /O2 /W3 /MT /D_UNICODE /DUNICODE /D_WIN32_WINNT=0x0602 CharmTray.cpp /Fe:build\CharmTray.exe /link user32.lib ole32.lib shell32.lib /SUBSYSTEM:WINDOWS
```

The repository build script uses MSVC and writes `build\CharmTray.exe`.

## Run

```cmd
build\CharmTray.exe
```

The app creates a tray icon. Right-click the icon and choose a charm flyout. The app is single-instance guarded by a mutex.

## Limitations

- Windows 10/11 are out of scope.
- This depends on undocumented Windows 8 shell internals and can break across shell updates.

## Generated Files

- `build\CharmTray.exe`
- compiler object files under `build\obj\` when built by the repository build script

Generated build output is ignored by git.

## Release

Prebuilt binary: [CharmTray v1](https://github.com/Antonomasia3rd/AIProjects/releases/tag/CharmTray-v1).

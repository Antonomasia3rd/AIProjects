# WindhawkMods

Source-only local Windhawk mods and the LockScreenWin10 research project.
Windhawk manages their settings, loading and compilation; these sources remain
under `legacy/` rather than the shared AIProjects application runtime.

This catalog covers the **20 tracked `.wh.cpp` files, representing 16 distinct
mod IDs**, as of 2026-10-15. Names, versions and declared purposes come from the
source headers. This metadata review does not verify runtime behavior, Windows
build compatibility or safe combinations of mods.

## Local mod catalog

| Source | Mod ID | Name / version | Declared purpose |
| --- | --- | --- | --- |
| [local@always-uiaccess.wh.cpp](local@always-uiaccess.wh.cpp) | `always-uiaccess` | Always UIAccess · 2.4.3 | Creates or patches allowlisted child processes as UIAccess from selected hook hosts. |
| [local@appsfolder-unhide-hidden-apps.wh.cpp](local@appsfolder-unhide-hidden-apps.wh.cpp) | `appsfolder-unhide-hidden-apps` | AppsFolder Unhide Hidden Apps · 1.1 | Adds selected hidden AppUserModelIDs to `shell:AppsFolder` enumeration. |
| [local@center-window-geometry.wh.cpp](local@center-window-geometry.wh.cpp) | `center-window-geometry` | Center Windows + Geometry · 0.1 | Centers new windows and displays position and dimensions while moving or resizing. |
| [local@dwm-3d-experiment.wh.cpp](local@dwm-3d-experiment.wh.cpp) | `dwm-3d-experiment` | DWM 3D Transforms · 0.3 | Experiments with older Windows 3D animations through internal DWM visuals. |
| [local@ep-colored-running-indicators.wh.cpp](local@ep-colored-running-indicators.wh.cpp) | `ep-colored-running-indicators` | Classic taskbar colored running indicators · 0.10 | Customizes per-app running indicators on ExplorerPatcher and selected native classic taskbars. |
| [local@keep-session-audio.playing.wh.cpp](local@keep-session-audio.playing.wh.cpp) | `keep-session-audio-playing` | Keep audio playing in switched-away sessions · 0.2.0 | Prevents the previous console session's audio from being muted during user switching. |
| [local@lockapp-xaml-dumper.wh.cpp](local@lockapp-xaml-dumper.wh.cpp) | `lockapp-xaml-dumper` | LockApp XAML Dumper · 0.5.0 | Records LockApp's XAML tree, properties, geometry and visual states for offline comparison. |
| [local@logonui-battery-volume-controls.wh.cpp](local@logonui-battery-volume-controls.wh.cpp) | `logonui-battery-volume-controls` | LogonUI Battery & Volume Controls · 1.0.0 | Adds configurable battery and volume controls to sign-in and security screens. |
| [local@logonui-scale.wh.cpp](local@logonui-scale.wh.cpp) | `logonui-scale` | LogonUI Custom Scale · 1.0.0 | Gives the sign-in screen independent XAML scaling and input-coordinate compensation. |
| [local@mouse-7.wh.cpp](local@mouse-7.wh.cpp) | `mouse-7` | MOUSE-7 — Tablet Mode mouse diagnostic suite · 1.0.16 | Collects Tablet Mode mouse and shell window-management diagnostics. |
| [local@snipping-tool-border-fix.wh.cpp](local@snipping-tool-border-fix.wh.cpp) | `snipping-tool-border-fix` | Snipping Tool Border Fix · 1.0 | Disables extended-frame-bound queries in Snipping Tool to avoid the Smart Crop border issue. |
| [local@soft-lock-screen-combined-lab.wh.cpp](local@soft-lock-screen-combined-lab.wh.cpp) | `soft-lock-screen-combined-lab` | Soft Lock Screen Combined Lab · 0.3.0 | Experiments with dismissible lock-screen presentation and passive LockApp validation for a specific Windows build. |
| [local@windows-10-tablet-mode-restoration.wh.cpp](local@windows-10-tablet-mode-restoration.wh.cpp) | `windows-10-tablet-mode-restoration` | Windows 10 Tablet Mode restoration · 0.3.13 | Restores surviving Tablet Mode shell, taskbar, Start and app-switch behavior on Windows 11. |
| [local@windows-11-lockapp-styler.wh.cpp](local@windows-11-lockapp-styler.wh.cpp) | `windows-11-lockapp-styler` | Windows 11 Lock Screen Styler - Windows 10 layout · 1.0.3 | Windows 10 layout variant of the Lock Screen Styler. |
| [local@word-hide-upgrade-your-plan.wh.cpp](local@word-hide-upgrade-your-plan.wh.cpp) | `word-hide-upgrade-your-plan` | Word - Remove Upgrade button · 1.1.0 | Removes Word's Upgrade button and reclaims its title-bar space. |

## LockScreenWin10 sources and variants

[LockScreenWin10/README.md](LockScreenWin10/README.md) describes its layout,
preferences and validation history. Its retained input sources and generated
variants share mod IDs with some local files above; they are alternatives, not
additional independent mods to enable together.

| Source | Mod ID | Header version / role |
| --- | --- | --- |
| [builds/lockapp-xaml-dumper.wh.cpp](LockScreenWin10/builds/lockapp-xaml-dumper.wh.cpp) | `lockapp-xaml-dumper` | 0.5.0 · generated dumper variant |
| [builds/windows-11-lockapp-styler.wh.cpp](LockScreenWin10/builds/windows-11-lockapp-styler.wh.cpp) | `windows-11-lockapp-styler` | 1.0.3 · Windows 10 layout variant |
| [sources/lockapp-xaml-dumper.wh.cpp](LockScreenWin10/sources/lockapp-xaml-dumper.wh.cpp) | `lockapp-xaml-dumper` | 0.3.0 · retained input source |
| [sources/windows-11-lockapp-styler.wh.cpp](LockScreenWin10/sources/windows-11-lockapp-styler.wh.cpp) | `windows-11-lockapp-styler` | No header version · retained input source |
| [sources/windows-11-notification-center-styler.wh.cpp](LockScreenWin10/sources/windows-11-notification-center-styler.wh.cpp) | `windows-11-notification-center-styler` | 1.6 · retained Notification Center / Action Center styling source |

## Usage

Import the `.wh.cpp` source in Windhawk or copy the files back into Windhawk's
local mod source folder:

```text
C:\ProgramData\Windhawk\ModsSource
```

Top-level filenames generally use Windhawk's `local@<mod-id>.wh.cpp` convention.
Keep the existing spelling of `local@keep-session-audio.playing.wh.cpp`; its
header mod ID is `keep-session-audio-playing`.

## Build

These files are compiled and loaded by Windhawk. They are not part of
`.github\scripts\build-windows.cmd` and are intentionally absent from
`.github\project-map.json`.

When Windhawk is installed, the following script performs local x64 and x86
syntax checks for **Always UIAccess, AppsFolder Unhide Hidden Apps and Snipping
Tool Border Fix only**:

```cmd
TestWindhawkMods.cmd
```

Set `WINDHAWK_COMPILER` to the full path of Windhawk's `clang++.exe` when it is not installed under a standard location.
The script does not cover the other catalog entries or prove that hooks work
on the installed Windows build.

## Safety

These mods hook Windows or application process behavior. Enable them one at a
time and verify the target process list in Windhawk before broad use.

`always-uiaccess` expects Windhawk to be running as a service so the service
process can use `SeTcbPrivilege` to set `TokenUIAccess`. Requests are bound to
the requesting process and actual child image, and unsupported CreateProcess
semantics fall back to suspended in-process creation before token patching.
Settings reloads atomically publish immutable snapshots shared by hooks and
workers, so in-flight operations finish against one coherent generation while
later operations use the replacement settings.
Its service-broker path still cannot perfectly reproduce every caller-owned
console, affinity, or job relationship, so test each allowlisted application.

`appsfolder-unhide-hidden-apps` hooks shell AppsFolder enumeration in Explorer
and shell hosts. It compares configured extra PIDLs through the AppsFolder
itself and suppresses an extra when Windows already returned the same item, so
future shell changes do not create duplicate entries. `snipping-tool-border-fix` limits itself to `SnippingTool.exe`
and deliberately makes `DWMWA_EXTENDED_FRAME_BOUNDS` queries return
`E_NOTIMPL`, forcing Snipping Tool away from the Smart Crop path that exhibits
the border issue.

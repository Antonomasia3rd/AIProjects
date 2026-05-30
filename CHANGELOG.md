# Changelog

This repository uses project-scoped release tags for Windows binaries.

Release tag families:

- `<Project>-vN` for each built project, for example `DesktopStub-vN`, `DiscordRPC-vN`, or `asusblink-vN`.
- Full builds and shared workflow/repository changes publish one release per built project, each in that project's `<Project>-vN` family.
- `DesktopStub-vN` contains `DesktopStub.exe` and `DesktopStubLiveTileBroker.exe`.

Older releases are intentionally kept when practical so users can compare behavior across versions and identify when regressions started.

## Unreleased

### Repository workflow

- Centralized Windows build metadata in `.github/project-map.json`.
- Added project-map validation before Windows builds.
- Added changed-project Windows builds and project-scoped automatic releases.
- Switched Windows workflow artifacts and release uploads to direct project payload files instead of project-created release ZIPs.
- Added workflow concurrency so newer pushes cancel older in-progress builds on the same ref.
- Added warning-only repository policy and README consistency scans.
- Added Windows build smoke tests for generated artifacts.

### DesktopStub

- Release family is `DesktopStub-vN`; the built executable is `DesktopStub.exe`.
- Changed Live Tile relaunch handling from a mode-switch-only action into a runtime-condition repair. When enabled, packaged launches with Live Tile disabled relaunch as the unpackaged desktop app before manifest registration, and unpackaged launches with Live Tile forced on relaunch through the registered package entry.
- Fixed cold Start/tile activation in forced `LiveTile` mode so a stale one-time static icon/AppxManifest refresh relaunches through the unpackaged process first, matching the tray-switch path.
- Changed DesktopStub defaults: `LiveTileRelaunchOnSwitch=1`, `NotifyOnAlreadyRunning=0`, and `ComRegistrationUseHelper=0`.
- Tidied the tray menu by moving **Generate now** to the top level and nesting Live Tile mode/registration/runtime/Windows 8 options instead of separating them with dividers.
- Added optional tile text overlays configurable by INI, tray menu, and command line. Windows 10 Live Tile mode emits the text into Live Tile XML, while registration/static-image modes bake the text into generated tile PNGs.
- Improved tile text overlay parity: baked/static mode now defaults to top placement like Live Tile XML, and supports secondary/body text plus a large badge/counter text for Calendar/Mail-style tile layouts.

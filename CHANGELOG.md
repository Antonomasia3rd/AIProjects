# Changelog

This repository uses project-scoped release tags for Windows binaries.

Release tag families:

- `<Project>-vN` for each built project, for example `DesktopStub-vN`, `DiscordRPC-vN`, or `asusblink-vN`.
- Full builds and shared workflow/repository changes publish one release per built project, each in that project's `<Project>-vN` family.
- `DesktopStub-vN` contains `DesktopStub.exe` and `DesktopStubLiveTileBroker.exe`.

Older releases are intentionally kept when practical so users can compare behavior across versions and identify when regressions started.

## Unreleased

### Repository layout

- Added shared C++ baseline includes under `dependencies/`.
- Moved projects that have not adopted the shared baseline under `legacy/` while preserving their project keys and release families.
- Added shared tray-root and release-version helpers so migrated apps use one
  fixed dropdown, primary action, disabled version, and separator contract.
- Made shared sidecar logging retry transient reader sharing violations instead
  of silently dropping records while a log viewer or smoke check has the file
  open.

### DiscordRPC

- Restored Discord Gateway transport in the C++ implementation.
- Added DPAPI migration for plaintext `[general] token` values into `[general] token_protected`.
- Matched DesktopStub's console startup contract: normal launches no longer
  create a console before hiding it, while the configured console can still be
  allocated at runtime and mirror shared logger output.
- Kept **Show menu as dropdown** on the tray menu root in both flat and dropdown
  layouts.
- Matched DesktopStub's complete tray-root order and added tag-derived
  `--version`, startup-log, tray, `FileVersion`, and `ProductVersion` reporting.
- Aligned resident control with the fixed application-message pattern used by
  the other migrated apps and made sender success require synchronous message
  delivery.

### WindhawkMods

- Added source-only Windhawk mod sources for Always UIAccess, AppsFolder Unhide Hidden Apps, and Snipping Tool Border Fix.

### Repository workflow

- Centralized Windows build metadata in `.github/project-map.json`.
- Added project-map validation before Windows builds.
- Added changed-project Windows builds and project-scoped automatic releases.
- Switched Windows workflow artifacts and release uploads to direct project payload files instead of project-created release ZIPs.
- Added workflow concurrency so newer pushes cancel older in-progress builds on the same ref.
- Added warning-only repository policy and README consistency scans.
- Added Windows build smoke tests for generated artifacts.
- Made automatic release publication reject tags that do not match the
  executable's embedded major version.

### DesktopStub

- Release family is `DesktopStub-vN`; the built executable is `DesktopStub.exe`.
- Changed Live Tile relaunch handling from a mode-switch-only action into a runtime-condition repair. When enabled, packaged launches with Live Tile disabled relaunch as the unpackaged desktop app before manifest registration, and unpackaged launches with Live Tile forced on relaunch through the registered package entry.
- Fixed cold Start/tile activation in forced `LiveTile` mode so a stale one-time static icon/AppxManifest refresh relaunches through the unpackaged process first, matching the tray-switch path.
- Changed DesktopStub defaults: `LiveTileRelaunchOnSwitch=1`, `NotifyOnAlreadyRunning=0`, and `ComRegistrationUseHelper=0`.
- Added configurable ordinary second-launch actions (`ShowTray`, `Generate`, `ShowConsole`, or `Ignore`) while preserving explicit command-line request precedence and separate-INI multi-instance scope.
- Tidied the tray menu by moving **Generate now** to the top level and nesting Live Tile mode/registration/runtime/Windows 8 options instead of separating them with dividers.
- Added optional tile text overlays configurable by INI, tray menu, and command line. Windows 10 Live Tile mode emits the text into Live Tile XML, while registration/static-image modes bake the text into generated tile PNGs.
- Reworked baked/static tile text to use fixed Windows 8/8.1 medium, wide, and large template layouts. Removed arbitrary font, color, alignment, margin, and small/logo controls that native Live Tiles do not support.
- Added an optional Windows 8.1 preset-template catalog mode for Windows 10 Live Tile notifications while retaining adaptive XML as the compatibility-preserving default.
- Added configurable Windows 10 Live Tile branding with style-aware defaults, including restoring the manifest display name in preset-template mode.

### RssLiveTile

- Added `RssLiveTile`, a shared-baseline Win32 resident app that registers a loose Desktop Bridge package, preserves alternate INI scope across packaged relaunches, polls RSS or Atom feeds, and updates Windows Start with up to five queued text notifications.
- Added namespace-aware RSS/Atom parsing, declared feed-encoding support, article activation links, live configuration reload, cancellable network shutdown, typed CLI validation, package-file refresh, source regression tests, and no-tray resident smoke coverage.
- Preserved `--allow-multiple` across package bootstrap, made tray overrides
  last-option-wins, queued refreshes requested during active work, and prevented
  completed update results from being overwritten before the UI consumes them.
- Bounded PowerShell maintenance-command output capture to 1 MiB.
- Added DesktopStub-compatible root-level **Show menu as dropdown** behavior with Feed, Application, and Package tray sections.
- Matched DesktopStub's complete tray-root order, moved dynamic feed status into
  the Feed section, and added tag-derived CLI, tray, log, binary-resource, and
  default Appx manifest versioning.

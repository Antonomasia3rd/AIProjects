# Changelog

This repository uses project-scoped release tags for Windows binaries.

Release tag families:

- `DesktopStub-vN` for DesktopStub-only changes. The release payload is `GenerateAssets.exe`.
- `<Project>-vN` for other single-project changes, for example `DiscordRPC-vN` or `asusblink-vN`.
- `All-vN` for shared workflow/repository changes or full builds.

Older releases are intentionally kept when practical so users can compare behavior across versions and identify when regressions started.

## Unreleased

### Repository workflow

- Centralized Windows build metadata in `.github/project-map.json`.
- Added project-map validation before Windows builds.
- Added changed-project Windows builds and project-scoped automatic releases.
- Added workflow concurrency so newer pushes cancel older in-progress builds on the same ref.
- Added warning-only repository policy and README consistency scans.
- Added Windows build smoke tests for generated artifacts.

### DesktopStub

- Release family is `DesktopStub-vN`; the built executable remains `GenerateAssets.exe` for compatibility.

# Changelog

This repository uses project-scoped release tags for Windows binaries.

Release tag families:

- `<Project>-vN` for each built project, for example `DesktopStub-vN`, `DiscordRPC-vN`, or `asusblink-vN`.
- Full builds and shared workflow/repository changes publish one release per built project, each in that project's `<Project>-vN` family.
- `DesktopStub-vN` contains `GenerateAssets.exe` and `GenerateAssetsLiveTileBroker.exe`.

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

- Release family is `DesktopStub-vN`; the built executable remains `GenerateAssets.exe` for compatibility.

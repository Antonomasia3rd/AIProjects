# Shared baselines

## C++ desktop applications

New resident desktop projects should compose these modules instead of copying a
product implementation:

- `desktop_app_baseline.h`: stable aggregate entry point that includes the
  common modules below in their supported dependency order.
- `baseline_app.h`: single-instance identity/signaling, path-scoped stable
  hashing, taskbar recreation registration, resident shutdown state, command-line
  help templates, and flat/dropdown tray sections.
- `app_paths.inc`: executable sidecar path discovery for per-product INI and
  log files, including growable current-module path lookup, configured INI
  override handling, and executable-side log defaults for products that must
  preserve legacy log placement when `--ini` points elsewhere.
- `logging.inc`: UTF-8 BOM sidecar file logging, cross-process append locking,
  failure reporting, a reusable `RecentLogBuffer`, and bounded recent-log
  buffering for helper/broker processes.
- `config_ini.inc`: `IniConfigStore`, synchronized INI mutation, encoding, and
  document parsing.
- `command_line.inc`: parent-console command output, console stream binding,
  option value parsing, INI setting syntax, and boolean aliases.
- `tray.inc`: low-level menu construction, the baseline root header contract,
  popup ownership, and notifications.
- `release_version.inc`, `release_version_resource.rc.inc`, and
  `resolve_release_version.ps1`: reusable tag-derived runtime, Win32 resource,
  and build-script version metadata.
- `core.inc`: path, text, and JSON primitives. Configuration must stay INI-backed.

Include `desktop_app_baseline.h` from product translation units. That aggregate
header is the supported public entry point for resident desktop apps and owns the
shared include order. Individual `.inc` modules are include-guarded for focused
tests and compatibility, but they are not guaranteed to be standalone unless a
file explicitly says so. Optional facilities such as `dpapi.inc` stay separate.
Product code should keep policy and commands in product modules while using
these shared contracts for lifecycle, sidecar paths, logging, and persistence behavior.

## Product-owned source subfolders

`dependencies/DesktopStub/` and `dependencies/DiscordRPC/` hold each product's
own modular implementation fragments (`ga_*.inc` and `drpc_*.inc`
respectively). These used to live under `DesktopStub\src` and `DiscordRPC\src`;
they were relocated here so every project's source lives under one top-level
`dependencies` folder instead of being scattered across per-project `src`
folders. **This is purely a physical relocation, not a change in ownership or
sharing policy:** files in these subfolders remain product-specific policy code
owned by that one product, the same as before the move. They are not "shared
baseline" the way the root-level files above are, and other products should
not include from another product's subfolder. If a genuine cross-product need
emerges, promote the specific helper to a root-level shared module (following
the pattern above) instead of reaching into another product's subfolder.

## INI dialect compatibility

The shared INI helpers must stay compatible with DesktopStub's established INI
format. This is now the repository baseline for C++ projects:

- write UTF-8 with BOM;
- write assignments as `"Name" = "Value"`;
- preserve comments, unrelated lines, and ordering where practical;
- preserve whitespace inside quoted values;
- preserve unknown backslash sequences in raw INI values, especially Windows
  paths such as `C:\Users\Amiya\Desktop\file.txt`;
- keep app-level escape decoding separate from raw INI parsing, so templates may
  interpret `\n`, `\r`, and `\t` without making every INI value use those
  escapes.

Do not replace this with `GetPrivateProfileStringW` / `WritePrivateProfileStringW`
or another parser that changes quoting, comments, order, trailing spaces, or path
backslashes.


Additional baseline contracts:
- `aip::TryMakeAbsolutePath` is the strict path-resolution primitive for command-line paths such as `--ini`; callers should reject invalid or empty paths instead of silently falling back.
- `aip::Utf8Logger` resets its file-write failure state when the configured target path or file-output mode changes, so a repaired or changed log target can report fresh status.

## Logging and path baseline notes

`aip::BuildSidecarPathsFromExecutable` is the unchecked path builder and is best kept for already-trusted paths or internal derivation. Apps that accept user-provided config paths should use `aip::TryBuildSidecarPathsFromExecutable`/`aip::TryResolveConfigFilePath` so empty paths, directory paths, and trailing directory separators are rejected before write-time.

`aip::BuildSidecarPathsFromExecutable` derives the default log path beside an
explicit `--ini` override by default. Products that already promise
exe-side log placement, such as DesktopStub, should opt into
`aip::DefaultLogPathPolicy::BesideExecutable` or call
`aip::BuildExecutableSidecarLogPath` so a custom INI path does not silently move
the default log file.

`aip::TryWideToUtf8` is the checked UTF-8 conversion primitive. File/log writers
should treat a non-empty string that cannot be encoded as a write failure instead
of silently reporting success with no payload written.

`aip::IniWriteMutexGuard` defaults to an infinite wait for compatibility, but it
accepts a bounded wait in milliseconds for resident apps that must not hang
indefinitely while trying to save settings.

`aip::RecentLogBuffer` is the shared in-memory tray/diagnostic log model.
Products may keep their own timestamp and UI failure text, but should use this
buffer instead of open-coded vector trimming when preserving recent log lines.
`aip::Utf8Logger` can also mirror complete formatted log lines to an allocated
console and replay its bounded recent buffer when a product enables its console
at runtime.

Migrated tray applications should call `aip::AppendBaselineTrayMenuHeader`.
It keeps the root order stable: **Show menu as dropdown**, the product's primary
action, a disabled **Version** line, then a separator before product sections.

## C# registry notification services

`registry_notification_service.cs` is the reusable `ServiceBase` engine for
services that monitor per-user notification settings under `HKEY_USERS`.
Products provide only registry-entry policy by overriding `ProcessAllKeys`.
The shared engine owns loaded-user discovery, key recreation watching, worker
exception containment, one bounded aggregate stop deadline, sidecar logging,
and strict logging-boolean parsing.

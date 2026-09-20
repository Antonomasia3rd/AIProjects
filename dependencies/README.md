# Shared dependencies and product overlays

The current consolidation target is an overlay-style codebase: applications
compose shared implementations using includes and product declarations. Setup
convenience takes priority; additional DPAPI/path/hash protection is intended to
be opt-in after functional repairs. Some legacy products still enforce older
defaults in code; [the audit](../docs/audit-shared.md) tracks those gaps. Existing
mandatory security behavior described below is a compatibility fact, not a new
repository rule.

## C++ desktop applications

The Caps Lock LED action port lives in `hardware/caps_blink_engine.h`, a portable
pattern/controller with injected device operations and copied status snapshots.
`hardware/caps_blink_windows.inc` supplies the separately constructed Windows
backend. Device actions require `actionsEnabled=true`; preview and status reads
remain inert. Hosts should request stop and poll completion before joining on a
UI thread, since canceled driver I/O retains target ownership until it settles.
See [the Caps engine audit](../docs/audit-caps-source.md) for fake-test coverage
and the retained standalone migration boundary.

DesktopStub and NowPlayingTile now consume `appx_registration_script.h` for typed, literal-safe registration command construction and `packaged_startup_manifest.h` for the initially disabled startup extension. Both execute registration scripts through `powershell_runner.inc`; product fragments supply identity, policy and result presentation. See [the AppX sharing audit](../docs/audit-appx-sharing.md) for validation and the native COM mechanisms that remain separate.

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
- `tray.inc`: notification-area registration/removal, version-aware nonblank
  hover text, balloon delivery, low-level menu construction, the baseline root
  header contract, and popup ownership.
- `release_version.inc`, `release_version_resource.rc.inc`, and
  `resolve_release_version.ps1`: reusable tag-derived runtime, Win32 resource,
  and build-script version metadata.
- `core.inc`: path, text, and JSON primitives, plus a small set of Win32
  helpers that don't fit any other module cleanly (Win32 argv quoting via
  `QuoteCommandLineArg`, XML text escaping via `XmlEscape`, AppX
  package-identity detection via `CurrentProcessHasPackageIdentity`).
  Configuration must stay INI-backed.

Include `desktop_app_baseline.h` from product translation units. That aggregate
header is the supported public entry point for resident desktop apps and owns the
shared include order. Individual `.inc` modules are include-guarded for focused
tests and compatibility, but they are not guaranteed to be standalone unless a
file explicitly says so.

Optional facilities are not part of the `desktop_app_baseline.h` aggregate --
include them explicitly only if a product actually needs them, so products
that don't need e.g. PowerShell don't pull in `<winhttp.h>`/process-spawning
code for nothing:

- `dpapi.inc`: DPAPI (`CryptProtectData`/`CryptUnprotectData`) secret
  encryption for the current Windows user. New values use the self-identifying
  `dpapi:v1:utf8:` format; reads of the old ambiguous `dpapi:` format require
  each product to explicitly select its historical UTF-8 or UTF-16LE encoding.
  Used by DiscordRPC and RealTimeNotesDeskband for encrypted token storage.
- `powershell_runner.inc`: spawn `powershell.exe` with a timeout, an optional
  output-size cap, and captured stdout/stderr (`aip::RunPowerShellCaptured`),
  plus the quoting/encoding helpers around building a
  `-EncodedCommand` invocation (`PowerShellEncodedCommand`,
  `PowerShellSingleQuotedString`, `PowerShellUtf8Preamble`) and resolving
  which `powershell.exe` to use (`DefaultPowerShellExe`,
  `ResolveConfiguredPowerShellExe`). Process creation validates and supplies
  that absolute path as `lpApplicationName`, so a command-line token or working
  directory cannot redirect the launch. Needs `<appmodel.h>` (already in
  `desktop_app_baseline.h`) for nothing beyond what `core.inc` already
  requires, but needs `<winhttp.h>` and `winhttp.lib` in any product that
  also wants to make HTTP requests -- that part is not this module's
  responsibility, see DesktopStub's `ga_rss_feed.inc` for an example of a
  product building its own HTTP fetch on top.
- `http_fetch.inc`: worker-facing, asynchronous WinHTTP GET with one monotonic
  total deadline, cooperative cancellation, a response byte limit, HTTP status
  errors, and charset metadata. Query parameters are sent intact; fragments
  are excluded from the request target. User-info credentials in URLs are
  rejected explicitly because this helper does not implement URL-based auth.
  Read buffers and callback state stay alive through `HANDLE_CLOSING`, so a
  canceled request cannot write into a returned function's stack. It closes
  only asynchronous requests; no other thread closes a synchronous handle.
  DesktopStub's RSS fetch uses this helper. `tools\TestHttpFetch.cmd` exercises
  loopback query/empty/error/limit/slow-header/trickle/cancellation behavior and
  repeated late callbacks, without making external network requests.
- `startup_shortcut.inc`: create, query, replace, and remove a path-scoped
  per-user shortcut under the Windows `shell:startup` folder. It validates the
  shortcut target/arguments/working directory, serializes lifecycle changes
  across processes, stages a short temporary link outside the enumerated Startup
  folder on the same parent volume and commits it atomically,
  and refuses to replace or remove a same-named shortcut that targets another
  executable. Its desired-state operation holds the path lock across both the
  previous-state query and mutation. Its INI-coupled commit additionally holds
  the shared INI mutex across snapshot, mismatch repair, commit, and rollback.
  Enabling installs the shortcut before persisting `true`; disabling persists
  `false` before removal. An interrupted commit therefore self-reconciles on
  the next launch without a journal sidecar, while ordinary failures restore
  the prior configuration using the same safe ordering. Removal intentionally
  needs only structural identity validation,
  so an owned stale link remains removable after its executable or working
  directory has moved. ShellLink target and working-directory paths at or above
  `MAX_PATH` are rejected because `IShellLinkW::GetPath` cannot round-trip them
  reliably.
- `packaged_startup.inc`: query and change a packaged desktop app's declared
  `Windows.ApplicationModel.StartupTask` without creating a shortcut, registry
  value, or scheduled task. It runs WinRT async work on a dedicated MTA worker
  so synchronous tray/CLI callers do not block an STA async continuation,
  preserves `DisabledByUser`/policy decisions, and provides the same locked,
  direction-safe INI-coupled commit through the manifest API. A requested value
  that already matches an externally enforced OS state may repair the INI, but
  the helper never claims it overrode a user or administrator decision. The package manifest
  must declare a matching `desktop:Extension Category="windows.startupTask"`
  and `desktop:StartupTask TaskId`; this is the startup strategy for packaged
  apps such as NowPlayingTile.
- `managed_startup_shortcut.cs`: the equivalent target-safe, profile-scoped,
  cross-process-serialized Startup-folder shortcut lifecycle for .NET
  Framework desktop applications. It refuses foreign same-name links,
  validates target/arguments/working-directory metadata, and atomically
  replaces owned shortcuts. Temporary links are staged outside the enumerated
  Startup folder, and legacy-link cleanup failures are reported instead of
  silently leaving duplicate launch entries. Its shared INI-coupled commit
  installs before persisting `RunAtStartup=true` and persists `false` before
  removal. An interrupted commit therefore self-reconciles on the next launch
  without a separate recovery journal; ordinary failures restore the complete
  requested setting batch with the same direction-safe ordering. This provides
  eventual consistency after power loss, not preservation of an in-flight
  setting change that had not finished committing.
- `managed_ini.cs`: bounded, cross-process-serialized managed INI reads plus
  comment/order-preserving batch writes and atomic first-run creation. It
  rejects malformed target sections, non-round-tripping identifiers, and
  oversized defaults/results. Duplicate assignments are collapsed so a saved
  value is always the effective value. It reads DesktopStub's quoted section,
  name, value, escape, and inline-comment syntax and existing bare assignments;
  writes use quoted assignments and strict UTF-8 with BOM. Native and managed
  tests consume the same dialect fixture under `tools/fixtures/`.
- `managed_logging.cs`: path-scoped, cross-process-serialized UTF-8 sidecar
  appends for .NET Framework applications. It writes one BOM on a new file,
  permits concurrent diagnostic readers, reports bounded lock/write failures,
  and can rotate a configured number of generations under the same mutation
  lock. asusblink, capsblink, DNSAutoUpdate, and PhotoCollage use this instead
  of private `File.AppendAllText` implementations.
- `managed_tray.cs`: WinForms notification-icon construction, the length-safe
  tooltip policy, baseline product/version header, validated text prompts,
  tooltip updates, and disposal for managed tray applications. asusblink,
  capsblink, and DNSAutoUpdate compile this together with the shared managed
  INI, logging, and Startup sources.
- `privileged_path_trust.h`: fail-closed path validation for native processes
  that launch or register code as `LocalSystem`. It accepts only canonical
  Windows/Program Files roots selected by policy, rejects alternate streams and
  reparse components, validates each component's owner and write-capable DACL
  entries, and retains handles with write/delete sharing denied across the
  privileged operation. SecureDesktopLauncher uses the Program-Files-only
  policy for its binaries/configuration and the Windows-or-Program-Files policy
  for configured launch targets.
- `content_sources/smtc.inc`: read-only SMTC title, artist, source, and playback
  status provider through `aip::content::ReadSmtcContent(Text&, error, timeout)`.
  It owns no files, package registration, playback controls, or resident loop.
  Call on a worker; the helper initializes MTA, rejects an existing STA, and
  limits the combined asynchronous waits to the supplied timeout (1500 ms by
  default). Timeout cancellation is cooperative; synchronous Windows COM calls
  are not a hard process-level deadline. A missing media session returns idle
  text; unavailable APIs return an error. Build/read-only smoke:
  `tools\TestSmtcSource.cmd`. The standalone NowPlayingTile app remains until
  its artwork/widget/delivery features are integrated.
- `content_sources/notes.inc`: the extracted Real-Time Notes data engine for
  Genshin resin, Star Rail stamina, and ZZZ charge. Hosts supply explicit account
  values and cancellation; `Fetch` returns a typed count/recovery/status
  snapshot plus detail lines. The deskband consumes this implementation after
  its separate account loader, and DesktopStub can use it without an Explorer
  host. It uses named headers through `http_fetch.inc`, without logging header
  contents. `tools\TestNotesSource.cmd` exercises synthetic responses and an
  injected transport only. Endpoint/auth compatibility with the live service
  remains unverified; see [the provider audit](../docs/audit-notes-source.md).

Product declarations select policy and commands while shared modules own
lifecycle, sidecar paths, logging, and persistence behavior. Reusable source
providers should expose snapshots/options that multiple products can consume.

## Product-owned source subfolders

Product-specific implementations live under matching subfolders:
`dependencies/DesktopStub/`, `dependencies/DiscordRPC/`,
`dependencies/NowPlayingTile/`, `dependencies/CharmTray/`,
`dependencies/ADBController/`, `dependencies/SecureDesktopLauncher/`,
`dependencies/RealTimeNotesDeskband/`, `dependencies/PhotoCollage/`,
`dependencies/TaskSchedulerMigration/`, `dependencies/DNSAutoUpdate/`,
`dependencies/capsblink/`, and `dependencies/asusblink/`. Their project-local
source is limited to the includes, composition declarations, assembly metadata,
or declarative policy needed to combine those implementations with the
root-level shared modules.

These implementations used to live in project-local source files or `src`
folders. They were relocated so maintained product implementation bodies have
one top-level home. Much of this is still physical relocation: nested folders
contain whole application bodies and globals. That is an intermediate state,
not the finished overlay architecture. Extract reusable providers and helpers
behind shared interfaces as products consolidate; avoid importing another
app's entry point or resident loop. Keep project-map ownership accurate as
modules move so a shared-provider change selects every affected build in CI.

This has happened in practice, not just as a hypothetical: DesktopStub's
`PS_Run` and RssLiveTile's independently-written `RunPowerShellCommand` had
drifted into near-duplicate implementations of the same spawn/timeout/capture
logic (each missing a feature the other had), and `QuoteCommandLineArg`,
`XmlEscape`, and `CurrentProcessHasPackageIdentity` were each reimplemented
under the same name in more than one product. All were promoted to the
root-level modules described above rather than left duplicated.

## INI dialect compatibility

The shared INI helpers must stay compatible with DesktopStub's established INI
format. Native and managed shared helpers now use this value dialect:

- write UTF-8 with BOM;
- write assignments as `"Name" = "Value"`;
- preserve comments, unrelated lines, and ordering where practical;
- preserve whitespace inside quoted values;
- preserve unknown backslash sequences in raw INI values, especially Windows
  paths such as `C:\Users\Amiya\Desktop\file.txt`;
- keep app-level escape decoding separate from raw INI parsing, so templates may
  interpret `\n`, `\r`, and `\t` without making every INI value use those
  escapes.

Bare assignments remain readable. As in DesktopStub, `#` and `;` start an
inline comment in unquoted values; quote literal comment characters, URLs with
fragments, and paths containing them. Managed saves preserve quoted whitespace
and convert UTF-16 input to UTF-8 BOM. Managed apps still reject malformed target
lines and invalid programmatic setting names rather than silently repairing
them. See `tools/fixtures/ini-dialect.txt` and its shared expected values.

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
Products provide only a declarative, case-insensitive subkey-prefix filter and
the desired typed values through `RegistryNotificationPolicy`.
The shared engine owns loaded-user discovery, key recreation watching, worker
exception containment, one bounded aggregate stop deadline, subkey enumeration,
writable-key lifetime, independent per-value failure handling, exact registry
value-kind repair, protected installation, and strict logging-boolean
parsing. Duplicate logging assignments follow the shared INI last-value rule;
malformed section headers, malformed `[Settings]` assignments, and invalid
logging booleans fail startup before registry watching begins.

These services run as `LocalSystem`, so the same source also owns their managed
privileged-path boundary and command host. Install/runtime validation accepts
only files below canonical Program Files known-folder roots, walks and pins
every component without following reparse points, checks final handle paths,
requires a `LocalSystem`/`Administrators`/`TrustedInstaller` owner, and rejects
dangerous write ACEs for other principals. The executable and sibling INI are
both validated; service installation does not open the SCM until those checks
and bounded INI parsing succeed. Runtime additionally requires the current
`LocalSystem` token and retains both component-handle sets until stop.

`ManagedPrivilegedServiceHost.Run` gives consumers the same strict
`--help`/`--version`/`--install`/`--uninstall` surface. Installation explicitly
selects the `LocalSystem` account. A post-creation configuration failure rolls
the new service registration back and explicitly reports a failed rollback.
Uninstall verifies the registered executable and account before deletion.
Privileged diagnostics use `ReportEvent` plus
debugger output and never write sibling logs or create an Event Log source in
the registry. The shared base explicitly disables `ServiceBase.AutoLog` so the
framework cannot silently take a separate registry-backed Event Log path.

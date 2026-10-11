# Live Tile enable crash, 2026-10-11

The user's packaged DesktopStub v27 process crashed after enabling Live Tile
delivery. This is separate from the still-unresolved four-pixel static-text
renderer difference. No tolerance or Live Tile feature was disabled to address
this crash.

## Evidence

The full dump is preserved at
`D:\Users\Public\AppData\Local\CrashDumps\DesktopStub.exe.22372.dmp`.
WER report ID: `84bb8a69-8241-4bfd-9ce2-7c44d8fcf1a5`.
The access violation occurs in `combase.dll` 10.0.26100.5074, RVA `0x87c4d`.
The original executable SHA-256 is
`CBE389F196A9CC4798B579F42C33F6B4479B1648F94888FE71083E567E09C4CF`.

Local dump analysis used the system debugging engine; the dump was not uploaded.
The application had no matching PDB. Relinking the retained original objects
with a linker map produced an identical `.text` section (SHA-256
`489a712aab72c31f65b54184ffad21ffdb8de293a2468e6e870f01f071f80180`),
allowing exact application frame identification:

`GenerationWorkerThread -> Generate -> Appx_Update_Or_Request_LiveTile ->
Appx_Update_LiveTile -> ~WinrtApartmentGuard -> clear_factory_cache ->
factory_cache_typeless_entry::clear -> COM proxy Release`.

The failed release corresponds to the cached `StartupTask/IStartupTaskStatics`
factory entry at application RVA `0x2e4070`. The faulting dereference uses
`0x00007ffee983b788`, inside the dump's **unloaded**
`OneCoreCommonProxyStub.dll` range. The application log records a successful
Live Tile update immediately before cleanup. This identifies a stale factory
proxy release during apartment/cache teardown, rather than a failed tile XML
update. Loaded third-party modules are not by themselves evidence of causation.

## Change

The temporary packaged-Startup MTA worker used the projection's cached static
`StartupTask::GetAsync` accessor, leaving its factory in the process-wide cache
after uninitializing COM. A later STA guard tried to release that stale proxy.
Both StartupTask lookup paths now acquire an **uncached** factory, keep it local
through the asynchronous operation, and destroy it before the MTA exits.

The shared `dependencies/winrt_activation.h` helper preserves activation errors.
SMTC uses the same temporary-MTA pattern, so its session-manager and DataReader
factories receive the same local lifetime. No DLL split, package identity change,
Startup policy change, protection bypass, or resident-process workaround is used.

## Validation and limits

- Normal v27 host and broker compile successfully and receive clean custom
  Defender scans. Rebuilt host SHA-256:
  `BD84C659146152AD41F5F79F4BCD3DA71A925F0A75C312BCE5E43824CD369E02`.
- New factory lifetime fixture: **601 checks, zero failures**, covering 100 MTA
  teardown/STA cache-clear cycles and activation-error propagation. It acquires
  StartupTask/SMTC/DataReader factories but never calls StartupTask GetAsync,
  enables/disables Startup, requests media sessions, or registers a package.
- Existing packaged Startup fake-operation tests: **13 checks, zero failures**.
- Shared source regression checks: **277 checks passed**, including preventing
  restoration of the cached StartupTask static accessor.
- Injected SMTC/artwork tests: **38 checks, zero failures**, with real media
  session reads disabled.
- Copied fixed-host offline content smoke passes, including composed text,
  image/cache changes, invalid inputs and concurrent INI writes. It uses
  `--render-only`/`--configure-only`; real package and Startup operations are
  rejected. The separate exact-glyph renderer gate was not rerun or relaxed.

The fixture is part of the bounded, scan-before-run Windows content suite and
its existing CI entry point. Windows API factory activation is platform-specific;
the existing portable model/layout suites are unaffected.

The actual packaged tray-mode switch still requires a user acceptance retry.
Factory-only regression tests do not claim a completed live package/Start-tile
test. Detailed dump output, original executable, linker map and scan/run logs
remain under ignored `build/review-validation`; the original dump remains intact.

The scanned fixed host and broker were copied to the user's normal
`DesktopStub/build/DesktopStub.exe` and `DesktopStubLiveTileBroker.exe` paths,
their hashes verified, and both installed paths scanned clean again. The current
INI hash is unchanged. No resident application was running at replacement, and
validation did not start a resident instance. The previous data-reset backup is
preserved; this fix performs no further reset or package/Startup mutation.

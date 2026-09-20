# Packaged startup audit

Updated 2026-09-09. Implementation and bounded fake/compile validation are complete. Real startup/task/package mutation was not performed by this audit.

## Initial findings and intended integration

- `dependencies/packaged_startup.inc` already implements the Windows `StartupTask` path used by NowPlayingTile. Its WinRT `.get()` calls have no deadline, its wrapper joins a worker synchronously, and its INI transaction defaults to an infinite mutex wait. Calling this from a tray/menu thread can freeze that UI.
- DesktopStub currently exposes only the per-user Startup-folder shortcut. The requested additional packaged method must have an independent setting and clear UI label.
- Intended setting: `[Settings] RunAtStartupPackaged=0`; commands `--packaged-startup` and `--no-packaged-startup`; tray label `Run at sign-in (Windows startup task)`. The existing shortcut stays independent and limited to `shell:startup`.
- Planned implementation: deadline-aware shared API helper, asynchronous DesktopStub adapter, reusable pure manifest fragment, and isolated compilation/unit tests. The Windows 10 manifest declares the task initially disabled. Windows 8/8.1 manifest branches remain unchanged.
- A StartupTask is package-wide and its desktop manifest does not accept the arbitrary `--ini` profile arguments used by the legacy shortcut. Custom-profile behavior requires explicit handling so selecting a profile does not silently start a different one; root coordination pending.

## References

- [Microsoft desktop StartupTask manifest schema](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop-startuptask): task identity, initial enabled state, display name, and Windows 10 version 1607 minimum for this desktop schema.
- [Microsoft desktop Extension schema](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-desktop-extension): the `windows.startupTask` application extension and desktop namespace.

## Implementation checkpoint

- Replaced unbounded shared `GetAsync().get()` and `RequestEnableAsync().get()` calls with `AwaitPackagedStartupOperation`: five-second asynchronous waits, cooperative cancellation, and explicit unknown-state timeout errors. Changed the packaged transaction's default INI mutex wait from infinite to ten seconds. Synchronous COM/RPC calls are not force-terminated; callers still must use a worker.
- Added `dependencies/DesktopStub/ga_packaged_startup.inc`, an asynchronous worker with cached status, coalesced desired-state changes, query-only menu refreshes, and a joined shutdown. No StartupTask calls run from the tray's UI callbacks.
- Added the independent `Settings.RunAtStartupPackaged=0`, CLI aliases, requested-state tray checkbox, and a cached-status dialog. Existing legacy shortcut remains separate. Direct launches retain the requested preference and explain that the Windows task requires the registered Windows10 package.
- Custom INI profiles cannot enable or mutate the package-wide task; disabled zero is allowed. Querying its status is allowed. Windows8/Windows81 runtime profiles skip the packaged API.
- Added pure `dependencies/packaged_startup_manifest.h`; new generated Windows10 manifests declare the task with `Enabled=false`. Windows8/Windows81 manifest branches are untouched. Existing custom manifests are preserved; missing-extension errors explain regeneration/registration/relaunch requirements.
- Added narrow integration hooks in DesktopStub includes, command-line parsing/validation, defaults, manifest generator, tray, normal startup/reload/shutdown. The render-only early exit remains before these runtime hooks.
- Two compile-only full DesktopStub host checks passed with MSVC/Windows SDK, including the final target-validation signature and normal poll hook. Output: `DesktopStub/build/obj/PackagedStartupHostCheck.obj`; log: `DesktopStub/build/packaged-startup-compile.log`. No DesktopStub executable was produced or launched by these checks. The smoke agent also reported a subsequent fresh host build including this integration.
- `tools/TestPackagedStartup.cmd` compiled and ran `tools/PackagedStartupTests.cpp`: **12 checks, zero failures**. The tests use fake async operations only and cover immediate completion, finite pending waits, timeout cancellation without unfinished results, a zero deadline, terminal failure, user/policy/future state mapping, escaped XML attributes, the desktop extension contract, and initially disabled state. Log: `DesktopStub/build/packaged-startup-fake-tests.log`.
- The Windows source runner now includes these fake tests. Five new packaged-startup source guards passed. The full source checker then stopped at an existing smoke-runner guard expecting the prior temporary-copy pattern in `.github/tools/RepoTools.cs`; the smoke agent owns alignment with its new render-only runner. Log: `DesktopStub/build/packaged-startup-source-check.log`. This is not a packaged-startup test failure, but the complete source suite was not green at that checkpoint.
- README now describes both methods, requested versus OS state, package/default-profile limitations, old-manifest migration, and user/policy overrides. Unmodified old `Run at startup` tray wording is upgraded to `Run at sign-in (Startup folder)`; customized text is preserved. `git diff --check` passed for the changed tracked files.

## Safety and remaining validation

The observed pre-existing `DesktopStub.exe` PID 34424 was not launched or stopped by this subtask. Only isolated test binaries and compile commands have been run. Real OS task behavior, actual sign-in launch, custom installed manifests, and old-package migration remain untested.

The subsequent [AppX sharing stage](audit-appx-sharing.md) migrated NowPlayingTile's handwritten startup fragment onto `packaged_startup_manifest.h`. Both projects now consume it, and NPT's API calls also inherit the shared deadline fix.

Asynchronous waits are bounded, but synchronous COM/RPC and thread teardown are not force-terminated. DesktopStub keeps those calls off its UI thread. A timeout is reported as failure/unknown state because cancellation is cooperative; the adapter does not automatically repeat an unchanged failed enable request. The user can refresh actual state through the status menu. A subsequent deliberate preference change or process restart can request reconciliation again.

Existing manifests are not silently rewritten just to add this option. An older registered package needs an explicit manifest regeneration/registration/relaunch, as documented. The checkbox represents persisted requested state; actual success/refusal/pending state is shown in its status dialog and logs.

## Usage checkpoints

Initial usage check: five-hour window 11% consumed; weekly window 32% consumed. No API mutation, package registration, normal DesktopStub launch, or actual sign-in/startup execution has been performed. Tests will use only isolated fixtures/fakes and compile checks.

Before the final fake/compile runs: five-hour 43%, weekly 72%. Root's later checkpoint: five-hour 4%, weekly 83%. No usage-reset credits were consumed by this subtask.

## Final source-guard alignment (2026-09-10)

`tools/SharedBaselineSourceCheck.cpp` still required the obsolete literal `RequestEnableAsync().get()`. It now requires the bounded await call, five-second default, timeout-aware wait and cancellation, and rejects unbounded `.get()` calls in the helper. The **source-only** checker compiled and passed **301 checks**. Log: `DesktopStub/build/shared-startup-source-guard.log`. The full baseline runtime tests were not invoked, and no OS startup state was read or changed. Quota before this small follow-up: five-hour 12%, weekly 89%.

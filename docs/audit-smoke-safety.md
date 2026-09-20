# Smoke runner isolation audit

Current task: make the default smoke suite safe to interrupt on the user's laptop. Code that predates this conversation may be changed when it conflicts with the current instructions. No real package, service, desktop-setting or keyboard/hardware integration tests are authorized for this safety pass.

## Findings recorded before changes

- Default DesktopStub bitmap smoke invokes `--once`. `Generate` then registers the manifest or publishes/clears a native Live Tile. A temporary package identity prevents a naming collision but does not make registration inert.
- Package cleanup is in a managed `finally` block. Forced runner termination or machine shutdown can bypass that block and leave package registration behind.
- Resident smoke processes are killed in normal cleanup, but most child creation uses plain `Process.Start`. There is no process-tree lifetime guarantee when the runner itself is terminated.
- Several timeout paths call unbounded `WaitForExit()` after attempting to kill a process. The concurrent INI-writer cleanup only disposes process handles, leaving unfinished children alive on failure.
- Temporary files may remain after interruption. A leftover diagnostic directory is acceptable; changes to startup entries, package registration, desktop state, playback, services or hardware are not part of default smoke.

## Implementation ownership and plan

- Repository validation owns `.github/tools/RepoTools.cs`, process-lifetime isolation, inert interruption tests and this report.
- DesktopStub `--render-only` parsing/early entry/generation changes are coordinated with root before shared source is edited. The mode must return before activation, existing-instance signaling, startup reconciliation, registration and notification publishing.
- Default bitmap scenarios will use render-only. Real package integration will require an explicit named opt-in flag and will not run on this laptop during this pass.
- The smoke runner will install a Windows Job Object with kill-on-close semantics before creating any child. Child and descendant lifetime must remain tied to the runner even when managed cleanup does not execute. Job setup failure must stop smoke before launching applications.
- Interruption regression tests will use only bounded copies of the test helper as inert children and grandchildren, plus temporary marker files. They will kill their own fixture runner and verify its descendants exit.

## Usage checks

Initial check: 6% of the five-hour allowance and 32% of the weekly allowance consumed. Usage will be checked before each potentially persistent test batch and each major stage. Availability is not treated as a reason to begin an unsafe test.

## Validation status

The default DesktopStub offline smoke and interruption-cleanup validation are complete. No real system-integration tests were run for this pass.

Current saved implementation (2026-09-09):

- RepoTools installs a non-inheritable kill-on-close Job Object on itself before smoke child creation and retains its handle until OS process teardown. The total smoke deadline is 15 minutes. Setup failure aborts before application launch.
- `repo-tools.cmd smoke-cleanup-tests` passed both normal-exit and forced-termination cases: the inert fixture child and grandchild exited within bounded waits. Fixtures have their own 20/25-second limits and the test has a 60-second watchdog. No real applications were involved.
- Unbounded process cleanup/redirected-stream waits were replaced with bounded waits; concurrent INI-writer cleanup now kills unfinished children rather than only disposing their handles.
- Default bitmap scenarios select `--render-only`; package cleanup and the normal DesktopStub resident scenario require `--allow-package-integration`. That opt-in has not been run.
- DesktopStub has a new early render-only branch before helper activation, instance signaling, startup reconciliation and resident initialization. It accepts explicit images or Image/None content with CustomText, rejects startup/control/helper options, and bypasses external providers. `Generate` returns after local assets/cache before registration or tile publication. Console/tray/notification logging paths are disabled for offline rendering.
- RepoTools accepts `--desktopstub-binary` and `--desktopstub-broker`, allowing freshly built validation executables to be copied into the temporary fixture with the normal filenames. Helper files are copied as manifest-validation inputs and are never executed by default smoke.
- The separately named build passed: DesktopStubValidation.exe and DesktopStubValidationLiveTileBroker.exe. Its log is `build/render-only-build.log`. The user's normal DesktopStub process PID 34424 was not launched by this agent and was left untouched.
- `--configure-only` validates and commits local INI settings through the shared atomic writer, without resident signaling or either Startup transaction. Explicit startup aliases are rejected even when disabling Startup; pre-existing preferences remain readable and are not applied.
- Default manifest checks use `--render-only --regenerate-manifest`, default asset checks use `--render-only`, and concurrent INI writers use `--configure-only`. Help/version keep their existing side-effect-free entry. The real DesktopStub and Discord IPC resident checks are excluded from default smoke.
- Windows CI now runs the inert interruption-cleanup regression before normal build smoke.

Validation results:

- Full default **offline DesktopStub smoke passed**, including four startup-alias rejection cases, external-provider rejection, missing-input rejection, preservation of existing Startup preferences without applying them, None-background synthesis, custom help/legacy INI preservation, manifest variants, all bitmap pixel-region and one-line wrapping checks, composition overlay/cache/Off-mode restoration, and 12 concurrent local-only INI writers.
- The first offline run correctly stopped when a Win8 manifest helper file was absent. Inert helper copying and a fresh broker override fixed that fixture setup issue; no package was registered in either run.
- DesktopStub's source guards were aligned with offline commands and passed **1,726 checks**, including the new early-entry, package opt-in, Startup exclusion, provider exclusion and job-lifetime guards. Log: `build/offline-source-check.log`.
- Full offline smoke log: `build/offline-desktop-smoke.log`. The successful temporary fixture was removed; the earlier failed fixture was retained for diagnostics only.
- Usage was checked at each major test stage and before each application batch. When the five-hour window expired, the already-running offline batch remained bounded by per-process timeouts and the 15-minute job watchdog. No cleanup required continued model execution.

Repeat the same offline validation after building separately named binaries:

```text
.github\scripts\Smoke-WindowsBuild.cmd --projects DesktopStub --desktopstub-binary DesktopStub\build\DesktopStubValidation.exe --desktopstub-broker DesktopStub\build\DesktopStubValidationLiveTileBroker.exe
```

Limits: offline tests verify file generation and process isolation, not the Windows Start-screen presentation. The explicit package-integration option can leave a registered test package if Windows or the runner crashes during a real package operation; it is intentionally excluded from laptop/default/CI smoke and should only be used in a disposable test environment. Temporary files can remain after forced interruption. Existing independently launched user applications are outside the smoke job and are not terminated.

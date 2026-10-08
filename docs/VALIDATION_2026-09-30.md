# Scanned, staged validation of the review branch

User instruction: push first, then test step-by-step; scan with Defender before
executing new binaries where possible. The branch
`codex/review-original-requirements` at `b82bc87` was pushed before testing.

Local evidence is under ignored `build/review-validation/`. Processes run with
independent timeouts, hidden windows and captured output. The original products
are not overwritten. Tests receive temporary/synthetic profiles; real Startup,
packages, accounts, devices and resident integrations remain disabled.

Defender and real-time protection were enabled; signatures were updated on
2026-09-30. Custom scans use remediation normally, then check detections and
file hashes. No exclusions, restores, allowed-threat actions or submissions.

| Stage | Outcome |
| --- | --- |
| Chrome implementation/test script scan | No threats found. |
| Chrome synthetic JSON/directory/Windows replacement fixtures | Passed, exit 0. Real Chrome discovery is replaced with a fixture function. |
| Managed legacy suite compile and scan | Compiled; no threats found. |
| Managed legacy suite | Passed, including profile repair/inspection and atomic INI behavior. Real Startup integration explicitly skipped. |
| PhotoCollage and TaskSchedulerMigration product compile | Passed with warnings as errors; both scanned without detections. |
| TaskSchedulerMigration local suite | Compiled with warnings as errors; scanned; 29 checks passed. No Scheduler COM activation. |
| RepoTools | Compiled with warnings as errors; scanned; project map and workflow selection passed. |
| DesktopStub source checker | Compiled, scanned; 1,746 checks passed. |
| Portable content model | Compiled, scanned; 282,180 assertions and 100,000 randomized schedule iterations passed. |
| Tile layout / preset XML | Compiled, scanned; 236 / 494 checks passed. |
| Offscreen GDI+ renderer | Compiled, scanned; 18,826 checks passed. GDI handles 3 to 3 and USER handles 4 to 4 through stress. |
| No-hardware host harness | Compiled and scanned; 3 checks passed. |
| Full inert host/menu harness | Initial scanned run: 90 checks, 2 failures. On 2026-10-03, fixture writes were changed to refresh the host cache through its existing fresh-read path. Recompiled/scanned rerun: 90 checks, 0 failures. Production cache behavior and assertions were unchanged. |
| Fresh full DesktopStub validation host and broker, 2026-10-03 | Built with hardware providers included; both custom Defender scans reported no threats before execution. Normal product executables were not overwritten. |
| Smoke interruption cleanup, 2026-10-03 | Passed normal-exit and forced-termination child/grandchild cleanup checks. |
| Separate-process offline smoke, 2026-10-03 | Blocked by Defender quarantine of its copied DesktopStub.exe after initial help/configuration cases. This is not a completed smoke pass; later rendering/concurrency cases were not reached. |

An initial ad hoc managed compilation used forward-slash relative paths that
the old Framework compiler misinterpreted. Repeating with absolute Windows
paths succeeded; this was a test-command issue, not a product-source failure.

## Continuation, 2026-10-03

The two host failures were stale fixture reads: `WriteUtf8BomTextFile` writes
disk but does not invalidate DesktopStub's 250 ms read cache. Both the string
migration and visible CapsBlink checks ran against the preceding profile. The
test-only `WriteContentFixture` now writes and uses `ReadIniFileForMutation` to
refresh the cache, verifying the refreshed text equals the fixture. The same
90 assertions pass. Compiler output still has Windows SDK GDI+ shadow warnings.

Fresh host/broker compilation and pre-execution scans succeeded. The offline
smoke log completed version/help, four Startup-action rejection cases,
external-provider/missing-input rejection, existing-Startup-preference
preservation, and the legacy-preset CLI command. It then stopped with the Windows
virus/PUA blocking error. Defender identified the temporary smoke copy as
`Trojan:Win32/Bearfoos.A!ml` (Threat ID `2147731250`), with initial detection at
2026-10-03 06:39:06 +07:00 and successful remediation at 06:40:00 +07:00.

The threat record reports `IsActive=false`, `DidThreatExecute=false`, and
`ActionSuccess=true`; the copied executable is absent. Earlier test commands
did complete according to the smoke log, so that execution flag must not be
read as a claim that the test executable never ran. A scoped process check
found zero surviving test processes. The original validation build still
exists, but it will not be used to bypass the quarantine.

Evidence under `build/review-validation/`: `host-cache-fixture-run.log`,
`desktop-validation-compile.log`, `desktop-validation-host-defender.log`,
`desktop-validation-broker-defender.log`, `smoke-cleanup-oct03.log`, and
`desktop-offline-smoke-oct03.log`. Failed smoke directory preserved:
`C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-48f4c4b3786e4078a1accf25d942ccd4`.
No package, Startup, hardware, account or resident integration was enabled.
No exclusion, allow, restoration or external submission was performed.

Next: resolve/adjudicate this renewed detection before resuming execution of
the affected host. A clean custom scan alone is insufficient here. Once the
host can be tested, resume the exact separate-process offline suite; the old
four-pixel discrepancy remains unresolved and assertions must stay exact.

## Source and script continuation, 2026-10-03

The [local submission draft](defender-submission-2026-10-03.md) and
[offline startup audit](audit-offline-startup-2026-10-03.md) record the event-log
evidence and a later blocked attempt to read the original build. No sample was
uploaded or restored, and no affected host was rebuilt or executed.

The two YouTube Music source bodies were extracted into dependencies. Compared
with their tracked predecessors, the cleanup body is unchanged; the main body
only substitutes the explicit data-root and original-bound-argument variables.
Product parameter blocks are unchanged. All six entry/engine/test scripts
passed custom Defender scans. The four application ASTs parse, and 50 inert
wrapper checks pass independently under PowerShell 7.6.5 and Windows PowerShell
5.1. The fixture engines never access auth, clipboard, accounts or the network.
Linux/macOS execution remains unverified; CTest now registers these portable
PowerShell checks when the interpreter is available.

RepoTools compiled with warnings as errors, and the standalone DesktopStub
source checker compiled. Both were scanned without detections before their
source-only commands ran: project-map validation, workflow selection and all
1,747 DesktopStub source checks passed. The new help/version parent-wait runtime
regressions are compiled into RepoTools but remain unexecuted under the host
restriction. No previous bitmap or live-UI result is extended to this change.

The smoke helper also now fails when an expected executable disappears instead
of returning a skipped success. RepoTools was recompiled/scanned after that
change; its pure missing-file regression passed without starting a child
process. The source checks still pass 1,747 assertions. The regression is part
of the existing CI interruption-test command as well as a standalone guard-test
command. The interruption suite itself was not repeated in this continuation.

Additional logs in `build/review-validation/`: `ytm-*-defender.log`,
`ytm-inert-wrapper-pwsh.log`, `ytm-inert-wrapper-windows-powershell.log`,
`repo-tools-oct03-compile.log`, `repo-tools-oct03-defender.log`,
`source-check-oct03-compile.log`, `source-check-oct03-defender.log`,
`source-check-oct03-run.log`, `oct03-validate-project-map.log`,
`oct03-test-workflow-project-selection.log`, and
`oct03-smoke-process-guard-tests.log`.

## Detection-priority investigation, 2026-10-03

Following the user's new request to address detection before other rework,
Defender definitions updated normally to 1.459.523.0. One standard full-feature
host build from `53d9e32` compiled, was scanned without execution, and remained
readable at the same SHA-256 after PE metadata inspection. No source was split
into a DLL. The [comparison-build report](defender-review-current-build.md)
contains the exact identity, OS, scan result and local log names.

This is not a runtime smoke pass or a resolved false-positive verdict. The user
approved Microsoft submission of the comparison file and sanitized report,
completed sign-in and approved the CAPTCHA. Microsoft confirmed the submitted
case; final determination remains Pending. Its receipt is stored locally under
ignored `build/review-validation/microsoft-submission-receipt.json`. The older sample remains
quarantined. Existing automatic sample-submission preferences were read without
change; this task cannot claim that Defender itself never transmitted a sample.

## Persistence/editor continuation, 2026-10-03

| Check | Result |
| --- | --- |
| Inert DesktopStub host/menu harness | Compiled and custom-scanned; 114 checks pass, including actual configure-only calls against missing and write-locked temporary profiles. No resident entry point is called. |
| DesktopStub source checker | Existing scanned checker identity verified; 1,747 checks pass against the changed source. |
| Managed legacy fixture suite | Compiled with warnings as errors and custom-scanned; 150 checks pass. New editor tests cover read-only opening, typed rejection, failed replacement, multi-field repair and preserved external edits. No real tray/dialog is shown. |
| PhotoCollage / TaskSchedulerMigration | Both compile with warnings as errors and pass custom scans. Separate-process help, rejection of mixed tray/job modes and read-only inspection pass without creating INIs. |
| TaskSchedulerMigration local tests | Compiled/scanned; 29 checks pass. No Scheduler COM activation. |
| Project ownership / workflow selection | Both pass with the new shared configuration-tray dependency. |

An initial ad hoc test compilation used the non-profile overload of the Task
defaults API; the tray binding was corrected to use profile defaults. A later
ad hoc source-list extraction also included the test invocation's source-path
argument twice; deduplicating that command input produced a clean compilation.
Neither issue remains in the checked-in build commands.

Evidence: `configure-write-runtime-compile.log`,
`configure-write-runtime-defender.log`, `configure-write-runtime-run.log`,
`configure-write-source-check.log`, and `configuration-tray-*.log` in
`build/review-validation/`. Visible tray/editor behavior and the new host's
Startup transaction are not validated or complete. The full DesktopStub
process smoke, old exact-render discrepancy and target-Windows shell matrix
remain open. No protection settings were changed by this continuation.

## Startup and offscreen UI continuation, 2026-10-07

Fresh compile/custom-scan/run results under independent deadlines:

- Managed suite: 230 passing checks, with injected Startup transactions only.
- INI transaction fixture: 29 passing checks, including exact UTF-16 and
  original-absence rollback, competing writers, timeout, retry and disposal.
- Actual editor controls: 33 passing offscreen checks; no Show/Application.Run
  or NotifyIcon. Normal/minimum/expanded layouts and actual button handlers
  are covered.
- Local migration suite: 29 passing checks, without Scheduler activation.
- Both product builds: warnings-as-errors compilation, clean custom scans,
  and separate-process help/configuration/Startup-rejection guards pass.
- Fresh full native host/broker and RepoTools: scans passed with protection on
  after a normal signature update. Interruption guards passed. Exact smoke
  initially failed on the historical four-pixel case, then passed unchanged on
  a later control run. This is not stable renderer acceptance.
- Expanded GDI+ renderer: 19,019 passing checks, GDI 3 to 3 and USER 4 to 4.

Logs use `startup-oct07-*`, `native-review-oct07-*`, and `render-*-oct07-*`
under ignored `build/review-validation/`. The Microsoft-submitted executable
was not overwritten. No Startup-folder integration, package registration,
resident DesktopStub instance, account/provider or hardware operation ran.
Visible tray behavior and original target-OS shell appearance remain open.

## Managed Startup continuation, 2026-10-08

- Managed fixture suite: **274 checks passed** after warnings-as-errors
  compilation and a clean Defender custom file scan. The extra cases exercise
  the actual DNSAutoUpdate/CapsBlink/AsusBlink persistence and reload adapters
  with temporary profiles and a simulated shortcut platform.
- DNSAutoUpdate, CapsBlink and AsusBlink product source lists compile with
  warnings as errors. Their resident entry points were not run.
- Project ownership map and workflow selection checks pass for 14 projects
  plus the All selection, using a scanned RepoTools binary.
- Chrome's current PowerShell source and synthetic behavior checks pass in
  both Windows PowerShell 5.1 and PowerShell 7. A new regression rejects an
  unrelated same-valued counter when JSON escapes obscure the real property.
  Only selected function definitions were loaded; no real browser data,
  browser process discovery, or interactive product loop ran.
- The standalone C# Chrome engine fixture passes **220 checks**, with
  warnings-as-errors compilation and a clean Defender scan before the bounded
  30-second run. It covers exact backup bytes across six BOM/encoding forms,
  invalid byte rejection, property-path/duplicate-key ambiguity, competing
  edits, injected restarts and I/O failures, and mutex contention/abandonment.
  Logs use `chrome-engine-*-oct08-v2`. This verifies the engine slice only;
  it does not establish the unfinished launcher, tray or Startup surfaces.
- Defender antivirus and real-time protection were enabled; definitions were
  `1.459.576.0`. Logs are under ignored `build/review-validation/` with prefixes
  `resident-oct08-*` and `chrome-oct08-*`. Every executable run used an external
  deadline. No real Startup changes, DNS updates, or hardware actions ran.

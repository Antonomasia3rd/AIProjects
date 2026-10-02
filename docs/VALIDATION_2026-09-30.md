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

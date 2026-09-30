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
| Full host/menu integration and separate-process offline smoke | Not yet run in this staged pass. The earlier four-pixel separate-process discrepancy is not resolved by an offscreen-suite pass. |

An initial ad hoc managed compilation used forward-slash relative paths that
the old Framework compiler misinterpreted. Repeating with absolute Windows
paths succeeded; this was a test-command issue, not a product-source failure.

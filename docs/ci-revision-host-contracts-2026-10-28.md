# Revision-host contracts and Registration diagnostics — October 28

The hosted Revision suite at source `2c2bb84` reported 30 failures. The first four
were stale migration expectations: a numbered Content.Count=8 composition was
expected to turn into eight unrelated independent tiles. The reviewed October 26
upgrade policy intentionally retains such compositions on the legacy engine.

The fixture now separately checks the retained complex case and a convertible
Desktop-only case. The latter still exercises backup-destination refusal,
exact original BOM/bytes, one main Registration wallpaper tile, preservation of
dormant provider preferences, and idempotence. It does not manufacture RSS,
Prayer or game tiles from saved optional settings.

The complex fixture additionally exposed an actual no-op publication issue:
DesktopStub's mutation wrapper unconditionally rewrote a successful callback's
unchanged text as UTF8, re-encoding a retained UTF16 source. The narrow wrapper
now skips publication only for an already-read ordinary existing file with the
same path and identical resulting text, and still refreshes its cache. Missing
files, changed text/paths, failed callbacks and publication failures keep their
previous behavior. Shared INI code and other products are unchanged.

Private regressions cover exact UTF16 bytes and write time, same-value canonical
assignment/cache, a rejected changed callback, replacement-locked no-op versus
changed-write failure, empty missing-file creation and missing-parent failure.

## Registration evidence and boundaries

The exact committed source was exported below ignored artifacts/ci-source-2c2bb84
to avoid mixing the in-progress Discord service ABI with old dependency objects.
Only synthetic files and injected deployment callbacks are used. No real
package, account, Startup, hardware, Explorer launch or resident was touched.

Before these test repairs, the normal-temp export run passed every Registration
assertion and failed only the four migration expectations (1,263 total checks).
A temporary 8.3 alias run had the same four plus two comparisons of the correct
expanded physical deployment paths with their short lexical expectations.
Those assertions now expand the expected existing paths with GetLongPathName;
the physical-parent negative check and all production guards remain intact.

The local alias did not reproduce the hosted 26 Registration staging failures:
actual package preparation succeeded in both contexts. Preparation now emits a
test-only failure diagnostic plus a non-identifying owner classification
(current user, Administrators group or another owner). The elevated hosted
runner's default file ownership is a hypothesis until that diagnostic supplies
evidence. No ownership check has been weakened to make CI pass.

The final stable export compiled with optimized `/Ob1`, passed its Defender
scan, and passed all 1,282 extended host checks in both normal temporary paths
(18.6 seconds) and a distinct 8.3 temporary alias (11.1 seconds). Both retained
the fixture's existing internal watchdog. Receipts are under
`build/review-validation/ci-revision-normal-noop-final.log` and
`build/review-validation/ci-revision-short-noop-final.log`; the scan receipt is
`build/review-validation/ci-revision-noop-final-scan.sha256`.

Hosted acceptance remains separate and needs the next diagnostic run. The
unchanged four-pixel raster witness is not relaxed or closed by this repair.

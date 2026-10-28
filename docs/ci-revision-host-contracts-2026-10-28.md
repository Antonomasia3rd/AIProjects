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

## Hosted follow-up at `1ee1c4e`

Run `40687272287`, Windows job `122813217667`, stopped before the Revision
fixture. `ContentRuntimeTests` reported 24,419 checks and one failure:
`failed import publication reports a configure-only error`.

That hardware-import fixture had already applied its exact command, then held
the output INI against replacement and repeated the identical command. With
the no-op preservation fix, the repeat correctly succeeds without publication;
it cannot demonstrate a blocked changed write. The test now asserts that
unchanged success, changes the pending Caps interval from 900 to 901, and then
requires publication to fail while locked. After unlocking it checks unchanged
saved contents and a successful retry that actually persists 901. This is a
test-only correction. The hosted Registration error and owner diagnostic remain
unreached until a subsequent hosted run passes this earlier fixture.

The frozen main source then rebuilt DiscordService, DiscordEditor and
AsusService objects before building either host, using optimized `/Ob1` and the
existing 240-second compilation limit. Both final binaries passed Defender
scans. `ContentRuntimeTests` passed all 24,420 checks in 29.1 seconds;
`ContentRevisionHostTests` passed all 1,282 checks in 13.3 seconds. Intrinsic
45-second fixture watchdogs were unchanged. No native package, account,
Startup, hardware or resident operation was performed. No production ownership
or path validation was changed for this follow-up.

Fresh main receipts under `build/review-validation/`:

- `ci-main-DiscordService-20261028-build.log`
- `ci-main-DiscordEditor-20261028-build.log`
- `ci-main-AsusService-20261028-build.log`
- `ci-main-ContentRuntimeTests-20261028-build.log`
- `ci-main-ContentRuntimeTests-20261028-tests.log`
- `ci-main-ContentRuntimeTests-20261028-scan.sha256`
- `ci-main-ContentRevisionHostTests-20261028-build.log`
- `ci-main-ContentRevisionHostTests-20261028-tests.log`
- `ci-main-ContentRevisionHostTests-20261028-scan.sha256`

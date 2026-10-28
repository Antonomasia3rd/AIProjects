# Broad inert host contracts — October 28

Hosted run 40663375967 reached the broad ContentRuntimeTests fixture after the
temporary-path and /bigobj repairs. Its first failure still expected factory
Desktop delivery to be LiveTile. The user deliberately restored Registration
on the original Main package, and the beta cold-start evidence verifies that
policy. The factory assertion now requires Registration and an explicit Main
target, retaining the wallpaper, primary identity, branding and startup checks.
No production default was changed to satisfy the test.

The same hosted run exhausted the existing 45-second fixture watchdog. It spent
about 14.3 seconds in configuration snapshot publication and another 14.3 in
preset/import-confirmation checks, reaching offline persistence at 45 seconds.
The workflow now compiles the inert content suite with CONTENT_OPTIMIZE=1 and
/Ob1, matching the release-style local validation. This does not define NDEBUG,
remove checks, change fixture iterations, relax pixels or increase watchdogs.
The first optimized local run completed in 24.386 seconds with 24,417 checks
and one remaining failure; no four-pixel renderer discrepancy surfaced there.

That remaining failure was isolation: earlier broad groups intentionally write
Assets in the common temporary root, while the final configure-only check
treated the common root as initially empty. Offline persistence now creates its
own new private subdirectory. It also temporarily scopes the host executable
path to that directory, retaining the actual executable basename: GetExeDir is
the potential generation destination, so the assertion observes the directory
where a mistaken render would actually write. Both INI/executable paths are
restored afterward. Its existing failure-initialization, locked-write,
exact-byte rollback, successful persistence and no package/image assertions are
unchanged. Cleanup deletes only the fixture INI and its known migration backup
names, then removes the empty child directory nonrecursively; unexpected asset
directories remain evidence instead of being silently erased.

The final broad host compiled successfully, passed a fresh Defender scan and
completed **24,419 checks with zero failures in 21.383 seconds**, within the
unchanged 45-second watchdog. Its expected missing-parent, directory-target and
locked-write errors were followed by successful persistence/cleanup checks.
The existing strict renderer checks were unchanged; the historical four-pixel
discrepancy did not surface in this run and remains independently unresolved.
Hosted acceptance still requires the corresponding corrected Actions run.

All validation
uses injected providers and a temporary configuration; no resident, user
settings, account, package, Start pin, Startup or hardware integration is used.
The separate historical four-pixel witness and its tolerances are untouched.

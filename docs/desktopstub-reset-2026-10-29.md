# DesktopStub reset — October 29

The user explicitly requested another fresh start, superseding the prior request
to preserve their October 28 setup. The installed 27.0.0.5 local candidate was
retained. No DesktopStub process was running before the reset.

The single current-user Main package was verified against the installation root
and publisher and removed. Active settings, backups, manifest, generated assets,
caches, test-build files and the installation's Main profile registry were moved
to the ignored recovery directory:
`artifacts/desktopstub-reset-20261029-051900-d69072a7/`.
Moves were confined to the verified installation/profile scope and archive;
reparse points were rejected. The active folder contained only the seven payload
files before launch. No unrelated package, photograph, OAuth client file, browser
account, other installation/profile scope or source change was reset.

All three installed executables passed actual Defender scans. A normal launch
then created fresh settings with exactly one Desktop Wallpaper tile, primary
Registration/Main, and both sign-in startup methods off. No Discord source from
the previous configuration was restored. New Medium/Wide/Large wallpaper images
were generated at 150x150, 310x150 and 310x310. COM registration used its existing
PowerShell fallback, which completed successfully. The new 27.0.0.5 package and
its Desktop Start entry were verified; the resident was left running.

This is now an initialized fresh session, not an absent-INI state. Future work
must inspect later changes before assuming these defaults still apply. The
local build remains a candidate, not an Actions-attested release.

Private receipts are `removed-packages.json`, `launch.json` and `reset-result.json`
inside the recovery directory. Previous configuration is inactive and is not
automatically restored. The previously rejected temporary-fixture deletion was
not retried as part of this reset.

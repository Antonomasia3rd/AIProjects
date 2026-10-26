# One main checkout — October 26

The user requested one active checkout/branch and authorized coordination with
the other chat, **Restore the Windows back button**. The primary directory is
`D:\Users\Amiya\AppData\Local\Programs\Scripts\AIProjects`, using `main`.
The root AGENTS.md records this preference and shared-file/commit coordination.
One checkout reduces navigation confusion; concurrent edits still need care.

Main includes the DesktopStub branch through `2027b1e` and the other task's
seven-file checkpoint `260016f`, picked as `6f49dd1`. Only that checkpoint was
picked: merging its older branch history would also import the temporary
DesktopStub revert. The resulting DesktopStub/dependencies tree was checked
against the delivered beta2 source and matches. Private analysis/build/runtime
files were preserved; analysis/ is now ignored to prevent accidental uploads.

The LogonUI candidate remains an uninstalled research checkpoint, with its
documented unload, native Back and architecture limitations. Consolidation
does not promote it to a production-tested mod.

The extra managed DesktopStub checkout is archived only after all its needed
source is committed and incorporated into main. Its ignored beta binaries/ZIP
already have verified copies under the primary checkout's releases directory;
the installed DesktopStub build remains at its usual primary path.

GitHub Actions release validation is the next delivery gate. Existing CI has
build/test/checksum/provenance machinery; release success must be established
from the hosted run, not inferred from local tests. Preserve the distinction
between beta/draft artifacts and stable releases. Current DesktopStub native
validation and fresh-reset evidence remains in
[the beta2 record](desktopstub-beta2-2026-10-26.md).

The mobile summary of unresolved DesktopStub work remains: 1809 wallpaper/icons;
Calendar layout/selection/localization/reminder parity; broader account providers
and mail popup alerts; real Kuro session stability/additional login regions and
games; full Notes parity; lock-screen/Widgets, Window Capture, lyrics, weekly
timetable and fitness; charging ETA/Smart Charging; the unchanged four-pixel
renderer discrepancy and remaining native Windows/account/hardware acceptance.
Performance remains completed for now. Properties, Profiles integration,
configurable WuWa stale age/retry thresholds and ordinary Registration/Main
fresh setup are implemented, not missing features.

# Graceful stop checkpoint, October 23

**Resumed and installed:** see [the final installation record](desktopstub-installed-2026-10-23.md)
for successful native setup, reset backup, final checks and the retained legacy
renderer failure. The pause/intermediate details below are historical.

## Resumed follow-up

The user resumed. The per-frame Gallery ClickAction/owner-hash optimization is complete (181 portable runtime checks). Bootstrap recovery/quiet-shutdown/queue-lock races were fixed and covered by inert assertions. Native syntax and the first combined build passed; its single Gallery XML failure was a bad fixture asset/tag and was corrected without product changes or weakened checks. The resumed Core run reached 24,669 checks with one stale fresh-preset expectation. The offline smoke then found a real missing/empty-file preset preview bug: the shared reader returned success+empty for a missing INI, so preview used the wrong model and could create a rejected profile. That preflight now mirrors factory initialization under the profile mutex; new boundary tests and the core expectation were updated. Product, Core and combined NewSource are rebuilding with that fix. No installation/reset has happened yet.

The user clarified RSS notifications mean **desktop popups/balloons like Notify when generation starts**, which matches the implemented Alerts backend; the guide now explicitly distinguishes this from Live Tile notification updates. Source guards passed 1,927 checks after adding the new helper files to the UI-source audit. Shared Discord service/editor objects were rebuilt and final host objects are relinked against them. The reset draft now handles zero residents, checked click-directory backup/restore, approved hash binding immediately before native setup, guaranteed recovery launch after rollback errors, and requires actual Live Tile success in health logs. It still must not run before final gates.

Latest usage reading is 60% consumed on the available weekly window; no shorter-window reading is returned. Keep focus on installing the priority build before starting lower-priority suggestions.

The user explicitly requested a graceful stop to change networks. All agents were stopped or had already finished. Do not resume work until the user resumes the task. No installation, profile reset, commit or push was performed in this pass. The canonical DesktopStub remains the previously installed build; all edits are local and uncommitted.

## Completed implementation, awaiting final integrated gates

- Per-tile Prayer/RSS notifications, notification API selection/progress, safe source failure/recovery notices, gallery shrink/unpin defaults, and Prayer typography/AM-PM fixes.
- Sunrise timetable/countdown selection; independent Clock/world clock, local-file Calendar and overseas WuWa sources. Calendar file work is asynchronous and optional; no account access or file worker for date-only tiles.
- Gallery INI batching and safe suppression of an exactly repeated complete queue after all slot acknowledgements and successful durable ledger commit. Startup, force, lifecycle, configuration, epoch, files and Auto DPI context invalidate proof. A stale-final-ack interleave is fixed and tested.
- Latest user regression: restore the primary Desktop's original embedded bitmap/geometry and keep the Gallery Photo topic separate. New profiles are Desktop-only, Wallpaper, LiveTile, NameAndLogo, with startup off. Existing profiles are not reseeded.
- Confirmed standalone Discord defaults restored for new tiles: Details `{win_title}`, State `CPU: {cpu}, RAM: {ram_used}/{ram_total} ({ram_pct})`, activity name `a game`. Existing explicit values remain untouched.
- Explicit first-run `--setup-desktop`, discoverable tray setup, posted IPC and completion handling, one main-package setup job, interrupted-marker recovery and packaged relaunch. Native setup has not been exercised.
- Gallery exact-image activation via Windows chaseable notification metadata, opaque ownership-checked records, safe original-file resolution and Image/Setup preference. Static Registration or absent exact metadata uses an honest folder/settings fallback. The API's actual FullTrust/ExplorerPatcher behavior is unverified. Configured RSS chaseable source activation is included.

## Latest incomplete edge at interruption

The gallery activation agent reported that its initial `GallerySnapshotForFrame` read the gallery configuration once per image merely to find ClickAction. It had begun a narrow optimization to carry ClickAction on the runtime Frame and cache the profile/user owner hash. The user stop arrived during that follow-up. Inspect the current tree for consistency of those fields before compiling; do not assume the earlier "frozen" statement includes this last optimization. Agent `/root/reminders_sunrise` has the precise status and was interrupted. `/root/kuro_notes` finished bootstrap code/tests wiring and was interrupted before issuing its final result.

## Validation already recorded

Before the latest Desktop/bootstrap/click changes: 24,533 core host checks, 466 revision host checks, 82 new-source host checks, 3 no-hardware checks, 1,919 source guards, 1,043 notification checks, 69 instance checks, 64,236 Clock/Calendar checks, 86 alert checks, 226 Prayer model +21 source, 67 WuWa +20 HTTP, 4,194 INI batch equivalence and 295 gallery lifecycle checks passed with scans. The gallery exact-repeat final portable fixture passed 175 checks. The latest Desktop artwork gates passed 697 portable and 286,390 offscreen checks; Discord factory tests passed 149 plus concurrent read loops; Gallery image activation portable tests passed 27.

These are component/baseline results, **not a final integrated pass of the latest tree**. New PrimaryDesktopDefaultsHostTests, DesktopHostSetupHostTests and GalleryImageActivationHostTests still need final integrated execution. All three are wired into ContentRuntimeTests (new-source helpers under the NEW_SOURCE fixture, primary defaults under Core). Preserve the 45-second host watchdogs and exact pixel tolerances.

## Exact resume sequence

1. Finish/check the small Gallery activation optimization, then get both bootstrap/click agents' final status. Do not mutate active `.cmd` scripts during compilation.
2. Build final separately named product artifacts with `DesktopStub/BuildDesktopStub.cmd`, setting host `DesktopStubOct23Review.exe`, broker `DesktopStubOct23ReviewLiveTileBroker.exe`, launcher `DesktopStubOct23ReviewTileLauncher.exe`. Existing files at those names are intermediate, **not installable final artifacts**.
3. Build/run scanned Core, Revision, GalleryLifecycle and combined ContentNewSourceHostTests plus appropriate focused gates and no-hardware/source checks. Shared host objects/targets must not be built concurrently by two agents. Optimized fixtures use CONTENT_OPTIMIZE=1 and `_CL_=/Ob1`; product uses its normal optimized settings.
4. Use the updated scanned runner `build/review-validation/RepoToolsDesktopFactoryOct23.exe` for default offline `smoke-windows-build --projects DesktopStub --desktopstub-binary <final host> --desktopstub-broker <final broker> --repository-root <root>`. Never use `--allow-package-integration`. The runner contains explicit legacy profile seeds and separate fresh-factory assertions; no tolerance/assertion weakening. Runner hash B24ECE9A17379B211FAE1444DF7D4E28AA21A835BCF81CB5C06EFD8DCE6A5BA4.
5. Regenerate a fresh Desktop-only INI using the final binary's configure-only path and bind final binary/INI hashes. The ignored staging script `build/review-validation/InstallSourcesAndResetOct23.ps1` is a **draft**: it still needs optional zero-resident handling, archive/rollback support for `<ini>.gallery-clicks` directory with resolved-path/reparse checks, and the new explicit unpackaged `--setup-desktop` path to refresh the main Desktop icon before packaged startup.
6. Only after the final gates: gracefully stop the canonical resident, archive INI/accounts/gallery/alert/click sidecars and old binaries/manifest together, install, perform the authorized fresh reset while retaining source photos and Start pins, relaunch and verify actual primary native publication for 75 seconds. Always restore/relaunch on failure. No new source/account should activate after the reset.
7. Record final hashes/backup/health and a short explicitly limited CPU/private-byte sample; update CURRENT_STATUS/NEXT_PASS, commit/push the authorized branch. Historical Win+L attribution, controlled old 4 MiB wallpaper-only comparison, actual auth and native visual acceptance remain separate.

The user's later optional suggestions are prioritized in `tile-followups-2026-10-23.md`. The user confirmed **weekly timetable** (days by hours) for the Calendar grid. Font selection, lyrics, timetable, supported fitness API feasibility and Battery are lower priority and have not been implemented. Do them after priority fixes, as requested.

Usage was last checked at 35% consumed in the available weekly window; no shorter-window reading was returned. Recheck on resume and at major milestones. Detailed evidence is under `build/review-validation` and the dated feature documents.

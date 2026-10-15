# Tile and gallery follow-up, 2026-10-15

The user's screenshots confirmed working gallery pinning/photos, transparent
fallbacks and normal CPU. This change addresses gallery scale/folders, pinning,
menu structure/wording, tile information and game layout. It does not investigate
or tune the reported-normal system CPU.

## Galleries and pinning

The previous eight-tile application limit is now **1,024 configured tiles**.
This is a practical resource/Win32 command-ID bound, not an unlimited promise or
a Windows tile-count limit. Windows' separate five-notification queue remains.
Tile menus are divided into pages of 16. Larger initial setups use numbered
names instead of opening 28 naming dialogs.

Open **Tiles > Photo galleries > your gallery > Number of tiles...** to increase
the existing eight to 28. Expanding already-owned pools needs redistribution:
the app asks before briefly clearing historical photo queues and republishing
ownership. Native IDs, labels and Start positions are retained. Successful
clearing of every historical destination is required before an explicit full-CAS
ledger reset. Generation is drained and gallery publication suspended first.
Failure to clear preserves ownership/configuration and resumes publication before
showing an error. No redistribution was performed on the user's installation by tests.

**Add another gallery folder...** appends a separately named group, for example
`C:\Windows\Web`, with its own folder and chosen tile count. Folder and group
caption are stored as `Gallery.Tile.N.Folder` and `Gallery.Tile.N.GalleryName`;
blank overrides preserve existing `Gallery.Folder` profiles. One authoritative
ledger still prevents exact image IDs from acquiring multiple owners across
overlapping folders. One background worker handles distinct folders in bounded
round-robin passes. An unreadable new folder does not block a working folder.

Subfolders are scanned by default. Traversal is bounded to 32 directory levels,
4,096 supported images and the existing total-entry/time/byte budgets. Directory
reparse points and the cache subtree are skipped. This makes an OS image folder
with nested Screen/Wallpaper directories useful without changing source files.

First setup and adding a gallery start a serial pin batch. **Pin all gallery
tiles...**, each gallery's **Pin all tiles in this gallery...**, and Quick Actions'
**Pin all...** reuse that queue. Windows shows one confirmation at a time. No or
cancel declines that item and proceeds to the next. There is no visible custom
SecondaryTileOwner window. Native picker acceptance remains a local Windows test.

Pinned source/gallery destinations now drive updates. Old master/per-tile
Enabled fields remain readable for compatibility but do not pause pinned tiles;
the corresponding update checkboxes are removed. Use Start's Live Tile display
control or unpin a tile. Data-source/hardware settings keep their own meaning.

## Menus and wording

Grouped first-level categories have no internal separators. Flat categories use
separator, disabled `Category:` heading, then settings. Existing command IDs,
checks, disabled states, advanced fields and menu-object cleanup are retained.
An intermediate sanitizer mistakenly read submenu child-count bits as separator
flags; the full menu reachability tests caught it. It now uses MIIM_FTYPE, with
explicit grouped/flat structural regressions.

The cycle switch is **Cycle contents on the Desktop tile**. The independent
choice is **Desktop tile cycle method**: it selects the app timer versus Windows
queue policy. It does not enable cycling or control separate photo tile animation.
The chosen DesktopStub interval applies to its timer; Windows owns native timing.

**Application > Interface wording** can export missing defaults to `[Strings]`,
edit any named Strings key, or replace exact dialog wording. Menu commands use
`Menu` plus their numeric ID, for example `Menu19002`; `{value}` retains the
setting's current value. Submenu title keys and dialog literal `Text` keys are
stable hashes of their original text. Existing named string keys remain valid.
All use normal `--set Strings.Key=Value` and INI storage.

Common app-owned dialog titles/prompts, message boxes, labels and caption windows
use literal overrides. Editable values, schema values and the hidden resident's
identity are excluded. Literal message bodies are not exported automatically
because they may contain current source data. Windows picker text and website
content remain owned by Windows/the website. The shared input dialog's OK/Cancel
labels retain their existing named Strings controls.

## Tile contents and game artwork

Tile information uses the same structured RSS/Notes/media fields and three-row
Medium/Wide versus five-row Large limits as rendering. Tile blocks have blank
lines between them. It describes prepared contents and queued photos explicitly;
it cannot observe which notification Windows currently displays.

Game resource/activity rows have consistent left-hand icon columns. The original
Real-Time Notes ICOs are embedded, with MIT attribution/provenance under
`dependencies/third_party/RealTimeNotesAssets`. Adaptive XML and bitmap fallback
use those same per-row icons. There is no extra runtime DLL or loose icon-file
dependency. Missing artwork has a bounded fallback.

## Validation

Fresh normal host/broker compilation and bounded scan-gated tests are recorded
in the parent validation logs. Selected initial passing scopes: 778 gallery
model, 74 injected runtime, 93 private native image, 89 secondary/batch, 42 Notes
rows and 58 media-layout checks. The combined host includes actual private
ledger/config file transactions with an injected Windows-clear boundary, proving
failed clearing preserves old pools and confirmed expansion publishes 28 stable
IDs. Renderer tolerances and the historical opaque four-pixel gate are unchanged.
The final build and installed identities are recorded below.

### First startup failure and correction

The first new review build passed offline checks but crashed on real startup at
21:20. Its SHA256 was
`F17C473BEDB6A27926D6687AC83E40A74619BBADEB1E8117EFE0D74C7A030D34`.
The prior working host/broker were restored and relaunched immediately, retaining
the current INI and gallery ledger. The private dump was read locally, never
uploaded. A map relinked from the original object files located the exception:
`Gdiplus::Image::GetLastStatus` received a null `this`, from `LoadNotesTileIcon`,
called by `NotesTileIconUri` during `BuildLiveTileXml` on the generation worker.

The new icon helper had used GDI+ after the renderer's local scope ended. With
`KeepGdiPlusLoaded=false`, GDI+ was already shut down. Notes and media-app icon
URI helpers now own their own `ScopedGdiPlus`, enclosing bitmap construction,
save and destruction; bitmap allocation also has a null guard. The regression
starts with zero GDI+ references and a fully unloaded token, creates fresh Notes
and media PNGs without an outer scope, verifies the helpers release their scopes,
then decodes those files under an independent later scope. Earlier fixtures held
an outer scope and therefore masked the defect. The corrected host fixture
passed **14,334 checks**, including this regression, in 27.3 seconds.

### Final validation and installation

| Scope | Result |
| --- | --- |
| Normal host and broker | Fresh compilation succeeded |
| Combined inert host | 14,334 checks; zero failures |
| Adaptive renderer | 5,630 checks; zero failures; exact historical assertions retained |
| Gallery model / runtime / private native inventory | 778 / 74 / 93 checks; zero failures |
| Secondary pin batch / Notes rows / media layout | 89 / 42 / 58 checks; zero failures |
| No-hardware host / DesktopStub source guards | 3 / 1,764 checks; zero failures |
| Full copied-binary render/configuration smoke after the icon fix | Exit zero, including local concurrent INI writes |
| Defender custom scans | New host, broker, fresh runner and installed canonical binaries clean |
| Real resident startup | PID 35128 survived the 75-second guarded health check and subsequent readback; one resident |

The smoke retry first found a stale October 3 runner rejecting newer dependency
ownership. A fresh runner was compiled from the unchanged current RepoTools
source, scanned and used for the successful final run. No production assertion
was weakened. The installed host generated 16 original game-row PNG assets; the
recent log had no failure markers at the final readback.

- Host SHA256: `144058DF089D34D0828F14F695FA0C176AB06F393501A8E75D9F5F2A3F577326`.
- Broker SHA256: `D796048B07F0890FDB3E2FF93F9944247B674DE70150803885ACC661DC3F07EA`.
- Prior binaries and private sidecar backup:
  `DesktopStub/build/ReviewBackupOct15Feedback-20261015-213643`.
- Final smoke log: `build/review-validation/feedback-fixed-fresh-full-smoke.log`.
- Corrected host log: `build/review-validation/feedback-icon-fix-host-test.log`.

The installer preserves current sidecars, scans canonical paths and relaunches
the normal profile. Its startup health check independently restores/relaunches
the prior binaries on failure, without rolling user data back. The successful
installation did not resize the user's eight tiles, add a folder, reassign any
photo pools, request new pins, change accounts or exercise playback/hardware.
No computer input was issued.

### Remaining boundaries

The user still needs to accept native batch prompts, visible flat/grouped menus,
game-row layout and an actual 28-tile/multiple-folder setup. Synthetic fixtures
establish model/transaction/queue behavior, not Windows shell presentation at
every scale. Native codec calls remain cooperatively bounded rather than hard
preemptible. The original repository-wide review remains open in CURRENT_STATUS.

Redistribution clears notifications before changing ownership, preserving the
no-duplicate invariant, but the ledger and INI are two separate atomic files.
An INI write failure after a successful explicit reset can leave a fresh epoch
with the previous INI; a cross-file recovery journal/rollback is still open. The
clear-failure fixture proves preservation before reset; it does not prove a
joint atomic commit of both files. External concurrent gallery edits during an
add/resize dialog also need a full expected-configuration comparison, matching
the protection already used by first setup. These are recorded follow-ups, not
claims closed by the current passing tests.

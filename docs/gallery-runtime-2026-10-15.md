# Gallery coordinator and host integration, 2026-10-15

Later integration checkpoint: the coordinator is now included after content state
and before the secondary API. Generation, composition, slot delivery/retention,
fit/cache guards, and owned shutdown hooks passed coordinated compile, scan,
inert and copied-binary smoke gates. See the [combined validation](gallery-validation-2026-10-15.md)
for installation evidence and the remaining real-shell acceptance boundary.
GalleryHostTests, GalleryLedgerHostTests, and GallerySetupHostTests are registered
in the existing bounded host harness. Runtime pending/idle planning is cached;
no-progress incomplete inventories back off 30 seconds, while genuine deferred
cursor progress resumes after 250 ms. Per-member TileAppearance uses shared
Transparent/Name defaults. Activation carries the selected --ini profile.
No real photo, pin, provider, or user INI was exercised by this agent.

New source only: `dependencies/DesktopStub/ga_gallery_runtime.inc` and
`tools/GalleryRuntimeTests.cpp`. The product includes it before the secondary
API; shared source frames and gallery frames enter the same generation batch.
No actual photo, GUI, account, pin or user INI was used by the source subtask.

The injected core lives in `aip::gallery::runtime`. Define
`AIP_GALLERY_RUNTIME_NO_HOST` before including the new `.inc` to compile only the
portable core and injected inventory types. `Coordinator` takes `Services` for
ledger read, compare-and-save, create-only initialization, scan, cache-directory
selection, time, and wake notification. Wake callbacks must notify only; they
must not synchronously reenter the coordinator. The native wrappers have no
initialization-time filesystem/provider effects. The default inert host wrapper
does not refresh the source; tests construct an injected coordinator directly.

Approved public host hooks are:

```cpp
static void RefreshGallerySource();
static std::vector<aip::content::Snapshot> CurrentGalleryFrames();
static bool ValidateGalleryFrame(const aip::content::Snapshot&);
static void AcknowledgeGalleryFrame(const aip::content::Snapshot&);
static void FailGalleryFrame(const aip::content::Snapshot&);
static void StopGallerySource();
static void RequestGalleryRefresh(bool reloadLedger = false);
static bool ReadGalleryConfiguration(aip::gallery::Configuration&, std::wstring& error);
static std::wstring GallerySourceStatus();
static std::vector<aip::gallery::runtime::MenuState> GalleryTileMenuStates();
```

Include after `ga_content_state.inc` and before `ga_secondary_tile_api.inc`.
The content state forward-declares LocalAppDataPath, whose definition follows
in the live-tile module. Call Refresh before ContentEngineTick; call Stop
before global teardown, and before destroying notification/wake state.

Gallery requires `[Gallery].Enabled=1`, `[Content].Enabled=1`, `[Tiles].Enabled=1`,
and direct Live Tile delivery with package identity. Otherwise it publishes no
frames, cancels pending inventory work, and retains ownership. It does not start
registration helpers or change the seven existing contents. The native reader
copies one full cached INI document under the existing named/cache locks. The
schema is `[Gallery]` and `[Gallery.Tile.1]` through `.8`; relative folder paths
resolve against the active INI directory. Root owns typed CLI routing and whole
batch validation of these options.

The authoritative ledger is always `absoluteIniPath + ".gallery-ledger"`.
Changing the folder or GroupId never selects a different ledger. Missing,
malformed, conflicting, inaccessible, or mismatched history is a setup/error
state, never permission to start with an empty ledger. Explicit initial setup
uses:

```cpp
CreateGalleryLedgerForProfile(profile, cfg, epoch,
    historyKnownNewOrRetired, error);
PrepareGalleryConfigurationSettings(cfg, settingBatch, error);
```

The first function requires affirmative no-prior-history/all-history-retired
attestation. It creates only, under the named ledger lock, with flushed temporary
bytes and a rename without REPLACE_EXISTING. It cannot replace an existing valid
or corrupt ledger. The second returns a validated settings batch and writes
nothing. It does not erase inactive old slots, alter credentials, enable Content
or Tiles implicitly, pin anything, or start scanning. Root's setup transaction
must stage its intended Content/Tiles/layout options and initialize ownership
before enabling publication. After successful setup, request a ledger reload.
There is deliberately no ordinary runtime reset/overwrite helper.

Native saves take the ledger's named file lock, bounded-read and parse the fresh
file, and invoke the same `PrepareLedgerCas` used by injected fixtures. It compares
the complete expected/fresh ledger, including edits that failed to increment a
revision. It rejects epoch/group resets, nonadvancing revisions/counters, dropped
historical tiles, and released/reassigned/reordered image reservations. Only then
does the shared atomic flushed byte writer replace the file. Ownership commits
and pending windows must both succeed before any frame becomes visible through
CurrentGalleryFrames. Generation callbacks never perform disk writes.

The inventory worker uses the CPU agent's `ScanGalleryInventory` contract. There
is one owned thread. Matching noncancelled partial results retain `.index` so
cursor/listing progress continues after a short delay; `.images` becomes active
only after a complete pass. The previous complete inventory remains usable while
a background refresh is incomplete. Completed work is joined before adopting its
result. Folder/profile/group changes cancel old work and reject its late result;
normal disable does not join active work on the caller. Shutdown cancels and
joins it before captured/global state teardown. WIC initialization belongs to the
native helper. Its time budget is cooperative between codec stages, so shutdown
has no hard preemption guarantee if a native codec itself does not return.

Native thumbnail storage is under
`LocalAppData/DesktopStub/Gallery/<normalized-INI-identity-hash>`. There is no
temporary-folder fallback. Cached immutable PNGs are never deleted by this
coordinator. A completed inventory refresh is normally scheduled every 30
seconds; incomplete work resumes after 250 ms, failed work after five seconds.
No image decoding occurs in generation or menu construction. Root can request
an explicit inventory/queue refresh after setup or pin/update.

Every ready pending window becomes zero-based slots `0..count-1`, with native
tags `g1..g5`, target `AipGallery_<group>.<tile>` (64 characters maximum), Image background, text Off, and
`--open-folder <quoted absolute folder>` activation. Snapshot gallery fields are
the approved group/TileID/epoch/ticket/slot/count plus galleryFrame/galleryContain.
Queue layout includes destination/epoch/count/fit. The first slot resets on
startup or queue-count change; equal-size later windows replace the stable tags.
Frame keys also include profile, attempt, image, display name, and activation.

Success callbacks collect all distinct slots. Only after all slots succeed does
the poller persist model acknowledgement and advance the cursor. Callback receipt
itself does not mean Windows displayed the images. Failure keeps the same durable
ticket and images, invalidates all success bits/old frame keys, and backs off five
seconds. The retry is a distinct attempt, so old callbacks cannot combine with a
new reset. Ledger reload/configuration changes similarly invalidate attempts.
Failed acknowledgement persistence suppresses duplicate frames while retrying the
save; process restart resumes the durable pending window. Queue shrink/missing
pending assets is repaired by failed model acknowledgement before a new window
is prepared, retaining every image owner.

The host integration contract, implemented in the shared host fragments, is:

- Append gallery frames to the secondary batch without changing the primary or
  six source frames, including when existing composition branches return early.
- Distinguish secondary queue slots by destination AND tag/frame key; the current
  one-key-per-destination delivered map is insufficient for five gallery slots.
- Preserve slot order, validate immediately before target delivery, call success
  only after that target's update succeeds, and call failure on failed/skipped
  active attempts. Old retries must not reset a newer queue after validation.
- Bypass primary native-queue eligibility/layout checks for gallery frames while
  applying current Gallery config, native destination, and direct-delivery guards.
- Retain generated assets for every target/tag and old queue lifetime, preserve
  contain/fill behavior, and avoid global primary/source cache clears for a
  gallery reset. Existing queue images remain reserved even when originals vanish.
- Add canonical Gallery native pin metadata, per-group/member setup UI, explicit
  safe initialization, generic INI/CLI routing, and validated folder activation.

Injected fixtures cover explicit create-only initialization, two competing
writers through the real CAS decision, unchanged-revision conflicts, append-only
owner/reset guards, failed ownership and pending persistence, per-slot ownership,
all-slot/no-callback-I/O acknowledgement, partial failure/backoff/stale attempts,
restart, source/direct disable, missing history, pure menu reads, partial-index
continuation, and cancel/join before teardown. Static whitespace review passes.
The source subtask did not execute binaries. The parent subsequently completed
the [combined build, scans, fixtures and installation](gallery-validation-2026-10-15.md).
Actual native picker/pin/queue appearance remains local acceptance.

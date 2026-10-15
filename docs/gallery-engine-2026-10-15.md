# Gallery ownership and queue model, 2026-10-15

This is the pure model for the user's **Separate pools per tile** choice. It owns
assignment and scheduling policy; the separate inventory, coordinator and
[setup layer](gallery-setup-2026-10-15.md) supply files and Windows integration.
The model files are `dependencies/gallery_engine.h` and
`tools/GalleryEngineTests.cpp`. They use C++17 and the standard library only.
No filesystem, image decoder, networking, clock, Windows API, process, user INI,
artwork folder, or account is accessed by the model or these fixtures.

The public namespace is `aip::gallery`. One group supports up to eight active
tile configurations, each with at most five queued snapshots. Configuration
defaults are disabled, two fallback slots, queue size 5, refresh 900 seconds,
seeded Shuffle with seed 1, and Fill. The pure configuration reader accepts an
injected `(section,key,fallback) -> std::wstring` reader and publishes its result
only after total validation. The interactive setup explicitly proposes three
tiles and saves the user's chosen count. Empty folder text remains a possible setup state;
the host must display setup guidance instead of trying to scan it.

Stable group/tile IDs contain 1-32 ASCII letters, digits, or underscores. Tile
IDs must be unique even under ASCII case folding. `NativeTileId(group,tile)`
returns `AipGallery_<group>.<tile>`, at most 64 characters. The fixed prefix is
11 characters and the separating period is one, so component lengths together
must not exceed 52 even though each individual ID may contain up to 32. The
period cannot occur inside a component; underscores cannot create ambiguous
boundaries. Both full configuration and historical ledger validation enforce
this combined bound. Windows documents the limit in
[SecondaryTile.TileId](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytile.tileid).
Hosts should match generated full IDs against known configured destinations,
without assuming that underscores separate components. Display names are
separate from IDs: renaming a label must not change the destination identity.

The proposed typed INI schema is:

```ini
[Gallery]
Enabled=0
GroupId=gallery1
Folder=
TileCount=2
QueueSize=5
RefreshSeconds=900
Order=Shuffle
Seed=1
Fit=Fill

[Gallery.Tile.1]
Id=one
Name=Gallery 1
Enabled=1

[Gallery.Tile.2]
Id=two
Name=Gallery 2
Enabled=1
```

`Order` is Shuffle or Sequential; Sequential follows persistent reservation
append order. `Fit` is Fill or Fit. RefreshSeconds ranges from 60 to 86400 and
is a batch replacement cadence, not an estimate of Windows' visible queue
position. Generic configuration entry points are `KnownGallerySection`,
`NormalizeSetting`, `ValidateConfiguration`, and `ReadConfiguration`.

Every image has exactly one permanent owner in `Ledger.images`. Image IDs are
opaque valid Unicode strings of up to 256 UTF-16 units; they are not file paths
interpreted by the model. The host supplies canonical identities and must reuse
them across rename/move/restart and collapse physical aliases and duplicate
image content before assignment. Two IDs for the same photo defeat the model's
identity-level exclusivity; the model cannot discover that equivalence without
bytes or pixels. Repeated input rows with the same canonical ID are deduplicated.

`ReconcileOwnership(config, usableImageIds, oldLedger, plan, error)` keeps every
existing reservation unchanged. Missing images retain tombstones. Removed or
paused tiles retain their owner/progress records. Reappearing images return to
the same owner. Only unowned images may be assigned to enabled current members,
choosing the smallest currently usable pool and then stable TileID as a tie
breaker. Initial creation of all N tiles together gives balanced pools. Adding
a tile later can leave it empty until new unowned images arrive; existing pools
are never silently rebalanced. Changing shuffle seed/order only changes order
inside those pools. No assignment to another TileID happens because a file was
deleted, a directory listing changed order, or a destination was unpinned.

The ownership plan includes a candidate ledger, its expected previous revision,
`requiresLedgerCommit`, and a cached `PoolSnapshot`. Reconciliation occurs on
inventory/membership/order changes. Queue planning then reuses the ordered pools
without directory enumeration or decoding. Model functions still perform bounded
in-memory ledger validation; they do not promise constant-time validation for a
65,536-entry historical ledger. Hosts should retain parsed state and cached
assets, avoid rescanning on every poll, and coalesce scheduling/delivery results.

The integration order is deliberate:

1. Load the one authoritative ledger for this gallery history. Its location
   must not change just because GroupId or folder configuration changes.
   Parse/read failure stops publication; it never means an empty ledger. A new
   filename, new TileIDs, profile reset, or missing cache is not evidence that
   old Windows tiles no longer display reserved images.
2. For a genuinely first gallery, `CreateLedger(group,epoch,true,...)` requires
   host attestation that there is no earlier gallery history. Otherwise the
   attestation must confirm retirement of ALL historical pins and queues.
   Persist this ledger through the host's atomic, flushed writer.
3. Reconcile usable canonical identities. Persist its candidate under the
   ledger's file lock with a compare-and-swap on `expectedRevision` before using
   newly reserved images. Failed persistence discards the proposed state.
4. At the host's scheduled interval, call
   `PrepareQueueBatch(config,poolSnapshot,durableLedger,dueTileIds,batch,error)`.
   It stages a pending window for each due nonempty enabled tile. Each window
   has group/epoch/TileID, a unique delivery ticket, and at most five unique
   images from that owner's entire reserved pool.
5. Persist the pending-window candidate before submitting anything to Windows.
   `ValidateWindowForPublication(durableLedger,window,error)` rejects a window
   that is not recorded exactly in the durable ledger. A failed ownership or
   pending save therefore cannot authorize delivery.
6. Supply cached assets and publish the group of snapshots. The model cannot
   observe Windows' current/fifth notification and never predicts it. During
   partial replacement, older and newer notifications still belong to the same
   owner's disjoint pool.
7. `AcknowledgeDelivery(ledger,window,accepted,...)` advances the cursor only on
   accepted submission, then clears the pending ticket. Failed/partial delivery
   clears the ticket without advancing or releasing any owner. Persist the
   acknowledgement candidate; acknowledgements for different tiles can be
   applied to one in-memory candidate and coalesced into one disk save. Accepted
   submission is not a claim that Windows displayed every snapshot.

A crash or failed acknowledgement save leaves the durable pending window for
retry with the same ticket and images. A new attempt after failed acknowledgement
uses a different ticket, so a late old callback cannot advance it. If an image
in a retained pending window is no longer usable, planning reports
`PendingImagesUnavailable`; the host should acknowledge that attempt as failed
before preparing another window from remaining owned assets. Empty new pools
report `EmptyPool`, not borrowed images. Cached assets may be necessary for old
notifications, so host cache cleanup must not discard ownership or assume that
an absent original file proves that Windows stopped displaying it.

`ResetLedger(oldLedger,newEpoch,hostConfirmedAllOldPinsAndQueuesRetired,...)`
requires explicit retirement confirmation and a different epoch. It then permits
a new assignment history. Confirmation must cover historical removed/paused
destinations as well as current members, including any static photo fallback
that could still be visible. The model cannot verify that OS condition; a timeout,
failed unpin/clear request, or merely observing a different current picture does
not prove retirement. Old epoch tickets cannot publish or acknowledge after a
confirmed reset. A corrupt old ledger can be archived and replaced only after
the same complete host-confirmed retirement, never by automatic recovery.

The ledger wire format is a strict ASCII line format headed `AIPGALLERY 1`.
Unicode identity fields are encoded as hexadecimal UTF-16 units. It records
group/epoch, revision/counters, every reservation and durable ordinal, historical
tile cursors, and pending windows. Bounds are 65,536 historical image identities,
1,024 historical TileIDs, five images per pending window, 2,048 bytes per input
line, and 64 MiB per serialized document. Counts, numeric overflow, duplicate IDs,
ordinals, owner references, pending tickets, Unicode, full document consumption,
and format version are validated. Serialization/parsing failures leave output
unchanged. These are capacity limits, never reasons to discard tombstones.

`tools/GalleryEngineTests.cpp` uses synthetic IDs and serialized strings to cover
balanced/disjoint entire pools, input and membership reorder, shuffle changes,
new/removed/paused destinations, missing/reappearing files, durable restart,
failed persistence/publication authorization, partial/failed delivery, stale
acknowledgements, missing pending assets, epoch reset guards, typed options,
Unicode codec, malformed/truncated/oversized-count inputs, and corrupted cached
pools. Source-only whitespace/dependency review is complete. No compile or test
executable was run in this slice. Root owns build/test registration, native
provider/inventory/caching/delivery integration, tray/CLI setup, and acceptance.
No existing file was edited as part of this gallery slice.

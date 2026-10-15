# Gallery scale and multiple folders — 2026-10-15

The gallery model now accepts up to **1,024 configured tiles** and retains up to
65,536 historical tile records. These are application limits; this work does
not establish how many tiles a particular Windows Start implementation can pin
or display. Existing group IDs, tile IDs, native IDs and the single per-profile
`.gallery-ledger` path remain stable.

`[Gallery] Folder` remains the default for existing configurations. Each
`[Gallery.Tile.N]` may additionally specify `Folder` and `GalleryName`. An empty
tile folder inherits the default. `GalleryName` is a presentation caption and
does not change ownership or native identity. Relative folder overrides resolve
against the INI's directory, just like the default folder.

The coordinator keeps one owned inventory worker. It services distinct active
folders in round-robin bounded passes, retaining an independent incremental
index and last complete inventory for each folder. Rows sharing a folder also
share its immutable eligibility set. Thumbnails use the same content-addressed
cache across folders. Exact encoded image bytes therefore have one global image
ID and one permanent owner, even when copies appear in multiple folders. This
does not promise perceptual deduplication of differently encoded photos.

Configured rows define pools regardless of old `Enabled` flags. A separate
cached Windows pin set selects which folders are scanned and which destinations
receive notifications. No pinned destinations means no photo scan. The first
pin in a folder allocates its available images across **all configured rows for
that folder**, so subsequently pinning siblings does not leave them empty merely
because another tile was pinned first. The coordinator copies the cached pin
set once; it never performs a native pin-existence call per row or queue slot.
Each folder publishes after its own first complete pass; an inaccessible new
folder does not block a working gallery. Only ready folders participate in
current image eligibility and delivery. Unscanned folders keep their durable
pending windows intact until a complete inventory is available. Later
incomplete refreshes retain their last complete inventories. If exact copies
are first seen in different folders at different times, the first completed
eligible inventory establishes their permanent owner.

Ordinary append, rename, folder changes, disabling legacy switches, unpinning
and restart never release or reassign a historical reservation. If an image
disappears from its owner's folder but remains in a different folder, it stays
reserved to its original owner and is omitted from current usable pools.

Growing an existing gallery whose images already belong to its old rows needs
an **explicit redistribution** workflow if the user wants those photos shared
with new rows. Runtime hooks suspend publication immediately, reject old frame
receipts and cancel the owned worker. Reset preparation requires explicit
confirmation that every historical queue has been cleared, a fresh epoch and a
full expected/current ledger comparison under the native ledger lock. Partial
queue clearing, stale history, or a failed save must not authorize a reset.
The UI owns confirmation, clearing historical queues, configuration rollback
and resumption; the normal ownership-save path cannot reset history.

Validation additions are offline source fixtures only at this checkpoint:

- Legacy eight-tile defaults and the new 1,024-row model boundary.
- Twenty-eight five-slot queues, two overlapping synthetic folder inventories,
  global image deduplication and folder eligibility.
- First-pin allocation across unpinned siblings, no scans without pins, shared
  folder scan reuse and unchanged-plan reuse.
- A working gallery continues while another new folder is inaccessible.
- Restart with the same durable pending windows and unchanged reservations.
- Suspension, stale callbacks, unconfirmed/partial reset refusal, full-ledger
  conflicts and explicitly confirmed new epochs.

The parent subsequently compiled, scanned and ran these fixtures successfully;
see the [final validation and installed identities](tile-feedback-followup-2026-10-15.md).
Native multi-folder pinning and redistribution acceptance remain separate from
the synthetic checks. Existing user pools were not redistributed by validation.

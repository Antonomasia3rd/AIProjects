# Gallery inventory and thumbnails, 2026-10-15

`dependencies/gallery_inventory.inc` supplies the native worker inventory for
the separate-pool gallery model. It does not pin tiles, publish notifications,
change user configuration, assign image ownership or retire historical pools.
The durable gallery ledger remains authoritative for ownership. This helper
keeps old thumbnail files even when a source disappears or a scan changes.

## API and host contract

The Windows entry is `aip::gallery::ScanGalleryInventory(folder, cacheDirectory,
previousIndex, cancel, options)`. Construction/includes do no scanning. Invoke
it on an owned worker; the helper balances its own MTA/WIC lifetime. Its result
contains:

- `images`: unique usable `{id, thumbnailPath}` entries in source-name order.
- `index`: a copyable `InventoryIndex` containing file stamps, completed hashes,
  thumbnail paths, stable rejection information and the next bounded-pass cursor.
- `complete` and `cancelled`: publication state, separate from usable item count.
- Bounded generic `diagnostics`, plus `hashedFiles`, `decodedFiles`,
  `skippedFiles` and `deferredFiles` work counts.

Retain a matching, noncancelled result's **index after every completed call**,
including calls where `complete=false`. Otherwise each retry would restart at
the first files instead of reaching the tail. Adopt a new whole-inventory image
set only after `complete=true`; keep the previous complete snapshot while a
new scan is partial. The caller must reject stale profile/folder generations,
pace retries and catch unexpected allocation exceptions on its worker. No
mutable source file or decoder object is retained in an image result.

Metadata caching is in the supplied index. There is no new serialized metadata
index file in this module. On cold restart with an empty index, source bytes
are hashed again; already-published content-addressed PNGs are reused. The host
must never treat an absent index as permission to reset durable pool ownership.

## Identity and cache behavior

IDs are `sha256:` followed by the lowercase SHA-256 of the encoded source bytes.
The cache stamp includes canonical file path, volume/file identity, encoded size
and last-write time. Hardlink/file aliases reuse an existing matching stamp;
exact byte copies hash independently once, then share the same thumbnail. A
zero/unsupported file ID disables alias reuse. Changed stamps trigger a new
source read/hash. Reads exclude concurrent writers and recheck the opened
file's identity/stamp, so hashing and decoding use the same captured byte vector.

Stable corrupt/unsupported inputs are skipped until their stamp or decode
policy changes, avoiding repeated decode work. Missing thumbnails regenerate
without hashing unchanged files again. Size/cache-directory changes retain
known encoded hashes and publish a new immutable thumbnail path. Only metadata
entries absent from the current bounded listing are pruned; no published PNG
is automatically deleted. Canonical source/cache directory checks prevent the
cache from becoming its own flat input folder through a directory alias.

Deduplication is exact encoded-content identity. Differently encoded, resized
or metadata-edited variants can have distinct IDs despite similar or identical
visible pixels. This is not perceptual image matching. The model's separate
full pools and durable ownership are what prevent an exact content ID from
being assigned to two destinations while notifications may still show it.

## Bounded decoding and orientation

Defaults are 4096 supported flat-folder entries, at most four times that many
visited directory entries, 64 MiB encoded bytes per file, a 16384-pixel source
dimension cap, 64 MiPixels decoded, 32 changed-file reads per call and a 2500 ms
cooperative scan interval. The thumbnail long edge defaults to 620 and can be
set to 310. Diagnostics are limited to 16 unique generic messages, each at most
256 characters; no photo names or directory listings are logged. Options have
explicit upper bounds and invalid options fail before source/cache access.

PNG, JPEG, BMP, GIF and TIFF use the installed WIC codecs, with only the first
frame/page selected. Source size is checked before pixel conversion. EXIF
orientation 1–8 is read for JPEG/TIFF and supported PNG metadata, then applied
as separate clockwise rotation and reflection stages before thumbnail sizing.
Thus portrait JPEGs and transposed/reflected images retain the intended view.
Missing or invalid orientation is treated as normal. Premultiplied-alpha
resampling avoids transparent-edge fringes, then straight-alpha PNG encoding
preserves transparency. Full color-profile management and animated/multipage
playback are outside this implementation.

File reads and SHA-256 processing check cancellation/deadlines in 64 KiB chunks.
Other checks occur between WIC stages. Windows filesystem and codec calls are
synchronous and are **not hard-preemptible**; the time budget is cooperative,
not a promise to terminate a hung native codec. Use an owned worker and keep
UI/poll coordination nonblocking. Over-limit or incomplete listings remain
incomplete rather than silently pretending to represent the whole folder.

PNG output is bounded to 4 MiB and published atomically from this invocation's
`CREATE_NEW` stage. Only an owned failed stage is deleted. Paths include a
thumbnail-format version, encoded digest and edge size. Existing PNGs are never
overwritten or purged while historical notifications may still reference them.
The cache can consequently grow; reclamation requires a future owner-aware
retirement operation, not a scanner guessing which shell items are visible.

## Prepared validation

`tools/GalleryInventoryTests.cpp` defines `AIP_GALLERY_INVENTORY_PURE` and tests
the real policy with injected metadata/read/hash/decoder/clock callbacks. It
covers aliases and exact copies, unchanged work suppression, changed files,
missing-thumbnail repair, edge/cache changes, old-thumbnail retention, stable
corruption, size limits, partial-pass progress, metadata-only budgets,
cancellation, hash/decode deadline recovery, bounded diagnostics and all eight
EXIF transforms against an asymmetric pixel grid. It has no filesystem or
Windows API dependency.

`tools/GalleryInventoryNativeTests.cpp` creates a fresh private temporary root
containing only generated semitransparent PNG, BMP, tagged JPEG/TIFF, corrupt
bytes and an ignored text fixture. It checks real SHA-256, exact-copy
deduplication, PNG alpha, aspect-ratio scaling, portrait JPEG corner colors,
lossless transpose/transverse pixel order, unchanged scans, old thumbnail
retention and early cancellation. Cleanup deletes only explicitly owned named
files and their empty private directories. It accepts no user folder argument.

## Root validation after the inventory freeze

Root compiled both fresh fixtures with warnings treated as errors, completed
the Defender scan gate, and ran them with the bounded test workflow. **36 pure
inventory checks and 84 native synthetic-image checks passed.** The native run
used only the fixture's private generated images, including alpha, JPEG EXIF
orientation, TIFF transpose/transverse and thumbnail retention/readback cases.

The inventory subtask itself did not build or execute binaries. No actual
photo folder, account, pin, native GUI, user INI or resident process was accessed
for these gates. These results validate the inventory helper and its synthetic
fixtures; host integration, real Windows gallery delivery, actual photo sets,
shutdown under a nonresponsive codec and visible pinned-tile behavior remain
separate acceptance work.

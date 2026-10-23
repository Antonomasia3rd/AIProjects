# Avoiding identical gallery queue refreshes, 23 October 2026

The coordinator previously created a new durable ticket and resubmitted every
gallery's complete queue at each configured refresh interval (900 seconds by
default). When all of a tile's photos fit in its existing Windows queue, this
could send exactly the same ordered images again. The new receipts bypassed
transport deduplication, even when generation could reuse the existing PNGs.
This is a confirmed source of unnecessary interval work; it does not establish
the cause of a historical CPU graph without a matching trace.

The coordinator now skips an identical repeat only when all of these hold:

- The pool is nonempty and fits entirely in that tile's effective queue size.
- Every slot of the previous publication was acknowledged successfully.
- The subsequent ledger acknowledgement committed successfully through its CAS.
- The next ordered image IDs and content-addressed thumbnail paths match.
- Profile/group context, ownership epoch, complete tile configuration, delivery
  policy, queue capacity and active Auto DPI scale still match.
- There is no pending durable window or explicit refresh request.

The proof exists only in memory and stores small ordered ID/path vectors plus a
configuration generation. It is never inferred from an old cursor or generated
cache directory. Startup, manual refresh, profile/config/pin changes, suspension,
reload and render-environment changes invalidate it. A new or changed file,
thumbnail path, capacity, policy or image sequence therefore still produces an
actual publication. A partial/failing attempt or failed acknowledgement save
still follows the existing durable retry path. A thumbnail change within a
partial attempt also revokes the old slot acknowledgements and receipt attempt.

Skipped repeats create no new ticket, ledger save, render or registration work.
They do not move the cursor or last actual delivery time. An inventory result
arriving immediately after a skipped due check therefore remains due on the
next poll. Larger pools continue advancing through distinct windows. The
background inventory still checks for added/changed files; this change does not
disable image discovery or predict which animation Windows currently displays.

Auto DPI identity is sampled only when a configured gallery actually has native
or explicitly prepared Registration demand and automatic asset scaling enabled.
Changing detected scale invalidates the proof without changing the profile's
settings. Every notification already contains the supported tile-size bindings;
Start tile resizing does not require discarding those bindings.

The native generation path retains all active per-tile/per-slot PNGs in
`g_secondaryTileAssets`. `BuildKeptLiveTileAssetPaths` includes that complete map
when pruning old versioned images. Generated-cache capacity pruning does not
remove these active publication files. The existing host fixture demonstrates
40 published images remaining reusable after 32-entry cache eviction and active
asset cleanup. Gallery content-addressed thumbnails currently have no automatic
pruning. Normal shutdown cleanup and its per-tile preferences remain unchanged.

Microsoft documents that local tile notifications do not expire by default.
DesktopStub's local payload sets no expiration. This supports leaving a known
complete queue alone. It is not proof that a particular shell replacement,
Windows Live Tile off/on operation, notification database reset, or external file
deletion always preserves that queue. No real Start setting was toggled during
validation. Manual gallery refresh or restarting DesktopStub republishes the
whole queue and remains the recovery path if Windows or an external tool loses
its stored content. Suppression makes no claim of detecting such external loss.

Validation uses `tools/GalleryRuntimeTests.cpp`: injected time, in-memory
CAS-backed ledgers and synthetic inventory paths. No user file, pin, package,
notification or resident was touched. **175 runtime checks passed** in the final
scanned fixture, recorded in
`build/review-validation/gallery-repeat-final-oct23.log`; the fixture covers
partial success, failure, durable save failure, twenty repeated due intervals
without a new plan/save/ticket, source/thumbnail changes, actual larger-pool
sequence changes, policy/capacity/DPI changes, lifecycle invalidations and native
one/five-image plus Registration one-image boundaries. Defender scans precede
execution. A deterministic interleave injects an old last-slot success after
the poller's acknowledgement snapshot and before planning changed-path frames;
the replacement must revoke both the old slot bits and ready-to-commit flag,
retain the original pending ticket/cursor, and require every replacement slot.

Primary references:

- [Microsoft TileNotification expiration behavior](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tilenotification)
- [Microsoft local tile notifications](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/sending-a-local-tile-notification)
- [Microsoft TileUpdater notification setting](https://learn.microsoft.com/en-us/uwp/api/windows.ui.notifications.tileupdater.setting)

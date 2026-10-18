# Scheduled gallery refresh burst at 06:45

The user reported a 6.08% DesktopStub CPU peak at 06:45:10 on October 18, 2026
(Asia/Bangkok), in resident PID 31136. The final October 18 build remained
running from 06:29:47. This follow-up used bounded read-only log windows and
whitelisted configuration values; no app was closed, setting changed or test
executable launched.

## Correlation and measurements

- Gallery queue refresh is configured to 900 seconds, with up to five images
  per destination. The next due time is measured from a complete successful
  acknowledgment/ledger commit. The startup cohort therefore becomes due
  approximately fifteen minutes after its initial submissions.
- From 06:44:53 to 06:45:13, logs identify 74 gallery image jobs across all 16
  configured/pinned destinations. Ten destinations supplied five images and
  six supplied four. They completed in 15 successful batches; one batch merged
  two four-image destinations. No failure/retry was logged in this window.
- The batch ending at the screenshot's 06:45:10 completed all five images
  successfully in 1,211 ms. Every batch performed one asset cleanup.
- Across the 74 jobs, asset-generation timers sum to 7,363 ms and native-update
  timers to 4,225 ms. These are accumulated elapsed timers, not sampled CPU
  time or a breakdown of the screenshot's kernel/user percentages.
- All 74 jobs saved generated-asset cache entries; there were no logged cache
  hits. The configured limit and observed cache directory count were both 32.
- A bounded startup-log comparison found 77 initial saved keys, including
  non-gallery startup jobs. Of the 74 keys rendered during the refresh, 67
  were exactly equal to keys already rendered at startup. Thus at least those
  67 jobs repeated rendering inputs, rather than representing new visuals.

This establishes a scheduled gallery workload concentrated near one refresh
cohort, with avoidable repeated rendering under cache-capacity pressure. It
does not establish a new crash, a growing retry loop, or the cause of earlier
Win+L/NPSMSvc reports. The earlier 0.15–0.18% idle samples remain idle results;
they did not measure this refresh peak.

## Next repair

Reduce total rendering work before merely spreading it out. Secondary live
output PNG paths are deterministic for the visual generation key, and the
currently retained per-slot assets are protected independently of the
32-entry generated cache. Add a secondary-only fast path that validates all
three existing published PNGs (including dimensions/PNG integrity) and reuses
them without decoding, resizing, encoding or saving another cache entry.
Preserve current receipt/freshness guards, native delivery timing, primary
assets and fallback behavior for missing/damaged files.

The generation key does not include gallery tickets, receipts or attempt
counters; removing those fields is not a repair. A meaningful private fixture
should exceed 32 images, evict their generated-cache entries, change delivery
receipts and prove unchanged valid published images require no rerender or PNG
write. Missing/corrupt/wrong-size assets must still fall back to rendering.

Staggered delivery can smooth remaining native work but does not reduce its
total cost. Skipping an identical complete queue is a separate delivery-policy
change requiring explicit refresh/expiration and force-update semantics.
No implementation or preference change was made in that diagnostic follow-up.

## Prepared implementation

The approved repair now adds a secondary-live-only reuse path before generated
cache restoration or rendering. For the current visual key it builds the three
deterministic published output paths, validates every PNG's integrity and exact
dimensions, then reuses the complete set. Missing, corrupt or incorrectly sized
files reject the entire set and take the existing cache/render fallback. Primary
and static asset generation cannot enter this path. The existing publication
receipt, configuration, profile and transaction-freshness checks remain in place.

A successful reuse does not decode a source, resize images, encode or rewrite
PNGs, copy generated-cache entries, or save another cache entry. It still submits
the requested Windows notification through the existing path, and adds a log
message containing only the visual key. Published paths, queue intervals, native
delivery timing and the user's cache capacity remain unchanged. This is reuse
of existing correctly sized outputs, not thumbnail passthrough or a new queue
suppression policy.
The existing `GeneratedAssetCache=0` master switch disables this reuse as well
as ordinary cache restoration. A zero entry-copy capacity with the master still
enabled can reuse published files already needed by Windows; it creates no
additional generated-cache entry.

`DesktopStub/tools/PublishedSecondaryAssetReuseHostTests.inc` prepares a private
forty-image regression. It uses generated one-pixel source identities, three
synthetic PNG buffers, the actual thirty-two-entry cache pruning and injected
render/write counters. It retains the published slot images, proves an old
generated-cache entry was evicted, changes delivery receipts, then expects zero
additional render, PNG write, restore or cache-save work for all forty images.
Missing-file, checksum-corruption and wrong-dimension cases must fall back.
A corrupt published PNG with a valid generated-cache copy must be rejected
before that valid copy repairs it; the fixture covers this separately from
corruption with no available cache copy. Master-off and zero-capacity behavior
are also checked using only the private profile.
Primary/static sentinel bytes and registration-mode exclusion are also checked.

The generation key additionally records the new prayer-specific bitmap-layout
flag when text is baked. Existing generic detail-row and text fields already
cover its five prayer rows. The fixture checks that changing the prayer layout
flag or baked rows cannot reuse old pixels; no account or prayer API is called.

The implementing agent has not built or executed these changes. The parent
task coordinates fresh compilation, Defender checks, bounded offline fixtures
and any later binary update. No real photo scan, user configuration, ledger,
pin, resident or Windows notification was changed during implementation.

The parent has now built/scanned/run the combined extended fixture: all 440
checks passed, including the forty-image cache-eviction regression and fallback
cases. The full core host passed 15,609 checks; source guards passed 1,822
DesktopStub and 277 shared checks. All 5,630 exact rendering and 3 no-hardware
checks passed, followed by a clean product build, scans and copied-binary
offline smoke. The reuse repair is installed with the Prayer Times build at
07:39:35; PID 7856 passed its guarded 75-second startup check. See
[the build identity and validation](prayer-times-2026-10-18.md).

The real 74-image refresh peak has not yet been measured with this revision.
The forty-image fixture proves removal of render/encode/cache-copy work after
eviction; native queue submission and PNG validation still perform work.

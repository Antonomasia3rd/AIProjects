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
No implementation or preference change was made in this diagnostic follow-up.

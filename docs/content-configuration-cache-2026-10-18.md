# Content configuration cache — 2026-10-18

The normal content poll previously copied the whole INI document, parsed all
content/provider fields and expanded ASUS defaults on every read. The host now
keeps an immutable base configuration keyed by absolute INI profile and the
shared monotonic document revision. Both valid configurations and invalid-read
errors are cached for that exact document revision. An invalid edit therefore
continues to reject instead of silently reusing the preceding valid settings.

Every read still acquires the existing named path lock and refreshes the normal
INI cache according to its existing refresh policy. It requires a successful
cache refresh/use and a matching profile binding. It copies the document only
when the typed base cache misses, then releases the named lock before parsing.
The view mutex is acquired after the named mutex, preserving same-thread
mutation-callback reentry and avoiding a reader/writer lock-order inversion.
The active profile is checked again before caller output is published. No
additional TTL or relaxed publication boundary was introduced.

Legacy RSS profiles are migrated in memory during base parsing. Their configured
headline count, cycling behavior and original INI preservation remain intact.
ASUS defaults and numbered events come from the same captured document as the
other provider settings.

Secondary-tile settings are parsed only when a caller requests them. The cache
retains separate validation results for the base content count and the pinned
maximum-count variant. A malformed secondary setting does not affect callers
that request only the base configuration. Caller output is left unchanged on
any base or requested-secondary error.

Current pinned-tile selection is applied **after copying** the cached base and
requested secondary settings. Pin and unpin mutations therefore change provider
demand immediately without an INI revision, and their derived rows never become
part of the immutable base. Base-only callers retain their previous behavior.

`DesktopStub/tools/ContentCpuCacheHostTests.inc` prepares real reader tests using
private INIs, parser counters and the in-memory pin inventory. It checks eighty
unchanged reads parse the base once, secondary parsing is lazy, caller mutation
cannot poison the base, pin/unpin changes remain immediate, legacy RSS migration
is reused, invalid edits stay rejected, secondary errors remain isolated, profile
switches and changes during parsing are guarded, and failed refreshes cannot
reuse another profile's document. Existing coherent-snapshot/concurrent-writer
and named-lock reentry fixtures remain relevant regression gates.

The implementing agent performed source review; the parent then built, scanned
and ran the coordinated release. All 174 extended and 14,427 core host checks
passed, including concurrent snapshot readers/writers and named-lock reentry.
The 331 transparent, 5,630 exact renderer, 3 no-hardware checks and full offline
copied-binary smoke also passed. The repair is installed; the resident survived
75 seconds of guarded startup with the existing INI/accounts and ledger intact.
Short passive mean idle CPU fell from 0.454% before the two repairs to 0.154%
after. See [the coordinated validation](gallery-cpu-batch-2026-10-18.md) for
hashes, backup, measured limits and remaining Win+L/peak attribution work.

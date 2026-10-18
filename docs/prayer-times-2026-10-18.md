# Prayer Times source and tile — 2026-10-18

The user chose to leave location unset. Surabaya was not saved automatically.
The feature is installed, with city/country still blank and no prayer cache
created for the real profile. Public London examples were queried only to
verify API response structure; source fixtures use injected transport.

The new shared provider uses AlAdhan's documented daily city endpoint and
explicit-offset ISO timestamps. A portable model owns configuration, civil-day
math, display adjustments/countdown, detail rows, deadlines, and private-cache
serialization. The Windows adapter owns only JSON decoding and bounded shared
HTTP transport with injection. The host owns persistence and UI.

The request cache binds city, country, state, time zone, method, Asr school and
latitude adjustment. Formatting and local minute adjustments do not change
that key. Failed refresh retains usable previously retrieved data with stale
status and a five-minute retry; it never invents prayer times. Current/tomorrow
fetches share one timeout/cancellation budget.

## Shared architecture

The portable `prayer_times_model.h` and platform `prayer_times.inc` are written,
along with `PrayerTimesModelTests.cpp` and `PrayerTimesSourceTests.cpp`. The
host owns file I/O through a bounded, atomic per-profile cache adapter.

`ConfigurationKey` binds request-dependent settings only. `Data` owns the daily
schedule, refresh/retry deadlines, last-success time and stale/error state;
`Compose` makes five typed mandatory-prayer rows and a next-prayer countdown
rounded upward to minutes. Sunrise is optional metadata rather than a sixth
mandatory row. `NextUTCDeadline` identifies countdown, refresh, and date
boundaries without repeated HTTP work. `SerializeCache`/`ParseCache` use a
bounded versioned ASCII encoding with configuration identity checks.

## API offset compatibility

Public documentation/probe review found that an ISO prayer instant
may be serialized using `+07:00` even when `data.meta.timezone` says
`Europe/London`. Treating the ISO serialization offset as the city's display
zone would show the wrong clock time.

Each needed date therefore uses **two correlated responses**: `iso8601=true`
for an unambiguous UTC instant and `iso8601=false` for the requested location's
local clock. Their returned Gregorian dates and time zones must agree. The
provider derives each prayer's city offset from that date/local clock and UTC
instant, preserving individual offset changes. It does not substitute the PC's
time zone or guess that every timestamp's serialization zone is the city zone.

Ordinary cold loading uses four HTTP requests (today/tomorrow pairs); a bounded
UTC-date correction may require six. Daily rollover fetches only the missing
next-day pair. All requests share one total deadline and cancellation callback.
Current schedules are reused across display/adjustment changes. The optional
Hijri date is copied from the response without another request.

The fixtures include the London/+07 serialization case, mismatched response
dates, UTC/city date rollover in both directions, cache reuse, missing-day
fetching, failed-refresh retention/backoff, cancellation, byte limits,
configuration validation, leap/year rollover, explicit ISO offsets, and cache
rejection for another city/method or corrupt/oversized input.

Primary references: [Prayer Times API](https://aladhan.com/prayer-times-api) and
[Calculation Methods](https://aladhan.com/calculation-methods). The initial
provider supports documented numbered methods 0–23 except 6, with -1/Auto
omitting the method parameter. Custom method 99 is not implemented. The host
owns the source catalog, settings, tile layout and cache file I/O.

## Coordinated validation checkpoint

Fresh optimized warnings-as-errors builds passed 194 portable model and 21
injected source checks after scans. The combined extended host passed 440
checks, including named settings menus, occupied/full content mapping, inactive
defaults, deadline/force/cancellation behavior, cache-file failures and published
image reuse. The full core host passed 15,609 checks. Updated source guards
passed 1,822 DesktopStub and 277 shared checks. All 5,630 exact rendering and
3 no-hardware checks passed. The content engine passed 282,224 assertions
including 100,000 randomized schedule iterations; secondary-tile and artwork
plans passed 93 and 639 checks. The product built, scanned clean and passed
the full copied-binary offline smoke.

Native file-cache tests use only private temporary paths. They reject an empty,
corrupt, oversized, mismatched or contended cache without changing caller data,
and preserve prior bytes after an atomic replacement failure. Production cache
reads/writes run only in the selected source's worker. No real location was
configured, no new tile was pinned, and no real prayer provider was started.

Installed at 07:39:35 on October 18 (Asia/Bangkok), with backup
`DesktopStub/build/ReviewBackupOct18-20261018-073935`. Resident PID 7856 passed
the guarded 75-second startup check; no DesktopStub Application Error event
was found afterward. INI/accounts and current gallery ownership were preserved.
The final passive idle sample was 0.171% mean over 15.229 seconds on 12 logical
processors (0.385% one-second peak). This is not a gallery-refresh benchmark.

Final host SHA-256:
`22A2558878185C964A41BCCDABBE16F16713BB0A2F5855578FF198274C73EF34`;
broker: `C46E08F6393627197167DC1151A2A82F6B0383655506B482D56E968DC33B5418`.

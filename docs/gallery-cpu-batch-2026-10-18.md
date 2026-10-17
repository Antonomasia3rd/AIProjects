# Gallery delivery CPU and I/O changes — 2026-10-18

The user reported brief CPU/I/O spikes during normal use and a momentary spike
at Win+L. Source inspection identified repeated work in large gallery delivery
batches. These changes remove that work; they do not establish the cause of
every observed spike or promise a measured reduction before validation.

## Changes

Previously, every successful gallery slot enumerated and sorted all three live
asset patterns, then repeatedly scanned retained asset lists to decide which
files to keep. Generation now defers this cleanup until the current batch
finishes, including partial failure or exception unwinding. A batch performs
one cleanup per asset directory after successful slots have registered their
retained images. A precomputed case-insensitive set protects all primary,
queued and secondary assets. Standalone generation still cleans up immediately.

Secondary Live Tile generation previously rendered or copied the eight primary
manifest asset types and their configured scales for each photo. It now builds,
caches and restores only its three live images. It neither deletes disabled
primary assets nor rewrites the primary stable Live Tile images. Primary
generation retains its existing asset behavior. The secondary cache namespace
changes so older mixed primary/secondary entries cannot be reused accidentally.

Gallery validation previously parsed the full typed configuration and copied
every pending frame to validate each individual slot, several times per slot.
It now compares one complete frame against coordinator state under the
publication mutex. Typed configuration is an immutable shared view keyed by
absolute profile and a monotonic INI-document revision. Identical INI bytes
reuse the document and typed view; changed bytes publish a new revision.

Publication validation still reads the actual INI bytes every time, including
edits that preserve file size and timestamp. This is not a TTL cache. The
current profile, direct delivery mode and current cached Windows pin membership
are checked separately. The transaction journal is checked freshly before and
after configuration capture and again before accepting a gallery frame. Pending
or inaccessible journals block publication. Retired receipts, altered image
paths, reset semantics, activation arguments or other frame metadata reject.

Generation logs now identify `primary` or the stable source/gallery target ID.
Batch completion records frame, attempt, success and cleanup counts plus total
elapsed time. These new diagnostics omit account data and photo paths; existing
wallpaper-source log behavior is unchanged.

## Validation prepared

`DesktopStub/tools/GalleryCpuHostTests.inc` uses the real host helpers with
injected cleanup and a private profile. It checks eighty requests produce one
cleanup, partial failure still cleans up once, secondary cache plans contain
only three live PNGs, primary sentinel bytes survive, primary asset plans remain
available, eighty fresh configuration checks reuse one typed view, a same-size
and same-timestamp edit changes the view, and a new journal blocks cached data.
The existing real-file retention fixtures continue to exercise retained image
protection. `tools/GalleryRuntimeTests.cpp` adds exact-frame tamper rejection.

The implementing agent has not built or executed these changes. The parent
task coordinates compilation, Defender checks and bounded offline fixtures.
No user INI, ledger, photo, pin, resident process or lock-screen state was changed
by this implementation. Native acceptance and CPU/I/O measurements remain
separate from synthetic regression checks.

The inventory continues its bounded metadata refresh about every thirty
seconds. Unchanged inventories do not rehash or decode their sources, and the
idle planner already reuses unchanged plans. No interval was changed here.
DesktopStub has no explicit lock-session handler that starts gallery generation
on Win+L; operating-system and other-process work requires separate attribution.

## First validated installation

The October 18 logs correlate the 04:53:14 peak with gallery generation: 27
gallery jobs began during that minute. The 05:02:45 and 05:02:52 jobs used a
transparent non-gallery background; older logs do not identify that source.
The user's later live report was sampled at 05:24:03: a busy thread consumed
1,421.88 ms CPU during roughly three seconds, while the log showed successive
gallery frames taking 0.88–1.34 s for assets plus native-delivery time. This
supports the gallery-work diagnosis, not a claim that every lock-screen peak
has been traced.

All production translation units previously used the compiler's unoptimized
default. The build now uses `/O2 /fp:precise` by default, with
`DESKTOPSTUB_OPTIMIZE=0` for diagnostic builds. Named builds isolate their
objects, generated headers and resources. `/bigobj` is required for the optimized
template-heavy host; it creates no runtime DLL.

Automatic heap compaction/working-set trimming also occurred after individual
images despite the configured IdleTrimIntervalMs. It now respects a shared
atomic cooldown and waits for generation to be idle. Zero disables automatic
maintenance. Private tests cover eighty requests, concurrent callers, stale
clock samples and changed intervals; no preference was changed on the laptop.

Optimized validation passed: 14,427 core host, 122 extended host, 5,630 exact
renderer, 3 no-hardware, 78 gallery runtime and 523 portable transaction checks.
Source guards passed 1,764 DesktopStub and 277 shared checks. Full copied-binary
offline smoke passed without relaxing exact assertions. The extended tests have
their own unchanged 45-second watchdog; the original combined attempt reached
the watchdog and exposed a name-notice bookkeeping failure, subsequently fixed
without weakening its HSR-only retry assertion.

Host `FB3961C65B1852FDFF50D87FB5FB845014BF4D0B17C6AF2B03BD3585B6D83D47`
and broker `3BE629357DD4B37047A67A1E4F75C5E38A566F2B8F65735BE8457071D92B3972`
were scanned clean and installed at 05:46:41. Backup:
`DesktopStub/build/ReviewBackupOct18-20261018-054641`. Resident PID 39436 survived
the guarded 75-second startup check; a 74-frame batch completed successfully in
24.9 seconds with one cleanup. These initial frames include cold inventory/native
delivery work, so wall-time examples are not a controlled same-image benchmark.

Two passive fifteen-second samples, normalized across 12 logical processors,
measured 0.454% mean CPU before versus 0.256% after (828.12 versus 468.75 ms CPU).
One-second peaks were 0.520% and 0.514%; these idle samples do not measure maximum
gallery-cycle or lock-screen peaks. Private bytes were 30.31 MiB and 8.10 MiB;
restart/warm-up makes that an observation, not a sustained memory guarantee.

Remaining idle cost was localized to one active thread (125 ms over five
seconds), prompting the second repair below.

## Validated idle parsing repair

The second repair caches unchanged typed content settings and appearance/name
metadata by absolute profile and INI revision. The shared INI cache now binds
its document to the profile and reports refresh readiness separately from an
absent section. Failed refreshes cannot acquire a new fresh TTL interval from
old bytes. Gallery's ordinary cached capture also checks readiness/profile;
strict publication continues to read fresh bytes. Current pin membership is
applied after copying cached base settings, so pin/unpin changes remain live.

Fresh optimized runs passed 174 extended host, 14,427 core host, 331 transparent
background, 5,630 exact renderer and 3 no-hardware checks. Source guards passed
1,764 DesktopStub and 277 shared checks. The full copied-binary offline smoke
passed, including static overlay modes and concurrent INI mutation. One new
failure fixture initially used an attribute-only handle that did not actually
deny data reads on Windows; the corrected GENERIC_READ/share-zero fixture adds
an explicit sharing-violation preflight and retains all refusal assertions.

Host `7D554903456921288100C4F3904BFFEE27CB6EF2A6D5601FFF6B878CB76CAA37`
and broker `C686CBF813A3130B3C914D9BDDA5FC27365EB87AC6222BA5CBEAF6F75301039F`
were scanned clean before execution and after installation. Installed at
06:16:41 with backup `DesktopStub/build/ReviewBackupOct18-20261018-061641`.
Resident PID 44236 survived the guarded 75-second startup check with no
DesktopStub Application Error events since installation. INI/accounts, pins
and the current ownership ledger were preserved.

After compilation and startup work finished, a passive 15.213-second sample
used 281.25 ms CPU: 0.154% mean, 0.385% one-second peak on 12 logical processors.
This compares with 0.454% before and 0.256% after the first repair: about a 66%
reduction between the short initial/final idle samples. Private bytes were
7.96 MiB and working set 24.43 MiB. These are observational samples, not a
controlled maximum-burst benchmark. A further five-second thread sample still
showed one active thread (78.12 ms); reducing that residual work requires
phase-level attribution before changing scheduler behavior or user intervals.
Win+L was not triggered and its specific cause remains unproved.

## Final build with gallery status retention

The subsequent status-only repair passed 95 portable coordinator and 14,436
core host checks. The source guards and full copied-binary offline smoke were
repeated successfully. Final host
`CBC685FD58CAF400B55F5409781BC43C53AC2CE6C5D51C2199E3DEEA747A6141`
and broker `B13130B38EE89250545FD370F84D46F5E7A8A8C33E98E480C0210966983BCD46`
scanned clean and were installed at 06:29:38 with backup
`DesktopStub/build/ReviewBackupOct18-20261018-062938`. Resident PID 31136 passed
the 75-second startup guard; no DesktopStub Application Error events were found
since this installation. Existing INI/accounts, pins and ledger were retained.

A final passive 15.233-second sample used 328.125 ms CPU: 0.180% mean and 0.385%
one-second peak, with 8.29 MiB private bytes and 25.17 MiB working set. The two
final-source samples are therefore approximately 0.15–0.18% idle versus 0.454%
before repairs, a roughly 60–66% reduction in these short observations. Use
that range rather than selecting only the lowest sample. Maximum gallery and
Win+L peaks still need normal-use evidence; no lock/reboot or deliberately
disruptive scenario was exercised.

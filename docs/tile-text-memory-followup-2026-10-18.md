# Text customization, 09:47 refresh and memory comparison

## Confirmed 09:47:36 burst

The user's screenshot shows 4.45% CPU, with 3.59% kernel and 0.86% user time,
for the existing resident PID 7856. A bounded read-only 09:46–09:49 log window
contains 79 successful jobs: 74 gallery jobs and five prayer updates. The main
75-frame batch ended at 09:47:36 after 10,084 ms, with one cleanup and no failed
delivery. It included the 74 galleries' images and one prayer update.

Of the gallery jobs, 67 reused published PNGs and seven saved generated-cache
entries. Thus the previous rendering-reuse repair is active. The gallery jobs'
elapsed timers total about 3,645 ms for assets and 4,999 ms for native updates;
the first prayer job adds about 10 ms and 64 ms respectively. These are elapsed
phase timers, not a kernel CPU profile. Other work between timers remains
unattributed. A saved cache entry alone does not prove whether a photograph was
new or its old output was unavailable. Do not attribute all kernel CPU to a
single function from this graph.

Review also found an independent bug: normal non-gallery secondary updates
copied their PNGs to primary stable paths. The repair now requires an empty
secondaryTileId for primary copies in both normal and render-only paths. A
private sentinel regression covers preserving primary bytes after prayer/source
updates and allowing real primary copies.

An additional possible optimization is to avoid resubmitting an identical
complete gallery queue. Microsoft's local tile notifications do not expire by
default, unlike periodic/push notifications. Any such optimization must retain
startup, pin/config/epoch changes, manual force and explicit queue-clear
invalidation; it is not implemented in this pass.

## Memory: what can be justified

The screenshot's 11.25 MB is private bytes/commit, not private working set.
Read-only measurements at approximately 09:58 were 11.64 MiB private bytes and
6.48 MiB total working set. A later single counter snapshot at 10:05 reported
12.02 MiB private bytes, 15.13 MiB total working set and **5.68 MiB private
working set**. Working-set residency changes with trimming and demand; these
separate samples are not a controlled leak or performance benchmark.

The current profile configures SMTC, RSS, Notes, Discord and Prayer Times,
alongside 16 gallery sections and the primary tile. This is not a wallpaper-only
workload. Provider lifecycle inspection confirms that unselected AsyncSource
providers do not start worker threads, hardware/Discord backends are lazy, and
the SMTC native manager is acquired only when requested. Unused provider code
still contributes to the shared executable/base structures; parsed INI/config
views, snapshots, source data, image-processing heaps and gallery metadata can
remain allocated after work completes.

It is reasonable to expect fewer active modules/tiles to use less memory. It
is not justified to promise that the expanded build returns to an old 4 MB
figure: the old counter, build, warm-up, caches and trimming phase are unknown.
The old aggressive per-image trimming also traded working-set residency for
extra CPU/I/O; the current implementation honors its idle cooldown. A fair
wallpaper-only comparison needs the same private-commit and private-working-set
counters, compiler/build, workload and sampling phase. Selecting Wallpaper for
the primary alone does not disable other pinned sources or independent actions.
No real profile, pins or modules were disabled for this diagnostic comparison.

## Text customization scope

Shared opt-in presentation settings are prepared under global `[TileText]` and
inherited `Tile.<id>` overrides: TextPreset, TextStyle, TextSecondaryStyle,
TextBadgeStyle, TextWrap, per-field maximum lines, TextAlign, TextStacking and
TextGapLines. Default preserves the old path. Custom applies supported Windows
named styles; RssHeadlines explicitly selects one rich item on Medium/Wide and
three on Large, with wrapping and descriptions. Native text sizes are named
Windows styles, not arbitrary font-family/pixel-size selection. Larger text and
more spacing reduce the content that fits in a finite tile.

The referenced SimpleRSSLiveTile Resources.resw uses subtitle for its wide
headline, base for wrapped large-tile headlines, and captionsubtle descriptions.
The implementation follows the native schema without copying that project's
source.

## Validated installation

Passed 83 presentation-policy, 457 extended host, 24,691 core host, 5,630
exact renderer and 3 no-hardware checks. Source guards passed 1,822 DesktopStub
and 277 shared checks. The product and broker scanned clean; full copied-binary
offline smoke passed. Heavy fixture builds now use /bigobj after the expanded
test host exceeded MSVC's section limit; runtime deadlines were not relaxed.
A regression also caught a successful presentation read clearing an earlier
secondary-validation error; that error is now preserved without publishing
partial output. Legacy TileText command-line Boolean validation is preserved.

Installed at 10:21:54 (Asia/Bangkok). Backup:
`DesktopStub/build/ReviewBackupOct18-20261018-102154`. Resident PID 55592 passed
the guarded 75-second startup check. Current INI/accounts, configured prayer
location, pins and gallery ownership were retained; Default styling was not
automatically changed to Custom.

Host SHA-256 `9065C52C59AF0813CC6AD53EEC7ABB7F134738891C50C237BAF474C677F531F3`;
broker `FD25E0475D2473B93F22FC59A45F62DAABEE69AB55E945FA5A0F61AC42414508`.
Actual target-shell presentation still needs ordinary local acceptance.

References: [adaptive text styles](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles),
[local notifications](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/sending-a-local-tile-notification),
[memory counters](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex),
and [SimpleRSSLiveTile's template](https://github.com/Difegue/SimpleRSSLiveTile/blob/master/SimpleRSSLiveTile/Resources.resw).

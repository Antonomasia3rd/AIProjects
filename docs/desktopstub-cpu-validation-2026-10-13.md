# DesktopStub CPU investigation and media lifecycle, 2026-10-13

The user reported DesktopStub and the service host containing NPSMSvc becoming
busy and supplied a full DesktopStub dump captured at 06:37:24. A DesktopStub
dump cannot establish which other processes consumed CPU. The dump also lacks
the thread-information stream needed for per-thread cumulative CPU timings;
root owns the separate stack analysis.

The preserved dump is 132,679,752 bytes. Its recorded debug-session time is
06:37:40 (the file name reflects when capture began). Relinking the retained
original object files yielded an identical original `.text` SHA-256,
`6f83aba7a6bdf5370047e47ff349136da4af7412cfcbb3776d94f4e3fcf83199`,
so original application frame names could be recovered without a matching PDB.
The capture shows the UI in GetMessage, the generation and Startup workers
waiting on their condition variables, the SMTC source worker waiting for its
next request, and PollThread in its timed wait. Most other threads are system
worker waits. No spinning application thread is established by this snapshot.
The process had accumulated 23m23s kernel and 20m53s user CPU over 11h50m27s;
those totals cannot identify the reported momentary spike or service CPU.
The dump remains local and was not uploaded.

## Evidence

- The local `NPSMSvc_fb482` service description identifies it as the Now Playing
  Session Manager, hosted by svchost PID 29352 when inspected. Microsoft's
  [per-user service catalog](https://learn.microsoft.com/en-us/windows/application-management/per-user-services-in-windows)
  lists NPSMSvc as managing media sessions. It separately lists WpnUserService
  for notifications and tiles.
- The native SMTC reader previously called `RequestAsync` for a new manager on
  every fetch and released it when that temporary worker apartment exited.
  Microsoft's [RequestAsync contract](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager.requestasync)
  returns a new Session Manager. With the user's two-second media refresh, that
  repeatedly created about 30 media-service clients per minute even while the
  media entry waited for its next cycle slot.
- Around capture time, the log shows successful cached-content generation every
  10–11 seconds. The 06:37:24 wallpaper frame spent 400 ms preparing assets and
  659 ms updating the Live Tile. Those measurements are wall time, not CPU
  measurements. They do not show an unbounded generation loop or establish
  NPSMSvc causality.
- Artwork SHA-256 deduplication already avoids repeated WIC decoding of an
  unchanged leased image. Packaged Startup synchronization already deduplicates
  unchanged desired-state requests. Neither is changed by this fix.
- The default image-library unload, CRT compaction and working-set trimming can
  add work to each content cycle. Their behavior is unchanged pending measured
  evidence; the media client lifecycle is a concrete unnecessary cost.

## Change

`SmtcWorkerSession` lazily obtains one media manager on the existing source
worker, pins that worker's multithreaded apartment for the manager's lifetime,
and reads later snapshots from the same client. Factory activation remains
uncached so the earlier StartupTask/WinRT factory teardown fix is preserved.

The shared `AsyncSource` accepts an optional owning-worker cleanup callback.
Cancellation, profile replacement and Stop call it after the current fetch has
finished and before a replacement fetch, with no source mutex held. Repeated
idle cancellation does not clean resources twice. SMTC releases the manager
before its apartment is uninitialized; the UI/poll thread never owns a live
manager reference or apartment scope.

Disconnected/server-death errors discard the manager. A later configured
refresh retries acquisition; no immediate reconnect loop is introduced.
Acquisition failures also follow the normal refresh interval. Existing async
operation deadlines and cooperative cancellation remain; synchronous Windows
COM calls are not a hard process deadline.

Invalid content configuration now cancels RSS, all three Notes resources and
SMTC as well as Discord and hardware providers, immediately invalidating their
old cached results.

## Validation status

Root compiled, custom-scanned and ran the bounded inert fixtures:

- Portable model: 282,215 assertions, including 100,000 randomized schedules.
- Source lifecycle: 10,215 checks, including cancellation during a bounded fetch.
- Fake SMTC/codec fixtures: 150 checks, zero failures; real media reads disabled.
- Full host/menu/injected-provider fixture: 6,563 checks, zero failures, 14.788s
  under the existing 45-second outer deadline.
- DesktopStub source contracts: 1,764 checks passed.

Normal host/broker compile and scan clean. No real media, service, package,
Startup, account or hardware integration was exercised by these fixtures.

Prepared checks use fake WinRT managers and injected primitive snapshots only:

- 101 same-profile media refreshes create one manager;
- disconnected-manager failure releases it without an immediate retry;
- ordinary poll wakes cannot bypass retry cadence;
- profile changes release the old manager before acquiring a replacement;
- disabling a source releases it on its original MTA worker;
- repaired acquisition errors recover once, and Stop releases every manager
  while that worker is still initialized;
- generic source cleanup covers same-profile reuse, replacement, repeated
  cancellation and repeated Stop;
- 31 unchanged host polls request one media fetch and queue one frame, with the
  generation queue intercepted so no package or native TileUpdater operation
  can run;
- invalid edits invalidate RSS, Notes and media caches without account access.

Real media-service CPU behavior needs the user's follow-up on the installed
build. No playback, system media-session reads, service changes, account/network
operations or dump uploads are part of these inert checks.

The reviewed v27 host and broker are installed at the normal paths and those
paths scanned clean again; current INI bytes are unchanged. Installed host
SHA-256: `4B338F4AEA4B4DC1AAD153C4A7958BD5E7C34046CC27DBD639314311EB9BB352`.
Broker: `2EC5EB9241BF502EB88FA467F57DCD6B3BE9CEEE68947BDC5B9F3119EEBB2ABB`.
All 11 mapped products compiled without product execution. Copied current-host
content and full offline smoke passed; real service follow-up remains separate.

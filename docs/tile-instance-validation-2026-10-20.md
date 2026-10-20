# Independent tile implementation and validation, October 20

This records the earlier 15:16 installation. The later per-tile delivery and
Registration pass, final binary hashes, native publication and measured resource
usage are in [the delivery follow-up](tile-delivery-policies-2026-10-20.md).

The independent-instance build is installed. Current resident PID 45004 passed
the guarded 75-second startup check **and native publication checks**. All seven
existing source destinations appeared in successful completed batches; seven
complete batches and no failed batches were observed after final startup at
15:16:27. The observed 41-frame gallery batch completed all 41 frames, and
subsequent individual source updates completed too. This confirms delivery,
not merely process survival or successful queue construction.

See [the instance guide](tile-instances-2026-10-20.md) for the actual schema,
tray controls and CLI examples.

## Changes

- Stable arbitrary instance IDs replace fixed numbered content mappings in the
  normal UI/runtime. Sources, refresh settings, presentation, appearance and
  click actions belong to their selected tile. The model fixture covers 1,100
  instances; native Start pin stress was not performed.
- RSS instances have isolated URLs/cadences/caches. The named rich-headline
  layout materializes its text mode/styles/line limits rather than inheriting
  stale caption settings. Invalid sources publish their own repair state while
  healthy siblings continue.
- Plugins contains independent CapsBlink/ASUS actions. Application retains
  shared Windows delivery controls; per-tile update enable switches are not
  introduced. Pin membership drives background demand.
- Primary removal stops wallpaper observation and retires its old notification.
  Missing cached transparent images are recreated without refetching feeds.
- Prayer has semantic clock fields shared by native/bitmap/status paths, a
  large next-prayer time, location and countdown, plus an optional Large
  timetable. Existing location and 12/24-hour preferences are retained.
- Shutdown includes current/historical owned gallery destinations without
  unpinning or deleting their ledger, reservations or source photos. Status is
  scoped; Pin/Unpin are mutually exclusive with explicit unknown-state retry.
- Embedded Notes import targets the selected tile/account. Shared login fields
  remain separate from per-tile game UID/server; stale dialogs reject changed
  selections. Canceling sign-in changes no account. Account-scoped browser
  retention does not invoke server-side Logout.
- Live notification arguments preserve the quoted profile path. Primary
  instance branding overrides the global legacy value. Transport accepts
  generated hyphenated IDs while preserving its length/character/XML bounds.

## Integration failures caught and repaired

The first installed candidate remained alive but hit a legacy `Generate`
admission check which required the old secondary mapping. It was rolled back
to the previous binaries while preserving the converted INI and ledger.
Generation now resolves the current instance and uses that instance's own
configuration, retaining gallery receipt, native queue and delivery guards.
New inert regressions call the actual `Generate` entry point through those
checks before stopping at a test-only boundary ahead of external effects.

An independent audit also found profile arguments missing from live clicks,
primary branding falling back to the global value, and transport rejecting
hyphens used by newly generated IDs. Real XML/transport round-trip regressions
now cover these paths. A separate private deletion fixture proves missing
background recovery; the live logs did not establish that missing backgrounds
caused any particular earlier batch size.

The original core fixture's `0xc0000409` was an uncaught `map::at` exception in
an outdated menu assertion. It expected a disabled Pin command that the new
requested behavior correctly omits. The assertion now checks exact absence
and preserves the configured row. No production memory-crash patch was made.

## Gates

Executables were freshly scanned before execution. Default tests were private,
inert and bounded; no manufactured account, hardware, Startup or pin stress ran.

| Gate | Result |
| --- | --- |
| Pure independent instance model | 55 checks passed |
| Extended host, actual generation admission and activation | 628 passed |
| Core runtime/menu suite | 24,215 passed |
| Exact adaptive renderer/host | 5,630 passed |
| No-hardware host | 3 passed |
| Prayer model/layout and injected source | 220 / 21 passed |
| Notification payload, including dynamic IDs | 895 passed |
| Fresh DesktopStub / shared source guards | 1,827 / 277 passed |
| Final copied-binary full offline smoke | Exit 0 |
| Actual-profile private conversion and configure-only | Passed; actual files unchanged |
| Guarded install, 75-second survival and native batch publication | Passed |

The private actual-profile preflight preserved eight configured source
instances, Prayer location, existing source/gallery native IDs, gallery sections,
account sections, and the ledger copy. Account values were not printed.

## Installed artifacts and measurements

- Host SHA-256:
  `0E9B0A6C48F599436C2CB4383F8E7142C3EE8619DB0F68371CA9A287978753F2`
- Broker SHA-256:
  `CBD4D781BB67AC702DFFAE607404E729C07F442D7BD3D597C5E3E5D053347BE1`
- Final replacement backup:
  `DesktopStub/build/ReviewBackupOct20-20261020-151620`
- Original pre-conversion byte backup:
  `DesktopStub/build/DesktopStub.ini.before-tile-instances.bak`
- Native install evidence:
  `build/review-validation/tile-instances-oct20-native-admission-install-health.log`

One 15-second passive sample after final successful native publication measured
0.103% mean CPU, 0.259% peak one-second CPU, 8.24 MiB private committed memory
and 10.39 MiB total working set. The pre-upgrade sample measured 0.300%, 1.159%,
11.31 MiB and 18.77 MiB respectively. These are short observations with different
cache lifetimes, not a controlled benchmark or a guarantee about ten-minute
gallery-refresh peaks. The failed first candidate's sample is excluded.
The historical wallpaper-only 4 MB figure is not a comparable module/counter
baseline; no exact 4 MB guarantee is made.

## Still open

The first copied-binary smoke reproduced the exact historical four-pixel
opaque preset antialiasing variation. Later complete smoke runs and the final
candidate passed the unchanged exact assertions. The recurrence remains open;
it was not fixed or tolerated away. See
[the preserved evidence](render-determinism-recurrence-2026-10-20.md).

Native Start appearance, real sign-in challenges, keyboard LEDs after sleep and
real sign-in Startup remain normal-use acceptance. Repeated identical gallery
queue submission, owner-aware asset retirement and a fair wallpaper-only memory
baseline remain separate performance/storage work in NEXT_PASS.

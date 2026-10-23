# Next pass: outstanding fixes and acceptance

## October 24 Google/Kuro account flows and notification discovery

Google Calendar browser authorization/calendar selection, guided overseas Kuro
sign-in, Prayer upcoming/at-time/current phases and clearer Notifications versus
Advanced menus are implemented. The user's Google Desktop client was imported
and the requested Prayer phases configured. Actual Google consent/calendar
refresh and Kuro credentials/human verification still need user acceptance;
injected account fixtures cannot establish those accounts. See the
[delivery and setup record](calendar-accounts-prayer-notifications-2026-10-24.md).
Keep the broader lower-priority queue and exact legacy raster finding open.

## October 24 Calendar/Clock native layout correction installed

Calendar Small/date-only Medium and the Clock text/branding contract were traced
in the actual installed binaries and corrected. This update preserves the user
configuration/pins established after the earlier reset. Native submission and
75-second health passed; see [the result](calendar-clock-parity-2026-10-24.md).
Native visual equivalence across DPI/themes and Calendar's event-filled layout
remain separate acceptance/reverse-engineering work; the installed apps were
not launched or their account stores read. Existing explicit typography/branding
and 12/24-hour choices remain effective. The broad lower-priority feature queue
and unrelated historical raster issue remain open.

## October 23 delivery/confirmation repair installed

The second fresh reset was explicitly requested after the user removed the old
Start pins. Registration physical-path repair, sibling batch failure isolation,
Gallery placeholder cleanup and Yes/No notification continuations are installed;
see [the follow-up](tile-delivery-followup-2026-10-23.md). The active profile is
Desktop-only again. Windows package activation/native wallpaper submission and
75-second health passed. Native separate Registration deployment and interactive
notification buttons still need fresh ordinary-use acceptance; injected tests
cover their paths and one-shot/stale/cancellation behavior. Do not automatically
repin the user's cleared tiles. The historical raster issue remains open.

## October 23 priority update installed

The source/notification/performance/defaults slice and requested backed-up
Desktop-only settings reset are complete. Read [the installation record](desktopstub-installed-2026-10-23.md)
for final gates, hashes, backup and native health. Current private memory measured
4.77–5.01 MiB in a short fresh-profile sample; this is not an old-build comparison.
The full strict smoke still reproduces the identical historical four-pixel
legacy caption discrepancy. Its renderer and assertions are unchanged; no fix
or complete full-smoke pass is claimed.

Remaining: native popup/scenario behavior, chaseable image clicks, live account
acceptance, Registration/manual migration, lock/resume spike attribution and
the renderer defect. Lower-priority font selection, lyrics, confirmed weekly
Calendar timetable, supported fitness API feasibility and Battery are queued
in [the suggestion list](tile-followups-2026-10-23.md). Earlier preservation
notes describe their historical installs; the October 23 user-authorized reset
archives that profile and deliberately leaves the active source list Desktop-only.

## October 20 delivery follow-up completed

Per-tile delivery/cache/render policies and independent Registration identities
are installed and verified. See [the follow-up validation record](tile-delivery-policies-2026-10-20.md).
The 724 final host checks exercise actual PNG rendering with injected deployment;
the full product smoke and SDK schema validation passed. Native installation
passed its 75-second check with all seven source targets and a complete
76-frame batch. Account values, prayer configuration and gallery ownership were
preserved. Registration uses explicit setup and manual replacement pinning;
existing native pins were not silently migrated.

Remaining relevant acceptance/findings:
- Observe actual Registration deployment/manual pinning and Windows' visible
  image-cache refresh after choosing that mode; inert tests do not claim it.
- Check native pin-flyout placement and RSS Default appearance during ordinary
  use. Windows controls flyout size and its final position around the anchor.
- A malformed hand-edited gallery row still pauses the shared gallery
  configuration. Ordinary source tiles isolate invalid siblings; typed writes
  reject invalid policy combinations.
- Retain the historical exact four-pixel opaque caption variation as open.
- The short installed CPU/RAM sample is recorded, but a controlled historical
  wallpaper-only memory comparison and prolonged lock/resume stress are not
  implied by it.

## October 20 completed instance pass

The independent-instance build is installed and native publication is verified;
see [the final validation record](tile-instance-validation-2026-10-20.md).
The final 628 extended checks include actual generation admission, profile-aware
activation, dynamic-ID transport and background recovery. All seven source
destinations appeared in successful completed native batches; the final
resident passed 75 seconds with no failed batches in the observed window.
The original profile/ledger/pin identities and configured Prayer location were
preserved. The exact historical four-pixel renderer variation remains open.
The earlier checkpoint below is retained as investigation history, not current
installation status.

## October 20 earlier checkpoint (superseded)

Independent tile instances and the requested lifecycle/menu/Prayer changes are
implemented in the working tree. Read [the instance guide](tile-instances-2026-10-20.md)
for actual schema and controls. `[Tiles] Instances` selects independent
`[Tile.<id>]` type-specific settings. Existing fixed source IDs, gallery groups,
accounts and ownership history are retained by backed-up conversion. New-mode
menus replace global composition controls with Tiles and Plugins. No unused
Wallpaper tile means no wallpaper background queries in the runtime fixture.

Validation already passed: 55 pure instance checks (including 1,100 instances),
584 settled extended host checks, 220 Prayer model/layout checks, 21 injected
Prayer source checks, 1,827 fresh DesktopStub source checks and 277 shared source
checks. Product host and broker compile and scan clean. Reviewed host hash:
`B056247F4DFD02B013343A4652914B36B8449507DD75B84BDC505991A543F60B`;
broker: `DCF5B3EC1BB42F39C1F833E3DC07E850C3E0F11C2B43C0F5F1351DA74EE84D77`.

**Not installed yet.** The ordinary resident and actual user profile remain on
the previous build while these verification failures are investigated:

- The full optimized core fixture exits `0xc0000409` at offset `0x2b7405` after
  Gallery contain-pixel diagnostics, including a serial run without compiler
  contention. This is not a watchdog timeout. See private
  `tile-instances-oct20-core-serial-run.log`; a separately named diagnostic
  fixture is being prepared to localize the failing call.
- The product offline smoke reproduced the historical four-pixel opaque text
  variation in Medium's unchanged primary `AAAA` when only an unsupported
  secondary field changed. Preserved smoke directory:
  `DesktopStubSmoke-11f1131bc8a5439faaf31419457db75d` under the user's Temp folder.
  Differences are caption antialiasing pixels around y=93; exact comparison and
  tolerances remain unchanged. Investigate before installing.

Renderer/no-hardware gates and guarded installation remain. All work so far is
bounded/private; no account sign-in, native pin, hardware or Startup acceptance
was manufactured. The user's configured Surabaya location must be preserved.

## User-requested tile-type redesign: brainstorm checkpoint

The user requested documentation/brainstorming only near the current usage
limit; implement after reset. Compatibility is not a priority because this
version has not shipped. Preserve the actual user's accounts, pins and settings
through an explicit backed-up conversion rather than maintaining confusing UI.

New reported findings (not yet reproduced):
- Gallery notifications appear to remain populated after DesktopStub exits.
  Check shutdown clearing for every owned tile, including gallery destinations;
  retain pinned shortcuts and ownership history. Separate image-file retention
  from notification clearing: Windows may still need retained files.
- RssHeadlines appeared ineffective or its setup was unclear. Reproduce the
  exact saved per-tile settings, inheritance, active preset and native/bitmap
  paths before assuming user error. Replace hidden dependencies with a direct
  RSS layout selector and useful preview/status.
- Global Tile status and per-tile Status appear to show the same screen.
  Use one collection overview; a tile's status should select only that tile,
  or remove the redundant action if there is no distinct useful behavior.
- Pin/Unpin should be mutually exclusive when inventory is known. Unknown
  inventory needs an honest checking/retry state, not two conflicting actions.

Proposed architecture: tiles are independent instances identified by stable
IDs. Each owns a type (Wallpaper, Gallery, RSS, Now Playing, Prayer Times,
Notes/game, Discord, etc.), source configuration/account reference, layout,
click action and update policy. Support multiple instances of a type (e.g.
five independent RSS URLs/intervals). Retire the user-facing fixed global
source slots and numbered content plumbing. Keep implementations under shared
dependencies and one resident process/tray. Share internal connections/caches
where appropriate without coupling each tile's preferences. Activate listeners
only for demanded tile types; no Wallpaper tile means no wallpaper listener
unless another explicit feature requires it. Dynamic tiles must not be limited
to the current fixed seven source descriptors.

Proposed UI: Tiles > Add tile... / each configured tile's relevant settings;
Plugins for independent Caps/ASUS indicator actions; Application for Startup,
tray/logging and shared app controls. Appearance and update settings belong to
each tile. Optional defaults/templates can help creation but must not create
obscure live inheritance dependencies. Notes accounts may share one credential
store, with each tile explicitly selecting an account.

Prayer layout requested: large next-prayer clock, prayer name and location;
preferred separate top-right countdown column ("minutes" above a large number).
Fallback: large time, "Dhuhr at Surabaya", then "in 44 minutes". Respect 12/24h
choice; make this the Prayer type's default. A numeric countdown column can be
composed within tile content; do not promise a custom shell badge position.
Large tiles may retain the timetable beneath the header when it fits.

Next: verify reported exit/RSS/status/pin behavior, settle the design with the
user if necessary, implement tile-instance storage/runtime/UI, then private
multi-RSS isolation/lifecycle/shutdown/layout tests before guarded installation.

## Completed text/memory follow-up

Text presentation and primary stable-copy isolation are installed; see
[the follow-up](tile-text-memory-followup-2026-10-18.md). Media policy passed
83 checks, extended host passed 457, and source guards passed 1,822/277.
Full host initially hit C1128; heavy fixture builders now use /bigobj.
All 24,691 core, 5,630 renderer and 3 no-hardware checks then passed, followed
by clean product scans, full offline smoke and guarded installation. PID 55592
passed startup health; backup is ReviewBackupOct18-20261018-102154. The user's
now-configured Prayer Times location and existing content/pins were preserved.
The 09:47 burst confirms 67 reused gallery images but substantial remaining
native submission/validation work. An identical-queue optimization and a fair
wallpaper-only memory baseline remain follow-up work; no 4 MB guarantee or
kernel-CPU attribution has been established.

Checkpoint: 2026-10-18. The gallery recovery, stale-dialog and source Rename
fixes below are installed with two CPU optimizations. Typed idle parsing
caches and gallery status retention are validated and installed. The shared
notification-app live CLI reload repair passed private IPC/fake-runtime checks.
Read CURRENT_STATUS and the referenced records
before changing source; old chronological audit entries are not a current bug
list. Private bounded fixtures remain the default for failure injection.

## Latest completed pass

The user authorized fixing confirmed findings directly and requested Islamic
Prayer Times. Published-secondary-PNG reuse passed its 40-image/32-entry
eviction fixture and is installed. Prayer Times is installed as a shared source
and seventh optional fixed tile. Existing Content8 and old seven-content
profiles are preserved. The user's location preference
is explicitly **unset**; no location or calculation method should be applied to
the real profile automatically.

The source uses AlAdhan daily schedules cached for the current/next day, manual
city/country and selectable methods/Asr/timezone/adjustments. Public London
schema probes exposed an ISO-versus-city-clock discrepancy; paired ISO/plain
responses must correlate UTC instants with the city's civil clocks. This is a
required regression, alongside date/DST rollover, offline/stale data, cancellation,
old-profile compatibility, command-ID collision and retained-image reuse.
Private schedule persistence is `<profile>.prayer-cache`, ignored by Git.
The 194 model, 21 injected source, 440 extended host, 15,609 core host,
5,630 exact renderer, 3 no-hardware and source/smoke gates passed. The installed
resident passed guarded startup. Native source/pin/Startup/device acceptance
remains ordinary local use; do not manufacture failures against user data.

## October 18 fixes installed; acceptance remains

1. Gallery redistribution recovery: a journal bridges the separate ledger and
   INI writes; startup/manual recovery rejects ambiguous state. Private tests
   cover final write failure, rollback, interruption/retry and conflicting
   ledger/account edits. Do not force failures against the real profile.
2. Gallery edit conflicts: add/resize/folder and per-row settings compare the
   expected gallery configuration against the freshly locked document. Stale
   dialogs reject without losing unrelated settings; injected cases passed.
3. Source Rename propagation: explicit Rename persists name-only intent for an
   existing Windows secondary tile's DisplayName, preserving ID/placement.
   Feed URL and appearance refresh do not create a rename. Private success,
   missing-pin, failure, retry and stale-revision cases passed. Real Windows
   pin acceptance remains a normal-use check.
4. CPU: optimized builds, batched cleanup, secondary-only live assets and idle
   maintenance budgeting and typed content/appearance caches are installed.
   The fresh bounded gates passed; short passive mean idle CPU fell from
   0.454% to about 0.15–0.18%. Gallery-cycle peak, residual poll-thread phase cost and
   Win+L attribution need separate evidence.

## Engineering and closure work

- Check the next ordinary gallery refresh after installed retained-PNG reuse.
  The private eviction fixture proves zero repeated rendering for unchanged
  valid outputs beyond 32 cache entries, with integrity/dimension fallback.
  Real native-submission peak cost remains to be measured. See
  [the diagnosis and repair](gallery-refresh-burst-2026-10-18.md).
- Review owner-aware thumbnail/versioned-asset retirement and useful storage
  reporting. Historical gallery thumbnails/reservations currently accumulate;
  do not delete caches or ledger history based on guesses about Windows' queue.
- Native WIC calls remain cooperatively bounded, not hard-preemptible. Review
  decoder isolation/shutdown behavior before testing intentionally bad codecs
  or hangs; use injected/contained cases first.
- Reconcile every active and legacy product against the user's original shared
  dependencies, INI/CLI/tray, tooltip, per-user Startup and provider-parity rules.
  Existing passing slices do not establish repository-wide completion. Respect
  the agreed Windhawk legacy exception; avoid speculative language rewrites.
- The historical four-pixel opaque renderer variation remains unexplained.
  Keep the exact assertions/artifacts; repeat only a focused reproduction.
- Older Windows/unpackaged helper delivery and older Notes credential/import
  aliases still need compatibility review and appropriate target environments.
- Windhawk functional hooks and live legacy bot/HA/Minecraft behavior remain
  lower-priority reviews. Windhawk requires matching local binaries/symbols.
- Existing security is opt-in; new hardening and the deferred Microsoft case
  remain below functional work, under the user's existing instructions.
- Optional later source ideas: a local countdown/event tile and an agenda tile;
  a Hijri/Ramadan view could reuse Prayer Times' existing calendar metadata.
  These are ideas, not completed features or permission gates.

## Useful local acceptance by the user

Normal use is sufficient; no need to deliberately overload the laptop.

- Gallery: grow toward the desired 28+ pins, use two folders including nested
  images, accept/decline pin batch items, and check stable names/positions after
  restart. Observe that exact image copies do not appear on different galleries
  simultaneously. Avoid deleting the real ledger or forcing write failures.
- Status: check Previous/Next, long RSS/game detail scrolling, resize, and an
  existing high-DPI/second monitor if available. Hidden native-control tests
  passed; actual theme/RTL/accessibility appearance is still acceptance work.
- Media: use the preferred player normally, try configured quick actions,
  close/reopen the player, then sleep/wake. Check state/icon updates and that
  the primary wallpaper continues updating. CPU was reported normal over long
  use; do not restart/kill NPSMSvc or manufacture load to reproduce old CPU.
- Caps LED: check after sleep/wake and, if available, with multiple keyboards;
  confirm indicator actions do not add unwanted Desktop tile text.
- HoYoLAB: during ordinary account use, check selected-game discovery, separate
  account/session choices and refresh/error recovery. Preserve credentials;
  do not log out on the website as a test of local session cleanup.
- Startup: when convenient, one real sign-in check per chosen launch method;
  no repeated logouts or automatic machine reboot is needed for stress.

The agent can run bounded synthetic concurrency, private failure injection,
render/config CLI smoke, cached-provider lifecycle stress and hidden UI checks.
CLI success alone cannot verify Windows Start's actual presentation, physical
keyboard LEDs, real player behavior, sign-in challenges or startup at login.

## Recent observations, not new regressions

- The user reported Battery Saver delayed every application's tiles, then the
  gallery updated. Treat that as a system-wide observation, not evidence of a
  gallery recursion failure. No battery/power setting was changed by the agent.
- Recursive inventory is already enabled by default; a fresh recheck passed all
  93 native synthetic cases, including nested-image discovery. Normal directory
  traversal excludes reparse-point directories. Queue replacement was configured
  to 900 seconds at inspection; discovery and visible queue timing are separate.
- Recently fixed/installed: missing primary image retention, GDI+ icon lifetime,
  status pagination, gallery scale/folder groups, queued pin prompts, automatic
  pinned updates and uniform game icons. The RSS URL is the user-selected
  Windhawk releases Atom feed. These do not need to be reimplemented.
- October 18 also closes gallery redistribution recovery, stale dialogs,
  persisted explicit source renaming, repeated gallery/idle parsing work and
  gallery status loss after delivery. Shared notification-app CLI/tray reload
  parity is repaired in source and passes injected tests; actual user-policy
  execution remains ordinary local acceptance.

# Resume checkpoint and standing requirements, October 24

The user explicitly deferred Mail and the remaining findings to the next turn /
usage reset. Usage was checked at97% consumed. This checkpoint changes no product
code/settings, runs no builds/tests, and performs no installation or reset.
Current installed product is621189f; ca1bb79 added investigation evidence only.
Preserve the current accounts, settings and Start pins during resumption.

## Standing implementation requirements

- Every behavior/option, including Advanced and Experimental settings, must be
  configurable through CLI, INI and GUI. One validated setting model should back
  those surfaces; no hidden hard-coded user preference. Secrets use protected or
  masked/private input and must not be echoed or logged.
- Match native/reference behavior and presentation as closely as possible.
  Inspect the actual relevant installed binary/source and its version. Mark
  observations, verified contracts and remaining inference separately; passing
  our own fixtures is not proof of complete1:1 parity. Keep intentional
  enhancements selectable rather than silently replacing compatibility.
- Each tile owns its type-specific sources, accounts, display/cache/delivery,
  update/cycle and notification preferences. Multiple tiles may use independent
  sources and timings. Application-wide controls remain application controls.
- Keep idle CPU/RAM/I/O low. Unused modules must release observers/workers and
  avoid background polling. Reuse the existing resident where appropriate;
  backend separation should resolve lifecycle/identity problems, not add idle
  processes by default. Diagnose performance spikes with evidence.
- Keep common actions discoverable and technical controls under Advanced.
  Consistent errors/status/notifications; contextual Pin versus Unpin; avoid
  duplicate actions and setup text cluttering ordinary tile faces. Retain the
  tray GUI alongside native Properties. Fresh setup stays Desktop-wallpaper
  only with optional modules and Startup off.
- Maintain Live Tile and Registration delivery as explicit supported paths,
  including per-tile assets/identity and honest migration/manual-pinning UX.
  Do not pretend Windows exposes visible queue state or exact display timing.
- Preserve explicit customization and current credentials/pins unless a reset
  is explicitly requested. Account consent/CAPTCHA remains human-driven. Do not
  borrow credentials, silently read another application's account store, invoke
  server logout or log passwords/tokens. Unsupported fields stay unavailable.
- Validate meaningful native XML/render/lifecycle behavior with bounded inert
  fixtures, preserve exact historical raster assertions, and scan before
  executing build artifacts. Distinguish implementation, mocked validation,
  accepted native submission and actual visual/account acceptance.

## New Calendar request to implement next

Keep extended look-ahead as an optional enhancement, and add a per-tile selector
for **Today and tomorrow** versus **Custom days ahead**. Proposed schema (not yet
implemented): Calendar.EventWindow=TodayAndTomorrow|DaysAhead, with the latter
using Calendar.DaysAhead. Preserve existing configurations' extended behavior.
Apply the display window consistently to Medium/Wide/Large and both rotations,
for Local ICS and Google, across Live Tile/Overlay/Registration. Expose the same
choice in CLI/INI/tray/Properties. Compute local civil-day boundaries, including
DST, rather than treating tomorrow as a rolling24-hour interval. Keep tile
presentation policy distinct from source fetch/reminder data needs.

The latest screenshots confirm the original native agenda dims later timed
entries:9:00 bright,9:30 and10:00 subdued. This supports the recovered row-cutoff
rule; it does not settle the original getter's all-day/timezone normalization.
The user's report that native Calendar only shows through tomorrow is consistent
with these examples but is not a complete proof of all upstream selection rules.
Name the option by its exact behavior; do not claim a fully verified native mode.

Still open within Calendar:
- Recover primary-event ordering, reference-day admission, all-day timestamp
  normalization and complete localized range/header formatting.
- Replace display-label grouping with a stable civil-day identity; different
  months or custom Today/Tomorrow wording can currently collide.
- Verify three-row agenda selection and dimming using the new native examples.
- Interactive system Snooze/Dismiss, cancellation and per-source acceptance;
  current reminders use per-tile lead settings, not Google event overrides or
  ICS VALARM. See calendar-events-reminders-2026-10-24.md.

Private copies of the latest three screenshots are under
build/review-validation/Feedback-20261024-CalendarNative/Native-Calendar-1.png
through Native-Calendar-3.png. They are not staged in Git.

## Mail: explicitly next turn

Add a Mail tile after beginning with the unresolved-finding reconciliation.
Inspect installed UWP Mail/reference behavior for Small/Medium/Wide/Large,
branding, unread counts, preview privacy, queue/rotation and notifications.
Determine supported source/account APIs and ask which service the user wants
before dependent setup. Calendar authorization does not grant mail access;
separate read-only permissions/explicit consent may be needed. Keep sources,
accounts, refresh, display/privacy and notifications per tile and configurable
through all three surfaces. No Mail implementation/auth/research was started
in this checkpoint, and no unsupported access method is promised.

## Reconcile the entire earlier backlog before claiming closure

Historical records contain resolved entries; do not label all of their old
lists as currently open. Compare current code, later evidence and acceptance
before producing an updated open/fixed/acceptance-needed list.

Start with REWORK_AUDIT.md, remaining-findings-validation-2026-10-13.md,
next-work-user-feedback-2026-10-14.md, desktopstub-installed-2026-10-23.md,
registration-delivery-repair-2026-10-23.md, desktopstub-settings-ux-2026-10-24.md,
calendar-clock-parity-2026-10-24.md and calendar-events-reminders-2026-10-24.md.

Known retained groups from the reviewed records:
- Strict historical four-pixel legacy renderer discrepancy; preserve witnesses
  and exact assertions (render-determinism-recurrence-2026-10-23.md).
- Win+L/idle CPU-spike attribution and controlled old/new wallpaper-only memory
  comparison; a short healthy sample does not explain the historical incident.
- Actual Registration/manual-pin/migration, Gallery exact-image chaseable-click,
  notification/progress/system Snooze, Startup and hardware acceptance where
  the records still mark these unverified.
- Google multi-calendar/timezone/consent-expiry and Kuro real-session stability;
  previous transient failure cause is unknown, though stale-data/diagnostic
  handling was repaired. No blanket account/API success claim.
- Broader native branding/RTL/DPI/accessibility and legacy helper queue behavior.
- Original Real-Time Notes feature/UI parity and repository-wide requirement
  closure audit; reconcile later fixes before redoing old feedback.
- Deferred font selection, lyrics, weekly day-by-hour Calendar timetable,
  supported fitness API feasibility, Battery tile (tile-followups-2026-10-23.md).
- Very-low-priority authorized legacy-project audits remain separate from the
  DesktopStub priority work; inspect their latest records before revisiting.

Resume with a usage check, clean/dirty-tree inspection and this checkpoint.
Do not restart already completed resets or migrations, and do not run broad
package/account/hardware integration merely to refresh context.

## Additional requests saved for the next pass

These are deferred requests only. No product implementation, research, build
or test for this section was performed in this turn.

- Keep tiles independently configurable while giving related tile types
  coherent shared defaults. Shared defaults must not make one tile's explicit
  source, account, timing or presentation settings control another tile.
- Research a **Window Capture** tile, using OBS-like window capture as a
  reference. Assess supported capture APIs, low-idle operation and bounded
  frame sampling. Windows Live Tile publication/display cadence cannot be
  promised as a guaranteed video frame rate.
- Increase the currently undersized bottom-right branding logos and make them consistently sized, using the Desktop tile
  as the size reference. Investigate missing Prayer and other source logos,
  while respecting each tile's branding controls.
- Improve WuWa notes with readable wrapped rows and topic icons when available;
  otherwise use an RSS-style text presentation.
- Give Genshin Impact, Honkai: Star Rail and Zenless Zone Zero an RSS-style
  text layout with their topic icons on the **right**.
- Expose every new control consistently through CLI, INI and the native GUI.
  Preserve the independent-tile model across all three surfaces.

## Active configuration reset for daily testing, October24

The user explicitly requested resetting configs again. The existing installed
621189f binaries were retained. The default-profile resident was stopped
gracefully; current profile/account/cache/history files were archived with
verified hashes and original binary/manifest copies at
DesktopStub/build/SettingsResetBackup-Daily-20261024-092555-a2cde8.
A fresh INI was generated by the same canonical binary's configure-only path.
The initial validation script rejected quoted INI keys before touching the
resident; its check was corrected and the reset then succeeded.

The ACTIVE configuration is now only Tile.desktop: Wallpaper, Primary1,
LiveTile delivery, NameAndLogo branding, optional modules and Startup preferences
off. Previous Google/Kuro/Prayer/etc configuration is archived and inactive;
do not restore it automatically when resuming. No source photos or Windows
package were removed; Start pins/layout were retained and optional old pins no
longer have active configured sources. The existing main package was activated
at PID43104 and passed76seconds of same-PID health plus accepted one-frame native
wallpaper publication, with no old source/gallery publication.
Operation evidence: build/review-validation/fresh-daily-reset-oct24.log.

Independent defaults should share a coherent policy for typography, wrapping,
spacing, icon scale/placement and branding, with deliberate native/type-specific
exceptions explained and configurable. Window Capture is currently a proposal
for periodically captured window snapshots; API choice, fastest useful cadence,
minimized/occluded/protected-window behavior and idle cost require research.

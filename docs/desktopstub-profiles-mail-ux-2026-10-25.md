# DesktopStub profiles, Gmail and settings update — October 25

The user selected Gmail / Google Workspace as the first Mail account service and
separate tray icons as the default. This pass continues the October 24 fresh
daily-use configuration. It does not reset or reconnect archived accounts.

## Profiles and identity

Automatic identities use a cryptographically random persistent 128-bit profile
ID plus a Windows SID scope. Package name and Publisher are persisted, not
randomized at every launch. The friendly Publisher display name defaults to the
Windows username. Static name/Publisher overrides remain available.

Managed profiles have separate settings/account INIs and per-identity runtime
generations with verified host/broker/launcher copies. The manifest and generated
assets therefore cannot be overwritten by another profile. Migration preserves
the INI path and its credential binding. An existing main identity is adopted
only for its verified Windows owner. Another Windows user is routed to owned
per-user storage. The coordinator never impersonates another Windows user.

The Profile Manager is in the tray, Properties and `--profile-manager`. It can
create fresh profiles, start/stop/open settings, choose the default and opt into
autostart. Separate tray icons are the default; unified presentation is opt-in.
The ordinary second launch offers a fresh independent profile without copying
accounts. `Settings.AlreadyRunningProfilePrompt` controls this offer. Existing
explicit commands and configured Generate/Console/Ignore actions remain valid.

Unified presentation hides only managed nondefault profiles explicitly configured
for Toast; Auto/Balloon profiles keep their icon. If Toast subsequently falls
back to Balloon, the hidden profile restores its icon for that session before
delivery. A failed icon creation retains the hidden state for a later retry;
there is no extra polling worker.

Optional parent/child process-exit waits report native crash statuses in both
directions. They validate SID, session, process creation time and executable;
ordinary exit codes do not warn. There is no process-list polling worker.

Identity migration displays the exact current/proposed pair and the need to
repin first. It stages separate package files, checks exact configuration
revisions and package-family collisions, then writes a recoverable receipt.
Workers, controller and singleton are released before the new setup starts.
Setup/process-launch failures restore the previous identity fields while keeping
later account edits. Explicit Exit suppresses recovery relaunch until a later
manual start. Old registered files and pins remain recoverable.

Mixed delivery can share one profile resident: Live Tiles use its identity and
Registration jobs use separate owned static packages. Scheduler, COM apartment,
single-instance and image-engine lifetime remain process/profile controls.
Tile cache, text, PNG retry and delivery policies are per tile. Backend-specific
Registration controls appear only for that delivery method.

## Gmail Mail

Mail has a separate purpose-bound Google grant. Calendar credentials are never
silently reused. Headers preview defaults to Gmail metadata permission; Snippet
preview needs read-only Gmail permission and reconnecting. Fixed HTTPS endpoints,
bounded responses and INBOX/UNREAD labels avoid unsupported metadata-scope search.
No message bodies or attachments are fetched. The broader snippet response is
restricted to headers and snippets.

Unread messages and threads remain separate server counters. The optional native
badge uses the message count. Badge delivery follows the successful tile update;
failure does not replay a committed queue. Mailbox addresses are private
in-memory activation metadata, absent from tile XML and launcher arguments.

Medium/Wide rotate individual messages, up to five. The final item also carries
aggregate Large; Small keeps its static icon. Empty/disconnected/hidden text
replaces the old queue, and stale queued jobs are rejected. Transient errors keep
last-good content with a source diagnostic. Account/permission changes clear it.
Sender, subject and snippet stay separate. Their display mapping is configurable
because the original UWP producer-to-string mapping remains unproven.
Presentation strings are clipped safely at Unicode boundaries to keep five
long messages within the 64 KiB tile transport budget.

See [native Mail evidence](native-mail-tile-evidence-2026-10-25.md). Implementation
and fixtures did not access a personal mailbox or start Google sign-in.

## Properties and tile presentation

Properties has hover/pressed and keyboard highlights, Windows-accent/custom
`#RRGGBB` color, `Properties.CaptionMode=Properties|System`, stable indented
sections with explicit expansion, and Restart DesktopStub at the bottom.
The tray remains available. Highlighting adds no idle polling worker.

Fresh menus show Desktop only; other sources remain in Add tile. Empty Quick
Actions/gallery containers are hidden. Logging is under Advanced. Clock's
`IconStyle=Auto|Clock|WorldClock` distinguishes local and world-clock defaults.

Game resources use readable wrapped rows with optional original artwork on the
right. Shared `TextIconPlacement=Right|Left|None`,
`TextIconScalePercent=25..200`, `TextRowSpacing=0..24` preserve the current text
preset. Missing WuWa artwork uses text rather than an unrelated resource icon.
Prepared row limits are two Medium, three Wide and five Large; bitmap layout
drops whole overflowing rows. Tight versioned 44/30 corner assets increase the
visible glyph without changing body artwork. Static Registration faces can use
the same generated corner for Logo/NameAndLogo. Clock/Calendar and photo branding
retain their deliberate native exceptions.

Calendar `EventWindow=DaysAhead|TodayAndTomorrow` makes the horizon explicit.
TodayAndTomorrow ends at local midnight after tomorrow, including DST changes;
DaysAhead retains extended behavior. Agenda grouping uses target-zone
year/month/day keys instead of ambiguous localized weekday/day labels. This does
not claim every original Calendar selection rule has been reverse-engineered.

`Delivery.DisabledWarning=Off|Startup|OnChange` and
`Delivery.DisabledCheckSeconds=30..86400` configure each tile's permission warning.
Startup is the default. Supported `TileUpdater.Setting` results distinguish
application/user/policy/manifest disablement; missing/failed queries are unknown.
Custom Start implementations may not expose every local toggle through that API.

## Validation and delivery

Fixtures use synthetic credentials and injected HTTP/filesystem/process/package
operations plus offscreen rendering. They do not sign in, read mail, pin tiles,
register test packages or exercise hardware. Fresh binaries are scanned before
execution. Final check counts, hashes, guarded installation and resident health
are recorded after completion.

At the pre-install checkpoint the combined new-source host passed **1,283 checks**
and the maintainer source checker passed **2,269 checks**, both after clean scans.
Focused gates also passed: App launcher 73, Gmail/Mail 471, badge transport 963,
Properties model 32, game layout 35, asset/default-branding 762, artwork pixels
386,444, and the separate Calendar/Clock/delivery/game host 145. Profile model,
native storage and migration checks are being extended for the final routing
and registered-identity proof corrections. No binary update or configuration
reset has been performed at this checkpoint.

The historical four-pixel legacy Medium static-caption raster discrepancy stays
open; its tolerance was not weakened. Cross-user/multi-profile package deployment,
crash banners, Gmail consent/badges and custom-Start disablement still need native
acceptance beyond inert fixtures.

### Native acceptance correction: AppData virtualization

The first native update (host SHA256 `348CEC101AC34BDBEE14903259D3C702492C67D053DAD7F966CEB72496B009F9`)
published a valid tile batch and passed the 75-second health interval. Opening the
Profile Manager then exposed its missing native caption; a hidden-window caption
regression and the creation-handler correction were added.

A subsequent guarded update stopped before replacing files because ordinary
`--exit` returned without reaching the existing controller. Read-only inspection
proved that packaged Main's registry was in its package-private LocalCache,
whereas the plain helper saw an empty AppData registry. The controller title/INI
hash matched, its mutex was visible, and a verified request 2 received the normal
IPC acknowledgement and shut it down gracefully. Deferred recovery restarted
the previous build; no forced termination was used.

Coordination now uses `%USERPROFILE%\.DesktopStub\Profiles\<installation-hash>`
outside virtualized AppData. A guarded import copies registry metadata only from
the old verified locations; INIs, account data, runtimes and originals stay in
place. The owned-Main/empty-registry exit branch now proceeds to normal IPC.
The correction passes 65 injected native storage/migration tests and the combined
host gate passes **1,294 checks**. Profile help/version actions are handled before
context discovery or creation, unknown management arguments are rejected, and CLI
profile listings use the shared Unicode-capable output writer.

The final compiled factory check creates only Desktop/Wallpaper, without assigning
a resident identity. The compiled configuration check accepts the new typed
settings and rejects an invalid Calendar window without changing its INI. The
user's real pre-update file actually contained Desktop and an added Now Playing
instance; both are retained. The October 24 reset is not repeated.

## Final installed acceptance

The corrected build is installed at the existing canonical paths. SHA256:

- `DesktopStub.exe`: `903C92984FDFC4D56910C3380414D7C3DD4B65FCDB214D140D24868923FC238D`
- `DesktopStubLiveTileBroker.exe`: `9C4BE67B4BF788A7AE57D8DCF602F16D0D906B1B47799C26051E8F09A71CCB8B`
- `DesktopStubTileLauncher.exe`: `62CBF75BF5C0E76C694F99ABF2C6A49EFA4E2964621B048F39AEF7DAB5AB6127`

All three installed files scanned clean. The existing package published a
successful native tile batch and remained healthy at the same PID for 75
seconds. Ordinary external `--profile-list` then returned exactly one Main
profile with the original ID, matching the physical common registry under
`C:\Users\Amiya\.DesktopStub\Profiles\6EB4C38F97963FB4\profiles.ini`.
External `--exit` returned 0 and gracefully closed the packaged resident; the
existing package was reactivated successfully. The corrected Profile Manager
caption reads `DesktopStub profiles`, with one `Main (default)` entry and the
separate-tray default retained. No additional profile was created.

The final combined host passed **1,294 checks**, the source guard **2,269**,
the profile model/routing gates **79**, and injected native profile storage and
migration gates **65**. The other focused gates above remain passed. No product
source changed after the final compiled/scanned validation gates.

The guarded installation backup is
`DesktopStub/build/SettingsResetBackup-ProfilesShared-20261025-172755-672bcd`.
Current Desktop and Now Playing settings remain active. Existing package
identity and Start pins are retained; no reset, package registration, automatic
pin request or account sign-in was performed. The native proof receipt is
`build/review-validation/desktopstub-profiles-oct25-shared-native-proof.json`,
installation log `desktopstub-profiles-oct25-shared-install.log`, and visual
capture `desktopstub-profiles-native-oct25.png` in that same validation folder.

## Retained backlog and feasibility

Window Capture remains a proposed snapshot source. Windows Graphics Capture can
target an HWND, but capture cadence and Start display cadence are distinct. The
local tile update API provides no frame-rate contract. Backend, useful cadence,
minimized/occluded/protected-window behavior and idle cost require measured
acceptance: [CreateForWindow](https://learn.microsoft.com/en-us/windows/win32/api/windows.graphics.capture.interop/nf-windows-graphics-capture-interop-igraphicscaptureiteminterop-createforwindow),
[local tile updates](https://learn.microsoft.com/en-us/windows/uwp/controls-and-patterns/tiles-and-notifications-sending-a-local-tile-notification).

Widgets Board is a separate provider/manifest and widget-card system. A future
provider could share data models but needs its own presentation/lifecycle:
[widget providers](https://learn.microsoft.com/en-us/windows/apps/develop/widgets/widget-providers).

Lyrics sources/timing, the confirmed weekly day-by-hour Calendar timetable,
Battery, fitness-service feasibility, remaining reference parity and performance
investigations stay on the saved backlog. Unimplemented sources are not advertised
as available in Add tile.

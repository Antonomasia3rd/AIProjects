# Notes sign-in UX source checkpoint, 2026-10-15

The earlier native-only history conclusion was incomplete. The original managed
login is preserved at
`89d26e5^:legacy/RealTimeNotesDeskband/references/genshin-real-time-notes-0.0.8/LoginForm/WinFormsApp1/Form1.cs`
and its adjacent Designer file. It opened a different official Battle Chronicle
page for Genshin Impact, Honkai: Star Rail, and Zenless Zone Zero, used game icons,
and read a displayed UID with a game-specific DOM selector when Done was clicked.
The initialization handler called WebView2 CookieManager.DeleteAllCookies; it did
not call HoYoLAB Logout. It also used an old mobile User-Agent and hid scrollbars
with injected JavaScript. The native manual-import implementation found earlier
was a different layer. This corrects the historical conclusion in
`notes-embedded-signin-2026-10-14.md`.

The updated native window uses the original game's official page and names the
game in its title and heading. After sign-in, **Find game accounts** discovers
UID/server/nickname choices. One returned account is selected; multiple accounts
require an explicit choice. **Save selected account** reads the current embedded
cookies again and checks that the chosen UID/server still belongs to that session.
Discovery never saves credentials. An explained manual UID option remains for API
failure, unavailable account data, or unsupported servers. The manual option uses
the in-game UID, not the HoYoLAB account ID.

Discovery follows the current official role-selector protocol, verified from
public first-party JavaScript on 2026-10-15:

- [Battle Chronicle page](https://act.hoyolab.com/app/community-game-records-sea/index.html)
- [Page application code](https://act.hoyolab.com/app/community-game-records-sea/pc_3970008a.js)
- [Role-selector code](https://act.hoyolab.com/app/community-game-records-sea/vendors_88c135d8.js)

The code requests `/account/binding/api/getAllRegions` on
`api-account-os.hoyolab.com` with the selected `game_biz`, then
`/binding/api/getUserGameRolesByLtoken` with that game and each returned region.
It reads `data.list` entries with `game_uid`, `region`, `region_name`, and
`nickname`. The implementation uses the existing bounded HTTP transport on a
worker: at most eight known regions, 64 account rows, a 1 MiB total response
budget, and one 15-second overall deadline. Injected transport remains available
for tests. Unknown game/server combinations, malformed/oversize data, cancellation,
and transport errors publish no partial choices. Errors do not echo response
bodies, authentication headers, or exception contents. This is a first-party web
protocol, not a promise of a stable public HoYoLAB API; manual entry remains
necessary when its behavior changes. No live authenticated endpoint was called
while implementing it.

The selected server is saved as optional `Notes.Resin.Server`,
`Notes.Stamina.Server`, or `Notes.Charge.Server`. An allowed explicit server takes
precedence over UID-prefix inference; an empty value retains the old inference.
Imports preserve explicit Server/Region aliases and clear a stale saved server
when replacing an account without one. Manual UID replacement clears an old
server selection. The existing parser, optional DPAPI policy, native atomic batch
save, per-account refresh, and account-family alias clearing remain in use.

Fresh browser sessions are the default. Each game has its own named WebView2
profile under the existing normalized INI identity directory. With
`RetainSession=0`, the controller uses supported InPrivate mode; closing its last
view releases that private session. Empty on-disk profile infrastructure may
remain: InPrivate does not promise no profile directory. With `RetainSession=1`,
that game's ordinary browser profile is retained. The window's Remember checkbox
saves the typed preference and restarts its game view. The INI sections are
`Notes.SignIn.Resin`, `Notes.SignIn.Stamina`, and `Notes.SignIn.Charge`, each with
`RetainSession=0|1`. Other games use separate profiles, and existing older shared
browser data is not deleted during upgrade. These APIs and their profile lifetime
are documented by Microsoft in
[Environment10](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2environment10)
and [controller options](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2controlleroptions).

**Clear local browser data** uses Profile2.ClearBrowsingDataAll for the current
embedded session. It never invokes HoYoLAB Logout, clears imported INI credentials,
or touches another game's profile. In fresh mode it does not delete an older
remembered session; the confirmation explains that Remember must be selected to
clear that retained profile. Completion is reported only by a successful callback.
The cleanup state machine first hides/stops the game page, admits one exact
`about:blank` navigation, and waits for that navigation ID to complete successfully
before posting the clear operation. Ordinary page/popup navigation stays blocked
while pending. Normal Close is deferred until cleanup succeeds or fails; terminal
failure is described before a deferred close completes. After 30 seconds on a
monotonic clock, the user can explicitly cancel and close with a warning that
local data may remain. Forced application shutdown abandons the operation and
closes its view without waiting or claiming completion. Generation/operation
tickets reject late events and callbacks. This is not crash-atomic browser-data
erasure: runtime failure, explicit timeout cancellation, forced shutdown, or power
loss can interrupt it. None of those paths clears imported INI credentials. See
[Profile2 cleanup behavior](https://learn.microsoft.com/en-us/microsoft-edge/webview2/reference/win32/icorewebview2profile2).

Navigation start/completion, HTTP failure status, a 30-second still-loading hint,
and browser process failure now have visible feedback. Retry reloads an intact
view or recreates a failed one; Open game page returns to the selected game's
official page. Failed/canceled/reset/closed browser generations cannot save old
credentials. Security-verification pages remain user-interactive. The reported
white-screen cause has not been reproduced or diagnosed by these source changes.
No URL, authentication ticket, cookie value, or browser crash dump is logged by
this implementation. The recovery approach follows Microsoft's
[process failure guidance](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/process-related-events).

Public application hooks are unchanged:
`OpenEmbeddedNotesSignIn(HWND,int resource)` and `CloseEmbeddedNotesSignIns()`.
The window owns its message handling. UI state/COM access stays on the UI thread;
only copied HTTP inputs/results cross to cancellable, owned worker threads.
Completed records are reaped during sign-in UI activity; ordinary window close
cancels its lookup without joining active work on the UI thread. Global
`CloseEmbeddedNotesSignIns()` cancels all lookups first, then joins every owned
thread before closing windows and before program/global teardown. No thread is
detached. Shutdown can wait for the bounded HTTP work to finish: cooperative
WinHTTP cancellation normally returns promptly, while the lookup retains its
single 15-second request deadline plus bounded parsing/teardown. Thread-start
failure cancels the unpublished work, clears busy/lookup state, and saves nothing.
Worker cancellation callbacks hold weak result references so completed records
do not unnecessarily retain copied credentials. Workers have no configuration
writer; they post completion, and generation/profile checks guard the UI-thread
save. The inert build excludes the embedded implementation.

Changed files owned in this slice:

- `dependencies/DesktopStub/ga_notes_signin.inc`
- `dependencies/DesktopStub/ga_secondary_tile_tray.inc` (game-aware manual setup,
  stale server clearing, and root-requested SMTC setup submenu hook)
- `dependencies/content_sources/notes_signin_model.h` (new pure policy)
- `dependencies/content_sources/notes_accounts.inc` (new injected discovery API)
- `dependencies/content_sources/notes_signin_workers.h` (new owned worker lifecycle)
- `dependencies/content_sources/notes.inc` (account server/request override only;
  root separately moved unchanged enums to notes_model.h)
- `dependencies/content_sources/notes_profile.inc`
- `tools/NotesSourceTests.cpp`

Root/CPU owns matching content model/server/runtime plumbing, generic CLI/tray
normalization, build recipes, and final validation. This agent ran source/diff
checks only. Expanded Notes fixtures cover synthetic region/account replies,
multiple servers, cancellation after a partial lookup, size/row caps, mismatched
server replies, transport exceptions, optional server precedence/import, browser
preference validation, cookie-domain boundaries, and stale generation policy.
Additional cleanup-model cases cover exact navigation ordering, blocked page
navigation, one-time clear authorization, deferred Close, wrong-generation
completion, failed navigation/clear, retry, explicit abandonment, and late events.
Owned-worker fixtures cover nonblocking reaping of active work, joining canceled
work before shutdown returns, completed-record reaping, injected thread-start
failure, destructor lifetime, and cancel-all-before-join ordering.
Compilation, Defender scanning, inert execution, native WebView2 layout,
successful sign-in/discovery, actual browser cleanup/retention, and installation
are root/user acceptance work. The user account's current INI credentials, pins,
running process, and cached browser sessions were not inspected or modified here.

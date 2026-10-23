# Calendar accounts, Kuro sign-in and Prayer notifications

This update adds a user-directed Google Calendar connection, guided overseas
Wuthering Waves sign-in, and separate Prayer notification phases. It also moves
desktop notifications out of the source configuration menu. These notifications
are banners/Notification Center entries, separate from Windows tile updates.

## Setup and ordinary use

- **Calendar:** Tiles > your Calendar > Source settings > Calendar source >
  Google Calendar, then **Google account and calendars...**. Import your own
  Google Cloud Desktop OAuth JSON, choose **Connect to Google...**, approve the
  read-only access in your normal browser, select a calendar and choose **Use
  selected calendar**. Each tile can use its own account, calendar and refresh
  interval. The default interval is five minutes; Google look-ahead is 1–31 days.
  Local ICS files remain independently selectable. See the [provider guide](google-calendar-2026-10-24.md).
- **Wuthering Waves:** Tiles > your WuWa tile > Source settings > **Sign in to
  Kuro and choose character...**. Enter your Kuro email/password, complete human
  verification if requested, choose a character/server, and save. The dialog
  supports overseas Kuro email/password accounts; Google/Apple-only sign-in is
  not implemented. Passwords are temporary; only the returned game token is
  saved with Windows DPAPI. Manual token import remains under Advanced.
- **Prayer:** Tiles > your Prayer tile > **Notifications for this tile**.
  Enable notifications and configure **Before prayer**, **At prayer time** and
  **Current prayer status** separately. Defaults are a 20-minute upcoming
  notification, an at-time notification, and no quiet current status until
  explicitly enabled. Each phase has its own title/body templates. Notification
  time format can inherit the tile or use 12-hour, 24-hour or Windows format.
- **RSS:** the same direct Notifications submenu holds new-item thresholds,
  cooldown, type, sound and title/body templates. Its first successful fetch
  establishes the baseline without announcing all existing feed items.

Prayer defaults display `Fajr in 20 minutes` or `It's time for Fajr` as the title,
with `3:46 AM` as the body. The optional current status displays `Fajr, 3:46 AM`
and `at Surabaya`. Templates accept `%event%`, `%minutes%`, `%time%`, `%city%`,
`%country%` and `%tile%`; RSS accepts `%count%` and `%tile%`.

Current status uses a silent native toast with popup suppression and expiry at
the next actual prayer. Windows still allows dismissal. Ordinary polling and
resident restart respect dismissal; explicit disable/re-enable creates one new
status. Balloon mode has no equivalent history-only entry, so it does not emit
a substitute popup. Ordinary process exit retains the entry until its expiry.
Deleting/reconfiguring the source removes the owned entry.

HTTP timeouts, response-byte limits, user-agent/device overrides and related
mechanics are under each source's **Advanced** submenu. Notifications remain
directly accessible in the tile menu. Explicitly enabled Prayer/RSS alerts keep
their source active without a Start pin, but produce no tile background or
publication work. With neither pin nor alert demand, their workers are released.

## Account and validation boundaries

Google uses external-browser PKCE authorization and a bounded local loopback
callback. The account grant is DPAPI protected and bound to Windows user,
profile, tile and client. Async setup results and picker rows are rejected when
their originating account/configuration changes. Disconnect removes the local
grant; it does not revoke other applications' Google grants or delete events.

The account adapters were validated with injected HTTP, browser authorization
and credential transforms. No real Google or Kuro login is claimed by these
fixtures. The user must complete browser consent or Kuro sign-in/CAPTCHA to
validate the actual account path. Native submission/installation evidence and
the final check counts are recorded below after the delivery check.

The earlier four-pixel legacy preset raster finding and broader cross-DPI/UWP
parity work remain open; this change does not alter their exact assertions.

## Delivery record

Installed in the existing main package without resetting settings or Start pins.
Only the five Google Calendar client/source fields and six requested Prayer
notification fields were changed in the supplied profile. The downloaded JSON
was read locally by the validated importer; no credential contents were printed.
Google account consent remains pending user interaction.

Backup: `DesktopStub/build/SettingsResetBackup-CalendarAuth-20261024-053302-2e33f8`.
All existing profile/account/history files and the original binaries were copied
and hash verified before replacement. A local installer type-name error and an
empty-resident guard were corrected before the successful swap; the final import
was regenerated from the latest exact profile snapshot. No package was removed.

Validation completed:

- 334 combined native host checks, including notification phase/state lifecycle,
  quiet-entry expiry/removal, unpinned alert demand, independent Google accounts,
  timed/all-day events, stale async results and menu discovery.
- 2,038 source checks and 1,063 notification policy/adapter checks.
- 131 Prayer alert model checks, 60 Google provider checks, 69 independent tile
  model checks and 44 Kuro authentication checks.
- Native product and compile-only notification/UI branches built successfully.
  Validation executables and all three installed artifacts scanned clean.
- Accepted native tile publication and 75 seconds of same-PID packaged process
  health passed at PID 22996 and again at final PID 50976, with no deployment/path failure. This is not proof
  of actual Google/Kuro sign-in or visual parity on every DPI/theme.

The first visible setup-window check exposed source-encoding drift: the normal
host/broker build lacked `/utf-8`, while the fixtures used it. Both normal
compiler commands now explicitly use UTF-8, with independent source guards.
The one newly materialized garbled window-suffix default is upgraded only when
unmodified. The final native check found one visible Google setup window and
verified its exact Unicode title. The binary-only follow-up preserved the
already imported profile; its backup is
`DesktopStub/build/SettingsResetBackup-CalendarAuthUtf8-20261024-054228-1c8c68`.

Installed SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | `8D7000783F8D03F80122E00599882B380A09EC5F99BA72F44BB0106549266522` |
| DesktopStubLiveTileBroker.exe | `EC79E326E1270BC2FA9DECF5FDDD84D8FE899C0ECD59C78F002DD4CF0E71510B` |
| DesktopStubTileLauncher.exe | `3A19E7AC9A17991F34C5D0B17155CB26BF6B6C0AB291EC6D0D014F2C9221C7BA` |

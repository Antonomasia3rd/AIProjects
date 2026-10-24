# Calendar event layouts, reminders and Kuro refresh handling, October 24

The preceding date-only correction did not reconstruct event-filled Calendar
bindings. The user's screenshots exposed that gap. The installed
HxOutlookBackground.dll was inspected again; exact version/hash, addresses and
control-flow witnesses are recorded in
[native-calendar-clock-evidence-2026-10-23.md](native-calendar-clock-evidence-2026-10-23.md).
No personal Calendar store, event database or native app was opened or modified.

## Corrected rendering and publication

Medium uses a regular caption subject and a distinct start-time label. Wide
next-event uses 7:3 event/date columns; the subject can wrap to two lines.
Large uses a regular body subject for its first event and caption subjects for
later events, separated by blank rows. It puts the current weekday/day in name
branding, rather than putting a large numeral above the event list. Registration
bakes the per-size date footer and suppresses the duplicate manifest label.
Explicit typography and branding remain effective.

The native app builds a second notification containing Small and the Wide
agenda. DesktopStub now publishes separate tagged next-event and agenda payloads
with a reset on changed content, independent acknowledgement/asset retention,
and stale-frame checks. Agenda uses the first prepared event day, up to three
rows, 2:5 time/title columns (3:5 for longer times), and the recovered concurrent
event cutoff for normal versus subdued text. A native queue failure prevents
later slots from being sent into that failed destination.

Both adaptive XML and bitmap rendering consume one presentation plan. Status
uses that plan too. Structured start/end data supplies separate time-only/range
fields instead of parsing already formatted strings. Snapshot keys include the
new fields and layout version so old generic images cannot be reused.

The verified boundary is the recovered binding structure, styles, wrapping,
weights, branding and two-document queue shape. Exact localized native resource
wording, every appointment-selection rule, Windows rotation timing and universal
raster pixel equivalence remain unverified. Google/local ICS data is independent
of the discontinued Calendar app's private event store; different event lists
are not evidence that the same occurrence was formatted differently.

## Calendar reminders

Each Calendar tile has a direct **Notifications for this tile** submenu.
Reminders are opt-in, with a 15-minute lead by default, configurable lead/type/
sound, all-day inclusion, snooze choices/default and title/body templates.
Windows handles the actual Snooze/Dismiss buttons, following Microsoft's
[notification schema](https://learn.microsoft.com/windows/apps/design/shell/tiles-and-notifications/toast-schema).
Balloon mode has text only and the menu states that limitation.

The existing resident Calendar poll delivers initial reminders; no independent
scheduler or worker is added. Enabled reminders keep an unpinned Calendar source
active. A bounded hashed ledger prevents restart duplicates, fails closed on
unreadable/corrupt history, requires persistence before delivery, and limits
late/recovery announcements to a 90-second grace with at most three notices.
Authoritative cancellation, expiry, disablement and source change remove only
owned history/scheduled entries. Ordinary app exit preserves Windows-owned
snoozes. Google per-event reminder overrides and ICS VALARM are not imported.
Real system Snooze acceptance still requires interactive testing.

## Kuro mid-session failure

The generic failure notification did not establish that the saved login was
lost. The previous log contains no Kuro response details for that incident, so
its exact cause remains unknown. The provider now keeps the same configuration's
last successful snapshot during later failures, labels retained data as stale in
source diagnostics, and distinguishes transient/rejected/invalid/setup failures.
A second completed transient failure can show an attention notice; repeated
reads of one result do not count as more attempts. Healthy recovery resets the
threshold, and changing the account clears retained data. Sanitized stage/status
diagnostics are logged once per completed result; remote messages, tokens and
passwords are not logged. An unclassified OAuth rejection no longer asserts
that the login expired.

## Validation and delivery

The initial combined host fixture passed 462 checks. The focused reminder
model passed 80 and Kuro refresh/exception handling passed 83, all optimized and
Defender-scanned before execution. Final combined fixture/export/source results,
binary hashes and installation health are recorded below after the operation.

Final validation: **466 combined host checks, 2,132 source checks, 80 reminder
model checks and 83 Kuro refresh/exception checks passed**. Fixture and all three
product artifacts scanned clean before execution. Synthetic body previews using
`test` / `all dah` were exported and visually inspected under
`build/review-validation/calendar-event-layout-preview/`. Adaptive date footers
are supplied separately through the OS branding metadata and are not baked into
those Live Tile body previews.

The guarded binary-only update activated the existing main package at PID
**56100**. It passed accepted native tile publication and 75 seconds of same-PID
packaged health, with no deployment/path failure. Subsequent two-frame batches
also completed successfully. The operation preserved current accounts/settings
and pins; Calendar reminder defaults remain disabled until enabled per tile.
Backup: `DesktopStub/build/SettingsResetBackup-CalendarEvents-20261024-081720-83d2e8`.
Delivery log: `build/review-validation/calendar-events-install-oct24.log`.

Installed SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | `CC229C125F760240756846D0F2749E9C8445882E6236CA860FB70A8C3FB736A2` |
| DesktopStubLiveTileBroker.exe | `DD4EA2599A27CB50BC1C30B61C0C0FB80667C89DE44997DD72D0BB7CAA297B43` |
| DesktopStubTileLauncher.exe | `4B39C2B7CA1F611E05C76C701ED8105D77B85BB2453C29ADF3381DB5990179B5` |

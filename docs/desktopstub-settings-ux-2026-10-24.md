# DesktopStub settings and account UX follow-up, 2026-10-24

This record follows the earlier [account and notification delivery](calendar-accounts-prayer-notifications-2026-10-24.md). It records the implementation, focused validation and completed binary-only delivery below. Older feedback checkpoints describe their own dates and are not a current list of unresolved findings.

## Calendar and settings

- An ordinary Calendar tile keeps its date and event rows clean when no account is connected, a source is loading, or there are no events. These source notes appear in the centralized tile contents dialog, explicitly labeled as diagnostics. A genuine source failure still produces the configuration/error state; it is not silently treated as an empty successful calendar.
- The Google picker offers **All readable calendars on this account** followed by the individual calendars. `GoogleCalendar.AllCalendars=1` aggregates the account's readable calendars; selecting one saves `0` and that calendar's ID. The saved single-calendar ID survives an All selection. A newly imported client defaults to All; existing explicit single-calendar configurations remain single. Aggregation retains original timezones and fails rather than presenting a successful partial list. See the [Google provider contract](google-calendar-2026-10-24.md).
- A native **DesktopStub Properties** window provides tile selection, per-type options and application/plugin settings while retaining the tray menus. It invokes the existing validated settings dispatchers, retains the profile write mutex/revision checks, and rejects stale captured settings until reloaded. It adds no background worker, network request or idle timer of its own.
- The visual/interaction reference is ExplorerPatcher commit `0a88a6e0ef6b1752fea36e581cffff1097e862b0`, particularly `ep_gui/GUI.h` and `GUI.c`. DesktopStub's presenter is independently implemented under the repository's BSD-0-Clause license; it does not include or link ExplorerPatcher's GPLv2 source, resources, registry schema or shell hooks. Supported Windows 11 uses a single measured caption band, the main-window system backdrop and alpha-composited text/footer. Unsupported systems, High Contrast or compositor failures retain a solid standard frame. Resizing and scrolling remain DesktopStub extensions. System policy and battery saver can suppress the material. This does not establish universal pixel-perfect parity across themes, DPI, languages or shell modifications.
- Text inputs share the independently implemented native layout described in the [input-dialog parity record](explorerpatcher-input-dialog-parity-2026-10-24.md), including per-dialog state, password masking, default edit selection, tab order and long-prompt expansion.
- **All tile contents...** is the central, paged view of the actual size-specific tile contents and separately labeled source notes. The repeated per-tile contents/status menu action has been removed from the normal tile menus. Previous/Next, scrolling, resizing and DPI changes keep the dialog usable without an off-screen MessageBox.

## Kuro sign-in and notification replies

The Kuro window begins compact, without a reserved blank verification panel. It expands only after the current accepted challenge navigation succeeds and GeeTest sends its nonce-bound readiness message. Failed scripts, runtime errors and the 30-second loading deadline leave a compact retry path; completing or canceling verification collapses the browser area again.

The reported character-lookup code `1005` is a remote Kuro response after earlier authentication stages succeeded. Its server-side cause remains undocumented and unverified; the change does not claim to fix Kuro's service. The upstream-compatible initial attempt plus three bounded retries share the request deadline. **Retry character lookup** retains only the temporary authenticated token, avoiding another password entry, and expires with the five-minute sign-in session or Close. Only explicit character selection and Save persist the token through DPAPI. See the [WuWa account and validation guide](wuthering-waves-notes-2026-10-23.md). No real account acceptance is established by the fixtures.

Cold foreground toast activation previously reached the ordinary command parser, so a reply such as `confirm=...&choice=no` could be misread as a wallpaper argument. The reserved confirmation endpoint now runs before that parser, recognizes the explicit profile prefix and dedicated inert body marker, and forwards only a bounded, strictly parsed decision to the existing resident. The resident still requires its live profile-bound nonce registry, expiry and one-time resolution. Unknown, stale or replayed replies cannot approve a change, write settings or start another resident; opening a notification body is not approval. Legitimate ordinary CLI values named `review` remain ordinary values.

## Verified checks

The following focused runs passed. Their validation executables were scanned clean by Microsoft Defender before execution. These are injected, hidden-window or offline checks, not live account/package/notification acceptance tests.

| Fixture | Passed checks | Local evidence under `build/review-validation/` |
| --- | ---: | --- |
| Combined native host | 414 | `desktopstub-ux-complete-host-oct24.log` |
| Full source regression | 2,096 | `settings-cold-confirmation-source-final-oct24.log` |
| Google provider, including All calendars | 80 | `google-all-calendars-oct24-tests.log` |
| Google All/individual selection | 17 | `google-calendar-selection-oct24.log` |
| Kuro authentication and compact readiness | 135 | `kuro-auth-ready-oct24.log` |
| Kuro notes, retries and cancellation | 70 | `kuro-notes-retry-oct24.log` |
| Notification confirmation model/launch parsing | 336 | `notification-launch-complete-model-oct24.log` |
| Notification policy and native adapter | 1,063 | `quiet-notification-policy-oct24.log` |
| Paged message dialog | 25 | `paged-message-dialog-oct24.log` |
| Shared text input dialog | 23 | `text-input-dialog-final-oct24.log` |

The source sweep includes configurable string defaults/readers, settings dispatch and write guards, early cold activation, strict IPC, Calendar diagnostic separation, and the All-calendars configuration/picker boundaries. The dialog fixtures do not establish visual parity on every DPI/theme or actual external-service success. No historical raster tolerance or unrelated acceptance finding is waived by these results.

## Delivery record

The final optimized native host and both delivery helpers built successfully.
The host log is `desktopstub-ux-final-frame-build-oct24.log`; its remaining
warnings are the existing Windows SDK GDI+ shadow declarations and secondary
tile signedness warning. All three reviewed and installed artifacts scanned
clean with Microsoft Defender. No upstream ExplorerPatcher executable or hook
was built or executed.

The existing main Desktop package was restarted with the updated binaries,
without package removal, setup, repinning or a profile reset. The complete
pre-swap profile/account/history and binary backup was verified at
`DesktopStub/build/SettingsResetBackup-UX-20261024-071808-84860f`.
Accepted native tile publication and 75 seconds of same-PID packaged resident
health passed at PID **29520**, with no deployment/path failure. Evidence is
`desktopstub-ux-install-oct24.log` and the backup's `desktopstub-ux-health.log`.
No new COM registration was needed or claimed for this binary-only update.

After delivery, the existing Calendar tile
`calendar-ae2fae87b613fd48` was set to `GoogleCalendar.AllCalendars=1` through
the validated configuration-only command. Its existing client and account grant
were not reimported. The saved quoted INI field was checked explicitly.

The installed foreground confirmation endpoint was exercised from
`C:\Windows\System32` with an unknown all-zero nonce (`choice=no`), a malformed
nonce and the inert body marker. Expected exits were **0, 2, 0**, with no
blocking error dialog, additional resident or profile hash change. No live
approval token was used. Evidence files begin
`desktopstub-cold-confirmation-` under the local validation folder.

`DesktopStub.exe --settings` opened the single visible native window in the
existing resident. Own-window capture verified class `DesktopStubNativeSettings`,
title `DesktopStub Properties`, and an 810 × 640 window. The inspected default
view has one caption band, visible dark material, readable rows and no
scrollbar/caption-button overlap. Capture and metadata are
`desktopstub-settings-native-oct24.png` and
`desktopstub-settings-native-capture-oct24.json`. This default-view check does
not establish all scrolled pages, themes, monitors, accessibility tools or DPI
transitions; real Kuro sign-in/CAPTCHA and Google account acceptance also remain
interactive checks.

Installed SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| DesktopStub.exe | `3A20C2CFC5DF95EC70585211B4AE67B773B08F6A0D4D19CB24FEFA078A21B1A7` |
| DesktopStubLiveTileBroker.exe | `C0D91310F714ECFE6155DDABBB19899F54A80CFCFC1FF3AB2AA6C8CCE012A593` |
| DesktopStubTileLauncher.exe | `B5E266CD4428107F5F907FD05CAE26CF46FF2B985307E28E0EEBE4057C6233DC` |

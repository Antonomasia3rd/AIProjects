# DesktopStub continuation — October 28

Use the original D: checkout on main even if this chat remains attached to an
extra Codex worktree. The user explicitly deferred deleting/archiving this chat
and starting another until the existing findings are resolved, followed by the
Luna Windhawk reviews. Do not attempt that cleanup now. Preserve other chats'
uncommitted Windhawk source, root object files and private analysis artifacts.

## Completed implementation and local validation

- Prayer Large timetable separation: configurable
  `PrayerTimes.TimetableGapLines=0..3`, default 1, through CLI/INI/tray/Properties.
  Semantic XML and measured GDI spacing agree; changing display spacing does
  not fetch the schedule again. [Evidence](prayer-timetable-spacing-2026-10-28.md).
- Standard status headings follow the retained Clock contract: Medium 15,
  Wide 24, Large 34 logical pixels, including default family/weight. Prayer,
  Battery, Now Playing and Discord/wrapped status use it. Calendar/Mail retain
  native semantic layouts; image/action tiles and RSS/Games are intentional
  exceptions. Explicit Custom settings are preserved with no INI rewrites.
  Long SMTC allocations and its three-icon stack now respect the production
  branding footer. [Full type map](standard-tile-defaults-2026-10-28.md).
- Default Genshin/HSR/ZZZ/WuWa rows share RSS's size-aware presentation and
  one/two/three resource capacities. Available icons stay right; absent WuWa
  art leaves full text width. A freshness row does not consume the sole Medium
  resource slot. Custom setup/error XML also respects its explicit style.
  [Game evidence](games-rss-layout-2026-10-28.md).
- Clock Auto icon selection now depends on timezone mode, not translated or
  renamed Location text. Explicit icon choices remain unchanged. The composite
  world icon is still independently drawn; native pixel parity is not claimed.
  [Icon audit](clock-icon-policy-2026-10-28.md).
- CI INI exit2: hosted diagnostics and a local 8.3-alias reproducer proved a
  short/long temp-path comparison defect. The guarded worker compares directory
  identity and retains stricter private-root checks. 27 mixed-language checks
  pass; production locking/hash behavior is unchanged.
  [CI evidence](ci-ini-temp-alias-2026-10-28.md).
- CI large-object compilation: transparent fixture now has /bigobj in both
  optimization modes. Broad stale factory assertion now requires Registration/
  Main; offline no-image/no-package checks use a fresh private INI/exe scope.
  Optimized release-style fixture compilation preserves the 45-second watchdog
  and all assertions. [Broad evidence](ci-broad-host-contracts-2026-10-28.md).

Final local checks: 24,419 broad host checks in 21.383s; 2,585 focused host
checks; 2,287 source checks; 238 Prayer model, 132 media layout, 56 game layout,
780 artwork-policy and 27 mixed-INI checks. New executables were scanned clean
before execution. Production-footer offscreen PNGs at 100%/200%, prayer gaps at
100%/150%, and measured font-line heights were inspected/retained under ignored
artifacts/desktopstub-layout-20261028. Earlier intermediate witnesses in
artifacts/desktopstub-layout20261028 are not the final footer acceptance set.
The four-pixel legacy raster discrepancy remains separate and unchanged.

Version 27.0.0.3 (DesktopStub-v27-beta3) host/broker/launcher build passed and
scanned clean. Source/host evidence does not establish actual Start display,
account, hardware or every Windows-version acceptance. Hosted run results and
release/install receipts follow delivery; do not infer a successful hosted
release from the local build. Stable release safeguards remain enabled.

## Remaining work and review ordering

The user moved richer Discord tiles ahead of the remaining feature backlog,
with Luna review still last. The selected default click is compose for review,
not an automatic send. Personal-token access is explicitly experimental and
requires a prominent Discord policy acknowledgement and verified account
review. User approval is not evidence that Discord will permit it or that an
alt account cannot be terminated. Keep bot/OAuth/IPC capabilities distinct.

The account-reader/UI implementation and injected fixtures are described in
[the reader record](discord-account-reader-2026-10-28.md) and
[the host record](discord-reader-ui-2026-10-28.md). Native acceptance remains
separate. The authorized browser alt preflight successfully fetched identity
and one selected-channel message through HTTP; no message or read ACK was
posted. The subsequent authorized native HTTP test passed identity, one
selected-channel history read and avatar decoding, then stopped and cleared
its session credential. No raw token entered CLI/source/logs/files; the browser
automation kernel was reset afterwards. Gateway, Start and actual send acceptance
remain separate. [Native evidence](discord-reader-native-acceptance-2026-10-28.md).

Local beta3 is already delivered; the newer Discord work and no-op INI fix are
not part of that binary. Do not describe a test build as a shipped beta or a
successful Actions release. A separately named production build is used for
review, with fresh dependent objects after the shared reader schema changed.

Final Discord gates pass: 353 model, 64 layout, 135 synthetic transport, 45
actual inert host and 2,287 source checks. Independent review fixed Custom
render routing and nonempty display-name fallback. A degenerate 1x1 avatar
fixture was replaced with a correctly sized 8x8 solid image after the exact
renderer comparison already matched; no raster tolerance was relaxed.
The unchanged legacy-INI no-op fix and corrected locked-import fixture pass
fresh main broad/Revision hosts: 24,420 and 1,282 checks. Hosted CI still has to
reach the retained Registration error/owner diagnostic. All new account tests
have sent zero real messages and zero read acknowledgements. The user's new
reply explicitly permits this alternative account for future testing; exact
action/scope confirmation requirements still apply where the tools require it.

The [reconciled findings](desktopstub-open-findings-2026-10-25.md) retain the
unresolved 1809 report, Calendar behavior/providers/reminder overrides, real
account/session/native acceptance, full Notes parity, missing tile sources,
charging ETA/Smart Charging and the legacy raster witness. Performance stays
completed for now. No archive/new-chat operation is due while these remain.

After that DesktopStub queue, inspect the actual saved diffs and evidence from
the user's Luna chats before deleting/archiving this chat:

- Luna - Analyze LogonUI styler options
- Luna - Review colored taskbar indicators
- Luna - Reverse-engineer ExplorerPatcher
- Luna - Port fake lockscreen to non-Modern
- Luna - Replace UIAccess with custom ZBID
- Luna - Restore the Windows back button

Those titles were discovered read-only. Their code has not been reviewed in
this pass and must not be described as accepted or installed. Compare source,
claims and native evidence, check current binaries/PDBs per build, preserve
native transitions and disclose unsupported versions/lifecycle issues. The
existing LogonUI unload/Back/architecture limitations are not closed by layout
or CI work. Read-only analysis does not authorize injecting/reloading mods.

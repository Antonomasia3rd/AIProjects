# Next pass: outstanding fixes and acceptance

Checkpoint: 2026-10-18. The gallery recovery, stale-dialog and source Rename
fixes below are installed with two CPU optimizations. Typed idle parsing
caches and gallery status retention are validated and installed. The shared
notification-app live CLI reload repair passed private IPC/fake-runtime checks.
Read CURRENT_STATUS and the referenced records
before changing source; old chronological audit entries are not a current bug
list. Private bounded fixtures remain the default for failure injection.

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

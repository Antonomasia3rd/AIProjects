# Content presets, native rotation and bounded capture checkpoint

This follows commit `b9ecc06` on `codex/review-original-requirements`.
The user chose the existing two-item topology: Wallpaper with CustomText,
followed by transparent SMTC text. The preset preserves current captions and
timing, adds new active slots without deleting saved entries, and selects Auto
text delivery and Auto rotation. Existing profiles keep DesktopStub rotation
unless the user selects the new driver or applies a preset.

## Implementation and review findings

- Shared pure preset plans now drive actual numbered contents. Compatibility
  source switches remain separate. NowPlaying import explicitly confirms the
  replacement of all saved Content.* sections in the GUI.
- Windows rotation uses two through five enabled entries, stable tags, coherent
  snapshots and successful-delivery acknowledgments. Unchanged items do not
  produce API updates; a changed item replaces only its tag. Windows chooses the
  animation timing. Visibility-dependent hardware/presence actions fall back
  to app rotation.
- Native XML text reuses its unchanged bitmap cache/asset token. Queued assets
  remain protected from cleanup until replaced or the queue is reset.
- Review found and fixed an unbounded per-artwork background cache, a return to
  app rotation that could suppress an identical first frame, failed departure
  updates that otherwise never retried, and all-disabled contents leaving stale
  queued items. Cache entries now retain only keys composed by the current tick;
  queue departure remains pending until acknowledged, with delayed retries.
- A frozen native batch cannot cross pending mode registration and lose its
  first reset-bearing update. A failed batch stops before later slots.
- Final review found that compatibility helper traces could acknowledge a clear
  as an update, and do not correlate individual queue items or fence late
  requests. Update waits now require an update success. Native batching requires
  direct delivery from a resident with package identity; unpackaged Windows
  8/8.1 helpers retain app rotation until request correlation/lifetime handling
  is implemented. This remains a documented compatibility gap.
- SMTC manager/session subscriptions retain their worker apartment and coalesce
  session, metadata and playback changes. Timeline progress is not subscribed.
  Supported event mode has a fallback interval of at least 30 seconds;
  unsupported interfaces use configured polling.
- Live wallpaper capture uses a fresh child of the same executable, assigned to
  a kill-on-close Job before resuming. The shared three-second attempt deadline,
  shutdown polling and bounded owned-child cleanup replace the uncancellable
  main-process PrintWindow call. No new installed DLL is required.

## Final validation

Fresh fixtures compiled, scanned clean before execution and passed:

| Scope | Checks |
| --- | ---: |
| Pure preset plans | 1,719 |
| Pure queue delta/eligibility | 55 |
| Notification transport and fake updater | 839 |
| Capture cadence/helper protocol | 1,031 |
| Async source host | 10,217 |
| Fresh synthetic capture processes | 46 |
| Fake SMTC reader/events/apartment lifecycle | 164 |
| Assembled host/menu/configuration and queue/CLI regressions | 9,144 |
| Hardware-omitted host | 3 |
| Adaptive renderer/host | 5,522 |
| Transparent backgrounds | 331 |
| DesktopStub/shared source gates | 1,764 / 277 |

Normal host/broker and the existing optional background-task DLL compile. Six
copied-binary CLI cases pass, covering actual argument parsing, preset copying,
repeat/no-op behavior, canonical driver saves, invalid values, fresh profiles
and malformed helper entry. Full copied normal-binary offline smoke passes,
including the unchanged exact preset pixel checks, adaptive Auto/Overlay parity
and twelve concurrent INI writers. Project map and selector checks pass for all
eleven products plus All. Portable targets are wired to CMake/CI; no Linux
execution is claimed on this laptop.

The first final compile caught an out-of-scope parser error variable, fixed with
a local after the argument loop. Host/source gates caught old expectations for
the renamed menu and the former settings-only empty-batch guard. They now check
the intended preset behavior. The first copied CLI harness failed because its
reader did not recognize quoted INI keys/values; correcting that reader retained
all exact value/preservation assertions. These failed stages are preserved in
ignored logs and are not counted as passes.

The old resident remained running through validation. After final scans, the
guarded installer closed it gracefully, backed up the previous binaries/profile,
verified unchanged INI bytes during copying, scanned both canonical paths and
relaunched DesktopStub. Backup:
`DesktopStub/build/ReviewBackupOct13C-20261013-202436`.

Installed SHA-256 identities:

- Host: `AFD40F177EAA93EC51E5EFD681B26C130DB53F98066970234B35410AFF510FAF`
- Broker: `C27468F49FAA85347A7F14972D2A06D81E324FAFF9C390DF6F929AB4C82BE5A2`

Relaunched PID 36840 was independently queried as package
`dev.local.desktopstub_27.0.0.0_neutral__kvejxfsq1reej`; its normal log recorded
successful Live Tile updates. Existing `LiveTile` delivery and `DesktopStub`
rotation preferences remain selected. The new Auto driver was not applied to
the user's profile. These are checkpoint observations, not sustained stability
or native queue animation acceptance.

One retained preset diagnostic using the previous normal renderer passed the
unchanged exact pixel gates. Baseline and secondary witnesses were DARK/DARK
with zero differing pixels; all seven saved image/RGBA hashes were verified.
See [the retained witness review](renderer-witness-review-2026-10-13.md).
The historical four-pixel variation remains unexplained.

Tests used private configurations, in-memory menus, fake providers/updaters or
fresh sleeping/synthetic children. They did not request real package, Startup,
media, network/account, hardware, window capture or service changes. Actual OS
rotation, shell layout and sustained DesktopStub/NPSMSvc CPU behavior remain
local acceptance. Defender was not bypassed, dumps were not uploaded and the
deferred Microsoft case was not reopened. Windhawk and the user's untracked
Discord bot work remain outside this change.

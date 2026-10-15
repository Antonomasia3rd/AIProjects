# Primary Desktop tile image retention

The user reported the primary Desktop tile reverting to its static desktop
icon after the crash rollback, while source and gallery tiles continued showing
live content. The current profile had Content.1 enabled with Wallpaper as its
background, composition enabled, cycling disabled and native Live Tile delivery.
Those settings do not explain the loss.

The log showed a successful primary wallpaper generation at 21:43:46. Its
versioned Medium and Wide image files, token `01DD5CB39600641B`, were absent
when inspected afterward. Subsequent source/gallery generations had succeeded.
No reboot or Windows cache reset was performed.

The cleanup path protected secondary assets and native primary queue slots,
but omitted a nonqueued primary notification. Only two recent versions per
size survived the global fallback history. Unchanged primary frames remained
acknowledged and were not republished after their images disappeared.

`ga_generation.inc` now retains the last successfully published nonqueued
primary image set. Secondary/gallery deliveries preserve that lease. Successful
primary replacement retires it; primary native queues continue using their
existing slot retention. Cleanup includes both kinds of primary leases.

`PrimaryAssetRetentionHostTests.inc` exercises the real pruning path using only
private fixture files: five alternating source/gallery deliveries preserve the
unchanged primary, then successful replacement permits old-primary reclamation.
No source refresh, package, account, hardware or live notification API is called
by that fixture.

The first fixture revision used four-digit tokens, which the production pruner
correctly ignores, so its reclamation assertions failed. Tokens now match the
production 16-hex-character format and an explicit predicate check prevents the
test from silently bypassing pruning. The corrected combined host passed
**14,419 checks**, zero failures, in 28.1 seconds. Host and broker compiled
successfully and passed Defender custom scans before execution and again at
their installed canonical paths. Default validation remained inert.

Installed identities:

- Host SHA256: `BF587A543FE929D910DA32A274DFD756BF273BF3A87E51A908D41F0E717D4699`.
- Broker SHA256: `2F48AE387FB2828FCEA4E6CDCD39BA55BDDD645FFE94A4BA93B20053448EBA5C`.
- Backup: `DesktopStub/build/ReviewBackupOct15Feedback-20261015-215505`.
- Host test log: `build/review-validation/primary-retention-corrected-host-test.log`.

The normal app was gracefully stopped and relaunched with the existing default
profile. The installer verified unchanged INI/ledger bytes during binary copy;
no configuration, pin, account or pool reset was performed. A 75-second guarded
startup check passed with resident PID 40824. The new primary's three versioned
images (token `01DD5CB5326D5B1A`) remained on disk after a recent tail containing
29 gallery generations and 30 successful deliveries. One normal resident was
running at final readback. This confirms publication and file retention; no
computer input was used to inspect the Start surface itself.

# One-host tile migration validation, 2026-10-14

## Delivered change

DesktopStub owns its primary destination and six Windows secondary destinations
in the existing process/package. Shared source composition, target selection,
transport and tray implementation live under dependencies. Secondary mappings
use [Tiles] and [Tile.<key>] in the main INI, with Content 1 fixed as primary.
Source/provider snapshots are reused across destinations. Only changed, pinned
secondary destinations are sent; successful delivery acknowledges their key.
Secondary images do not overwrite the primary manifest's stable logo paths.

The Tiles menu exposes setup, pause, pin/update, unpin, rename, mapping and source
settings. Blank RSS/Notes accounts remain recoverable setup states.
Game configuration-error activation opens official HoYoLAB sign-in. Ordinary
tile activation routes setup to the existing resident. RSS article activation
validates HTTP(S) and opens the browser without another resident.

RSS composition has three rows on Medium/Wide and five on Large, with summaries
on Large. Adaptive delivery uses escaped group/subgroup XML; Overlay delivery
draws clipped rows inside the bitmap. Snapshot and asset keys include all rows.

## Exact build and validation

Final host SHA256:
ECF7B58845D3D9D1D1EA3A381C5C4FA32CDF6F0F8C593B1DA352CED3F96469C8

Final broker SHA256:
73783C3DE8FBBD2A206F5D20B3A79A6949454AB0DB9B143231DB55FC389B9624

- Product and broker compiled successfully with MSVC.
- ContentRuntimeTests: 10,311 checks passed with inert hardware and fake delivery.
  Includes initializer preservation/idempotence, primary isolation, target-only
  changes, unpinned skipping, repin planning and RSS XML layout/escaping.
- SecondaryTileTests: 36 pure mapping checks passed.
- TileNotificationPayloadTests: 862 checks passed, including v2 target metadata,
  fake per-target updater selection, rejection without side effects and unchanged
  primary/v1 compatibility.
- NoHardware host: three checks passed.
- DesktopStubSourceCheck: 1,764 checks passed.
- Final full copied Windows offline smoke: exit 0. Includes exact existing pixel
  assertions, Adaptive/Overlay/Off field behavior, cache invalidation/restoration,
  CLI validation and 12 concurrent INI writers.
- Fresh Defender custom scans found no threats in the final reviewed host/broker
  and their installed canonical copies. Fixture scans preceded execution.
- Compatibility-script PowerShell parsing and git whitespace checks passed.

Logs are private ignored files under build/review-validation. Offline smoke did
not invoke live package registration, Startup, native hardware or account APIs.
RSS bitmap rows compile but do not yet have their own dedicated raster golden
image test; the host fixture checks adaptive XML rows, and existing general
Overlay pixel tests remain unchanged.

## Target laptop migration

The original DesktopStub was closed through --exit, backed up, replaced, scanned,
configured through one atomic --configure-only batch and relaunched in try/finally.
Rollback restores both previous binaries and INI on unsuccessful installation.

Backup: DesktopStub/build/ReviewBackupOct14OneHost-20261014-194634.
Previous six source INIs: DesktopStub/build/Tiles/BeforeOneHost-20261014-190621.
Main profile now contains seven enabled contents: Wallpaper, SMTC, RSS, Genshin,
HSR, ZZZ and Discord preview; primary cycling is off. RSS uses BBC Technology,
900-second refresh and five items. Discord sending remains off. Account
credentials were not obtained or invented. Existing packaged Startup remains
enabled and the legacy Startup setting remains off.

All six dev.local.aiprojects.tiles.<key> copied-host packages were removed after
checking each exact expected install location. The original dev.local.desktopstub
registration remains. No private source/profile folders were deleted.

Observed after migration: one DesktopStub.exe (PID 33176), no Aip*Tile residents,
successful primary Live Tile update. One five-second CPU sample consumed
93.75 processor milliseconds (~1.9% of one logical processor), with 9.2 MiB
working set. This short sample is not a long-term performance/stability claim.

## Remaining acceptance and findings

- User must pin the six replacement secondary tiles through the Tiles menu and
  confirm each Windows prompt. No unattended pin dialog was opened.
- Actual native pin/update/unpin completion, Start rendering/truncation and
  tile-click setup UI remain unverified on this Windows/Start replacement.
- The async native UI retains WinRT proxies on its owning UI thread; completion
  delegates post plain records. Teardown/cancellation is implemented, compiled,
  but still needs real OS acceptance.
- HoYoLAB account setup remains deferred because the browser bridge cannot
  initialize. Browser sign-in alone does not import credentials.
- Discord requires a client/ClientId for sending. Another application's current
  Rich Presence cannot yet be read by this provider.
- Caps hardware behavior remains untested; enabling hardware without selecting
  CapsBlink as an active source does not start it.
- Existing profiles that already use Content 2–7 should review saved mappings
  after initialization. The initializer preserves their fields rather than
  replacing their source layouts.
- Windows 8/8.1 secondary-tile compatibility is not claimed; the new native
  desktop pin path depends on the documented supported Windows 10/11 APIs.
- This change does not mean every repository-wide historical finding is closed.
  Remaining findings in earlier audit/checkpoint records retain their status.
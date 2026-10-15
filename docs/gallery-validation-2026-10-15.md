# Gallery and combined build validation, 2026-10-15

The combined gallery/appearance host and broker compile and are installed at
the canonical DesktopStub paths. Gallery remains disabled until explicit setup.
One canonical packaged-relaunch resident was observed as PID 40976. Existing
source pins retained their verified transparent v3 artwork and configured
background/branding. No gallery pin, default gallery ledger, user photograph,
new account, package registration or Startup preference was created by this
validation. The installer preserved the INI bytes during replacement and
relaunched the application.

## Passing gates

| Scope | Result |
| --- | --- |
| Pure ownership, persistence and typed gallery model | 263 checks |
| Injected inventory/cache/EXIF policy | 36 checks |
| Native inventory with private generated images | 84 checks |
| Injected coordinator, CAS, retries, reset ordering and idle planning | 56 checks |
| Host/menu/configuration/delivery/setup/lifecycle fixtures | 12,709 checks, zero failures, 19.3 seconds |
| Adaptive renderer/XML/static-overlay host fixtures | 5,630 checks, zero failures |
| No-hardware host | Three checks, zero failures |
| Topic/appearance plans | 614 checks |
| Offscreen artwork | 26 topics, dimensions/alpha/bounds and explicit dark/light plates passed |
| Existing DesktopStub source regression contracts | 1,764 checks |
| Normal product and broker compilation | Passed |
| Full copied-binary render/configuration smoke | Exit zero; historical exact assertions unchanged |

Fresh native executables were scanned before execution. Both final review
binaries and both canonical installed paths passed Defender custom scans.
The offscreen artwork count is 240,200 raw assertions, mostly pixel-read return
checks; it is not that many independent test scenarios. The artwork sheet was
visually inspected. Portable model/injected targets are registered in CMake;
no Linux/WSL or native Windows shell appearance pass is claimed from them.

The host fixture includes 14 explicit native ledger file checks using only its
fresh private directory: confirmation, create-only publication, readback,
stale-writer conflict, reassignment refusal and corrupt-file preservation. Its
gallery transport, directory opening and pin boundaries remain fake or inert.
No real playback, hardware, sign-in, database or network operation was tested.

## Findings corrected during integration

- Native gallery IDs now obey Windows' 64-character limit and separate the
  group and tile with an unambiguous period. Full generated IDs are matched;
  underscore boundaries are not guessed.
- Fresh gallery-only setup formerly enabled default source mappings outside
  Content.Count=1. It now stages a valid baseline, disables only absent source
  mappings and preserves explicit source flags, primary content and accounts.
  Active legacy text/RSS migrates only into absent numbered fields. The complete
  candidate document is validated before history creation and INI publication.
- Delivery acknowledgements and retained assets are keyed by destination and
  queue tag. A reset affects only that gallery target. Ownership and pending
  windows persist before publication, and every slot must succeed before the
  cursor advances. Late resets invalidate earlier slot successes.
- The late-reset fixture originally expected completion after acknowledging
  reset slot g1 last. It now verifies that the window remains pending and
  re-acknowledges g2–g5. Its scheduled-idle/planning assertions remain intact.
- Unchanged source frames no longer call Windows Exists before checking their
  delivered key. Dirty gallery slots share one existence query per destination.
  Idle gallery ticks skip full-ledger planning; stalled/incomplete inventory
  without progress uses a 30-second backoff.
- Gallery Fit now supplies transparency through ContentCanvasBackgroundColor
  at the actual production resize call sites. A wrapper-only fix had missed
  that path. Photo fit and cache keys are independent of desktop Tile/Span or
  fitting-disable preferences.
- A new synthetic pixel oracle incorrectly assumed bicubic scaling would keep
  intermediate RGB 240 bit-perfect; measured output was opaque RGB 241 with
  correct transparent corners. Its input now uses saturated red and still
  requires exact source/center ARGB 0xFFFF0000, alpha-zero corners and successful
  reads. Shared resampling and historical renderer tolerances were untouched.

## Installation evidence

- Host SHA256: F283F805147F72BDBEE2D32CA67EC9158AF34E09A3D46B59EFB295808AF55098
- Broker SHA256: D9719B2C71E67BE1DD4CE668FCD10A8E03C3FC2FCD74C5ECE55F5743A3F59268
- Successful backup: DesktopStub/build/ReviewBackupOct15Gallery-20261015-194152.

The first replacement encountered a sharing violation after the old process
exited. The guarded installer restored the prior host/broker/INI and relaunched
it. The retry bounded sharing-violation waits and refused to copy if a resident
appeared during replacement. It succeeded, verified hashes and unchanged INI
bytes, scanned canonical paths and relaunched the normal profile. This was a
local file-replacement failure, not a claimed application crash fix.

## Remaining acceptance and limits

The real folder picker, confirmation/pinning, Windows queue animation, actual
photo sets, cold-start cost and native-codec shutdown behavior require local
acceptance. No computer input was issued. Windows chooses the displayed queue
item; the app cannot observe a reliable fifth-item transition. Separate durable
pools avoid depending on that observation. Exact byte copies/aliases deduplicate;
perceptually identical images encoded differently are not merged. The scanner
is flat and bounded; published thumbnails and old ownership reservations are
retained. See [setup](gallery-setup-2026-10-15.md),
[inventory limits](gallery-inventory-2026-10-15.md) and
[the current remaining findings](CURRENT_STATUS.md).

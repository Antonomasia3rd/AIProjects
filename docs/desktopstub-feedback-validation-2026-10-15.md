# DesktopStub user-feedback fixes and validation, 2026-10-15

This pass addresses the existing source-tile/logo/media/Notes feedback before
gallery expansion. Prior user pins, INI accounts, Startup preferences and source
mappings were preserved. Normal DesktopStub was gracefully closed, backed up,
updated, scanned and relaunched in a finally-protected installation.

## Delivered source changes

- Removed the redundant Quick Actions pinning-enable UI/gate. Existing pins stay.
- Source/action topic logos migrate automatically on startup using existing-only
  FindAllAsync/UpdateAsync. No new pin or confirmation dialog is created.
- Native Windows glyphs prefer verified Segoe Fluent Icons then MDL2, with a
  fallback when unavailable. Quick Action/status/resource icons are monochrome
  with transparent backgrounds and versioned font-specific asset paths.
- Explicit playback metadata drives SMTC status. Text is top-aligned, AppIcon/
  Status/None and optional repeat/shuffle indicators share typed INI/CLI/tray
  settings. Artwork-only content remains image-only when artwork is present.
- Discord uses a compact title and wrapped lower details/state/setup instruction.
- Sole-Notes entries stack only reported, game-appropriate resource/activity rows,
  with a resource icon. Medium/Wide show up to three rows, Large up to five;
  mixed-source entries preserve their ordinary composition and chosen details.
- The correct original managed Real-Time Notes login was recovered. Sign-in
  opens game-specific official pages, discovers UID/server/nickname choices,
  verifies selection membership before saving, and retains manual fallback.
- Fresh private sessions are default; remembered browser profiles are per-game.
  Cleanup stops/hides the page, waits for the exact blank-page navigation, then
  clears local data. Normal Close defers; explicit warned cancellation is offered
  after 30 seconds. This never invokes HoYoLAB Logout or clears imported INI
  credentials. Forced exit/runtime failure/power loss can interrupt cleanup.
- Lookup workers are owned, cooperatively canceled and joined before global
  teardown. No detached worker outlives app globals. One request's HTTP budget
  is 15 seconds; ordinary window closure does not wait for active requests.
- New optional Server settings preserve discovered regions; manual/CLI UID
  replacement clears stale regions unless explicitly supplied in the same batch.
- Cache keys include visible row/icon/status information; Off avoids hidden
  metadata churn. Provider loading/timeouts preserve the selected game identity.

## Tests and exact scope

Fresh Defender custom scans preceded every newly built fixture execution.

| Check | Result |
| --- | --- |
| Real inert host/menu/configuration fixture | 10,771 checks, 0 failures |
| Adaptive bitmap/native-XML host fixture | 5,636 checks, 0 failures |
| No-hardware host | 3 checks, 0 failures |
| Shared Notes parser/discovery/cleanup/worker fixtures | 120 checks, 0 failures |
| SMTC injected provider/lifetime fixture | 171 checks, 0 failures |
| Pure media layout/settings fixture | 41 checks passed |
| Pure Notes rows | 16 checks passed |
| Pure topic/migration model, including observed alias | 547 checks passed |
| Windows offscreen topic artwork | 25 topics, all dimensions/glyph bounds passed |
| Discord shared service fixture | 141 checks, 40,000 snapshot and 80,000 relay reads |
| DesktopStub source guardrails | 1,764 checks passed |
| Product and broker compilation | Passed |
| Full copied offline smoke | Exit 0; exact historical assertions unchanged |

The native artwork fixture's raw 230,806 assertion count includes per-pixel API
return checks; it is not 230,806 independent scenarios. All provider/account/
hardware input in these fixtures is fake or inert; no authenticated API request,
playback change, Startup mutation or live keyboard operation was tested.

Initial host run caught and corrected an artwork-only regression. The old Discord
headline expectation was updated to the requested compact headline while retaining
assertions for both details and state. Existing exact pixel comparisons were not
relaxed. Actual source preview PNGs were rendered and visually inspected under
build/review-validation/ux-source-previews-oct15 (synthetic data only).

## Native installation and metadata evidence

Final installed host SHA256:
53D1D3FA42BD1947E1EA6EF5EFF1C0AF01C6362797C7A97D4CDB2FC26E48A09E

Final installed broker SHA256:
2C39775CA3295E7C425669D76BEA147267D2696B7F5B72CFC3912F672E3890C0

Backup: DesktopStub/build/ReviewBackupOct15UxAlias-20261015-163003.
One canonical DesktopStub resident was observed after relaunch (PID 45564), and
all seven existing pins (six sources plus Previous track) were read back with
verified topic-logo metadata. Their earlier logo paths pointed at shared Desktop
assets, confirming the missing upgrade path.

The actual Windows runtime aliases deprecated Square30x30Logo with
Square44x44Logo. Setting 30 last overwrote 44 and caused false mismatches.
The supported 44 setter now runs last; comparison permits that observed alias
only for the deprecated field, requiring every actual tile-size URI to match.
A dedicated aliased fake verifies ordering and does not mask stale real sizes.

The native computer-use bridge initialized, but Start was not separately exposed
as a targetable window. The user Esc-canceled computer use because it took too
long and prevented answering, and said they will check later. No further computer
input should be issued in this pass. Visual shell fallback acceptance remains
pending; metadata readback and offscreen PNGs are not represented as that pass.

## Remaining work

- User's actual Start appearance, player app-icon availability, HoYoLAB login/
  discovery/retention and sleep/wake keyboard behavior still need local acceptance.
- Native layout/DPI/RTL and historical opaque four-pixel variation remain
  distinct original audit items; no unexplained rendering tolerance was weakened.
- Existing native-queue eligibility remains conservative for selected CapsBlink
  status with hardware enabled, although hardware itself no longer depends on
  visible text. This can be relaxed in a focused later scheduling change.
- The account runtime preserves the donor's v2 cookie protocol. Legacy cookie
  discovery/import aliases need a separate protocol-compatibility review rather
  than assuming a successful role lookup proves daily-note compatibility.
- Gallery pools were approved by the user; their pure model is being developed
  separately and is not part of this installed UX build.
- Low-priority bot/Minecraft changes remain isolated from this commit/publication
  until their source/runtime-data exclusions and offline tests are reviewed.
# Renderer determinism investigation

Updated 2026-09-11. Bounded investigation completed; the original discrepancy's cause remains unproven. Pixel comparisons remain exact and unchanged.

## Trigger and permitted scope

The latest isolated DesktopStub smoke run reported four changed pixels when only the unsupported medium-tile secondary text changed. Earlier runs passed this exact comparison. Failed fixture: `C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-35109f3d46be4caaa8565bb09d074955`; log: `build/integrated-sources-offline-smoke.log`.

First compare decoded RGBA values and coordinates across the saved medium-tile images and cache variants. Trace the actual renderer inputs/settings before deciding whether the renderer or the assertion is wrong. Do not weaken the assertion without evidence.

Experiments may use only isolated renderer harnesses or the existing render-only/offline entry points. No normal DesktopStub launch, package operations or Windows settings changes. RepoTools changes require coordination with the root agent.

## Initial pixel evidence

- Baseline cache `8DD90FD148050CDF` versus secondary variant `4CE151DAA1DAF29F`: exactly `(20,93)`, `(30,93)`, `(40,93)`, `(50,93)` differ. RGBA changes from `(174,212,242,255)` to `(156,203,239,255)`. These are the same raster location in each unchanged `A` of the primary caption, inside its region, not secondary-text glyphs.
- Each image equals its own `.scale-100.png` variant pixel-for-pixel. Both PNGs report the same decoded DPI, 95.986595. The difference is real decoded color data, not just PNG encoding/metadata.
- A new isolated probe invokes the exact shared renderer with `AAAA`, alternating `IIII`/`MMMM`, and badge `7`. Twelve fresh probe processes, sixteen renders each (192 renders), all produced the same full-pixel hash and the variant color at `(20,93)`; no within-process or between-process variation reproduced in this minimal path. Each reported GDI+ text contrast 4 and 96 DPI.
- Probe outputs: `DesktopStub/build/render-determinism-probes.log`, `DesktopStub/build/render-determinism/run-*/probe-0.png`; exact saved pixel differences: `DesktopStub/build/render-determinism-pixel-diff.json` (that JSON lists ARGB order).

## Production and state probes

- Twelve new isolated production `--render-only` processes with cache disabled, alternating ignored secondary IIII/MMMM, produced identical PNGs with SHA256 `F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED` (the original darker variant). Artifacts: `DesktopStub/build/render-determinism/host-7da948f7c5964281b406ba3c3bad4ec6`.
- A cache-enabled isolated production sequence repeated no-text → all-fields baseline → primary variant → secondary variant four times (16 processes). Every baseline and secondary output was identical, including the initial cache-miss baseline. Artifacts: `DesktopStub/build/render-determinism/host-979392ef08dd4c17872dca4a3fdb092d`. This retained the failed fixture's INI, wallpaper and manifest, with only the intended text fields changed.
- Explicit floating-point rounding modes (nearest/down/up/truncate) did not change the minimal output. Bitmap DPI values 96, 95.986595, 96.012001 and 120 also did not change it. These probes do not establish the cause of the original discrepancy.
- Rendering-hint changes produced 201–326 changed pixels; text-contrast values 0–12 changed 220 antialiased pixels. Neither matched the original four-point variation, so changing hint/contrast settings is not justified by the evidence.
- A 441-render warmup covering all tested tile sizes, scales and text combinations was followed by sixteen exact medium probes. All medium outputs remained identical. The production generation order also renders Medium before the other text-bearing tile sizes; StoreLogo before it contains no simulated text.

## Changes and conclusion

- Added a bounded sixteen-render exact equality regression to the native renderer suite: changing only the unsupported medium secondary field must preserve every pixel. The diagnostic modes can report full decoded-pixel hashes, the affected glyph point, text contrast, bitmap DPI and floating-point control. These diagnostics create only local renderer artifacts.
- Updated the smoke error wording to report an unexpected pixel change without claiming it necessarily rendered the unsupported field. The zero-difference assertion remains intact; no tolerance, ignored pixels or retry suppression was introduced.
- The evidence localizes the original difference to the unchanged primary glyphs and rules out PNG metadata alone. Controlled same-process rendering, fresh production processes, cache-enabled sequencing, DPI/rounding changes and font-cache warmup did not reproduce it. This does **not** establish that the renderer is fixed, nor that the test is too strict.
- Root owns the planned full offline smoke rerun after the integrated binaries stabilize. Preserve the original failed fixture for comparison. A repeated failure should retain the same exact pixel diagnostics and be investigated before changing the assertion.

Final native renderer suite passed **18,826 checks, zero failures**. Its 600-render resource stress still measured GDI handles **3 → 3** and USER handles **4 → 4**. Log: `DesktopStub/build/render-determinism-final-tests.log`. `git diff --check` passed. Production renderer behavior is unchanged by this investigation.

Last pre-test quota checkpoint: five-hour 69%, weekly 47%; zero reset credits. No reset, Windows settings, package operation or normal app launch was performed by this investigation.

## Recurrence after source consolidation, 2026-10-11

A fresh normal DesktopStub v27 review host and broker compiled from the current
consolidated source both scanned clean. Isolated copied-binary offline smoke
passed the passive CLI/manifest/static-image steps, then reproduced the exact
four-pixel medium discrepancy on the secondary IIII-to-MMMM step. The current
comparison still requires zero changed pixels for this unsupported field.

Preserved fixture:
`C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-36b6e37b3d9844c3b08d4d59a12a4a84`.
Evidence log: `build/review-validation/desktop-full-offline-smoke-final-oct11.log`.
Scanned review inputs: `DesktopStub/build/DesktopStubReviewOct11.exe` and
`DesktopStub/build/DesktopStubReviewOct11LiveTileBroker.exe`. The earlier Microsoft
submission comparison file was not overwritten. No package, Startup, source
account, media or hardware integration was enabled. The smoke runner ended and
its Job Object contained the children.

This is a reproduced unresolved defect, not a passing rerun or evidence that the
assertion should be relaxed. Next work compares exact saved assets and effective
configuration/cache/scale inputs, then the production drawing path. Verified
source consolidation is checkpointed separately so this investigation cannot
lose it if quota is exhausted.

Read-only decoded-pixel comparison confirms the same four coordinates as the
original failure: `(20,93)`, `(30,93)`, `(40,93)`, `(50,93)`. RGBA changes again
from `(174,212,242,255)` to `(156,203,239,255)`. The secondary variant's medium
PNG has the exact historical darker-state hash
`F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED`;
the baseline is
`EC8F0259326C166E7239FFE90C18985B6A7F93FDAE6D8D1F344992F2958526D8`.
Inventory: `build/review-validation/renderer-recurrence-pixels-oct11.json`.

Baseline/primary/secondary cache keys are `7570DB1DA623618D`,
`C106AABF951C756D`, and `C54A79CA6CEFAF4D`. Nontext assets are byte-identical
across those entries; each base PNG equals its own scale-100 PNG. Wallpaper and
manifest were unchanged before the three text runs. Capture copies each decoded
pixel array before disposing the source image, excluding later-file overwrite of
the captured baseline. Both secondary values select mask 7; the medium renderer
draws only badge and primary text with the same regions/fonts.

Independent source/test reviews found no causal configuration, capture or cache
copying difference. A bounded copied-production probe is next: observe existing
medium draw state (font metrics, graphics state and complete floating-point state)
without extra drawing/warmup, then one synthetic four-child cache-disabled
sequence. Tracked renderer behavior remains unchanged pending evidence.

The approved four-child copied-production probe completed after a clean scan in
5.94 seconds. Both baseline and secondary reproduced the darker PNG hash recorded
above, with zero decoded pixel changes. All 12 medium state records matched:
full floating-point control `0008001f`, MXCSR `00001f80`, body font 15 pixels,
style 0, UnitPixel, height bits `419f9c00`, 96 DPI, hint 3, contrast 4,
compositing 0, quality 0, offset 0, smoothing 4, Display page units, scale 1 and
identity transform. No extra warmup draw/GetHDC/pixel reads were added to drawing.
Evidence: `build/review-validation/renderer-probe-oct11/outcome.md` and
`sequence-results.json`. The instrumented/cache-disabled path changes allocation
and timing and captured no lighter failing state; it does not establish a cause.

`--desktopstub-phase content` now runs the later offline composition/save tests
independently, requiring an explicit DesktopStub-only selection and forbidding
package integration. It passed changed-text cache checks, hidden-text background
restoration on Medium/Wide/Large, invalid wallpaper rejection and 12 concurrent
INI writers. The default `all` phase retains every exact glyph comparison and
the reproduced failure. The passing selected phase is not a passing full smoke.
Windows CI runs this selected phase before the full smoke so an early glyph
failure does not prevent the unrelated composition/save checks from executing.

One subsequent copied-production probe used the full default/all smoke prehistory
and cache-enabled path. The copied runner's only diagnostic change forwards
successful child output; assertions, entry-point guards and Job Object behavior
remain intact. A phase-default control failure launched no children, and an
incorrect diagnostic CLI version stopped after one version child with no renders.
The canonical phase default was corrected and checked with missing-binary guards;
the diagnostic-only build then used the product's exact v27 numeric defines and
generated-header search, with CLI/PE version equality verified before rendering.
Neither aborted control is a rendering result.

The intended exact-version full batch passed all unchanged assertions, including
medium unsupported-secondary equality, expected Wide/Large secondary differences
of 345/866 pixels, badge, line-limit and composition checks. All 90 observed
medium state records retained the same floating-point/graphics/font invariants.
The successful runner cleaned its fixture, so no PNG/color/hash result is claimed
for that full batch. Instrumentation changes allocation/timing and no lighter
failing raster was captured. The normal-product failure remains unresolved.

Scanned diagnostic renderer:
`B11559FA44104988BA940F03947B2D3D00294A9DFA4F27AF217186241CAD7A3C`.
Copied diagnostic runner:
`61E8BD9C0A68A494970A5B31223F05B8FB5EECF454C64F8BC7323B4EB624EB48`.
Final report: `build/review-validation/renderer-probe-oct11-full/outcome.md`;
captured log: `renderer-probe-oct11-full-exact-version-smoke.log`.
All owned processes ended. No renderer behavior, tolerance or protection change
is supported by these results, and none was made.

The October 13 current normal build passed the full exact offline smoke once.
A separate [original-binary hardware-breakpoint observation](renderer-debug-observation-2026-10-13.md)
captured native font/brush/rectangle/FP arguments on unchanged copies of both
historical executables. Both secondary-text pairs produced identical darker
rasters; no lighter state was captured. The historical failure remains open,
with original pixel comparisons and artifacts preserved.

## Identical recurrence during tile-instance validation, 2026-10-20

The copied normal review product (SHA-256
`B056247F4DFD02B013343A4652914B36B8449507DD75B84BDC505991A543F60B`)
reproduced the unchanged exact Medium unsupported-secondary failure during the
full offline smoke. No tolerance or ignored-pixel rule was changed. The product
was not installed as a result of this failed run.

- Fixture: `C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-11f1131bc8a5439faaf31419457db75d`.
- Log: `build/review-validation/tile-instances-oct20-offline-smoke.log`.
- Baseline cache `FABF25F738382C94` is the exact historical LIGHT PNG,
  SHA-256 `EC8F0259326C166E7239FFE90C18985B6A7F93FDAE6D8D1F344992F2958526D8`.
- Secondary cache `A1A0355174B4DF54` is the exact historical DARK PNG,
  SHA-256 `F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED`.
- Exactly `(20,93)`, `(30,93)`, `(40,93)`, `(50,93)` differ, again from
  opaque ARGB `FFAED4F2` to `FF9CCBEF` in the unchanged `AAAA` caption.
  Base and scale-100 PNGs agree within each cache entry. Decoded PNG DPI is
  95.9866 in both cases.

Eight bounded copied-product render-only processes, with the generated-asset
cache disabled and secondary text alternating `IIII`/`MMMM`, all generated the
exact historical DARK PNG. These passing probes do not repair or invalidate
the full-smoke failure. Their private images and process logs remain under
`build/review-validation/opaque-product-repro-oct20` and
`opaque-pixel-product-oct20-*`.

The existing isolated renderer's DPI and floating-point rounding probes again
produced only DARK. A private clone then varied all 25 combinations of the five
supported pixel-offset modes and five compositing-quality modes. None produced
the historical LIGHT state: half/high-quality offsets changed 713 pixels;
gamma/high-quality compositing changed at least 519 pixels. Ordinary state was
Display page units, scale 1, text contrast 4 and MXCSR `00001F80`. Contrast
changes affect 220 antialiased pixels and likewise do not match the four-point
variation. Probe sources, scanned executable and results remain in
`build/review-validation/PixelStateProbe.*`, `opaque-state-probe-oct20-*`,
`opaque-dpi-oct20.log`, `opaque-rounding-oct20.log`, and
`opaque-contrast-oct20.log`.

The matching complete historical PNG pair establishes recurrence of the same
pre-existing defect. It does not establish its cause as an operating-system
bug, prove a font-cache hypothesis, or support changing the font, hinting,
geometry or exact comparison. Production renderer behavior remains unchanged;
the missing evidence remains a LIGHT draw captured with its native arguments.

## Identical recurrence during final feature validation, 2026-10-23

The strict copied-product offline smoke again produced the exact historical
LIGHT/DARK PNG pair and failed its unchanged four-pixel comparison. A bounded
private probe swept seven geometry/font/transform inputs by plus/minus 128
float ULPs, tested explicit page units/DPI, and primed native GDI fonts in 21
fresh processes. It generated no LIGHT state and supports no production fix.
The failing legacy profile does not enter the new typed source rendering
branches. See [the preserved recurrence and probe evidence](render-determinism-recurrence-2026-10-23.md).
The historical defect and failed gate remain open; unrelated passing feature
gates must not be described as fixing or passing this comparison.

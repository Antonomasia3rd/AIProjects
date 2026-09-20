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

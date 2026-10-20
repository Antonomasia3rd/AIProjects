# Exact preset renderer recurrence, 2026-10-20

The normal review product's copied-binary full offline smoke reproduced the
existing four-pixel opaque Medium preset discrepancy. The failure remains
unresolved. No production renderer change, tolerance change, ignored pixels or
retry suppression was introduced by this investigation.

## Preserved failing evidence

- Product SHA-256:
  `B056247F4DFD02B013343A4652914B36B8449507DD75B84BDC505991A543F60B`.
- Failed smoke log: `build/review-validation/tile-instances-oct20-offline-smoke.log`.
- Retained synthetic fixture:
  `C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-11f1131bc8a5439faaf31419457db75d`.
- All-fields baseline: `AssetCache/FABF25F738382C94/Assets/MediumTile.png`.
- Secondary variant: `AssetCache/A1A0355174B4DF54/Assets/MediumTile.png`.

| Witness | PNG SHA-256 | ARGB at the four historical positions |
| --- | --- | --- |
| LIGHT baseline | `EC8F0259326C166E7239FFE90C18985B6A7F93FDAE6D8D1F344992F2958526D8` | `FFAED4F2` |
| DARK secondary variant | `F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED` | `FF9CCBEF` |

The complete PNG hashes exactly match the historical LIGHT/DARK pair, rather
than merely having a similar small difference. Decoded comparison finds exactly
`(20,93)`, `(30,93)`, `(40,93)`, `(50,93)`: the same location within each
unchanged `A` of the primary caption. The unsupported secondary text is not
drawn by this template. Each base PNG agrees with its own scale-100 variant;
both witnesses report decoded DPI 95.9866.

## Bounded follow-up observations

All executable probes were scanned before execution and used synthetic local
images/configuration. No existing user process was attached, no resident was
started, and no network, account, physical hardware, package or Startup
integration was enabled.

- Eight fresh copied-product render-only processes, cache disabled, alternated
  `IIII` and `MMMM`. All eight produced the exact historical DARK PNG. Private
  evidence is under `build/review-validation/opaque-product-repro-oct20` and
  `opaque-pixel-product-oct20-*`.
- The existing isolated renderer's four rounding modes and bitmap DPI values
  96, 95.986595, 96.012001 and 120 again produced DARK with no differing pixels.
- Text contrasts 0 through 12 alter 220 antialiased pixels, not the saved four.
- A private clone of the exact renderer probe tested 25 combinations of the
  five supported pixel-offset modes and five compositing-quality modes. None
  produced LIGHT. Half/high-quality offsets alter 713 pixels;
  gamma/high-quality compositing alters at least 519. Ordinary state was
  Display page units, page scale 1, text contrast 4 and MXCSR `00001F80`.

State-probe source/executable are `build/review-validation/PixelStateProbe.*`.
Logs are `opaque-state-probe-oct20-*`, `opaque-contrast-oct20.log`,
`opaque-dpi-oct20.log`, and `opaque-rounding-oct20.log` in the same validation
directory. These diagnostic variations did not modify tracked renderer source.

## Interpretation and validation boundary

The evidence establishes recurrence of the identical pre-existing defect. It
does not prove an OS or font-cache cause, explain the lighter glyph coverage,
or establish a fix from later DARK/DARK passing observations. The canonical
native renderer and its exact assertions remain unchanged. A fresh full smoke
may satisfy the current validation gates while this historical limitation
remains explicitly open; it must not be reported as repaired.

Prior investigations already captured native font/brush/rectangle and complete
floating-point arguments on original executable copies, but only for DARK.
The missing causal evidence remains a LIGHT native draw captured with the same
level of observation. See [the renderer ledger](audit-render-determinism.md),
[original-binary native observations](renderer-debug-observation-2026-10-13.md),
and [retained witness review](renderer-witness-review-2026-10-13.md).

This bounded investigation is complete. Failed artifacts are preserved; no
additional probe, renderer rewrite or assertion change is scheduled by it.

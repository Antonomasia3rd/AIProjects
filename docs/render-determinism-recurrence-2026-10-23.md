# Exact legacy preset renderer recurrence, 2026-10-23

The copied normal product's full offline smoke again reproduced the historical
four-pixel opaque Medium preset discrepancy. The test remains failed and the
defect remains open. No production renderer, cache behavior, pixel tolerance,
ignored-pixel rule or retry rule was changed during this investigation.

## Preserved evidence

- Product SHA-256:
  `0B6DEA386BED46266F12B10A99DCD4AD2A67E86A34974429667F30451C8A741B`.
- Log: `build/review-validation/resume-oct23-preset-fixed-offline-smoke.log`.
- Synthetic fixture:
  `C:/Users/Amiya/AppData/Local/Temp/DesktopStubSmoke-75b5b46729ec48d38f07ba4d91ce5ca0`.
- Baseline: `AssetCache/05124A997F10535D/Assets/MediumTile.png`.
- Secondary variant: `AssetCache/7E49108D8C6E4C1D/Assets/MediumTile.png`.

The baseline's complete PNG SHA-256 is
`EC8F0259326C166E7239FFE90C18985B6A7F93FDAE6D8D1F344992F2958526D8`;
the secondary variant's is
`F5A76FCD16428B4AEC2819C07BF5A49FB221653EDE9C109062AA6F3C900DFBED`.
These are the exact historical LIGHT and DARK PNGs. Decoded comparison finds
only `(20,93)`, `(30,93)`, `(40,93)`, `(50,93)`, each changing from opaque ARGB
`FFAED4F2` to `FF9CCBEF` in the unchanged `AAAA` caption. Both files report DPI
95.986595. The Medium mask-seven layout does not draw the secondary field.

The failing profile disables Content and has no typed tile configuration.
The new Prayer, Clock, Calendar and rich RSS rendering branches therefore do
not run in this case. The shared legacy renderer and its layout/specification
were unchanged by the current feature work.

## Bounded private probes

`build/review-validation/PixelPrecisionProbe.cpp` uses the production shared
renderer helpers without resident startup, notifications, packages, accounts
or network access. The new executable was scanned before execution; its final
SHA-256 is `67A4E83E203C6B45D0C17898F7344325706397B858C4F109889B04EBECB597F6`.

- 1,799 renders varied each of seven inputs by plus/minus 128 adjacent
  representable float values: body font em size, caption x/y, graphics x/y
  translation, graphics scale, and the final bounded line height passed to
  `DrawString`. None generated LIGHT or a one-to-four-pixel variation.
- Four additional renders combined Display/Pixel page units with explicitly
  set 96/95.986595 bitmap DPI. DPI was set before constructing Graphics and
  its effective DPI was checked. All four remained byte-for-byte DARK at the
  decoded pixel level. Log: `pixel-precision-frozen-oct23.log`.
- 21 fresh processes primed GDI's Segoe UI font at quality values zero through
  six using `TextOut`, `GetGlyphOutline(GGO_GRAY8_BITMAP)` or
  `GetGlyphOutline(GGO_NATIVE)` before starting GDI+. All subsequent production
  Medium renders were DARK. Logs: `pixel-gdi-prime-oct23-*-*.log`.

These negative observations do not prove a font-cache or operating-system
cause. They do not repair the failed full smoke, and a later passing run must
not be presented as a fix. A LIGHT native draw with its full arguments and
effective font/raster state remains the missing causal evidence. Further
speculative renderer changes were stopped; unrelated feature validation is
reported separately from this unchanged historical limitation.

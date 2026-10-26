# Tile icon font policy and Windows 10 1809 evidence

The generated topic artwork supports `Artwork.IconFont=Auto|MDL2|Fluent|Vector`
per tile, with `Settings.TileIconFont=Auto` as the global fallback. The host
supplies the current validated preference through
`aip::tile_visual::SetFontPreferenceReader(FontPreference (*)())`; there is no
font polling thread or dependency on the tile text renderer's font fallback.

## Selection and fallback

- Auto prefers Segoe MDL2 Assets on Windows builds below 22000, including
  Windows 10 1809/build 17763. It prefers Segoe Fluent Icons on Windows 11.
  The native adapter obtains the actual build with `RtlGetVersion`, not a
  manifest-sensitive version comparison. An unavailable build uses the older
  MDL2 preference and still performs availability checks.
- Explicit MDL2 or Fluent overrides that preference. The requested family and
  each required glyph are checked. If unavailable, the other verified native
  family is tried, then the repository's independent vector geometry.
- Vector never probes fonts. Composite symbols require all their glyphs in one
  selected family. A missing component cannot become a substituted square.
- Actual family identity and `GetGlyphIndicesW` are checked before native
  outlining. Native draw failure still reaches the vector renderer.
- Cache keys include preference, build and topic glyph. Asset paths identify
  the selected rendering font. One resolved font is frozen across all six
  immutable body/corner images, avoiding a mid-generation global edit mixing
  fonts under one path.

The control applies to generated topic and indicator artwork. It does not change
externally supplied album covers, game-resource artwork or application icons.
Changing a preference does not install a font; a missing requested font remains
a fallback case.

## Cached image repair

Previously an existing PNG with the correct dimensions was accepted even if it
contained only transparent pixels or a solid background. The first check of a
font-qualified asset set now verifies actual glyph ink using a bounded locked
pixel scan. Blank body or 30/44 branding images are regenerated. New images
must contain ink before publication. Successfully checked sets keep the existing
cheap file-presence cache path; no repeated image decoding is added to idle
polling. Drawing contexts are released before image validation and encoding.

This is a tested robustness correction, not proof that an empty PNG caused the
user's particular 1809 report.

## What is and is not established for 1809

The user screenshot shows a live Prayer face without a corner logo and a main
Desktop face remaining on its static icon. Static image generation, live corner
branding and primary wallpaper publication are separate contracts. No real
1809 VM pin, package or display acceptance was performed by this change.

Microsoft documents MDL2 as the Windows 10 icon family and E708 as QuietHours,
the crescent used for Prayer. Its guidance also explains why private-use icon
characters must not be put directly in tile text: tile text cannot select the
font. DesktopStub rasterizes these characters into PNGs instead.
[MDL2 guidance and E708 table](https://learn.microsoft.com/en-us/windows/apps/design/iconography/segoe-ui-symbol-font)

The installed 10.0.19041 SDK's `windows.ui.startscreen.idl` declares
`ISecondaryTileVisualElements3.Square44x44Logo` in UniversalApiContract 1.0, so
its existence is not a Windows 11-only assumption. The 30px property is
deprecated; the current setter order keeps 44px last to avoid an observed
modern alias replacing it. The actual 1809 metadata/readback must still be
verified before attributing the reported missing logo to either property.
[44px contract](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytilevisualelements.square44x44logo),
[30px deprecation](https://learn.microsoft.com/en-us/uwp/api/windows.ui.startscreen.secondarytilevisualelements.square30x30logo)

## Bounded validation

`tools/BuildTileIconFontTests.cmd` builds two fixtures with `/W4 /WX /O2`.
The portable gate passed **1,644 checks** over Windows-build/preference/glyph
availability combinations and canonical field validation. The offscreen native
gate passed **161 checks**, including blank transparent and colored PNG repair,
all six body/corner sizes, deterministic Vector output, preference cache changes
and a single captured preference per asset set. Actual installed MDL2, Fluent,
Auto and Vector Prayer generation is checked through all six images. Both executables scanned clean
before execution. Logs are `build/review-validation/icon-font-oct26-*`.

The tests query only ordinary installed font/OS metadata and create private
temporary images. They do not pin, register, update live tiles, access accounts,
exercise device hardware, or alter the user's profile. The independent strict
four-pixel legacy caption witness remains unchanged.

The existing portable asset gate also passed **774 checks** and its offscreen
artwork gate passed **398,520 checks** after clean scans. Their contact sheet
was visually inspected; the Prayer crescent and every other topic remained
visible. These are host-side checks, not a substitute for the user's 1809 VM.

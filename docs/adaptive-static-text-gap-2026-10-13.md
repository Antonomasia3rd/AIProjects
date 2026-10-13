# Adaptive native text versus static text fallback

Status: the separate adaptive bitmap fallback compiles and custom-scans clean.
Coordinated execution passes 539 portable-layout and 5,522 renderer/host checks,
plus copied normal-binary content and full smoke. The original mismatch below explains the
change. The exact preset opaque renderer comparison remains unchanged and still
tracks its independent four-pixel discrepancy.

## Precise trigger

Use the Windows 10 target with `Settings.LiveTileTemplateStyle=Adaptive`, composed
`Content.TextMode=Auto`, an image/wallpaper background, and primary, secondary
and badge fields. Enabling Live Tile updates chooses native adaptive XML.
Disabling Live Tile updates chooses bitmap fallback but retains the same INI
template selection.

```ini
[Settings]
AppxManifestTarget=Windows10
LiveTileTemplateStyle=Adaptive
ExperimentalLiveTileUpdate=Registration

[Assets]
MediumTile=1
WideTile=1
LargeTile=1

[Content]
Enabled=1
Count=1
TextMode=Auto

[Content.1]
Background=Image
ImagePath=wallpaper.bmp
TextSources=CustomText
Text=Primary
SecondaryText=Secondary
BadgeText=7
```

This is a local-file example, not instructions to start a normal process during
automated validation. `Registration` is the disabled/native-Live-Tile-off mode.

## Behavior before this fix

`ga_live_tile.inc::AppendLiveTileImageBinding` retains the background image and,
when all three fields exist, emits primary and secondary text in a subgroup of
weight 3 and a right-aligned badge in a subgroup of weight 1. All three fields
can therefore appear on a native medium adaptive tile. The primary uses native
caption style, the secondary captionSubtle with wrapping, and the badge title.

`ga_generation.inc::ShouldBakeTileTextIntoAssets` correctly selects bitmap
fallback after native Live Tile delivery is disabled. However,
`ga_image.inc::ApplyTileTextOverlay` always dispatches to the Windows 8/8.1
preset simulation in `tile_text_render.inc`, independently of the selected
`LiveTileTemplateStyle`.

For a medium tile with all three fields, preset mask 7 draws only badge and
primary text. It fills the entire medium background with the simulated manifest
color before drawing. Consequently, disabling native delivery can both hide
the previously visible secondary field and replace the selected wallpaper/image
with a solid plate. Wide and Large also use preset geometry/font roles rather
than the selected adaptive grouping. `Background=Transparent` suppresses the
plate but does not solve the field/layout mismatch.

This is distinct from the documented behavior of an explicitly selected
`Windows81Preset`: the medium preset intentionally has only two slots and some
presets intentionally replace the image. Those behaviors should remain intact.

## Implemented compatibility policy

`ShouldUseAdaptiveStaticTextFallback()` selects a separate adaptive fallback
only for the Windows 10 target and Adaptive template, for both automatic bitmap
fallback and explicit Overlay delivery. It also
applies to the older standalone TileText fields, which have automatic delivery
when no composed snapshot is active. The existing baking decision still keeps
automatic text native when Live Tile delivery is enabled. Overlay means bitmap
delivery; it does not select a different template. Explicit `Windows81Preset`,
Windows 8/8.1 targets, and their
existing preset geometry remain unchanged.

The new portable policy in `adaptive_tile_text_layout.h` retains every selected
field, including medium secondary text alongside primary and badge. Caption
and secondary occupy the left three quarters of available column space; badge
occupies the right quarter with an eight-pixel gap. Caption uses a 12-pixel
regular font, secondary uses the same font with wrapping and 60% white opacity,
and badge uses a 24-pixel semilight font aligned right. Those styles and the
column gap follow Microsoft's adaptive guidance. The renderer uses measured
GDI+ line heights, keeps the selected background intact, and adds no text plate
or darkening because the current native XML explicitly requests overlay zero.

Outer text padding is an explicit eight-pixel product approximation. The guide
documents eight-pixel image edges and column gaps but does not provide an
invariant pixel-level text-padding, branding space, glyph raster or line-height
specification across Windows versions and themes. Font fallback, branding,
RTL behavior and exact native shell equivalence require actual OS acceptance.
This fix provides consistent fields/styles/background retention, not a claim
of pixel-exact shell reproduction.

The selected fallback policy is part of cache identity. New independent
`AdaptiveTileTextLayoutTests.cpp` and `AdaptiveTileTextRenderTests.cpp` cover all
field-presence masks, Medium/Wide/Large sizes, image and transparent backgrounds,
seven scales, saved PNG pixel/alpha equality, field visibility, retained border
pixels, Off mode, native XML grouping and the template/target predicate. Every
matrix case also compares Auto fallback and Overlay with exact pixel equality;
forced Overlay XML must contain only an image, with no duplicate native text.
The pure
layout also rejects tiny, huge and nonfinite font metrics before arithmetic can
overflow a line count. The inert host fixture has a 45-second internal deadline.
Compile-only recipe `tools/BuildAdaptiveTileTextTests.cmd Layout` builds the
portable fixture; `Render` builds the host fixture using freshly compiled inert
content dependency objects. Scan each new executable before bounded execution.
An optional render-fixture argument selects a folder for sample PNGs.
The full opaque smoke uses an explicit Windows81Preset setting and must keep
its exact pixel equality checks; neither this mismatch nor a new adaptive path
justifies relaxing those checks. Actual Windows 10 visual acceptance remains
necessary because Microsoft's adaptive hints do not specify pixel-exact shell
rendering across builds/themes.

Related documented structure:
[Microsoft adaptive tiles](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles),
[tile content schema](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/tile-schema).

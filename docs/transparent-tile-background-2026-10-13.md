# Transparent tile content

`Background=Transparent` adds no wallpaper, custom image or solid color behind
the content. The tray calls it **Nothing / transparent** under **Tile contents >
Content list > an entry > Background**. It uses the same shared catalog and
validation as direct INI edits and CLI `--set Content.1.Background=Transparent`.
The existing `Background=None` remains **Solid color**, preserving old profiles.

```ini
[Content]
Enabled=1
Count=1
TextMode=Auto

[Content.1]
Background=Transparent
TextSources=CustomText
Text=Title
SecondaryText=Description
```

Keep `Settings.ManifestBackgroundColor=transparent` (the default) to let Windows
choose the package's tile plate. An explicitly configured solid manifest color
still belongs to the package, even when a content entry has no app background.
Changing one content background does not silently edit that global setting.

## How delivery differs

- **Windows 10, Adaptive, Auto:** native text bindings omit the background image
  entirely when they have text. An empty/off entry uses an alpha-zero image so
  there is still a valid image binding. Nothing is submitted to a remote service.
- **TextMode=Overlay:** text is rendered into a transparent PNG, and native text
  is omitted to avoid duplication. Adaptive image overlay remains zero.
- **Static tile / Live Tile disabled:** bitmap text retains transparent pixels;
  medium fills, wide text bands and the large-tile darkener are skipped.
- **Explicit Windows 8.1 preset:** the chosen template is preserved. Templates
  that require images receive an alpha image. Their built-in plate/decoration is
  still controlled by Windows. DesktopStub does not switch silently to Adaptive.
- **Windows 8/8.1 compatibility:** text uses the existing bitmap fallback with
  alpha preserved; the older shell still controls its tile background plate.

Normal image and solid backgrounds use their existing opaque rendering behavior.
Disabled asset sizes keep their existing logo/asset policy, and tile branding
remains independently configurable. Selecting no content background does not
change application icons or hide Windows branding.

## Platform limits and evidence

The app can remove its own background, but cannot guarantee that Start becomes
fully see-through. Microsoft documents `transparent` as a supported package
background color, and its adaptive schema makes the background image optional:
[VisualElements](https://learn.microsoft.com/en-us/uwp/schemas/appxpackage/uapmanifestschema/element-uap-visualelements),
[tile content schema](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/tile-schema).
Microsoft's adaptive guide demonstrates native text without images and describes
image overlays separately:
[adaptive tiles](https://learn.microsoft.com/en-us/windows/uwp/launch-resume/create-adaptive-tiles).

Windows 10 20H2 introduced a uniform, partially transparent tile plate that
depends on light/dark themes and the Start accent setting. That final shell
appearance is outside the PNG and notification XML:
[Microsoft's October 2020 announcement](https://blogs.windows.com/windowsexperience/2020/10/20/whats-new-in-the-windows-10-october-2020-update/).
These sources support offering no app background; they do not establish that
every Windows build, theme or third-party Start replacement shows the same
degree of transparency. Actual Windows 10 Start appearance requires user
acceptance on that installation.

## Regression fixture

`DesktopStub/tools/TransparentBackgroundTests.cpp` compiles the real host but
never calls its entry point. It inspects INI/catalog/tray consistency, the
generated one-pixel PNG, the old solid BMP, real GDI+ text at 100%/200% scales,
saved/reloaded alpha, snapshot/cache intent, native XML, explicit preset
preservation, and older-target fallback. It uses no live providers, package
operations, Startup changes or notification update API. A 30-second internal
deadline complements the external bounded runner.

First compile `DiscordService`, `DiscordEditor` and `AsusService` with
`tools/BuildContentIntegrationTests.cmd`, then compile the fixture with
`tools/BuildTransparentBackgroundTests.cmd`. Both recipes compile only. Scan the
new executable using `Assert-ReviewScan`, then use `Invoke-ReviewProcess` with
an explicit output folder and a bounded timeout. The fixture saves alpha PNGs
for visual inspection. Existing exact opaque renderer assertions remain intact.
Compilation, scans and execution results must be recorded separately; this
document alone does not claim that validation ran.

Coordinated root validation subsequently compiled the fixture and normal v27
host/broker, custom-scanned all three successfully, and ran the fixture with a
45-second outer deadline. **331 checks passed, zero failures**. The 18 exported
PNG samples are retained locally in
`build/review-validation/transparent-samples-oct13`. This covers alpha pixels
and native XML construction; actual Windows 10 shell appearance remains a user
acceptance step.

The first bounded fixture run exposed an incorrect foreground assumption in the
new test: partial-alpha white GDI+ glyph edges round to RGB `254,254,254` when
converted between straight and premultiplied alpha. Read-only inspection of all
18 saved samples found grayscale channels of 254 or 255, exact-white opaque
cores, alpha-zero corners, and no ink at all in the six hidden-text samples.
The corrected assertion accepts only these grayscale white edges with partial
alpha; opaque cores must still be exactly white. It additionally requires exact
zero alpha at every border pixel and outside the selected text layout regions.
This correction changes only the new transparency fixture, not the production
renderer or the pre-existing exact opaque pixel comparison.

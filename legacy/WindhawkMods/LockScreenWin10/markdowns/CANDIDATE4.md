# Candidate 4: feedback controls and material assessment

## Settings confirmation

The user export in `analysis/logs/candidate3-user-advanced-settings.json` parsed as valid flat JSON and exactly matched candidate 3: 350 entries, 61 targets, 285 styles, numeric `win10Spotlight=1` and `syncDismissalFade=1`, no missing keys or `[object Object]` strings. The later candidate 4 export intentionally differs: 389 entries and 68 targets.

The provided log contains generated Spotlight nodes and no explicit exception/failure messages. `WidgetErrorMessage` is an element name that also exists in the stock tree, not by itself an error report. Logs and screenshots were captured at different times and do not establish complete runtime correctness.

## Spotlight changes

The Windows 10 expanded reference uses Segoe MDL2 Assets U+E006 (heart) and U+EA92 (broken heart), at 16 DIP with an 18-DIP line box and #FFCCCCCC foreground. The helper now appends an equivalent TextBlock in each native icon grid and hides the two native PathIcons. Native button objects, actions and child indices remain intact. Cleanup restores the original visibility values/bindings and removes only the appended glyph. The installed font renders the glyph; extracted proprietary font outlines are not distributed.

The preset changes feedback labels to Segoe UI 15 Normal and descriptions to Segoe UI 12 with #99FFFFFF foreground. At 276-DIP width, the reference row is 53 DIP high: icon at (0,9), label at (24,7), description at (24,29). Like has a 10-DIP top and 4-DIP bottom margin; Dislike has no margin. The two rows occupy 120 DIP together. The native feedback Border uses transparent Normal paint and #33FFFFFF PointerOver/Pressed paint through the existing styler's visual-state support. Hover opacity is supported by the reference's feedback Background value; an exact reference press animation has not been captured.

The generated location/copyright link now has CommonStates with matching #33FFFFFF hover/press fill and a transparent Normal state. This also makes its padded hit area explicit. The location remains dynamic, preserves its measured natural width, and invokes the native title action. Header positions, one/two-line sizing, fades and cleanup are unchanged.

## Media background

Both Windows 10 captures identify `Grid#MediaTransportControls.Background` as a black SolidColorBrush. The screenshots show wallpaper detail through it. The schema-3 property description drops the brush's Opacity, so black in the dump does not mean an opaque panel. The current 0.6 opacity remains provisional; increasing it based on different wallpapers would not prove a closer match. The improved dumper captures brush Opacity explicitly. Media hover/press fills also remain provisional until their states are captured.

## Widget corners and acrylic

Windows 10 has rounded card corners in the supplied screenshots too. The dump exposes `LockApp.AdaptiveCardHost` buttons with an AcrylicBrush and `BackPlateBorder`, `BackPlate`, and `LayerOverAcrylic` template layers. Windows 11 uses a different host: `Grid#WidgetFrameGrid` has an AcrylicBrush, `ContentPresenter#WidgetContent` adds #0DFFFFFF, and `Border#WidgetBorder` is a separate outline. No prior preset rule changed these paints.

These Windows 11 surfaces are styleable. However, the old reference dump serializes AcrylicBrush only as its type and CornerRadius as an empty string, losing the actual parameters. Setting them to arbitrary tints or zero radius would be an approximation. Candidate 4 keeps these values until a schema-4 reference can establish the intended tint, luminosity, fallback, backdrop source and radius. `ContentPresenter` paint/corner capture, missing from 0.4.0, is now included in 0.4.1. Acrylic output depends on its parameters and system fallback state; [Microsoft's material guidance](https://learn.microsoft.com/en-us/windows/apps/design/style/acrylic) describes these mechanisms.

## Widget interaction

The preset has no rule disabling widget input. In the supplied stock Windows 11 tree, widget frame/content surfaces have IsHitTestVisible=1. The false values found under widget content belong to placeholder TextBlocks, where not receiving input is normal. Schema 3 does not give us evaluated IsEnabled for the embedded controls. Those values are added in 0.4.1.

No input workaround is shipped. Property styling cannot by itself repair provider action routing or unlock/activation behavior. Microsoft's [lockscreen documentation](https://support.microsoft.com/en-us/windows/experience/personalization/customize-the-lock-screen-in-windows) describes selecting a widget, signing in and viewing details in Edge; it does not confirm a fix for the particular Focus/Timer/Phone Link behavior reported here. An unstyled reproduction and evaluated input-state capture would distinguish a style issue from host/provider behavior. This investigation does not diagnose a specific Microsoft bug or recommend force-enabling controls across authentication boundaries.

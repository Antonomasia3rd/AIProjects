# Reference measurements and remaining uncertainty

Both supplied dumps contain two complete snapshots with zero reported failures. Windows 10 has 367 elements in each snapshot; Windows 11 has 666. The second snapshot was used for comparison. The dumps identify OS builds 19045 and 26200; the revision numbers in the request are not present in their headers.

The main roots measure 1920 × 1080 logical XAML units, matching the supplied 1920 × 1080 images. That is consistent with 100% display scaling, but schema 3 did not record rasterization scale, so it should still be included in the next capture.

| Reference element | Windows 10 measured rectangle / property | Candidate approach |
|---|---|---|
| Time | x=36, y=578; height=170.25; Segoe UI Light, 128, LineHeight=140 | Same outer position and typography; left/optical alignment |
| Date | x=36, y=729; height≈74.48; Segoe UI SemiLight, 56, LineHeight=64; Margin=0,-20,0,0 | Same outer position and typography |
| First widget row | x=36, y=836; height=146 | Left alignment, preserve native card count/size selection |
| Media panel | x=1560, y=870; width=360, height=112 | Same outer rectangle; 110-DIP thumbnail area and two columns |
| Media buttons | 48 × 48; horizontal centers 1735, 1795, 1855; y center=955 | 48-DIP controls within a centered 180-DIP list, 6-DIP margins |
| Normal Spotlight circle | 32 × 32; fill=#4D171717 | One replacement circle per hotspot |
| Normal Spotlight text | x=84, y=41 for the top-left hotspot; font=Segoe UI 15; text alignment=Left | Preserve 36-DIP icon/text offset plus 12-DIP text inset |
| Information hotspot | x=1548, y=36; width=336; title area starts x=1584 | Same outer inset and width |
| Network font | Segoe MDL2 Assets | Set explicitly instead of Segoe Fluent Icons |

The isolated UWP layout fixture reproduces the time/date origins, widget origin, and media rectangle above. It uses the captured 146-DIP widget payload height and the Windows 11 outer tree shape. It is not a test of LockCanvas itself or a rendered comparison on Secure Desktop.

In the two screenshots showing a leading “3”, counting nearly-white glyph pixels in the clock region puts the first substantial glyph column at x=36 in Windows 10 and x=43 in the existing preset. This supports the seven-pixel observation. It does not by itself prove the cause: schema 3 omitted OpticalMarginAlignment and the screenshots show different clock contents. The candidate uses the documented optical-margin control rather than encoding a seven-pixel negative margin. The new dumper captures that property for confirmation.

Microsoft describes `TrimSideBearings` as aligning the outside character strokes by trimming the font's side spacing. [Microsoft's XAML text explanation](https://blogs.windows.com/windowsdeveloper/2013/11/11/xaml-text-improvements-in-windows-8-1/)

The existing preset set a 30%-alpha background on both `Button#Spot` and its child rectangle. Two such layers produce about 51% combined alpha where they overlap. The reference has one ellipse with `#4D171717`. The candidate removes the doubled paint and creates a separate non-hit-testable circle/glyph at the hotspot root. The original named elements can continue their native animations without hiding that sibling.

The original preset explicitly collapsed `TextBlock#TitleText`. In the Windows 11 dump that element already contains a photo location. Candidate 2 retains that native TextBlock in place and displays a live bound copy inside the expanded panel. Activating the copy invokes the original title button's native action. This replaces candidate 1's reparenting approach; see `CRASH-AND-SPOTLIGHT-FIX.md`.

The title text was already left aligned by the existing preset. Its expanded body is a deeper subtree under `Border#ExpandedContentParent`, whose TextBlocks were still centered. The additional scoped rule reaches that subtree.

Windows 10's clock, widgets and media share the old text/badge container hierarchy. Windows 11 puts widgets and media in a footer beside the separately faded clock panel. Moving them visually with margins does not make them children of that panel. The candidate links their compositor opacity to `TimeAndDatePanel` without a separate easing curve. Whether that is the native swipe fade source on every relevant state still needs the requested interaction trace. XAML and compositor properties are related but not interchangeable readbacks. [Microsoft's XAML/compositor interoperation documentation](https://learn.microsoft.com/en-us/windows/uwp/composition/using-the-visual-layer-with-xaml)

A local property value cannot reliably cancel an active animation. This is why repeatedly setting the original circle's opacity is insufficient evidence of a hover fix. [Microsoft's dependency-property precedence documentation](https://learn.microsoft.com/en-us/windows/apps/develop/platform/xaml/dependency-properties-overview)

The media panel's old dump reports a solid black brush but omits `Brush.Opacity`. Normal-state snapshots also omit the contents of the button visual-state storyboards. Exact media hover/press opacity and the panel's alpha cannot be recovered from those records. Candidate values are identified as provisional in the README and preset constant.

The comparison tool keeps complete snapshots separate, rejects count mismatches, and prefers unoverridden/evaluated properties when schema 4 supplies them. For schema 3 it uses the first property-chain entry and shows a warning; treating the last occurrence as effective would incorrectly replace many values with defaults. Schema 3's add-time sibling indices can also become stale after later insertions. Schema 4 refreshes them from the live tree before producing indexed paths.

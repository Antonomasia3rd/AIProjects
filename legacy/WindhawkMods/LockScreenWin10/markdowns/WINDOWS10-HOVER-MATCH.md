# Candidate 3 / styler 1.0.2 — hovered Spotlight heading

Reference: `LockApp-XamlDump-20260913-152627-pid6728.jsonl`, Windows 10 build 19045. Both snapshots contain 391 elements and report zero failures. Snapshot 2 is the measured reference. It uses schema 3, which is sufficient for these fonts, colors, margins and rectangles, although it does not contain all schema 4 animation details.

| Item | Measured Windows 10 values |
|---|---|
| Location | Segoe UI, Light, 24; WrapWholeWords; LineHeight 0; Margin 0,2,0,0; white |
| Copyright | Segoe UI, Normal, 12; WrapWholeWords; Margin 0,5,0,0; foreground #99FFFFFF |
| Shared location/copyright button | Padding 8,4,8,4; natural width constrained by the 276-DIP content area |
| Separator | Margin 0,25,0,0; white stroke, thickness 1, opacity 0.7 |
| Separator visible length | 255 physical pixels in the supplied 1080p image, from x=1596 through x=1850. Its XAML layout width is 276; the old dump does not expose X1/X2. |
| Two-line location origin | x=1604, y=67 |
| Copyright origin | x=1604, y=136 |
| Separator layout origin | x=1596, y=181 |
| Native feedback control origin | x=1596, y=182 |

The implementation displays live bound copies of location and copyright together, before a generated separator. It hides the old Windows 11 copyright footer without removing or reparenting its native controls, and restores its visibility binding when the helper is removed. Activating the heading still invokes the native title button.

The block starts immediately after the visible “Like what you see?” heading. Feedback spacing follows the measured height of the block; it is no longer a fixed 32-DIP reservation. This accommodates two-line locations and longer copyright strings.

The working ellipse, native fade linkage, helper lifetime handling, and Translation-setter crash guard are retained. This update concerns the location/copyright header. Native feedback icons and other unverified state-specific styling are not claimed to be an exact Windows 10 match.

Update the styler source to 1.0.2 and compile. Candidate 2's settings are byte-for-byte unchanged, so an existing installation can keep them. For a new installation, use `windows-10-lockscreen.advanced-settings.json` in **Advanced → Mod settings**, or the `.wh.preferences.yaml`/`.txt` file in the **Settings tab's YAML editor**. Keep the source inputs in `sources`; the preparation scripts now use that folder by default. New output files remain in this project folder.

The new normal-feedback reference makes a Spotlight cache reset unnecessary for this comparison. The earlier feedback-confirmation dump was retained as supporting data; no source capture or cache was deleted.

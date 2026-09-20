# Windows 10 lock screen appearance — candidate 3 (styler 1.0.2)

This package updates your Windows 11 Lock Screen Styler fork and provides a cleaned preset, a more informative dumper, and an offline comparison tool. The supplied Windows 10 19045 and Windows 11 26200 dumps are the reference. Your original Desktop files were left unchanged.

**Candidate 3:** the hovered Windows 10 dump now provides the exact location/copyright typography and spacing. The location is 24-point Segoe UI Light without an underline; copyright is 12-point at 60% white directly underneath, followed by the measured separator and then feedback. The header height adapts to wrapping. See `WINDOWS10-HOVER-MATCH.md`. Source inputs now live in this project's `sources` folder, and new outputs stay in the project.

For an existing candidate 2 installation, update the source to 1.0.2 and compile; the settings are unchanged. The installation steps below also cover a fresh installation.

**Candidate 2 fixes:** the icon backdrop is now a true ellipse, and the circle and location follow the native Spotlight fade. Native text controls remain in their original parents and order. Helper callbacks use weak state with explicit cleanup. A guard rejects the `Translation` Setter identified in the older crash dump. Read `CRASH-AND-SPOTLIGHT-FIX.md` for the evidence, changes and retest steps. Install the new source **and** settings for this update.

**Settings import correction (September 13):** The first package supplied nested JSON for the raw Advanced settings field. Windhawk converted its objects to `[object Object]` strings and its switches to the string `"true"`, leaving no usable style rules. The exports below fix the settings representation. The installed candidate's mod logic does not need recompiling for this correction.

The preset and source must be used together. The Spotlight and dismissal helpers are additions to this fork; the ordinary Notification Center Styler does not implement them.

**This is a locally checked test candidate, not yet a verified pixel-perfect recreation.** The 1080p outer layout matches the reference coordinates in an isolated UWP layout fixture. Secure-desktop interaction, actual glyph rasterization, other display scales, and the exact Windows 10 media hover/backdrop opacity still need paired captures.

**Install the appearance changes**

1. In Windhawk, edit the source of your existing Lock Screen Styler fork and replace it with `windows-11-lockapp-styler.wh.cpp`. Compile it.
2. **For the Advanced tab → Mod settings field:** replace the entire text with `windows-10-lockscreen.advanced-settings.json`, then click **Save**. This file has numeric switches and flat keys such as `controlStyles[0].target`. Click **Load** afterwards to confirm `win10Spotlight` and `syncDismissalFade` are both the number `1`, and the indexed style keys remain present.
   **For the Settings tab → YAML editor instead:** use `windows-10-lockscreen.wh.preferences.yaml` or its identical `.txt` copy, then click **Save**. This is the nested, indented format shown by that editor. The first target should be `Grid#LockScreenTextContent`, with 61 target entries in total.
3. Use one styler targeting `LockApp.exe` for this test. If your Notification Center Styler also targets LockApp, remove that target there while testing this fork.
4. Lock normally and inspect the result. The changes have not been installed into your active Windhawk configuration by this task.

To restore your previous setup, restore the source and preferences you originally supplied, or disable the modified fork. Keep those originals as your rollback copy.

**What changed**

- The clock and date use left text alignment and `OpticalMarginAlignment=TrimSideBearings`. The screenshots show the same leading “3” seven physical pixels farther right in your current preset. Optical alignment is the proposed fix; a paired screenshot still needs to confirm the visible ink position.
- Spotlight gets one independent 32-DIP ellipse and 16-DIP glyph. Its alpha follows the native backdrop animation between the equivalent of `#4D171717` and `#99171717`. Native icon controls remain in place.
- The existing photo-location TextBlock remains in its native title button. A bound copy is displayed inside the native expanding panel, where it fades with the backdrop and inherits the panel's clipping. Its link invokes the native title button's action. Missing location data is not fabricated.
- The expanded Spotlight body and copyright text are left aligned. The separate Windows 11 title-button hover patch is hidden.
- Widget and media container opacity follows the native `TimeAndDatePanel` compositor opacity. This avoids inventing a separate swipe timing curve. This synchronization needs a real LockApp swipe test.
- Media buttons retain their native Button/RepeatButton objects and live play/pause content, with a replacement square template. Previous/next use the Windows 10 glyphs. The list's extra selection/press backgrounds are transparent.
- Widget width and height decisions remain with LockCanvas instead of forcing a 1316-DIP width and a fixed group height. A widget-text selector that matched nothing was removed. Network glyphs use the reference's Segoe MDL2 Assets font.

The media panel's `mediaBackdropOpacity=0.6`, and the replacement button hover/press fills `#1AFFFFFF`/`#33FFFFFF`, are provisional. The old dumper omitted brush opacity and animation values, so these cannot honestly be called exact Windows 10 values yet. Change the style constant in the Settings tab if you want to compare opacity candidates; the next reference capture can settle it.

The Windows 11 widget contents, provider data and generated battery image remain native. This candidate does not claim that every widget interior or battery asset is identical to Windows 10. The other person's preset was reviewed, but its 109-DIP clock, clock-beside-widgets layout, and broad square/acrylic widget rules do not match your supplied 1080p Windows 10 reference.

**Capture the remaining evidence**

Use `lockapp-xaml-dumper.wh.cpp` and `lockapp-xaml-dumper.capture.advanced-settings.json` in its **Advanced → Mod settings** field. Alternatively, use `lockapp-xaml-dumper.capture.preferences.yaml` in its **Settings → YAML editor**. The profile preserves your 10-second initial minimum, 3-second settle delay, 20-second maximum wait and 5-second verification delay. It adds a 60-second sampled interaction trace and periodic snapshots within an eight-snapshot cap.

Reload the dumper after saving settings, before each capture. Settings are intentionally immutable during an active capture. It writes to LockApp's LocalFolder as before; Windhawk's log gives the actual file path. The clipboard is not involved.

For one Windows 10 reference capture and one Windows 11 capture with this candidate applied:

1. Lock and wait about 25 seconds with the pointer on empty wallpaper.
2. Hover a normal Spotlight hotspot for several seconds, then move away.
3. Hover “Like what you see?” until expanded. Capture a screenshot including the location and circle.
4. Hover and press a media button, including one play/pause change.
5. Perform a slow partial swipe/drag upward, then release and unlock.

Please return both JSONL files, an idle screenshot and an expanded-Spotlight screenshot from each system, and a short recording of the swipe if possible. Include resolution, Windows display scaling, and Accessibility text size. The new records include evaluated clock text, which helps associate screenshots with the right snapshot; the supplied screenshots and dumps were taken at different times.

The dumper now records evaluated text, optical margins, corner radii, brush opacity, transforms, image URI details, rasterization scale, visual-state setters/storyboards/transitions, and refreshed sibling indices. `interaction` records contain changed samples for selected elements. They are sampled at 100 ms, not frame-exact, and private native animation code cannot be reconstructed solely from XAML storyboards. Leave the trace off for ordinary captures if it is not needed.

**Compare dumps without Secure Desktop clipboard access**

The archive includes `dump-explorer.html`, which can load two JSONL files directly. A separate `comparison.html`, saved next to the working sources, already embeds your supplied snapshots. Open either in your browser, search a name such as `Time` or `Spot`, and select one element on each side. It shows property differences, original property chains, visual states, rectangles and copyable selectors. No network request is required. The browser automation tool blocked inspection of local file URLs, including a manually opened tab, so visual browser QA is not claimed.

For another pair of dumps, Python 3.10 or later is sufficient:

```text
python tools/compare_dumps.py win10.jsonl win11.jsonl --output comparison.html --preset windows-10-lockscreen.preset.json
```

The source archive excludes the embedded personal dump data in `comparison.html`. Keep that report separate when sharing only the mod.

See `EVIDENCE.md` and `VALIDATION.md` for the measured values and validation scope. The standalone `.wh.cpp` files need only Windhawk to build. Your installed Windhawk 1.7.3 compiler and Windows SDK 10.0.19041 were sufficient for the additional checks; no Visual Studio workload or administrator session was needed.

The supplied forks derive from GPLv3 code in the Windhawk stylers and UWPSpy. The modified sources remain GPLv3; see `LICENSE` and the preserved source notices.

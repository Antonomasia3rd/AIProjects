# Candidate 2 / styler 1.0.1

Replace the installed styler source with `windows-11-lockapp-styler.wh.cpp` and compile. Then replace **Advanced → Mod settings** with the full `windows-10-lockscreen.advanced-settings.json` and click Save. The Settings tab's YAML editor instead uses `windows-10-lockscreen.wh.preferences.yaml` or its identical `.txt` copy. The settings now have 61 targets; the old synthetic title-column rule was removed.

Use the new source and settings together. Version 1.0.1 no longer moves the native location and prompt TextBlocks into a new column. The native location stays in place, hidden by the preset, and supplies a live binding to a new display inside the native expanded-content panel. The display relays user activation to the native title button. Original native child indices remain unchanged.

The icon backdrop is a real 32×32 `Ellipse`. The previous `CornerRadius{16}` C++ aggregate initialized only the top-left corner; the other three corners stayed zero. This produced the shape in the screenshots.

The ellipse opacity now follows the native text-backdrop compositor opacity from 77/255 at rest to 153/255 when fully expanded. No pointer-enter/exit boolean switches its paint. The location follows the backdrop while also inheriting the native expanded panel's visibility, clipping and opacity. Its additional opacity factor avoids multiplying the same fade twice. The brief-hover/early-exit case therefore has no separate timer or stale pointer state to leave the location visible.

All helper event handlers and the helper timer now capture weak C++ state. Disposal marks the state inactive before unregistering callbacks or restoring properties. Cleanup removes map entries before XAML mutations can re-enter the diagnostics callback. The erroneous text-callback removal from the old `Refresh()` method is gone.

**Crash evidence**

The supplied Public CrashDumps directory contained five LockApp dumps, all dated September 11–12. The newest was `LockApp.exe.41688.dmp`, recorded September 12 at 15:07:57 local time, before candidate 1 was delivered. The Application log likewise had no newer LockApp fault entry when checked on September 13. These files cannot establish the cause or frequency of today's reported failures.

The newest dump was inspected locally with the installed Microsoft debugger and Microsoft symbols. It contains an access violation (`0xC0000005`) in `Windows.UI.Xaml.dll!CDependencyPropertyProxy::GetDP`, reading address `0x8`. The older loaded mod was `local@windows-11-lockapp-styler__690419.dll`.

The crashing property proxy's `m_nPropertyIndex` is 2082. The matching symbols identify that as **`UIElement_Translation`**. The XAML string recovered from the crashing frame includes:

```xml
<Setter Property="Translation" Value="0,20,0" />
```

It was being parsed by `GetStyleFromXamlSetters` through the visual-tree callback. The more specific property evidence identifies an invalid facade-property Setter; merely delaying that parse would not remove the invalid Setter. Version 1.0.1 rejects this form before calling XamlReader, with a diagnostic directing the author to `RenderTransform`/`TranslateTransform`. The current candidate preset contains no Translation setter.

The guard addresses the crash path demonstrated in the old dump. The callback and tree-preservation changes remove risks in the new helper, but no claim is made that today's intermittent failure has been reproduced or conclusively fixed without a current dump.

**Next test**

After applying both new files, hover a Spotlight item normally, then repeat with a brief hover followed by an early pointer exit. Check that all four quarters of each icon backdrop are round, and that the location and icon backdrop fade with the native panel. Repeat a few lock/unlock cycles. If LockApp fails again, send the new timestamped dump or the corresponding new Application fault event; the existing dumps are from the older code.

No changes were made to the dumper's startup-delay implementation.

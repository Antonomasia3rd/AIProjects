# Compatibility with the original mod

**A partial version is available without changing the original Lock Screen Styler code.** Paste `preferences/compatibility/windows-10-lockscreen.preferences-only.advanced-settings.json` into that mod's Advanced → Mod settings field. The YAML counterpart is for the Settings tab's YAML editor. It has 30 targets and 184 flat entries, with no custom helper switches. This profile targets the original fork supplied under `sources/windows-11-lockapp-styler.wh.cpp`; other versions should be checked separately.

The variant is generated as an exact subset of the validated full preset. It covers clock/date layout and typography, widget placement, media size and square button templates, and status styling. It leaves the entire native Spotlight layout intact, including its location and copyright. It does not collapse source elements whose replacement would be missing without the helper.

## Why the complete candidate needs code

The original engine parses selector matches and applies XAML dependency-property setters. It can supply templates, brushes, fonts, geometry and values for an existing VisualStateGroup. It does not execute helper code or provide a general facility for editing an existing visual tree's child collections and creating compositor reference animations.

| Complete candidate behavior | Additional mechanism |
|---|---|
| Circle opacity continuously follows the native expando animation | A Composition ExpressionAnimation refers to the existing background Visual and a visibility gate. `UIElement.Opacity` binding alone does not establish equivalence with all composition animation states. |
| Location and copyright are displayed above feedback while their source bindings survive | Generated controls bind to existing live TextBlocks, reserve measured space as text wraps/changes, invoke the native title action, and restore native properties during teardown. |
| Original Windows 10 feedback font glyphs inside the Win11 PathIcon-based template | The helper adds a TextBlock using the installed MDL2 font while retaining the native button and native template children. A PathIcon Data setter could draw an approximate heart, but cannot turn a PathIcon into a font TextBlock. |
| Widget/media fade matches the clock during dismissal | A compositor expression references the native clock Visual; it is installed and removed with the target's lifetime. |
| Lifetime and crash handling | Weak callbacks, native-state restoration, the worker AddRef fix and the unsafe Translation Setter guard are C++ changes. The compatibility preset contains no Translation Setter, but cannot add the guard to an unmodified engine. |

An alternative all-template design could reproduce some more of this using XAML. It would require separately proving that namescopes, localized text bindings, native template parts, commands and native state transitions still work. A `{Binding ElementName=...}` parsed in a newly constructed resource/template does not automatically acquire the references used by the C++ helper. Simply deleting the two custom settings is therefore not an equivalent complete theme.

This is a limitation of the current implementation and the guarantees we have tested, not a claim that no future preferences-only technique could improve it. The complete 1:1 result has not been demonstrated with the original engine alone.

# Validation record — candidate 4 / styler 1.0.3

This page records the candidate-4 styler and earlier dumper validation. The current continuous dumper 0.5.0 has a separate [validation and behavior record](DUMPER-0.5.0.md); its capture profile has 14 entries and schema 5 output.

Validation ran on Windows 11 25H2 build 26200.6584 using the installed Windhawk 1.7.3 compiler and SDK 10.0.19041 manifest tool. No mod was installed or injected by these tests; no live widget or media actions were invoked.

| Check | Result and scope |
|---|---|
| Real standalone builds | Both x64 mods compile and link with the Windhawk import library. `build/build-inputs.json` records the exact source hashes. |
| UWP XAML settings | 70 setter/state cases for 68 targets pass with zero failures. State-specific values are parsed separately. Private LockApp types are represented by their base Control property types. |
| Selector audit | All 68 full-profile targets match the supplied 666-node stock Windows 11 tree. Schema-3 sibling indices remain provisional until a new capture confirms them. |
| Import format | Full profile: 389 flat entries; capture profile: 13; original-mod compatibility profile: 184. All round-trip through the installed Windhawk UI's actual YAML parser, schema and flattening converter. Helper switches are numeric 1. |
| User's earlier pasted settings | Exactly matched candidate 3: 350 keys, 61 targets and 285 styles. No malformed entries. Candidate 4 intentionally adds rules. |
| Feedback geometry | A fixture with the stock Win11 two-row template and the actual new styles produces the reference 276x53 row, icon (0,9), heading (24,7), description (24,29). |
| Location highlight | Actual helper template passed PointerOver, Normal, Pressed and Normal paint checks; background appears and restores. This is a VisualStateManager test, not pointer automation on Secure Desktop. |
| Feedback glyph lifecycle | 200 attach/restore cycles preserve both native PathIcon identities and indices, hide them even when their opacity changes, render the intended MDL2 character and restore visibility on teardown. |
| Existing helper lifecycle | 200 real UWP cycles preserve native parents/indices, bind live location/copyright, handle empty and wrapped text, gate visibility, invoke a fixture's native title action and restore state. |
| Header geometry | Relative to the popup origin: location (56,31), copyright (56,100), separator (48,145), feedback (48,146), equal to the expanded Windows 10 reference. Single-line text reduces reserved height. |
| Clock and lower layout | Reference fixture positions remain Time (36,578), Date (36,729), widgets (36,836), media (1560,870), media size 360x112. It supplies a 146-DIP widget payload and does not instantiate LockCanvas. |
| Media templates | All three preserve square Normal/PointerOver/Pressed paint and play/pause Content updates. No actual media command is executed. |
| Crash guard | The Translation Setter recovered from the earlier crash dump is rejected before XamlReader. That older fault does not establish the cause of every reported crash. |
| Dumper material/input serializer | The production serializer is exercised with real AcrylicBrush, ContentPresenter and disabled Button objects. Output must preserve tint, brush opacity, luminosity, backdrop source, fallback flag, corner radii and evaluated input flags. See the current `build/dumper-validation.log`. |
| Dump parser | Eight offline tests cover complete snapshots, interrupted tails, corruption, count mismatches, duplicate sessions, property precedence, evaluated values and child indices. |
| Explorer | Regenerated with the 391-node expanded Windows 10 reference and 666-node stock Windows 11 tree. JavaScript syntax is checked locally. Browser visual QA was not performed; prior local-file inspection was rejected by the browser tool. |

The main behavior added in this candidate still requires a live lockscreen check. Parsing a state setter does not prove when a private LockApp control enters that state; fixture geometry does not prove final glyph rasterization on every display scale. The user confirmed the preceding circle/header implementation, which this candidate preserves.

The dumper's new API reads are compiled and exercised in an isolated UWP host on this Windows 11 system. Schema 4 is still awaiting a Windows 10 LockApp capture. The initial delays remain unchanged. Compositor property readback and the interaction trace are not frame-exact measurements of native compositor animations.

The new material serializer test initially used the wrong numeric expectation for AcrylicBackgroundSource.Backdrop (it is 1, while HostBackdrop is 0). The emitted value was correct; the test expectation was corrected against the installed WinRT header.

See the current [README](../README.md) for reproducible commands, [candidate findings](CANDIDATE4.md) for unresolved material/input questions, and [capture instructions](NEXT-CAPTURE.md). The historical candidate-3 README and earlier evidence documents record prior validation and may contain superseded counts or paths.

# Prayer timetable separation and Clock-sized headings

Large Prayer Times tiles now insert one structural blank caption line between
the current/next-prayer status and the timetable. This is an adaptive XML
spacer, not a newline appended to translated prayer/location text. The static
Registration renderer reserves the equivalent measured caption-line height.

The setting is `PrayerTimes.TimetableGapLines`, supported range **0–3**, default
**1**. Zero keeps the compact layout. It affects only a displayed Large
timetable; Medium, Wide, hidden timetables and missing timetable data receive
no spacer. Custom per-field text gaps remain independent.

Legacy/global configuration:

```ini
[PrayerTimes]
TimetableGapLines=1
```

Independent tile configuration:

```ini
[Tile.prayer]
PrayerTimes.TimetableGapLines=1
```

The normal CLI accepts either scope, for example:

```powershell
.\DesktopStub.exe --configure-only --set Tile.prayer.PrayerTimes.TimetableGapLines=1
```

Both source-settings menus expose **Blank lines before timetable (0–3)**; the
independent menu is the same model used by Properties. Invalid values fail
validation before saving. Existing explicit text styles and wrapping choices
remain authoritative.

The spacing travels in the immutable prayer snapshot. Changing it updates the
provider's cached formatted view, prepared-content key and rendered-asset key,
without changing the schedule request/cache identity or fetching the location
again. It adds no timer, observer, account or network operation.

Prayer headings also use the shared Clock contract: Medium **base/15 px**, Wide
**title/24 px**, Large **subheader/34 px**, including setup/error fallback
headings. This replaces the former fixed title/24 px on every size. The shared
status-layout work preserves explicit custom styles; countdown numerals retain
their separate semantic role.

## Validation boundary

The portable Prayer model checks validate defaults, range/canonical values,
display-only cache identity, per-size heading styles, Large-only spacer
metadata, zero-gap opt-out and hidden timetable behavior. The final model
fixture passed **238 checks** after a clean Defender scan.

The real-host Prayer fixture additionally covers global/per-tile validation,
menu exposure, same-minute cached reformatting without HTTP, publication-key
invalidation, final semantic XML and comparative offscreen timetable ink at
100% and 150% scale. Parent combined-host results are recorded in its delivery
checkpoint; the portable model alone does not establish these host checks.

For optional visual QA the host fixture accepts the test-only environment
variable `DESKTOPSTUB_TEST_LAYOUT_WITNESSES` naming an output directory. It
retains `Prayer-Large-gap0-100.png`, `Prayer-Large-gap1-100.png` and
`Prayer-Medium-100.png`, produced by the actual production text renderer. With
no variable it produces no retained witnesses. This is not a product setting.

No user configuration, account, native package, Start pin or resident was
changed for implementation/model validation. Actual native Start display and
custom-theme/DPI acceptance remain separate from offscreen/XML evidence.

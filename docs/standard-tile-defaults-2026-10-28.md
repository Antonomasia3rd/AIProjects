# Shared status-heading defaults — October 28

The native Clock reference is the installed Time.dll 11.2607.6.0 contract
recorded in [the binary evidence](native-calendar-clock-evidence-2026-10-23.md)
and [the parity correction](calendar-clock-parity-2026-10-24.md). Its three
bindings select **base on Medium, title on Wide, subheader on Large**. Their
logical sizes in the existing bitmap style map are 15, 24 and 34 pixels.

The new `standard_tile_layout.h` holds that one size-dependent heading policy.
Clock delegates to it without changing its output. Prayer Times, Battery,
Now Playing and the Discord/general wrapped-status headline use it by default
in both adaptive XML and the bitmap path. Medium base headings use Segoe UI
Semibold in the bitmap renderer, matching Clock; Wide/Large use its Light face.
This fixes Prayer's formerly fixed 24px header, Battery's 24px Medium charge,
and the formerly fixed 12px SMTC/wrapped headings.

## Complete source audit

| Tile types | Default policy |
| --- | --- |
| Clock / world clock | Existing traced 15/24/34 heading and native weekday/branding contract. |
| Prayer Times | Shared 15/24/34 heading, including setup/error fallback; prayer name/location keep their shared secondary role. Timetable separation is a distinct source setting. |
| Battery | Shared 15/24/34 charge heading; Small remains charge-only, right-hand metric remains its distinct numeral role. |
| Now Playing | Shared 15/24/34 song heading, subordinate artist/status, bounded line allocations and optional right indicator column. |
| Discord / general wrapped status | Shared 15/24/34 heading with subordinate wrapped details. |
| Calendar | Retains native date/numeral and event plans; a calendar day stamp is not reinterpreted as a generic status headline. |
| Mail | Retains its native mail preview/queue plan and empty-mailbox icon behavior. |
| RSS; Genshin, HSR, ZZZ, WuWa | Explicit exceptions: RSS-style rich rows, with game icon placement separately configurable. Even a game setup/error fallback keeps a base-sized RSS heading rather than a Large generic subheader. |
| Desktop wallpaper, photo galleries, App Launcher, Quick Actions | Primarily images/action icons, with no implicit status headline. Explicit or historical custom text overlays retain their own settings. |

The standardization concerns the comparable primary status field. It does not
erase a source's semantics, its distinct count/date numerals, native Calendar or
Mail composition, or the RSS/game exception requested by the user.

## Long text and metadata

A larger song headline cannot safely retain the old fixed 2/2/4 caption-line
allocation on every tile. Default Now Playing now uses shared XML/bitmap line
budgets, listed as title / artist / status:

| Size | Ordinary status | Known repeat/shuffle metadata |
| --- | --- | --- |
| Medium | 2 / 1 / 3 | 1 / 1 / 4 |
| Wide | 2 / 1 / 1 | 1 / 1 / 3 |
| Large | 2 / 2 / 4 | 2 / 2 / 4 |

Wrapped Discord-style details use 4 Medium, 2 Wide and 5 Large lines after
the two-line heading. These bounded defaults reserve room for the final field
and the branding footer instead of relying on accidental bitmap clipping.
Full source strings remain in the structured snapshot, semantic XML and
contents view; Windows may truncate the visible part to the allocated lines.
Known metadata selects the metadata budget even when optional icon export is
unavailable, so text allocation does not depend on an asset-write success.
The bitmap indicator stack includes both 8px inter-icon gaps when bounding
three icons above the 26px Registration footer. This repairs a separate Wide
overlap without changing explicit custom typography.

## Settings and compatibility

No INI value is rewritten and no ambiguous old customization is migrated.
`TextPreset=Default` selects the implicit size-aware source layout. Existing
explicit `Custom`/`RssHeadlines` presentations retain their styles, wrapping,
line counts, alignment and stacking. The existing validated CLI/INI/tray/
Properties fields remain the controls; there is no second settings schema.

For example, a user who deliberately wants title-sized text on all sizes can
select Custom and `TextStyle=title`. Returning to Default restores source
defaults without deleting the saved custom fields. Typography fields such as
TextMaxLines remain explicit choices rather than silent factory rewrites.

The immutable content and baked-image generation keys include a scoped
`StatusHeadingClock1` marker for the affected default layouts. This prevents
unchanged provider data from retaining an old cached bitmap/header after an
upgrade. Off, explicit custom, native Calendar/Mail and image-only layouts do
not receive an unrelated status-heading cache marker. No timer, provider read
or additional resident is added.
SMTC also has a `MediaIndicatorGeometry2` cache marker, including custom text
layouts, because the footer-safe icon bound applies independently of typography.

## Validation boundaries

The focused MediaTileLayoutTests fixture covers the Clock size mapping,
Battery XML/bitmap plan, SMTC/wrapped bindings, explicit styles, game fallback,
source exceptions and metadata-without-icons behavior. The new
`TestStatusTileDefaultsHost()` exercises actual publication XML and offscreen
dispatch for five status sources at all three sizes and at 100%/200% scale.
It compares primary ink width to the existing Clock path for identical text,
checks long-title field allocations and exports local `status-*.png` witnesses
under `artifacts/desktopstub-layout-20261028/`.

The first line-budget proposal was corrected after measuring actual fonts.
At 100%, Segoe UI Semibold 15 has a 19.951172px line height, Light 24 has
31.921875px, Light 34 has 45.222656px, and Segoe UI 12 has 15.960938px.
These are substantially taller than their nominal em sizes. The corrected
SMTC maximum allocations, including both 3px text gaps, use about 109.746px
on ordinary Medium, 105.756px on metadata Medium and 101.766px on Wide,
within the production 114px body. Tests use NameAndLogo, its actual 26px
Registration inset, and 100%/200% scale. They compare actual final status,
Repeat and Shuffle ink against separately blanked fields, rather than merely
checking that some text drew. `status-production-*` PNGs and measured font
metrics retain that evidence. Arbitrarily long provider fields can still be
ellipsized to the configured visible line limits; complete structured text is
retained. The former None-branding-only witness did not establish this bound.

The parent records final combined-host and visual results after its integration
gate. These fixtures use prepared strings, not media accounts or battery
hardware. They do not establish pixel identity to Windows' final compositor on
every DPI/theme, actual Start presentation cadence, or a fix for the unchanged
four-pixel historical opaque-caption witness.

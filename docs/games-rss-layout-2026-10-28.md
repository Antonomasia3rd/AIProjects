# Game tiles and the RSS presentation baseline

The October 28 reminder exposed a real remaining difference. All four game
types already routed to the same game renderer and put available resource art
on the right, but their Default policy admitted two/three/five resources on
Medium/Wide/Large with two lines for both fields everywhere. RSS admitted
one/two/three entries and used size-aware line limits. The game bitmap fallback
pruned overflowing rows while native XML still admitted its larger row budget.

Default Genshin, HSR, ZZZ and WuWa now reuse the existing RSS effective text
policy and resource-entry capacity. RSS itself is unchanged:

| Size | Resource entries | Primary maximum lines | Secondary maximum lines |
| --- | --- | --- | --- |
| Medium | 1 | 3 | 2 |
| Wide | 2 | 2 | 1 |
| Large | 3 | 2 | 2 |

The shared Default uses RSS's base primary/captionSubtle secondary styles,
wrapping, top alignment and no full blank line between a label and its value.
The bitmap game path also drops its old extra two-pixel label/value gap for
Default. This matches the layout policy rather than asserting exact Windows
font/raster pixels or dynamic Start display timing.

Available bundled HoYo resource art keeps its separate right column. WuWa
continues to use full-width text when its source has no resource artwork.
No placeholder icon or artwork from another game is invented. The existing
Left/Right/None, icon scale and inter-row spacing controls remain independent
and survive Default's effective typography selection.

One leading text-only freshness/status row remains separate from the Default
resource-entry capacity. It gets at least two secondary lines for age plus
threshold-qualified error text. Thus Medium can retain a useful resource
alongside "Refreshed 5m ago", instead of spending its sole resource slot on
metadata. Wide uses one title line per resource while that header is present,
reserving space for both resource entries and the status. Native XML, bitmap rendering and the contents adapter share this
row plan. The actual resource snapshot is never truncated or refetched merely
because a smaller tile displays fewer entries.

Explicit Custom presentation remains effective, including its compact
two/three/five total-row capacities, typography, wrapping and spacing choices.
The same validated CLI/INI/tray/Properties fields are used:
Content.TextPreset, TextStyle, TextSecondaryStyle, TextMaxLines,
TextSecondaryMaxLines, TextWrap, TextGapLines, TextIconPlacement,
TextIconScalePercent and TextRowSpacing. No INI rewrite or account migration
is performed. Native-content and generated-image cache markers were advanced
from GameRows1 to GameRows2 so old rendered defaults are not reused.

## Validation boundary

The focused portable fixture passed **56 checks**, compiled with /W4 /WX /O2
and scanned clean with Defender before execution. It covers all effective RSS
size budgets, native order/escaping, available/absent artwork, explicit Custom
behavior, game-specific controls and a leading freshness header.

The new TestGameRssParityHost fixture covers each actual typed game route,
semantic native XML, contents limits and the real bitmap rendering path with
injected resource rows. Its account fields are blank, transports are inert,
and it performs no account, package, pin or running-app operation. Parent
combined-host results are recorded separately; this document does not imply
successful real-account refresh or visual acceptance on every Windows version.

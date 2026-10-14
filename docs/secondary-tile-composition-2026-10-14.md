# Secondary tile composition, 2026-10-14

The shared mapping model in `dependencies/secondary_tiles.h` gives six source
tiles stable IDs and numbered content defaults: `smtc` -> Content.2, `rss` ->
Content.3, `genshin` -> Content.4, `hsr` -> Content.5, `zzz` -> Content.6, and
`discord` -> Content.7. The primary application tile stays on Content.1. These
are destinations within one DesktopStub process; the model starts no extra
resident hosts or provider processes.

`[Tiles] Enabled=1` selects this layout. Each `[Tile.<id>]` accepts `Enabled`,
`Content` (one-based), and `Name`. Enabled mappings must select an existing
numbered content. Disabled mappings may retain a future content preference up
to the existing 32-content limit. Tile IDs come from the fixed six-source
catalog and cannot collide. Existing profiles default to `Tiles.Enabled=0`.

The host reads the mapping and content settings from the same published INI
document. It requests providers only for enabled primary and mapped contents,
excluding unrelated enabled contents. Multiple destinations sharing a content
reuse its composition. Each tick copies provider results once: SMTC text and
artwork, the RSS feed, Discord status, indicator status, and each required notes
account. Notes formatting then uses the same account snapshot for every detail.

With separate tiles enabled, primary cycling is disabled even while package
identity or mode registration is pending. Until direct Live Tile delivery is
available, only Content.1 uses ordinary primary delivery. Secondary desired
frames wait for direct delivery. The host does not substitute an unrelated
source or background onto the primary tile.

Direct delivery collects changed destinations into one generation batch.
Secondary existence and Windows delivery belong to the root delivery layer,
outside provider composition. Individual unavailable backgrounds are omitted
and logged when their error changes; they do not prevent other usable tile
frames from being composed. Explicitly disabling Content.1 publishes a blank
primary while enabled mapped contents can continue.

## Validation status

`tools/SecondaryTileTests.cpp` is an inert portable fixture for the real mapping
model. It covers the six defaults and IDs, typed settings, transactional
configuration failure, enabled mapping ranges, names and booleans, duplicate
or unknown IDs, shared content de-duplication, disabled providers, and exclusion
of unmapped entries. The portable fixture passed 36 checks. The integrated host passed 10,311 checks. See the validation record for build,
scan, host fixture and target-system validation. Pinning and player/account
behavior require separate Windows acceptance tests; the pure model does not
exercise those operations.

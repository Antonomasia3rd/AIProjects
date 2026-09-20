// Declarative geometry for DesktopStub's baked/static TileText simulation.
//
// This file intentionally has no include guard. tile_text_layout.h expands it
// into the C++ runtime table, while tools/TestTileTextLayout.ps1 parses these
// same rows on Windows, Linux, and macOS without requiring a C++ compiler.
//
// Arguments:
//   size, input mask (primary=1, secondary=2, badge=4), role, source field,
//   x, y, width, height, horizontal alignment, trimming, documented max lines

// TileSquareText02: one header over one body string (maximum three lines).
TILE_TEXT_REGION(Medium, 1, Title, 0, 10, 8, 130, 32, Near, Character, 1)
TILE_TEXT_REGION(Medium, 2, Title, 1, 10, 8, 130, 32, Near, Character, 1)
TILE_TEXT_REGION(Medium, 3, Title, 0, 10, 8, 130, 32, Near, Character, 1)
TILE_TEXT_REGION(Medium, 3, Body, 1, 10, 44, 130, 60, Near, Word, 3)

// TileSquareBlock: one block value over one short caption. The template has
// only two slots, so primary wins when all three configured fields are set.
TILE_TEXT_REGION(Medium, 4, Badge, 2, 10, 7, 130, 72, Near, Character, 1)
TILE_TEXT_REGION(Medium, 5, Badge, 2, 10, 7, 130, 72, Near, Character, 1)
TILE_TEXT_REGION(Medium, 5, Body, 0, 10, 80, 130, 22, Near, Character, 1)
TILE_TEXT_REGION(Medium, 6, Badge, 2, 10, 7, 130, 72, Near, Character, 1)
TILE_TEXT_REGION(Medium, 6, Body, 1, 10, 80, 130, 22, Near, Character, 1)
TILE_TEXT_REGION(Medium, 7, Badge, 2, 10, 7, 130, 72, Near, Character, 1)
TILE_TEXT_REGION(Medium, 7, Body, 0, 10, 80, 130, 22, Near, Character, 1)

// TileWideImageAndText02: two regular, non-wrapping lines in a bottom band.
// Secondary-only input is promoted to the first visible slot, matching the
// preset XML generator's fallback behavior.
TILE_TEXT_REGION(Wide, 1, Body, 0, 10, 101, 290, 22, Near, Character, 1)
TILE_TEXT_REGION(Wide, 2, Body, 1, 10, 101, 290, 22, Near, Character, 1)
TILE_TEXT_REGION(Wide, 3, Body, 0, 10, 101, 290, 22, Near, Character, 1)
TILE_TEXT_REGION(Wide, 3, Body, 1, 10, 124, 290, 22, Near, Character, 1)

// TileWideBlockAndText02: one wrapped left string (maximum four lines), a
// large block value on the right, and one short caption beneath the block.
TILE_TEXT_REGION(Wide, 4, Badge, 2, 210, 20, 80, 72, Far, Character, 1)
TILE_TEXT_REGION(Wide, 5, Body, 0, 20, 30, 170, 88, Near, Word, 4)
TILE_TEXT_REGION(Wide, 5, Badge, 2, 210, 20, 80, 72, Far, Character, 1)
TILE_TEXT_REGION(Wide, 6, Body, 1, 20, 30, 170, 88, Near, Word, 4)
TILE_TEXT_REGION(Wide, 6, Badge, 2, 210, 20, 80, 72, Far, Character, 1)
TILE_TEXT_REGION(Wide, 7, Body, 0, 20, 30, 170, 88, Near, Word, 4)
TILE_TEXT_REGION(Wide, 7, Badge, 2, 210, 20, 80, 72, Far, Character, 1)
TILE_TEXT_REGION(Wide, 7, Body, 1, 210, 103, 80, 22, Far, Character, 1)

// TileSquare310x310ImageAndTextOverlay02: a large top string (maximum two
// lines) and a regular bottom string (maximum three lines) over an image.
TILE_TEXT_REGION(Large, 1, Title, 0, 20, 15, 270, 80, Near, Word, 2)
TILE_TEXT_REGION(Large, 2, Title, 1, 20, 15, 270, 80, Near, Word, 2)
TILE_TEXT_REGION(Large, 3, Title, 0, 20, 15, 270, 80, Near, Word, 2)
TILE_TEXT_REGION(Large, 3, Body, 1, 20, 224, 270, 62, Near, Word, 3)

// TileSquare310x310BlockAndText02: one large block value followed by two
// large, unwrapped header lines. IDs 4-7 are unused because the public INI
// schema exposes only primary, secondary, and badge fields.
TILE_TEXT_REGION(Large, 4, Badge, 2, 20, 8, 270, 82, Near, Character, 1)
TILE_TEXT_REGION(Large, 5, Badge, 2, 20, 8, 270, 82, Near, Character, 1)
TILE_TEXT_REGION(Large, 5, Title, 0, 20, 98, 270, 40, Near, Character, 1)
TILE_TEXT_REGION(Large, 6, Badge, 2, 20, 8, 270, 82, Near, Character, 1)
TILE_TEXT_REGION(Large, 6, Title, 1, 20, 98, 270, 40, Near, Character, 1)
TILE_TEXT_REGION(Large, 7, Badge, 2, 20, 8, 270, 82, Near, Character, 1)
TILE_TEXT_REGION(Large, 7, Title, 0, 20, 98, 270, 40, Near, Character, 1)
TILE_TEXT_REGION(Large, 7, Title, 1, 20, 138, 270, 40, Near, Character, 1)

#ifndef AIP_DESKTOPSTUB_TILE_TEXT_LAYOUT_H
#define AIP_DESKTOPSTUB_TILE_TEXT_LAYOUT_H

// Pure layout math for the "simulated" Live Tile text overlay (ga_image.inc's
// Render{Medium,Wide,Large}TileTextSimulation), extracted so it has zero
// dependency on <windows.h>, GDI+, or any other Windows-only header. This
// file compiles standalone with any C++17 compiler, including g++/clang on
// non-Windows platforms -- see DesktopStub/tools/TileTextLayoutTests.cpp, which runs the
// same layout logic exercised by the real Windows build without needing
// Windows to do it.
//
// This approximates the real Windows tile-rendering engine. Microsoft's
// referenced catalog documents field structure, line counts, and sample
// images, not a pixel-exact specification for this renderer. It does:
//   1. Model the documented field structure of the Windows 8/8.1 preset
//      templates used by ga_live_tile_templates.inc.
//   2. Let both a portable PowerShell test, a C++ unit test, and the real
//      Windows build exercise the exact same declarative region specification,
//      catching structural regressions
//      (overlapping regions, regions extending outside tile bounds, a
//      badge/primary/secondary combination silently producing zero visible
//      regions) that a "did the .exe not crash" smoke test can't.

#include <cstddef>
#include <string>
#include <vector>

namespace tiletext
{

enum class TileSize
{
    Medium, // 150x150
    Wide,   // 310x150
    Large   // 310x310
};

enum class TextRole
{
    Title,        // Larger/header-weight text (title font family)
    Body,         // Regular body text
    Badge,        // Large block text (block font family)
};

enum class HorizontalAlign
{
    Near, // Left in LTR
    Far   // Right in LTR
};

enum class Trimming
{
    Character, // Truncate at a character boundary with an ellipsis
    Word       // Truncate at a word boundary (wrapping/multi-line fields)
};

// A single text region to draw. Coordinates are in the same 100%-scale
// logical units ga_image.inc has always hardcoded (e.g. 150x150 for Medium);
// the caller applies its own scale factor at render time, same as before.
struct TileTextRegion
{
    TextRole role = TextRole::Body;
    // Which of the three [TileText] fields this region's content comes from.
    // 0 = primary (Text), 1 = secondary (SecondaryText), 2 = badge (BadgeText).
    int sourceField = 0;
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
    HorizontalAlign align = HorizontalAlign::Near;
    Trimming trim = Trimming::Character;
    // Documented maximum line count for this region, per the closest
    // matching entry in the Windows 8/8.1 tile template catalog. The renderer
    // bounds drawing using actual GDI+ line metrics and disables wrapping
    // for one-line fields. Geometry and native glyph checks complement this.
    int documentedMaxLines = 1;
};

struct TileTextInputs
{
    bool hasPrimary = false;
    bool hasSecondary = false;
    bool hasBadge = false;
};

// Font sizes (in the same 100%-scale logical pixel units as the regions
// below), matching ga_image.inc's CreateSystemTileFontFamily/Font calls
// exactly -- kept here so a test can sanity-check region-height-to-font-size
// ratios without needing to duplicate the numbers separately.
struct TileFontSizes
{
    double title = 0.0;
    double body = 15.0;
    double badge = 54.0;
};

inline TileFontSizes MediumFontSizes() { return TileFontSizes{ 24.0, 15.0, 54.0 }; }
inline TileFontSizes WideFontSizes() { return TileFontSizes{ 0.0, 15.0, 54.0 }; } // Wide has no distinct title font
inline TileFontSizes LargeFontSizes() { return TileFontSizes{ 30.0, 15.0, 54.0 }; }

struct TileTextSpecEntry
{
    TileSize size;
    unsigned int inputMask;
    TileTextRegion region;
};

inline constexpr TileTextSpecEntry kTileTextSpec[] = {
#define TILE_TEXT_REGION(size, mask, role, source, x, y, width, height, align, trim, lines) \
    { TileSize::size, mask, { TextRole::role, source, x, y, width, height, HorizontalAlign::align, Trimming::trim, lines } },
#include "tile_text_layout_spec.inc"
#undef TILE_TEXT_REGION
};

inline unsigned int TileTextInputMask(const TileTextInputs& in)
{
    return (in.hasPrimary ? 1u : 0u) |
        (in.hasSecondary ? 2u : 0u) |
        (in.hasBadge ? 4u : 0u);
}

inline std::vector<TileTextRegion> ComputeTileTextLayout(TileSize size, const TileTextInputs& in)
{
    std::vector<TileTextRegion> regions;
    unsigned int inputMask = TileTextInputMask(in);
    for (const TileTextSpecEntry& entry : kTileTextSpec)
    {
        if (entry.size == size && entry.inputMask == inputMask)
            regions.push_back(entry.region);
    }
    return regions;
}

inline std::vector<TileTextRegion> ComputeMediumTileTextLayout(const TileTextInputs& in)
{
    return ComputeTileTextLayout(TileSize::Medium, in);
}

inline std::vector<TileTextRegion> ComputeWideTileTextLayout(const TileTextInputs& in)
{
    return ComputeTileTextLayout(TileSize::Wide, in);
}

inline std::vector<TileTextRegion> ComputeLargeTileTextLayout(const TileTextInputs& in)
{
    return ComputeTileTextLayout(TileSize::Large, in);
}

inline void TileSizeDimensions(TileSize size, double& width, double& height)
{
    switch (size)
    {
    case TileSize::Medium: width = 150; height = 150; return;
    case TileSize::Wide: width = 310; height = 150; return;
    case TileSize::Large: width = 310; height = 310; return;
    }
    width = 0; height = 0;
}

} // namespace tiletext

#endif

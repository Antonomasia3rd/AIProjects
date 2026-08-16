#ifndef AIP_DESKTOPSTUB_TILE_TEXT_LAYOUT_H
#define AIP_DESKTOPSTUB_TILE_TEXT_LAYOUT_H

// Pure layout math for the "simulated" Live Tile text overlay (ga_image.inc's
// Render{Medium,Wide,Large}TileTextSimulation), extracted so it has zero
// dependency on <windows.h>, GDI+, or any other Windows-only header. This
// file compiles standalone with any C++17 compiler, including g++/clang on
// non-Windows platforms -- see tools/TileTextLayoutTests.cpp, which runs the
// same layout logic exercised by the real Windows build without needing
// Windows to do it.
//
// This does NOT reproduce the real Windows tile-rendering engine (Microsoft
// never published exact pixel coordinates for text placement within the
// legacy Windows 8/8.1 tile template catalog -- only line-wrap/line-count
// semantics and image pixel sizes; the actual text rendering was always an
// OS-internal implementation detail). What this DOES do is:
//   1. Keep the same rectangles ga_image.inc has always used, as portable
//      data instead of inline GDI+ calls, so a Windows rebuild produces
//      byte-identical tile assets to before this refactor.
//   2. Let both a Linux unit test and the real Windows build exercise the
//      exact same region-selection logic, catching structural regressions
//      (overlapping regions, regions extending outside tile bounds, a
//      badge/primary/secondary combination silently producing zero visible
//      regions) that a "did the .exe not crash" smoke test can't.
//
// Known, deliberately-not-fixed gap: the real TileWideBlockAndText02
// template has a short caption line under its badge/block number (a 6th
// XML text field). DesktopStub's [TileText] INI schema only has three
// fields (Text, SecondaryText, BadgeText), so there's no text source for
// that caption without adding a new config key -- documented in
// DesktopStub/README.md rather than worked around with reused fields.

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
    Character, // Truncate mid-character with an ellipsis (single line fields)
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
    // matching entry in the Windows 8/8.1 tile template catalog. Informational
    // only -- GDI+'s StringFormatFlagsLineLimit doesn't take an explicit
    // line-count parameter, so this isn't enforced at render time, only
    // checked against the region's height/font-size ratio in tests.
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

inline std::vector<TileTextRegion> ComputeMediumTileTextLayout(const TileTextInputs& in)
{
    std::vector<TileTextRegion> regions;
    if (in.hasBadge)
    {
        regions.push_back({ TextRole::Badge, 2, 10, 7, 130, 67, HorizontalAlign::Near, Trimming::Character, 1 });
        // Fixed from showing both primary and secondary here: the closest
        // catalog match (TileSquareBlock) has exactly one caption string
        // under the block number, not two. Prefer primary; if only
        // secondary was supplied, fall back to that so the badge isn't
        // left with a silently-dropped caption.
        if (in.hasPrimary)
            regions.push_back({ TextRole::Body, 0, 10, 80, 130, 57, HorizontalAlign::Near, Trimming::Word, 1 });
        else if (in.hasSecondary)
            regions.push_back({ TextRole::Body, 1, 10, 80, 130, 57, HorizontalAlign::Near, Trimming::Word, 1 });
        return regions;
    }

    double secondaryTop = in.hasPrimary ? 44.0 : 10.0;
    if (in.hasPrimary)
        regions.push_back({ TextRole::Title, 0, 10, 8, 130, 32, HorizontalAlign::Near, Trimming::Character, 1 });
    if (in.hasSecondary)
        regions.push_back({ TextRole::Body, 1, 10, secondaryTop, 130, 96, HorizontalAlign::Near, Trimming::Word, 3 });
    return regions;
}

inline std::vector<TileTextRegion> ComputeWideTileTextLayout(const TileTextInputs& in)
{
    std::vector<TileTextRegion> regions;
    if (in.hasBadge)
    {
        if (in.hasPrimary)
            regions.push_back({ TextRole::Body, 0, 10, 10, 190, 22, HorizontalAlign::Near, Trimming::Character, 1 });
        if (in.hasSecondary)
            regions.push_back({ TextRole::Body, 1, 10, 36, 190, 104, HorizontalAlign::Near, Trimming::Word, 4 });
        regions.push_back({ TextRole::Badge, 2, 210, 27, 90, 76, HorizontalAlign::Far, Trimming::Character, 1 });
        // Known gap, not fixed here: the real TileWideBlockAndText02 has a
        // short caption line under the block number (a 6th XML text field).
        // [TileText] only has three fields, so there's no text source for
        // it -- see DesktopStub/README.md.
        return regions;
    }

    if (in.hasSecondary && in.hasPrimary)
    {
        regions.push_back({ TextRole::Body, 0, 10, 103, 290, 20, HorizontalAlign::Near, Trimming::Character, 1 });
        regions.push_back({ TextRole::Body, 1, 10, 124, 290, 19, HorizontalAlign::Near, Trimming::Character, 1 });
    }
    else if (in.hasPrimary)
    {
        regions.push_back({ TextRole::Body, 0, 10, 104, 290, 39, HorizontalAlign::Near, Trimming::Word, 2 });
    }
    else if (in.hasSecondary)
    {
        // ApplyTileTextOverlay only calls the wide simulation when at least
        // one field is non-empty, but the original inline code had no
        // explicit "secondary only" branch -- it fell through to the
        // primary-only rect and would have drawn secondaryText into a
        // rect sized/positioned for a single primary line. Preserved as a
        // deliberate choice here (secondary-only is an unusual
        // configuration) but now at least explicit instead of implicit.
        regions.push_back({ TextRole::Body, 1, 10, 104, 290, 39, HorizontalAlign::Near, Trimming::Word, 2 });
    }
    return regions;
}

inline std::vector<TileTextRegion> ComputeLargeTileTextLayout(const TileTextInputs& in)
{
    std::vector<TileTextRegion> regions;
    double titleWidth = in.hasBadge ? 170.0 : 270.0;
    if (in.hasPrimary)
        regions.push_back({ TextRole::Title, 0, 20, 15, titleWidth, 82, HorizontalAlign::Near, Trimming::Word, 3 });
    if (in.hasSecondary)
        regions.push_back({ TextRole::Body, 1, 20, 224, 270, 66, HorizontalAlign::Near, Trimming::Word, 4 });
    if (in.hasBadge)
        regions.push_back({ TextRole::Badge, 2, 200, 17, 90, 72, HorizontalAlign::Far, Trimming::Character, 1 });
    return regions;
}

inline std::vector<TileTextRegion> ComputeTileTextLayout(TileSize size, const TileTextInputs& in)
{
    switch (size)
    {
    case TileSize::Medium: return ComputeMediumTileTextLayout(in);
    case TileSize::Wide: return ComputeWideTileTextLayout(in);
    case TileSize::Large: return ComputeLargeTileTextLayout(in);
    }
    return {};
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

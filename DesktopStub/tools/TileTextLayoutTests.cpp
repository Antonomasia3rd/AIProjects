// Portable smoke test for dependencies/DesktopStub/tile_text_layout.h.
//
// Compiles and runs on any C++17 compiler -- no Windows headers, no GDI+.
// Windows CI: TestDesktopStubSource.cmd builds and runs this
// with cl.exe, same as the rest of the source-check suite.
// Anywhere else: compiles directly with g++/clang,
// no cl.exe or Windows SDK required:
//   g++ -std=c++17 -Wall -Wextra -o /tmp/TileTextLayoutTests DesktopStub/tools/TileTextLayoutTests.cpp
//   /tmp/TileTextLayoutTests

#include "../../dependencies/DesktopStub/tile_text_layout.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace tiletext;

static int g_checks = 0;
static int g_failures = 0;

static void Check(bool condition, const std::string& description)
{
    ++g_checks;
    if (condition)
    {
        std::printf("ok - %s\n", description.c_str());
    }
    else
    {
        std::printf("FAIL - %s\n", description.c_str());
        ++g_failures;
    }
}

static bool RegionsOverlap(const TileTextRegion& a, const TileTextRegion& b)
{
    bool separateHorizontally = a.x + a.width <= b.x || b.x + b.width <= a.x;
    bool separateVertically = a.y + a.height <= b.y || b.y + b.height <= a.y;
    return !(separateHorizontally || separateVertically);
}

static double FontSizeForRole(const TileFontSizes& fonts, TextRole role)
{
    switch (role)
    {
    case TextRole::Title: return fonts.title > 0.0 ? fonts.title : fonts.body;
    case TextRole::Badge: return fonts.badge;
    case TextRole::Body:
    default: return fonts.body;
    }
}

// Runs one (size, inputs) combination through every check that applies to
// any tile size: in-bounds, no overlap, every requested field produced at
// least one region, and every produced region's height can fit at least
// one line of its own font size without being clipped to nothing.
static void CheckLayout(TileSize size, const char* sizeName, const TileTextInputs& in, const std::string& label)
{
    double tileWidth = 0, tileHeight = 0;
    TileSizeDimensions(size, tileWidth, tileHeight);
    TileFontSizes fonts = size == TileSize::Medium ? MediumFontSizes()
        : size == TileSize::Wide ? WideFontSizes()
        : LargeFontSizes();

    std::vector<TileTextRegion> regions = ComputeTileTextLayout(size, in);
    const unsigned mask = TileTextInputMask(in);
    Check(regions.empty() == (mask == 0), std::string(sizeName) + " " + label + ": empty inputs produce no regions");

    for (size_t i = 0; i < regions.size(); ++i)
    {
        const TileTextRegion& r = regions[i];
        char desc[256];

        std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) is within tile bounds",
            sizeName, label.c_str(), i, r.sourceField);
        Check(r.width > 0 && r.height > 0 && r.x >= 0 && r.y >= 0 && r.x + r.width <= tileWidth && r.y + r.height <= tileHeight, desc);
        Check(r.sourceField >= 0 && r.sourceField <= 2 && (mask & (1u << r.sourceField)) != 0,
            std::string(sizeName) + " " + label + ": region references a configured field");
        Check(r.documentedMaxLines > 0 && r.documentedMaxLines <= 4,
            std::string(sizeName) + " " + label + ": region has a supported line limit");

        std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) fits at least one line of its font size",
            sizeName, label.c_str(), i, r.sourceField);
        Check(r.height >= FontSizeForRole(fonts, r.role) * 0.9, desc);

        for (size_t j = i + 1; j < regions.size(); ++j)
        {
            std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) does not overlap region %zu (field=%d)",
                sizeName, label.c_str(), i, r.sourceField, j, regions[j].sourceField);
            Check(!RegionsOverlap(r, regions[j]), desc);
            Check(r.sourceField != regions[j].sourceField,
                std::string(sizeName) + " " + label + ": a field is not drawn twice");
        }
    }

    // Every representable field should be routed to a region. TileSquareBlock
    // has only a block and one caption slot, so Medium primary+secondary+badge
    // deliberately gives the caption to primary and cannot show secondary.
    auto hasFieldRegion = [&](int field)
    {
        for (const auto& r : regions)
        {
            if (r.sourceField == field)
                return true;
        }
        return false;
    };
    if (in.hasPrimary)
    {
        char desc[256];
        std::snprintf(desc, sizeof(desc), "%s %s: primary text (field 0) is routed to a region", sizeName, label.c_str());
        Check(hasFieldRegion(0), desc);
    }
    if (in.hasSecondary)
    {
        bool mediumBlockHasOnlyOneCaption = size == TileSize::Medium &&
            in.hasPrimary && in.hasBadge;
        if (!mediumBlockHasOnlyOneCaption)
        {
            char desc[256];
            std::snprintf(desc, sizeof(desc), "%s %s: secondary text (field 1) is routed to a region", sizeName, label.c_str());
            Check(hasFieldRegion(1), desc);
        }
    }
    if (in.hasBadge)
    {
        char desc[256];
        std::snprintf(desc, sizeof(desc), "%s %s: badge text (field 2) is routed to a region", sizeName, label.c_str());
        Check(hasFieldRegion(2), desc);
    }
}

static void RunAllCombinationsFor(TileSize size, const char* sizeName)
{
    // Include empty input so disabling text cannot accidentally select a
    // stale/default region during future composition changes.
    static const TileTextInputs combos[] = {
        { false, false, false },
        { true, false, false },
        { false, true, false },
        { false, false, true },
        { true, true, false },
        { true, false, true },
        { false, true, true },
        { true, true, true },
    };
    static const char* labels[] = {
        "empty",
        "primary only", "secondary only", "badge only",
        "primary+secondary", "primary+badge", "secondary+badge",
        "primary+secondary+badge"
    };

    for (size_t i = 0; i < sizeof(combos) / sizeof(combos[0]); ++i)
        CheckLayout(size, sizeName, combos[i], labels[i]);
}

int main()
{
    RunAllCombinationsFor(TileSize::Medium, "Medium");
    RunAllCombinationsFor(TileSize::Wide, "Wide");
    RunAllCombinationsFor(TileSize::Large, "Large");

    // Regression pin for the Medium-tile badge fix: TileSquareBlock (the
    // closest real catalog match) has exactly one caption line under the
    // block number, not two. Before the fix, primary+secondary+badge on
    // Medium produced 3 regions (badge, primary, secondary all shown);
    // after the fix it produces 2 (badge, primary only).
    {
        TileTextInputs in{ true, true, true };
        auto regions = ComputeMediumTileTextLayout(in);
        Check(regions.size() == 2,
            "Medium badge+primary+secondary produces exactly 2 regions (badge + one caption, not two)");
        bool secondaryPresent = false;
        for (const auto& r : regions)
        {
            if (r.sourceField == 1)
                secondaryPresent = true;
        }
        Check(!secondaryPresent,
            "Medium badge+primary+secondary: secondary text is not drawn when primary is available for the caption slot");
    }

    // TileWideBlockAndText02 uses secondary as the short caption beneath the
    // block value when all three fields are configured.
    {
        TileTextInputs in{ true, true, true };
        auto regions = ComputeWideTileTextLayout(in);
        bool foundCaption = false;
        for (const auto& r : regions)
        {
            if (r.sourceField == 1 && r.align == HorizontalAlign::Far &&
                r.trim == Trimming::Character && r.documentedMaxLines == 1)
                foundCaption = true;
        }
        Check(foundCaption,
            "Wide badge+primary+secondary routes secondary to the one-line right-side caption");
    }

    // TileSquare310x310BlockAndText02 puts the block first, followed by two
    // large unwrapped header lines. It does not put the block at upper right
    // with secondary text at the bottom of the tile.
    {
        TileTextInputs in{ true, true, true };
        auto regions = ComputeLargeTileTextLayout(in);
        bool badgeAtLeft = false;
        bool primaryHeader = false;
        bool secondaryHeader = false;
        for (const auto& r : regions)
        {
            if (r.sourceField == 2 && r.role == TextRole::Badge &&
                r.align == HorizontalAlign::Near && r.x == 20)
                badgeAtLeft = true;
            if (r.sourceField == 0 && r.role == TextRole::Title &&
                r.trim == Trimming::Character && r.documentedMaxLines == 1)
                primaryHeader = true;
            if (r.sourceField == 1 && r.role == TextRole::Title &&
                r.trim == Trimming::Character && r.documentedMaxLines == 1)
                secondaryHeader = true;
        }
        Check(badgeAtLeft && primaryHeader && secondaryHeader,
            "Large badge layout matches BlockAndText02 block plus two header lines");
    }
    {
        // Fallback: if only secondary was configured (no primary), it should
        // still be used as the caption rather than leaving the badge with
        // nothing under it at all.
        TileTextInputs in{ false, true, true };
        auto regions = ComputeMediumTileTextLayout(in);
        bool secondaryPresent = false;
        for (const auto& r : regions)
        {
            if (r.sourceField == 1)
                secondaryPresent = true;
        }
        Check(secondaryPresent,
            "Medium badge+secondary (no primary): secondary text is used as the caption instead of being dropped");
    }

    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// Portable smoke test for dependencies/DesktopStub/tile_text_layout.h.
//
// Compiles and runs on any C++17 compiler -- no Windows headers, no GDI+.
// This is deliberately checked at the repo root's tools/, not under
// DesktopStub/tools/, since (unlike DesktopStubSourceCheck.cpp) it has
// nothing DesktopStub-specific about *how* it's built; it only depends on
// the portable header.
//
// Windows CI: TestDesktopStubSource.cmd builds and runs this
// with cl.exe, same as the rest of the source-check suite.
// Anywhere else (e.g. this Linux sandbox): compiles directly with g++/clang,
// no cl.exe or Windows SDK required:
//   g++ -std=c++17 -Wall -Wextra -I dependencies/DesktopStub -o /tmp/TileTextLayoutTests tools/TileTextLayoutTests.cpp
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

    for (size_t i = 0; i < regions.size(); ++i)
    {
        const TileTextRegion& r = regions[i];
        char desc[256];

        std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) is within tile bounds",
            sizeName, label.c_str(), i, r.sourceField);
        Check(r.x >= 0 && r.y >= 0 && r.x + r.width <= tileWidth && r.y + r.height <= tileHeight, desc);

        std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) fits at least one line of its font size",
            sizeName, label.c_str(), i, r.sourceField);
        Check(r.height >= FontSizeForRole(fonts, r.role) * 0.9, desc);

        for (size_t j = i + 1; j < regions.size(); ++j)
        {
            std::snprintf(desc, sizeof(desc), "%s %s: region %zu (field=%d) does not overlap region %zu (field=%d)",
                sizeName, label.c_str(), i, r.sourceField, j, regions[j].sourceField);
            Check(!RegionsOverlap(r, regions[j]), desc);
        }
    }

    // Every field the caller says is present should be routed to at least
    // one region -- catches "silently dropped this text field" bugs.
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
    if (in.hasBadge)
    {
        char desc[256];
        std::snprintf(desc, sizeof(desc), "%s %s: badge text (field 2) is routed to a region", sizeName, label.c_str());
        Check(hasFieldRegion(2), desc);
    }
    // Secondary text is intentionally NOT checked here: Medium's badge mode
    // drops secondary in favor of primary as the single caption line (the
    // fix this test suite exists to pin -- see the dedicated regression
    // check below), so "secondary is always routed" isn't true in general.
}

static void RunAllCombinationsFor(TileSize size, const char* sizeName)
{
    // ApplyTileTextOverlay only calls into a size's render function when at
    // least one of the three fields is non-empty, so the all-false case is
    // deliberately excluded here -- it's not a real input this code sees.
    static const TileTextInputs combos[] = {
        { true, false, false },
        { false, true, false },
        { false, false, true },
        { true, true, false },
        { true, false, true },
        { false, true, true },
        { true, true, true },
    };
    static const char* labels[] = {
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

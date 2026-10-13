#include "../../dependencies/DesktopStub/adaptive_tile_text_layout.h"
#include <iostream>
#include <limits>
#include <set>
static int checks = 0, failures = 0;
static void Check(bool value, const char* label) { ++checks; if (!value) { ++failures; std::cerr << "FAIL: " << label << '\n'; } }
int main()
{
    using namespace tiletext;
    for (const auto size : {TileSize::Medium, TileSize::Wide, TileSize::Large}) for (unsigned mask = 0; mask < 8; ++mask) {
        TileTextInputs input{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0};
        const auto regions = ComputeAdaptiveTileTextLayout(size, input, 16, 32);
        std::set<int> fields;
        double width = 0, height = 0; TileSizeDimensions(size, width, height);
        for (const auto& region : regions) {
            Check(fields.insert(region.sourceField).second, "each enabled native field is represented once");
            Check(region.x >= AdaptiveEdgePadding && region.y >= AdaptiveEdgePadding && region.width > 0 && region.height > 0 &&
                region.x + region.width <= width - AdaptiveEdgePadding + 0.001 &&
                region.y + region.height <= height - AdaptiveEdgePadding + 0.001, "adaptive regions stay inside the declared edge policy");
            Check(region.maxLines > 0 && (region.sourceField == 1 || region.maxLines == 1), "only the wrapped secondary field can use multiple lines");
            Check(region.style == (region.sourceField == 0 ? AdaptiveTextStyle::Caption :
                region.sourceField == 1 ? AdaptiveTextStyle::CaptionSubtle : AdaptiveTextStyle::Title), "field styles match the current native XML");
            Check(region.align == (region.sourceField == 2 ? HorizontalAlign::Far : HorizontalAlign::Near), "only the badge is right aligned");
        }
        for (int field = 0; field < 3; ++field) Check(fields.count(field) == ((mask >> field) & 1), "all masks preserve exactly their applicable native fields");
        for (std::size_t a = 0; a < regions.size(); ++a) for (std::size_t b = a + 1; b < regions.size(); ++b) {
            const auto& x = regions[a]; const auto& y = regions[b];
            Check(x.x + x.width <= y.x + 0.001 || y.x + y.width <= x.x + 0.001 ||
                x.y + x.height <= y.y + 0.001 || y.y + y.height <= x.y + 0.001, "text fields do not overlap");
        }
        if (input.hasBadge && (input.hasPrimary || input.hasSecondary)) {
            const auto& badge = regions.back(); const auto& left = regions.front();
            Check(std::abs(left.width / badge.width - 3) < 0.00001 &&
                std::abs(badge.x - (left.x + left.width) - AdaptiveColumnGap) < 0.00001,
                "caption and badge columns keep native weights three-to-one and the documented eight-pixel gap");
        }
        for (double scale : {0.8, 1.0, 1.25, 1.5, 1.8, 2.0, 4.0}) for (const auto& region : regions)
            Check(region.x * scale >= 0 && region.y * scale >= 0 &&
                (region.x + region.width) * scale <= width * scale &&
                (region.y + region.height) * scale <= height * scale, "every configured scale retains bounds and positive geometry");
    }
    for (double invalid : {0.0, -1.0, std::numeric_limits<double>::denorm_min(),
            std::numeric_limits<double>::min(), std::numeric_limits<double>::max(),
            std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        Check(ComputeAdaptiveTileTextLayout(TileSize::Medium, {true, true, true}, invalid, 32).empty() &&
            ComputeAdaptiveTileTextLayout(TileSize::Medium, {true, true, true}, 16, invalid).empty(), "invalid font metrics produce no geometry");
    Check(AdaptiveCaptionEm == 12 && AdaptiveTitleEm == 24 && AdaptiveSubtleAlpha == 153,
        "font em sizes and subtle opacity follow Microsoft adaptive styles");
    std::cout << checks << " adaptive layout checks; " << failures << " failures\n";
    return failures ? 1 : 0;
}

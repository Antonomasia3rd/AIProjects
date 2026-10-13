#pragma once
// Portable policy for the Windows 10 adaptive XML emitted by ga_live_tile.inc.
// Native hints define field/style/group behavior, not pixel-exact shell geometry.
#include <algorithm>
#include <cmath>
#include <vector>
#include "tile_text_layout.h"

namespace tiletext {
enum class AdaptiveTextStyle { Caption, CaptionSubtle, Title };
struct AdaptiveTileTextRegion {
    int sourceField = 0;
    AdaptiveTextStyle style = AdaptiveTextStyle::Caption;
    double x = 0, y = 0, width = 0, height = 0;
    HorizontalAlign align = HorizontalAlign::Near;
    int maxLines = 1;
};
inline constexpr double AdaptiveCaptionEm = 12;
inline constexpr double AdaptiveTitleEm = 24;
inline constexpr double AdaptiveColumnGap = 8;
// An explicit product approximation: Microsoft's guide specifies eight-pixel
// image edges and column gaps, but not invariant text padding for every build.
inline constexpr double AdaptiveEdgePadding = 8;
inline constexpr unsigned char AdaptiveSubtleAlpha = 153; // Documented 60%.

inline std::vector<AdaptiveTileTextRegion> ComputeAdaptiveTileTextLayout(
    TileSize size, const TileTextInputs& input, double captionLineHeight, double titleLineHeight)
{
    std::vector<AdaptiveTileTextRegion> result;
    double tileWidth = 0, tileHeight = 0;
    TileSizeDimensions(size, tileWidth, tileHeight);
    if (!std::isfinite(captionLineHeight) || !std::isfinite(titleLineHeight) ||
        captionLineHeight <= 0 || titleLineHeight <= 0 || tileWidth <= 0 || tileHeight <= 0)
        return result;
    const double width = tileWidth - AdaptiveEdgePadding * 2;
    const double height = tileHeight - AdaptiveEdgePadding * 2;
    // These are logical-pixel line metrics, not arbitrary scale factors. A
    // subpixel/tile-sized line cannot model the declared 12/24-pixel styles.
    // Bounding them also keeps division and conversion to maxLines defined.
    if (captionLineHeight < 1 || titleLineHeight < 1 ||
        captionLineHeight > height - 0.01 || titleLineHeight > height - 0.01)
        return result;
    const double captionBox = captionLineHeight + 0.01;
    const double titleBox = titleLineHeight + 0.01;
    if (!std::isfinite(captionBox) || !std::isfinite(titleBox)) return result;
    const bool columns = input.hasBadge && (input.hasPrimary || input.hasSecondary);
    const double textWidth = columns ? (width - AdaptiveColumnGap) * 0.75 : width;
    const auto add = [&](int field, AdaptiveTextStyle style, double x, double y, double w, double h, HorizontalAlign align, int lines) {
        if (w > 0 && h > 0 && lines > 0)
            result.push_back({field, style, x, y, w, h, align, lines});
    };
    double y = AdaptiveEdgePadding;
    if (input.hasPrimary) {
        add(0, AdaptiveTextStyle::Caption, AdaptiveEdgePadding, y, textWidth,
            std::min(captionBox, height), HorizontalAlign::Near, 1);
        y += captionBox;
    }
    if (input.hasSecondary) {
        const double available = tileHeight - AdaptiveEdgePadding - y;
        const int lines = available > 0 ? static_cast<int>(std::floor((available + 0.001) / captionLineHeight)) : 0;
        add(1, AdaptiveTextStyle::CaptionSubtle, AdaptiveEdgePadding, y, textWidth,
            available, HorizontalAlign::Near, lines);
    }
    if (input.hasBadge) {
        const double badgeWidth = columns ? (width - AdaptiveColumnGap) * 0.25 : width;
        const double badgeX = columns ? AdaptiveEdgePadding + textWidth + AdaptiveColumnGap : AdaptiveEdgePadding;
        add(2, AdaptiveTextStyle::Title, badgeX, AdaptiveEdgePadding, badgeWidth,
            std::min(titleBox, height), HorizontalAlign::Far, 1);
    }
    return result;
}
} // namespace tiletext

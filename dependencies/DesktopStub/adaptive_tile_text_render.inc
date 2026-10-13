// Uses the existing system-font/text helpers. The preset renderer stays intact.
#include "adaptive_tile_text_layout.h"

static bool DrawAdaptiveSubtleTileText(Graphics& graphics, const std::wstring& text,
    Font& font, const RectF& layout, int maxLines)
{
    if (text.empty()) return true;
    const REAL lineHeight = font.GetHeight(&graphics);
    if (font.GetLastStatus() != Ok || lineHeight <= 0 || maxLines <= 0) return false;
    RectF bounded = layout;
    bounded.Height = std::min(layout.Height, lineHeight * maxLines + 0.01f);
    StringFormat format;
    format.SetAlignment(StringAlignmentNear);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord);
    // captionSubtle has hint-wrap=true even if only one line currently fits.
    format.SetFormatFlags(StringFormatFlagsLineLimit);
    SolidBrush brush(Color(tiletext::AdaptiveSubtleAlpha, 255, 255, 255));
    return GdiOk(graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font,
        bounded, &format, &brush), L"draw adaptive secondary text");
}

static bool RenderAdaptiveTileTextSimulation(Graphics& graphics, tiletext::TileSize size,
    const std::wstring& primary, const std::wstring& secondary, const std::wstring& badge, double scale)
{
    if (!std::isfinite(scale) || scale <= 0) return false;
    auto captionFamily = CreateSystemTileFontFamily(L"Segoe UI");
    auto titleFamily = CreateSystemTileFontFamily(L"Segoe UI Semilight");
    if (!captionFamily || !titleFamily) return false;
    Font caption(captionFamily.get(), ScaleTileMetric(static_cast<REAL>(tiletext::AdaptiveCaptionEm), scale), FontStyleRegular, UnitPixel);
    Font title(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(tiletext::AdaptiveTitleEm), scale), FontStyleRegular, UnitPixel);
    if (caption.GetLastStatus() != Ok || title.GetLastStatus() != Ok) return false;
    const double captionHeight = caption.GetHeight(&graphics) / scale;
    const double titleHeight = title.GetHeight(&graphics) / scale;
    if (caption.GetLastStatus() != Ok || title.GetLastStatus() != Ok || captionHeight <= 0 || titleHeight <= 0) return false;
    const tiletext::TileTextInputs input{!primary.empty(), !secondary.empty(), !badge.empty()};
    const auto regions = tiletext::ComputeAdaptiveTileTextLayout(size, input, captionHeight, titleHeight);
    for (const auto& region : regions) {
        const auto& text = region.sourceField == 0 ? primary : region.sourceField == 1 ? secondary : badge;
        const auto rectangle = ScaleTileRect(static_cast<REAL>(region.x), static_cast<REAL>(region.y),
            static_cast<REAL>(region.width), static_cast<REAL>(region.height), scale);
        if (region.style == tiletext::AdaptiveTextStyle::CaptionSubtle) {
            if (!DrawAdaptiveSubtleTileText(graphics, text, caption, rectangle, region.maxLines)) return false;
        } else {
            Font& font = region.style == tiletext::AdaptiveTextStyle::Title ? title : caption;
            if (!DrawSystemTileText(graphics, text, font, rectangle, ToGdiAlignment(region.align),
                StringTrimmingEllipsisCharacter, 1, L"draw adaptive primary/badge text")) return false;
        }
    }
    // The source already contains its selected image/solid/transparent canvas.
    // Adaptive native XML uses hint-overlay=0, so no plate or darkener is added.
    return true;
}

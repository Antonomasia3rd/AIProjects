// Actual GDI+ drawing shared by DesktopStub and its isolated render smoke tests.
// Host supplies GdiOk(Status, operation) and SimulatedTileBackgroundColor().
static bool ConfigureTileTextGraphics(Graphics& graphics)
{
    // These are resizable PNG assets, not pixels on a known RGB LCD panel.
    // Grayscale antialiasing avoids baked-in ClearType color fringes.
    return GdiOk(graphics.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit), L"set tile text rendering") &&
        GdiOk(graphics.SetSmoothingMode(SmoothingModeAntiAlias), L"set tile smoothing");
}

static std::unique_ptr<FontFamily> CreateSystemTileFontFamily(const wchar_t* preferred)
{
    std::unique_ptr<FontFamily> family(new FontFamily(preferred && *preferred ? preferred : L"Segoe UI"));
    if (!family || family->GetLastStatus() != Ok)
        family.reset(new FontFamily(L"Segoe UI"));
    if (!family || family->GetLastStatus() != Ok)
        family.reset(new FontFamily(L"Arial"));
    return family;
}

static bool DrawSystemTileText(
    Graphics& graphics,
    const std::wstring& text,
    Font& font,
    const RectF& layout,
    StringAlignment alignment,
    StringTrimming trimming,
    int maxLines,
    const wchar_t* operation)
{
    if (text.empty())
        return true;

    // Font sizes are em sizes, not line heights. Respect the actual font's
    // metrics at this DPI; a rectangle tall enough for the em may still draw
    // nothing with LineLimit. Bound hard newlines as well as automatic wraps.
    const REAL lineHeight = font.GetHeight(&graphics);
    if (font.GetLastStatus() != Ok || lineHeight <= 0 || maxLines <= 0)
        return false;
    RectF boundedLayout = layout;
    boundedLayout.Height = std::min(layout.Height, lineHeight * maxLines + 0.01f);

    StringFormat format;
    format.SetAlignment(alignment);
    format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(trimming);
    int formatFlags = StringFormatFlagsLineLimit;
    if (maxLines <= 1)
        formatFlags |= StringFormatFlagsNoWrap;
    format.SetFormatFlags(formatFlags);
    SolidBrush brush(Color(255, 255, 255, 255));
    return GdiOk(
        graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, boundedLayout, &format, &brush),
        operation);
}

static REAL ScaleTileMetric(REAL logicalValue, double scale)
{
    return static_cast<REAL>(logicalValue * scale);
}

static RectF ScaleTileRect(REAL x, REAL y, REAL width, REAL height, double scale)
{
    return RectF(
        ScaleTileMetric(x, scale),
        ScaleTileMetric(y, scale),
        std::max(1.0f, ScaleTileMetric(width, scale)),
        std::max(1.0f, ScaleTileMetric(height, scale)));
}

static StringAlignment ToGdiAlignment(tiletext::HorizontalAlign align)
{
    return align == tiletext::HorizontalAlign::Far ? StringAlignmentFar : StringAlignmentNear;
}

static StringTrimming ToGdiTrimming(tiletext::Trimming trim)
{
    return trim == tiletext::Trimming::Word ? StringTrimmingEllipsisWord : StringTrimmingEllipsisCharacter;
}

// Draws every region the portable layout selected for this tile size/input
// combination, picking the right font by role and the right source string
// by field index. Returns false (without drawing further regions) on the
// first GDI+ failure, matching the early-return-on-failure behavior the
// inline per-tile-size code always had.
static bool DrawTileTextRegions(
    Graphics& graphics,
    tiletext::TileSize size,
    const tiletext::TileTextInputs& inputs,
    const std::wstring& primaryText,
    const std::wstring& secondaryText,
    const std::wstring& badgeText,
    Font& titleFont,
    Font& bodyFont,
    Font& blockFont,
    double scale,
    const wchar_t* operationPrefix)
{
    for (const tiletext::TileTextRegion& region : tiletext::ComputeTileTextLayout(size, inputs))
    {
        const std::wstring* text = region.sourceField == 0 ? &primaryText
            : region.sourceField == 1 ? &secondaryText
            : &badgeText;
        Font& font = region.role == tiletext::TextRole::Title ? titleFont
            : region.role == tiletext::TextRole::Badge ? blockFont
            : bodyFont;
        std::wstring operation = std::wstring(operationPrefix) + L" region field=" + std::to_wstring(region.sourceField);
        if (!DrawSystemTileText(graphics, *text, font,
                ScaleTileRect(static_cast<REAL>(region.x), static_cast<REAL>(region.y),
                    static_cast<REAL>(region.width), static_cast<REAL>(region.height), scale),
                ToGdiAlignment(region.align), ToGdiTrimming(region.trim), region.documentedMaxLines, operation.c_str()))
            return false;
    }
    return true;
}

static bool RenderMediumTileTextSimulation(
    Graphics& graphics,
    const std::wstring& primaryText,
    const std::wstring& secondaryText,
    const std::wstring& badgeText,
    double scale,
    bool preserveBackground = false)
{
    SolidBrush background(SimulatedTileBackgroundColor());
    if (!preserveBackground && !GdiOk(graphics.FillRectangle(&background, ScaleTileRect(0, 0, 150, 150, scale)), L"fill medium tile text background"))
        return false;

    std::unique_ptr<FontFamily> titleFamily = CreateSystemTileFontFamily(L"Segoe UI Light");
    std::unique_ptr<FontFamily> bodyFamily = CreateSystemTileFontFamily(L"Segoe UI");
    if (!titleFamily || !bodyFamily)
        return false;

    const tiletext::TileFontSizes fonts = tiletext::MediumFontSizes();
    Font titleFont(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.title), scale), FontStyleRegular, UnitPixel);
    Font bodyFont(bodyFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.body), scale), FontStyleRegular, UnitPixel);
    Font blockFont(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.badge), scale), FontStyleRegular, UnitPixel);
    if (titleFont.GetLastStatus() != Ok || bodyFont.GetLastStatus() != Ok || blockFont.GetLastStatus() != Ok)
        return false;

    tiletext::TileTextInputs inputs{ !primaryText.empty(), !secondaryText.empty(), !badgeText.empty() };
    return DrawTileTextRegions(graphics, tiletext::TileSize::Medium, inputs,
        primaryText, secondaryText, badgeText, titleFont, bodyFont, blockFont, scale, L"draw medium tile text");
}

static bool RenderWideTileTextSimulation(
    Graphics& graphics,
    const std::wstring& primaryText,
    const std::wstring& secondaryText,
    const std::wstring& badgeText,
    double scale,
    bool preserveBackground = false)
{
    Color backgroundColor = SimulatedTileBackgroundColor();
    SolidBrush background(backgroundColor);
    std::unique_ptr<FontFamily> titleFamily = CreateSystemTileFontFamily(L"Segoe UI Light");
    std::unique_ptr<FontFamily> bodyFamily = CreateSystemTileFontFamily(L"Segoe UI");
    if (!titleFamily || !bodyFamily)
        return false;

    const tiletext::TileFontSizes fonts = tiletext::WideFontSizes();
    Font bodyFont(bodyFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.body), scale), FontStyleRegular, UnitPixel);
    Font blockFont(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.badge), scale), FontStyleRegular, UnitPixel);
    if (bodyFont.GetLastStatus() != Ok || blockFont.GetLastStatus() != Ok)
        return false;

    tiletext::TileTextInputs inputs{ !primaryText.empty(), !secondaryText.empty(), !badgeText.empty() };

    if (!badgeText.empty())
    {
        if (!preserveBackground && !GdiOk(graphics.FillRectangle(&background, ScaleTileRect(0, 0, 310, 150, scale)), L"fill wide tile block background"))
            return false;
        return DrawTileTextRegions(graphics, tiletext::TileSize::Wide, inputs,
            primaryText, secondaryText, badgeText, bodyFont, bodyFont, blockFont, scale, L"draw wide tile block");
    }

    if (!preserveBackground && !GdiOk(graphics.FillRectangle(&background, ScaleTileRect(0, 100, 310, 50, scale)), L"fill wide tile text band"))
        return false;
    return DrawTileTextRegions(graphics, tiletext::TileSize::Wide, inputs,
        primaryText, secondaryText, badgeText, bodyFont, bodyFont, blockFont, scale, L"draw wide tile text");
}

static bool RenderLargeTileTextSimulation(
    Graphics& graphics,
    const std::wstring& primaryText,
    const std::wstring& secondaryText,
    const std::wstring& badgeText,
    double scale,
    bool preserveBackground = false)
{
    SolidBrush imageDarkener(Color(72, 0, 0, 0));
    if (!preserveBackground && !GdiOk(graphics.FillRectangle(&imageDarkener, ScaleTileRect(0, 0, 310, 310, scale)), L"darken large tile image"))
        return false;

    std::unique_ptr<FontFamily> titleFamily = CreateSystemTileFontFamily(L"Segoe UI Light");
    std::unique_ptr<FontFamily> bodyFamily = CreateSystemTileFontFamily(L"Segoe UI");
    if (!titleFamily || !bodyFamily)
        return false;

    const tiletext::TileFontSizes fonts = tiletext::LargeFontSizes();
    Font titleFont(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.title), scale), FontStyleRegular, UnitPixel);
    Font bodyFont(bodyFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.body), scale), FontStyleRegular, UnitPixel);
    Font blockFont(titleFamily.get(), ScaleTileMetric(static_cast<REAL>(fonts.badge), scale), FontStyleRegular, UnitPixel);
    if (titleFont.GetLastStatus() != Ok || bodyFont.GetLastStatus() != Ok || blockFont.GetLastStatus() != Ok)
        return false;

    tiletext::TileTextInputs inputs{ !primaryText.empty(), !secondaryText.empty(), !badgeText.empty() };
    return DrawTileTextRegions(graphics, tiletext::TileSize::Large, inputs,
        primaryText, secondaryText, badgeText, titleFont, bodyFont, blockFont, scale, L"draw large tile text");
}


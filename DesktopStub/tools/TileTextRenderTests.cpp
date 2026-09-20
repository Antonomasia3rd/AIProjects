// Isolated Windows UI-render smoke: the production GDI+ code, with no app
// startup, INI writes, package registration, tray icons, or notifications.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objidl.h>
#include <algorithm>
using std::min;
using std::max;
#pragma warning(push)
#pragma warning(disable: 4458) // Older Windows SDK headers shadow their members.
#include <gdiplus.h>
#pragma warning(pop)
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <float.h>
#include "../../dependencies/DesktopStub/tile_text_layout.h"
using namespace Gdiplus;

static int checks = 0, failures = 0;
static void Check(bool ok, const std::string& name)
{
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL - %s\n", name.c_str()); }
}
static bool GdiOk(Status status, const wchar_t* operation)
{
    if (status != Ok) std::fwprintf(stderr, L"GDI+ %s: %d\n", operation, status);
    return status == Ok;
}
static Color g_testBackground(255, 0, 90, 160);
static Color SimulatedTileBackgroundColor() { return g_testBackground; }
#include "../../dependencies/DesktopStub/tile_text_render.inc"

static bool IsInk(const Color& c)
{
    return c.GetA() > 0 && c.GetR() > 210 && c.GetG() > 210 && c.GetB() > 210;
}
static size_t InkPixels(Bitmap& bitmap, int left, int top, int right, int bottom)
{
    size_t count = 0;
    for (int y = std::max(0, top); y < std::min(static_cast<int>(bitmap.GetHeight()), bottom); ++y)
        for (int x = std::max(0, left); x < std::min(static_cast<int>(bitmap.GetWidth()), right); ++x)
        {
            Color color;
            bitmap.GetPixel(x, y, &color);
            if (IsInk(color)) ++count;
        }
    return count;
}
static int InkLines(Bitmap& bitmap)
{
    int lines = 0;
    bool previous = false;
    for (UINT y = 0; y < bitmap.GetHeight(); ++y)
    {
        bool current = InkPixels(bitmap, 0, y, bitmap.GetWidth(), y + 1) != 0;
        if (current && !previous) ++lines;
        previous = current;
    }
    return lines;
}
static bool SavePng(Bitmap& bitmap, const std::filesystem::path& path)
{
    UINT count = 0, bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || !bytes) return false;
    std::vector<BYTE> storage(bytes);
    auto* encoders = reinterpret_cast<ImageCodecInfo*>(storage.data());
    if (GetImageEncoders(count, bytes, encoders) != Ok) return false;
    for (UINT i = 0; i < count; ++i)
        if (std::wstring(encoders[i].MimeType) == L"image/png")
            return bitmap.Save(path.c_str(), &encoders[i].Clsid, nullptr) == Ok;
    return false;
}

static void CheckLineLimits()
{
    auto family = CreateSystemTileFontFamily(L"Segoe UI");
    Font font(family.get(), 15, FontStyleRegular, UnitPixel);
    // A large rectangle deliberately exposes the old bug: maxLines only
    // toggled wrapping, so explicit newlines ignored even a one-line limit.
    for (int limit = 1; limit <= 4; ++limit)
    {
        Bitmap bitmap(240, 180, PixelFormat32bppARGB);
        {
            Graphics graphics(&bitmap);
            graphics.Clear(Color(255, 0, 0, 0));
            Check(ConfigureTileTextGraphics(graphics), "production graphics settings");
            Check(DrawSystemTileText(graphics, L"HH\nHH\nHH\nHH\nHH\nHH", font,
                RectF(0, 0, 240, 180), StringAlignmentNear, StringTrimmingEllipsisWord,
                limit, L"line limit smoke"), "line-limit rendering succeeds");
        }
        const int lines = InkLines(bitmap);
        Check(lines == limit, "explicit newlines obey maxLines=" + std::to_string(limit) +
            " (actual=" + std::to_string(lines) + ")");
    }
}

static void RunRenderMatrix(const std::filesystem::path& output)
{
    const tiletext::TileSize sizes[] = { tiletext::TileSize::Medium, tiletext::TileSize::Wide, tiletext::TileSize::Large };
    const double scales[] = { 0.8, 1.0, 1.25, 1.5, 1.8, 2.0, 4.0 };
    const int offsets[] = { 12, 176, 500 };
    Bitmap overview(822, 7 * 348 + 24, PixelFormat32bppARGB);
    {
        Graphics graphics(&overview);
        graphics.Clear(Color(255, 28, 30, 34));
    }
    for (int sample = 0; sample < 3; ++sample)
    for (double scale : scales)
    for (int sizeIndex = 0; sizeIndex < 3; ++sizeIndex)
    for (unsigned mask = 1; mask <= 7; ++mask)
    {
        double width = 0, height = 0;
        tiletext::TileSizeDimensions(sizes[sizeIndex], width, height);
        Bitmap bitmap(static_cast<INT>(std::lround(width * scale)), static_cast<INT>(std::lround(height * scale)), PixelFormat32bppARGB);
        const std::wstring text = sample == 0 ? L"Ag" : sample == 1 ?
            L"A long title with words that must wrap or truncate cleanly" : L"\x4e2d\x6587 \x65e5\x672c\x8a9e \x0627\x0644\x0639\x0631\x0628\x064a\x0629";
        std::wstring primary = mask & 1 ? text : L"";
        std::wstring secondary = mask & 2 ? text : L"";
        std::wstring badge = mask & 4 ? L"42" : L"";
        bool rendered = false;
        {
            Graphics graphics(&bitmap);
            graphics.Clear(Color(255, 20, 100, 110));
            Check(ConfigureTileTextGraphics(graphics), "production graphics settings");
            switch (sizes[sizeIndex])
            {
            case tiletext::TileSize::Medium: rendered = RenderMediumTileTextSimulation(graphics, primary, secondary, badge, scale); break;
            case tiletext::TileSize::Wide: rendered = RenderWideTileTextSimulation(graphics, primary, secondary, badge, scale); break;
            case tiletext::TileSize::Large: rendered = RenderLargeTileTextSimulation(graphics, primary, secondary, badge, scale); break;
            }
        }
        const std::string label = "size=" + std::to_string(sizeIndex) + " mask=" + std::to_string(mask) +
            " scale=" + std::to_string(scale) + " sample=" + std::to_string(sample);
        Check(rendered, label + " renders");
        tiletext::TileTextInputs inputs{ (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
        for (const auto& region : tiletext::ComputeTileTextLayout(sizes[sizeIndex], inputs))
        {
            Check(InkPixels(bitmap, static_cast<int>(region.x * scale), static_cast<int>(region.y * scale),
                static_cast<int>(std::ceil((region.x + region.width) * scale)),
                static_cast<int>(std::ceil((region.y + region.height) * scale))) > 0,
                label + " field=" + std::to_string(region.sourceField) + " has visible glyphs");
        }
        if (scale == 1 && sample == 1)
        {
            Graphics graphics(&overview);
            Check(graphics.DrawImage(&bitmap, offsets[sizeIndex], 28 + (mask - 1) * 348) == Ok, "contact sheet tile");
            auto family = CreateSystemTileFontFamily(L"Segoe UI");
            Font font(family.get(), 12, FontStyleRegular, UnitPixel);
            const std::wstring labelText = L"mask " + std::to_wstring(mask) + L" (primary=1 secondary=2 badge=4)";
            SolidBrush brush(Color(255, 220, 220, 220));
            if (sizeIndex == 0) graphics.DrawString(labelText.c_str(), -1, &font,
                PointF(12, static_cast<REAL>(8 + (mask - 1) * 348)), &brush);
        }
    }
    Check(SavePng(overview, output / L"tile-text-render-matrix.png"), "contact sheet saved");
}

static void CheckRegionLineCapacity()
{
    auto titleFamily = CreateSystemTileFontFamily(L"Segoe UI Light");
    auto bodyFamily = CreateSystemTileFontFamily(L"Segoe UI");
    for (double scale : { 0.8, 1.0, 1.25, 1.5, 1.8, 2.0, 4.0 })
    for (const auto& entry : tiletext::kTileTextSpec)
    {
        const auto& region = entry.region;
        const auto fonts = entry.size == tiletext::TileSize::Medium ? tiletext::MediumFontSizes() :
            entry.size == tiletext::TileSize::Wide ? tiletext::WideFontSizes() : tiletext::LargeFontSizes();
        const double em = region.role == tiletext::TextRole::Badge ? fonts.badge :
            region.role == tiletext::TextRole::Title ? fonts.title : fonts.body;
        Font font(region.role == tiletext::TextRole::Body ? bodyFamily.get() : titleFamily.get(),
            static_cast<REAL>(em * scale), FontStyleRegular, UnitPixel);
        Bitmap bitmap(static_cast<INT>(std::ceil(region.width * scale)),
            static_cast<INT>(std::ceil(region.height * scale)), PixelFormat32bppARGB);
        {
            Graphics graphics(&bitmap);
            graphics.Clear(Color(255, 0, 0, 0));
            Check(ConfigureTileTextGraphics(graphics), "region graphics settings");
            Check(DrawSystemTileText(graphics, L"H\nH\nH\nH\nH\nH", font,
                RectF(0, 0, static_cast<REAL>(region.width * scale), static_cast<REAL>(region.height * scale)),
                ToGdiAlignment(region.align), ToGdiTrimming(region.trim), region.documentedMaxLines,
                L"region line capacity"), "region line capacity renders");
        }
        const int actual = InkLines(bitmap);
        Check(actual == region.documentedMaxLines, "region fits declared line count size=" +
            std::to_string(static_cast<int>(entry.size)) + " mask=" + std::to_string(entry.inputMask) +
            " field=" + std::to_string(region.sourceField) + " scale=" + std::to_string(scale) +
            " expected=" + std::to_string(region.documentedMaxLines) + " actual=" + std::to_string(actual));
    }
}

static void CheckRepeatedRenderResources()
{
    // Run after the matrix has warmed GDI+/font caches. These counters catch
    // leaking GDI/USER handles; they are not a claim of a full heap leak audit.
    const DWORD beforeGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD beforeUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 600; ++i)
    {
        const double scale = i % 2 == 0 ? 1.0 : 1.8;
        Bitmap bitmap(static_cast<INT>(310 * scale), static_cast<INT>(310 * scale), PixelFormat32bppARGB);
        Graphics graphics(&bitmap);
        graphics.Clear(Color(255, 20, 100, 110));
        Check(ConfigureTileTextGraphics(graphics), "stress graphics settings");
        const std::wstring text = L"Source cycle " + std::to_wstring(i);
        bool result = i % 3 == 0 ? RenderMediumTileTextSimulation(graphics, text, L"Details", L"42", scale) :
            i % 3 == 1 ? RenderWideTileTextSimulation(graphics, text, L"Details", L"42", scale) :
            RenderLargeTileTextSimulation(graphics, text, L"Details", L"42", scale);
        Check(result, "repeated render " + std::to_string(i));
    }
    const DWORD afterGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD afterUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    Check(afterGdi <= beforeGdi + 8, "600 renders keep GDI handles bounded");
    Check(afterUser <= beforeUser + 8, "600 renders keep USER handles bounded");
    std::printf("Render stress handles: GDI %lu -> %lu, USER %lu -> %lu\n", beforeGdi, afterGdi, beforeUser, afterUser);
}

static void CheckOverlayBackgroundPreservation()
{
    const tiletext::TileSize sizes[] = { tiletext::TileSize::Medium, tiletext::TileSize::Wide, tiletext::TileSize::Large };
    for (const auto size : sizes)
    for (unsigned mask = 1; mask <= 7; ++mask)
    for (double scale : { 0.8, 1.0, 1.25, 1.5, 1.8, 2.0, 4.0 })
    for (bool preserve : { false, true })
    {
        double logicalWidth = 0, logicalHeight = 0;
        tiletext::TileSizeDimensions(size, logicalWidth, logicalHeight);
        const INT width = static_cast<INT>(std::lround(logicalWidth * scale));
        const INT height = static_cast<INT>(std::lround(logicalHeight * scale));
        Bitmap bitmap(width, height, PixelFormat32bppARGB);
        const Color topColor(255, 20, 100, 110), bottomColor(255, 90, 35, 60);
        const std::wstring primary = mask & 1 ? L"Primary" : L"";
        const std::wstring secondary = mask & 2 ? L"Secondary" : L"";
        const std::wstring badge = mask & 4 ? L"42" : L"";
        {
            Graphics graphics(&bitmap);
            graphics.Clear(topColor);
            SolidBrush bottom(bottomColor);
            graphics.FillRectangle(&bottom, 0, height / 2, width, height - height / 2);
            Check(ConfigureTileTextGraphics(graphics), "overlay graphics settings");
            bool rendered = size == tiletext::TileSize::Medium ? RenderMediumTileTextSimulation(graphics, primary, secondary, badge, scale, preserve) :
                size == tiletext::TileSize::Wide ? RenderWideTileTextSimulation(graphics, primary, secondary, badge, scale, preserve) :
                RenderLargeTileTextSimulation(graphics, primary, secondary, badge, scale, preserve);
            Check(rendered, "overlay/preset rendering succeeds");
        }
        const std::string label = "background size=" + std::to_string(static_cast<int>(size)) + " mask=" +
            std::to_string(mask) + " scale=" + std::to_string(scale) + " preserve=" + std::to_string(preserve);
        // All text rectangles have horizontal margins. Both vertical edges
        // sample the entire image, including the old wide bottom-band fill.
        for (INT y = 0; y < height; y += std::max(1, height / 11))
        for (INT x : { 0, width - 1 })
        {
            Color pixel;
            Check(bitmap.GetPixel(x, y, &pixel) == Ok, "read background sample");
            const Color original = y < height / 2 ? topColor : bottomColor;
            const bool nativeWideImageArea = size == tiletext::TileSize::Wide && !(mask & 4) &&
                y < static_cast<INT>(100 * scale);
            const bool shouldKeep = preserve || nativeWideImageArea;
            Check((pixel.GetValue() == original.GetValue()) == shouldKeep,
                label + " image preserved only when Overlay or native image region");
        }
        tiletext::TileTextInputs inputs{ (mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0 };
        for (const auto& region : tiletext::ComputeTileTextLayout(size, inputs))
            Check(InkPixels(bitmap, static_cast<int>(region.x * scale), static_cast<int>(region.y * scale),
                static_cast<int>(std::ceil((region.x + region.width) * scale)),
                static_cast<int>(std::ceil((region.y + region.height) * scale))) > 0,
                label + " overlay text remains visible");
    }
}

static int ProbeDeterminism(const std::filesystem::path& output, bool varyRounding = false, bool varyDpi = false, bool varyHint = false, bool varyContrast = false, bool quiet = false)
{
    const Color previousBackground = g_testBackground;
    g_testBackground = Color(255, 0, 120, 215);
    unsigned initialControl = 0;
    _controlfp_s(&initialControl, 0, 0);
    std::vector<ARGB> baseline;
    for (int iteration = 0; iteration < 16; ++iteration)
    {
        unsigned control = 0;
        const unsigned roundModes[] = { _RC_NEAR, _RC_DOWN, _RC_UP, _RC_CHOP };
        _controlfp_s(&control, varyRounding ? roundModes[iteration % 4] : initialControl, _MCW_RC);
        Bitmap bitmap(150, 150, PixelFormat32bppARGB);
        if (varyDpi) {
            const REAL resolutions[] = { 96.0f, 95.986595f, 96.012001f, 120.0f };
            bitmap.SetResolution(resolutions[iteration % 4], resolutions[iteration % 4]);
        }
        UINT contrast = 0;
        REAL dpi = 0;
        {
            Graphics graphics(&bitmap);
            graphics.Clear(Color(255, 10, 30, 50));
            Check(ConfigureTileTextGraphics(graphics), "probe graphics configuration");
            if (varyContrast) graphics.SetTextContrast(iteration % 13);
            if (varyHint) {
                const TextRenderingHint hints[] = { TextRenderingHintAntiAliasGridFit, TextRenderingHintAntiAlias,
                    TextRenderingHintClearTypeGridFit, TextRenderingHintSingleBitPerPixelGridFit };
                graphics.SetTextRenderingHint(hints[iteration % 4]);
            }
            contrast = graphics.GetTextContrast(); dpi = graphics.GetDpiY();
            Check(RenderMediumTileTextSimulation(graphics, L"AAAA", iteration % 2 ? L"MMMM" : L"IIII", L"7", 1),
                "probe medium rendering");
        }
        std::vector<ARGB> pixels;
        std::uint64_t hash = 14695981039346656037ull;
        int changed = 0;
        for (int y = 0; y < 150; ++y) for (int x = 0; x < 150; ++x) {
            Color color;
            bitmap.GetPixel(x, y, &color);
            const auto value = color.GetValue();
            if (!baseline.empty() && baseline[pixels.size()] != value) ++changed;
            pixels.push_back(value);
            hash = (hash ^ value) * 1099511628211ull;
        }
        Color glyph;
        bitmap.GetPixel(20, 93, &glyph);
        if (!varyRounding && !varyDpi && !varyHint && !varyContrast)
            Check(changed == 0, "ignored medium secondary text preserves every output pixel");
        if (!quiet) std::printf("probe iteration=%d hash=%016llX changed=%d glyph=%08X contrast=%u dpi=%.6f fp=%08X\n",
            iteration, static_cast<unsigned long long>(hash), changed, glyph.GetValue(), contrast, dpi, control);
        if (iteration == 0 || changed) SavePng(bitmap, output / (L"probe-" + std::to_wstring(iteration) + L".png"));
        if (baseline.empty()) baseline = std::move(pixels);
    }
    unsigned restored = 0;
    _controlfp_s(&restored, initialControl, _MCW_RC);
    g_testBackground = previousBackground;
    return failures == 0 ? 0 : 1;
}

int wmain(int argc, wchar_t** argv)
{
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) return 2;
    const std::filesystem::path output = argc > 1 ? argv[1] : L"build/tile-render-smoke";
    std::filesystem::create_directories(output);
    if (argc > 2 && (std::wstring(argv[2]) == L"--determinism-probe" || std::wstring(argv[2]) == L"--rounding-probe" || std::wstring(argv[2]) == L"--dpi-probe" || std::wstring(argv[2]) == L"--hint-probe" || std::wstring(argv[2]) == L"--contrast-probe" || std::wstring(argv[2]) == L"--warmup-probe")) {
        if (std::wstring(argv[2]) == L"--warmup-probe") RunRenderMatrix(output);
        const int status = ProbeDeterminism(output, std::wstring(argv[2]) == L"--rounding-probe", std::wstring(argv[2]) == L"--dpi-probe", std::wstring(argv[2]) == L"--hint-probe", std::wstring(argv[2]) == L"--contrast-probe");
        GdiplusShutdown(token);
        return status;
    }
    CheckLineLimits();
    RunRenderMatrix(output);
    CheckRegionLineCapacity();
    CheckRepeatedRenderResources();
    CheckOverlayBackgroundPreservation();
    ProbeDeterminism(output, false, false, false, false, true);
    GdiplusShutdown(token);
    std::printf("TileText GDI+ render smoke: %d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

// -----------------------------------------------------------------------------
// Lazy GDI+ lifetime
// -----------------------------------------------------------------------------
#include "../media_tile_layout.h"
#include "../prayer_tile_layout.h"
#include "../rss_tile_layout.h"
static std::mutex g_gdiPlusMutex;
static ULONG_PTR g_gdiPlusToken = 0;
static unsigned int g_gdiPlusUseCount = 0;

static bool AcquireGdiPlus()
{
    std::lock_guard<std::mutex> lk(g_gdiPlusMutex);

    if (g_gdiPlusToken == 0)
    {
        GdiplusStartupInput input{};
        ULONG_PTR token = 0;
        Status status = GdiplusStartup(&token, &input, nullptr);
        if (status != Ok)
        {
            LogText(g_ui.gdiPlusStartupFailedMessage);
            return false;
        }
        g_gdiPlusToken = token;
    }

    ++g_gdiPlusUseCount;
    return true;
}

static void ReleaseGdiPlus()
{
    bool released = false;
    {
        std::lock_guard<std::mutex> lk(g_gdiPlusMutex);
        if (g_gdiPlusUseCount > 0)
            --g_gdiPlusUseCount;

        if (g_gdiPlusUseCount == 0 && g_gdiPlusToken != 0 && !g_keepGdiPlusLoaded.load())
        {
            ULONG_PTR token = g_gdiPlusToken;
            g_gdiPlusToken = 0;
            GdiplusShutdown(token);
            released = true;
        }
    }

    if (released)
        TrimIdleWorkingSetIfConfigured();
}

static void ReleaseIdleGdiPlusIfAllowed()
{
    bool released = false;
    {
        std::lock_guard<std::mutex> lk(g_gdiPlusMutex);
        if (g_gdiPlusUseCount == 0 && g_gdiPlusToken != 0 && !g_keepGdiPlusLoaded.load())
        {
            ULONG_PTR token = g_gdiPlusToken;
            g_gdiPlusToken = 0;
            GdiplusShutdown(token);
            released = true;
        }
    }

    if (released)
        TrimIdleWorkingSetIfConfigured();
}

static void ShutdownGdiPlusForExit()
{
    std::lock_guard<std::mutex> lk(g_gdiPlusMutex);
    if (g_gdiPlusToken != 0)
    {
        ULONG_PTR token = g_gdiPlusToken;
        g_gdiPlusToken = 0;
        g_gdiPlusUseCount = 0;
        GdiplusShutdown(token);
    }
}

struct ScopedGdiPlus
{
    bool acquired = false;

    ScopedGdiPlus()
        : acquired(AcquireGdiPlus())
    {
    }

    ~ScopedGdiPlus()
    {
        if (acquired)
            ReleaseGdiPlus();
    }

    bool ok() const { return acquired; }

    ScopedGdiPlus(const ScopedGdiPlus&) = delete;
    ScopedGdiPlus& operator=(const ScopedGdiPlus&) = delete;
};

// -----------------------------------------------------------------------------
// GDI+ PNG save helpers
// -----------------------------------------------------------------------------
static CLSID g_png{};
static std::mutex g_pngMutex;
bool FindPngEncoder()
{
    std::lock_guard<std::mutex> lk(g_pngMutex);
    if (g_png.Data1) return true;

    UINT n=0, sz=0;
    if (GetImageEncodersSize(&n, &sz) != Ok || !sz) return false;
    auto buffer = std::make_unique<BYTE[]>(sz);
    auto* p = reinterpret_cast<ImageCodecInfo*>(buffer.get());

    if (GetImageEncoders(n, sz, p) == Ok)
        for (UINT i=0;i<n;i++)
            if (wcscmp(p[i].MimeType, L"image/png")==0)
                g_png = p[i].Clsid;

    return g_png.Data1 != 0;
}

static bool GdiOk(Status status, const wchar_t* operation)
{
    if (status == Ok)
        return true;

    Log(g_ui.gdiPlusOperationFailed.c_str(), operation ? operation : L"operation", static_cast<int>(status));
    return false;
}

static bool BitmapOk(Bitmap* bitmap, const wchar_t* operation)
{
    return bitmap && GdiOk(bitmap->GetLastStatus(), operation);
}

static bool GraphicsOk(Graphics& graphics, const wchar_t* operation)
{
    return GdiOk(graphics.GetLastStatus(), operation);
}

static bool BitmapDimensionsToInt(Bitmap* bitmap, int& width, int& height)
{
    if (!bitmap)
        return false;

    UINT bitmapWidth = bitmap->GetWidth();
    UINT bitmapHeight = bitmap->GetHeight();
    if (bitmapWidth == 0 || bitmapHeight == 0 || bitmapWidth > INT_MAX || bitmapHeight > INT_MAX)
        return false;

    width = static_cast<int>(bitmapWidth);
    height = static_cast<int>(bitmapHeight);
    return true;
}

struct WallpaperBitmapLoadInfo
{
    UINT originalWidth = 0;
    UINT originalHeight = 0;
    UINT loadedWidth = 0;
    UINT loadedHeight = 0;
    UINT requestedWidth = 0;
    UINT requestedHeight = 0;
    int maxLongEdge = 0;
    bool usedWic = false;
    bool attemptedWic = false;
    bool downscaled = false;
    bool memoryConstrained = false;
};

struct ComApartmentScope
{
    HRESULT hr = E_FAIL;
    bool uninitialize = false;

    explicit ComApartmentScope(DWORD flags = COINIT_MULTITHREADED)
    {
        hr = CoInitializeEx(nullptr, flags);
        uninitialize = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE)
            hr = S_OK;
    }

    ~ComApartmentScope()
    {
        if (uninitialize)
            CoUninitialize();
    }

    bool ok() const { return SUCCEEDED(hr); }
};

template <typename T>
struct ComReleaser
{
    void operator()(T* p) const noexcept
    {
        if (p)
            p->Release();
    }
};

template <typename T>
using UniqueComPtr = std::unique_ptr<T, ComReleaser<T>>;

static UINT ScaleDimensionForLongEdge(UINT value, UINT sourceLongEdge, UINT targetLongEdge)
{
    if (value == 0 || sourceLongEdge == 0 || targetLongEdge == 0 || sourceLongEdge <= targetLongEdge)
        return value;

    unsigned long long scaled =
        (static_cast<unsigned long long>(value) * targetLongEdge + sourceLongEdge / 2u) /
        sourceLongEdge;
    if (scaled < 1)
        scaled = 1;
    if (scaled > static_cast<unsigned long long>(UINT_MAX))
        scaled = UINT_MAX;
    return static_cast<UINT>(scaled);
}

static bool TryWicDecodeByteCount(UINT width, UINT height, size_t& bytes)
{
    bytes = 0;
    if (width == 0 || height == 0)
        return false;

    const size_t stride = static_cast<size_t>(width) * 4u;
    if (stride / 4u != static_cast<size_t>(width))
        return false;
    if (static_cast<size_t>(height) > static_cast<size_t>(-1) / stride)
        return false;

    bytes = stride * static_cast<size_t>(height);
    return true;
}

static bool ConstrainWicDecodeDimensionsToMemory(UINT& width, UINT& height)
{
    size_t bytes = 0;
    if (TryWicDecodeByteCount(width, height, bytes) && bytes <= static_cast<size_t>(INT_MAX))
        return false;

    const long double maxPixels = static_cast<long double>(INT_MAX) / 4.0L;
    const long double currentPixels = static_cast<long double>(width) * static_cast<long double>(height);
    if (currentPixels <= 0.0L)
        return false;

    long double scale = std::sqrt(maxPixels / currentPixels);
    if (!(scale > 0.0L) || scale >= 1.0L)
        return false;

    long double scaledWidth = std::floor(static_cast<long double>(width) * scale);
    long double scaledHeight = std::floor(static_cast<long double>(height) * scale);
    UINT constrainedWidth = scaledWidth < 1.0L ? 1u : static_cast<UINT>(scaledWidth);
    UINT constrainedHeight = scaledHeight < 1.0L ? 1u : static_cast<UINT>(scaledHeight);

    while ((!TryWicDecodeByteCount(constrainedWidth, constrainedHeight, bytes) ||
        bytes > static_cast<size_t>(INT_MAX)) &&
        (constrainedWidth > 1 || constrainedHeight > 1))
    {
        if (constrainedWidth >= constrainedHeight && constrainedWidth > 1)
            --constrainedWidth;
        else if (constrainedHeight > 1)
            --constrainedHeight;
        else
            break;
    }

    if (!TryWicDecodeByteCount(constrainedWidth, constrainedHeight, bytes) ||
        bytes == 0 || bytes > static_cast<size_t>(INT_MAX))
        return false;

    width = constrainedWidth;
    height = constrainedHeight;
    return true;
}

static std::unique_ptr<Bitmap> TryLoadWicBitmapForRender(
    const std::wstring& path,
    int maxLongEdge,
    WallpaperBitmapLoadInfo& info)
{
    info = WallpaperBitmapLoadInfo{};
    info.maxLongEdge = maxLongEdge;
    if (path.empty() || maxLongEdge <= 0)
        return nullptr;

    ComApartmentScope apartment;
    if (!apartment.ok())
        return nullptr;

    IWICImagingFactory* rawFactory = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&rawFactory));
    if (FAILED(hr) || !rawFactory)
        return nullptr;
    UniqueComPtr<IWICImagingFactory> factory(rawFactory);

    IWICBitmapDecoder* rawDecoder = nullptr;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(),
        nullptr,
        GENERIC_READ,
        WICDecodeMetadataCacheOnDemand,
        &rawDecoder);
    if (FAILED(hr) || !rawDecoder)
        return nullptr;
    UniqueComPtr<IWICBitmapDecoder> decoder(rawDecoder);

    IWICBitmapFrameDecode* rawFrame = nullptr;
    hr = decoder->GetFrame(0, &rawFrame);
    if (FAILED(hr) || !rawFrame)
        return nullptr;
    UniqueComPtr<IWICBitmapFrameDecode> frame(rawFrame);

    UINT srcW = 0;
    UINT srcH = 0;
    hr = frame->GetSize(&srcW, &srcH);
    if (FAILED(hr) || srcW == 0 || srcH == 0 || srcW > static_cast<UINT>(INT_MAX) || srcH > static_cast<UINT>(INT_MAX))
        return nullptr;

    info.originalWidth = srcW;
    info.originalHeight = srcH;

    UINT sourceLongEdge = std::max(srcW, srcH);
    UINT targetLongEdge = static_cast<UINT>(std::max(1, maxLongEdge));
    UINT dstW = ScaleDimensionForLongEdge(srcW, sourceLongEdge, targetLongEdge);
    UINT dstH = ScaleDimensionForLongEdge(srcH, sourceLongEdge, targetLongEdge);
    if (dstW == 0 || dstH == 0 || dstW > static_cast<UINT>(INT_MAX) || dstH > static_cast<UINT>(INT_MAX))
        return nullptr;

    info.requestedWidth = dstW;
    info.requestedHeight = dstH;
    info.memoryConstrained = ConstrainWicDecodeDimensionsToMemory(dstW, dstH);
    if (dstW == 0 || dstH == 0 || dstW > static_cast<UINT>(INT_MAX) || dstH > static_cast<UINT>(INT_MAX))
        return nullptr;

    IWICBitmapSource* source = frame.get();
    UniqueComPtr<IWICBitmapScaler> scaler;
    if (dstW != srcW || dstH != srcH)
    {
        IWICBitmapScaler* rawScaler = nullptr;
        hr = factory->CreateBitmapScaler(&rawScaler);
        if (FAILED(hr) || !rawScaler)
            return nullptr;
        scaler.reset(rawScaler);
        hr = scaler->Initialize(frame.get(), dstW, dstH, WICBitmapInterpolationModeFant);
        if (FAILED(hr))
            return nullptr;
        source = scaler.get();
        info.downscaled = true;
    }

    IWICFormatConverter* rawConverter = nullptr;
    hr = factory->CreateFormatConverter(&rawConverter);
    if (FAILED(hr) || !rawConverter)
        return nullptr;
    UniqueComPtr<IWICFormatConverter> converter(rawConverter);

    hr = converter->Initialize(
        source,
        GUID_WICPixelFormat32bppBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
        return nullptr;

    const UINT wicStride = dstW * 4u;
    size_t wicBytes = 0;
    if (!TryWicDecodeByteCount(dstW, dstH, wicBytes))
        return nullptr;
    if (wicBytes == 0 || wicBytes > static_cast<size_t>(INT_MAX))
        return nullptr;

    std::unique_ptr<Bitmap> bitmap(new Bitmap(static_cast<INT>(dstW), static_cast<INT>(dstH), PixelFormat32bppARGB));
    if (!BitmapOk(bitmap.get(), L"create WIC decoded wallpaper bitmap"))
        return nullptr;

    Rect lockRect(0, 0, static_cast<INT>(dstW), static_cast<INT>(dstH));
    BitmapData data{};
    if (!GdiOk(bitmap->LockBits(&lockRect, ImageLockModeWrite, PixelFormat32bppARGB, &data), L"lock WIC decoded wallpaper bitmap"))
        return nullptr;

    BYTE* base = static_cast<BYTE*>(data.Scan0);
    bool copied = base != nullptr && data.Stride != 0;
    if (copied)
    {
        const int absStride = data.Stride < 0 ? -data.Stride : data.Stride;
        if (absStride < static_cast<int>(wicStride))
        {
            copied = false;
        }
        else if (data.Stride > 0)
        {
            size_t copyBytes = static_cast<size_t>(absStride) * static_cast<size_t>(dstH);
            if (copyBytes == 0 || copyBytes > static_cast<size_t>(UINT_MAX))
            {
                copied = false;
            }
            else
            {
                hr = converter->CopyPixels(nullptr, static_cast<UINT>(absStride), static_cast<UINT>(copyBytes), base);
                copied = SUCCEEDED(hr);
            }
        }
        else
        {
            for (UINT y = 0; y < dstH; ++y)
            {
                WICRect rowRect{};
                rowRect.X = 0;
                rowRect.Y = static_cast<INT>(y);
                rowRect.Width = static_cast<INT>(dstW);
                rowRect.Height = 1;

                BYTE* dstRow = base + static_cast<size_t>(dstH - 1u - y) * static_cast<size_t>(absStride);
                hr = converter->CopyPixels(&rowRect, static_cast<UINT>(absStride), static_cast<UINT>(absStride), dstRow);
                if (FAILED(hr))
                {
                    copied = false;
                    break;
                }
            }
        }
    }

    Status unlockStatus = bitmap->UnlockBits(&data);
    if (!copied || unlockStatus != Ok)
        return nullptr;

    info.usedWic = true;
    info.loadedWidth = dstW;
    info.loadedHeight = dstH;
    return bitmap;
}

static std::unique_ptr<Bitmap> LoadWallpaperBitmapForRender(
    const std::wstring& path,
    int autoMaxLongEdge,
    WallpaperBitmapLoadInfo* loadInfo = nullptr)
{
    WallpaperBitmapLoadInfo info{};
    int configuredMaxLongEdge = IniReadClampedI(L"Settings", L"WallpaperDecodeMaxLongEdge", 0, 0, 32768);
    int maxLongEdge = configuredMaxLongEdge > 0 ? configuredMaxLongEdge : autoMaxLongEdge;
    info.maxLongEdge = maxLongEdge;

    if (IniReadI(L"Settings", L"WallpaperDecodeLowMemory", 1) != 0 && maxLongEdge > 0)
    {
        std::unique_ptr<Bitmap> wicBitmap = TryLoadWicBitmapForRender(path, maxLongEdge, info);
        info.attemptedWic = true;
        if (wicBitmap)
        {
            if (loadInfo)
                *loadInfo = info;
            return wicBitmap;
        }
    }

    std::unique_ptr<Bitmap> bitmap(new Bitmap(path.c_str()));
    if (bitmap && bitmap->GetLastStatus() == Ok)
    {
        UINT w = bitmap->GetWidth();
        UINT h = bitmap->GetHeight();
        info.originalWidth = w;
        info.originalHeight = h;
        info.loadedWidth = w;
        info.loadedHeight = h;
    }
    info.maxLongEdge = maxLongEdge;

    if (loadInfo)
        *loadInfo = info;
    return bitmap;
}

static bool TryParseHexNibble(wchar_t ch, BYTE& value)
{
    if (ch >= L'0' && ch <= L'9')
    {
        value = static_cast<BYTE>(ch - L'0');
        return true;
    }
    if (ch >= L'a' && ch <= L'f')
    {
        value = static_cast<BYTE>(10 + ch - L'a');
        return true;
    }
    if (ch >= L'A' && ch <= L'F')
    {
        value = static_cast<BYTE>(10 + ch - L'A');
        return true;
    }
    return false;
}

static bool TryParseHexByte(const std::wstring& text, size_t offset, BYTE& value)
{
    if (offset + 1 >= text.size())
        return false;
    BYTE hi = 0;
    BYTE lo = 0;
    if (!TryParseHexNibble(text[offset], hi) || !TryParseHexNibble(text[offset + 1], lo))
        return false;
    value = static_cast<BYTE>((hi << 4) | lo);
    return true;
}

static Color ParseTileColor(const std::wstring& raw, Color fallback)
{
    std::wstring value = TrimCopy(raw);
    if (!value.empty() && value.front() == L'#')
        value.erase(value.begin());

    BYTE a = 255;
    BYTE r = 255;
    BYTE g = 255;
    BYTE b = 255;

    if (value.size() == 8)
    {
        if (TryParseHexByte(value, 0, a) &&
            TryParseHexByte(value, 2, r) &&
            TryParseHexByte(value, 4, g) &&
            TryParseHexByte(value, 6, b))
            return Color(a, r, g, b);
    }
    else if (value.size() == 6)
    {
        if (TryParseHexByte(value, 0, r) &&
            TryParseHexByte(value, 2, g) &&
            TryParseHexByte(value, 4, b))
            return Color(255, r, g, b);
    }

    return fallback;
}

static std::wstring TileTextPrimaryText()
{
    if (g_renderContent) return g_renderContent->text.primary;
    return TrimCopy(IniReadS(L"TileText", L"Text", L""));
}

static std::wstring TileTextSecondaryText()
{
    if (g_renderContent) return g_renderContent->text.secondary;
    return TrimCopy(IniReadS(L"TileText", L"SecondaryText", L""));
}

static std::wstring TileTextBadgeText()
{
    if (g_renderContent) return g_renderContent->text.badge;
    return TrimCopy(IniReadS(L"TileText", L"BadgeText", L""));
}

static bool TileTextOverlayEnabled()
{
    if (g_renderContent)
        return g_renderContent->textMode != aip::content::TextMode::Off &&
            (!g_renderContent->text.primary.empty() || !g_renderContent->text.secondary.empty() || !g_renderContent->text.badge.empty());
    return IniReadI(L"TileText", L"Enabled", 0) != 0 &&
        (!TileTextPrimaryText().empty() ||
            !TileTextSecondaryText().empty() ||
            !TileTextBadgeText().empty());
}

static bool TileTextAppliesToTileName(const wchar_t* tileName)
{
    std::wstring name = tileName ? tileName : L"";
    if (IEquals(name, L"MediumTile") || IEquals(name, L"LiveMediumTile"))
        return IniReadI(L"TileText", L"ApplyToMediumTile", 1) != 0;
    if (IEquals(name, L"WideTile") || IEquals(name, L"LiveWideTile"))
        return IniReadI(L"TileText", L"ApplyToWideTile", 1) != 0;
    if (IEquals(name, L"LargeTile") || IEquals(name, L"LiveLargeTile"))
        return IniReadI(L"TileText", L"ApplyToLargeTile", 1) != 0;
    return false;
}

enum class SimulatedTileTextLayout
{
    None,
    Medium,
    Wide,
    Large
};

static SimulatedTileTextLayout SimulatedTileTextLayoutForName(const wchar_t* tileName)
{
    std::wstring name = tileName ? tileName : L"";
    if (IEquals(name, L"MediumTile") || IEquals(name, L"LiveMediumTile"))
        return SimulatedTileTextLayout::Medium;
    if (IEquals(name, L"WideTile") || IEquals(name, L"LiveWideTile"))
        return SimulatedTileTextLayout::Wide;
    if (IEquals(name, L"LargeTile") || IEquals(name, L"LiveLargeTile"))
        return SimulatedTileTextLayout::Large;
    return SimulatedTileTextLayout::None;
}

static Color SimulatedTileBackgroundColor()
{
    return ParseTileColor(EffectiveManifestBackgroundColor(), Color(255, 0, 120, 215));
}

static Color ContentCanvasBackgroundColor()
{
    return ContentHasTransparentBackground() || (g_renderContent && g_renderContent->galleryFrame)
        ? Color(0, 0, 0, 0) : Color(255, 0, 0, 0);
}

static bool PreserveContentBackgroundForText()
{
    return ContentHasTransparentBackground() ||
        (g_renderContent && g_renderContent->textMode == aip::content::TextMode::Overlay);
}

#include "tile_text_render.inc"
#include "adaptive_tile_text_render.inc"

static bool ShouldUseAdaptiveStaticTextFallback()
{
    return ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10 &&
        ConfiguredLiveTileTemplateStyle() == LiveTileTemplateStyle::Adaptive &&
        (!g_renderContent || g_renderContent->textMode != aip::content::TextMode::Off);
}
static std::wstring StaticSourceCornerTopic(const aip::content::Snapshot& frame,bool registration){
    if(!registration||frame.galleryFrame||frame.clockSource||frame.calendarSource||frame.mailSource||frame.textMode==aip::content::TextMode::Off||
        (frame.artworkTopic==L"desktop"&&frame.secondaryTileId.empty()))return {};
    if(frame.secondaryBranding!=L"Logo"&&frame.secondaryBranding!=L"NameAndLogo"&&frame.secondaryBranding!=L"Auto")return {};
    auto topic=frame.artworkTopic;if(topic.empty()&&frame.prayerSource)topic=L"prayer";
    if(topic.empty()&&frame.smtcSource)topic=L"smtc";if(topic.empty()&&!frame.rssHeadlines.empty())topic=L"rss";
    return aip::tile_visual::FindTopic(topic)?topic:std::wstring{};
}
static int SourceTextBottomInset(const aip::content::Snapshot& frame){return StaticSourceCornerTopic(frame,TileSnapshotUsesRegistration(frame)).empty()?20:26;}
static bool DrawStaticSourceCorner(Graphics& graphics,const aip::content::Snapshot& frame,int width,int height,double scale,bool registration){
    const auto topic=StaticSourceCornerTopic(frame,registration);if(topic.empty()||width<150||height<150)return true;
    Bitmap icon(44,44,PixelFormat32bppARGB);const auto color=SimulatedTileBackgroundColor();
    const bool dark=frame.registrationAppearance.background==L"Color"&&aip::tile_visual::DarkArtworkInk({true,(static_cast<std::uint32_t>(color.GetR())<<16)|(static_cast<std::uint32_t>(color.GetG())<<8)|color.GetB()});
    {Graphics canvas(&icon);canvas.Clear(Color(0,0,0,0));if(!aip::tile_visual::DrawCornerTopicGlyph(canvas,topic,44,44,dark?Color(255,20,20,20):Color(255,255,255,255)))return false;}
    const auto saved=graphics.Save();graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    const auto result=graphics.DrawImage(&icon,RectF(static_cast<REAL>((width-26)*scale),static_cast<REAL>((height-24)*scale),static_cast<REAL>(16*scale),static_cast<REAL>(16*scale)));
    graphics.Restore(saved);return result==Ok;
}

static bool DrawTopSourceField(Graphics& graphics, const std::wstring& text, Font& font,
    REAL x, REAL& y, REAL width, REAL bottom, int maxLines, BYTE alpha, REAL gap,
    const aip::content::TextPresentation* overridePresentation = nullptr,bool measureOnly=false)
{
    if (text.empty() || width <= 0 || y >= bottom) return true;
    std::unique_ptr<Font> presented;
    Font* selected = &font;
    const auto* presentation = overridePresentation ? overridePresentation :
        g_renderContent && g_renderContent->presentation.Active() ? &g_renderContent->presentation : nullptr;
    if (presentation) {
        const bool badge = !overridePresentation&&g_renderContent&&text == g_renderContent->text.badge && alpha < 255;
        const auto& style = alpha == 255 ? presentation->primaryStyle : badge ? presentation->badgeStyle : presentation->secondaryStyle;
        const REAL scale = font.GetSize() / 12;
        presented = std::make_unique<Font>(L"Segoe UI", static_cast<REAL>(aip::content::TextStylePixels(style)) * scale,
            style.compare(0,4,L"base") == 0 ? FontStyleBold : FontStyleRegular, UnitPixel);
        if (presented->GetLastStatus() != Ok) return false;
        selected = presented.get();
        maxLines = presentation->wrap ? (alpha == 255 ? presentation->primaryLines : badge ? presentation->badgeLines : presentation->secondaryLines) : 1;
        alpha = style.find(L"Subtle") == std::wstring::npos ? 255 : 153;
        gap += static_cast<REAL>(presentation->gapLines * 12) * scale;
    }
    const REAL lineHeight = selected->GetHeight(&graphics);
    if (lineHeight <= 0 || selected->GetLastStatus() != Ok) return false;
    const REAL height = std::min(bottom - y, lineHeight * maxLines + 0.01f);
    if (height < lineHeight && !presentation) return true;
    RectF bounds(x, y, width, height), measured;
    StringFormat format;
    format.SetAlignment(StringAlignmentNear); format.SetLineAlignment(StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord); format.SetFormatFlags(StringFormatFlagsLineLimit);
    if (presentation) {
        format.SetAlignment(presentation->align == L"center" ? StringAlignmentCenter : presentation->align == L"right" ? StringAlignmentFar : StringAlignmentNear);
        format.SetLineAlignment(presentation->stacking == L"center" ? StringAlignmentCenter : presentation->stacking == L"bottom" ? StringAlignmentFar : StringAlignmentNear);
        if (!presentation->wrap) format.SetFormatFlags(StringFormatFlagsLineLimit | StringFormatFlagsNoWrap);
        if (height < lineHeight) format.SetFormatFlags(presentation->wrap ? 0 : StringFormatFlagsNoWrap);
    }
    if (graphics.MeasureString(text.c_str(), static_cast<INT>(text.size()), selected, bounds, &format, &measured) != Ok) return false;
    SolidBrush brush(Color(alpha, 255, 255, 255));
    Status result=Ok;if(!measureOnly){const auto saved = graphics.Save();
        graphics.SetClip(bounds, CombineModeIntersect);
        result = graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), selected, bounds, &format, &brush);graphics.Restore(saved);}
    y += std::min(height, std::max(lineHeight, measured.Height)) + gap;
    return result == Ok;
}
static REAL PresentedTextOffset(const aip::content::Snapshot& frame, int height, double scale)
{
    if (!frame.presentation.Active() || frame.presentation.stacking == L"top") return 0;
    const auto& p = frame.presentation;
    const int estimate = aip::content::TextStylePixels(p.primaryStyle) * p.primaryLines +
        aip::content::TextStylePixels(p.secondaryStyle) * p.secondaryLines +
        (frame.text.badge.empty() ? 0 : aip::content::TextStylePixels(p.badgeStyle) * p.badgeLines) + p.gapLines * 24;
    const int free = (std::max)(0, height - 30 - estimate);
    return static_cast<REAL>(free * scale * (p.stacking == L"center" ? .5 : 1));
}

static bool RenderMediaTileSimulation(Graphics& graphics, const aip::content::Snapshot& snapshot,
    int logicalWidth, int logicalHeight, double scale)
{
    const auto extras = aip::content::MediaExtraTopics(snapshot);
    const bool mainIcon = snapshot.smtcIndicator != aip::content::MediaIndicatorMode::None;
    const auto layout = aip::content::ComputeMediaTileGeometry(logicalWidth, logicalHeight, mainIcon || !extras.empty(), extras.size());
    Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
    if (font.GetLastStatus() != Ok) return false;
    REAL y = static_cast<REAL>(layout.textY * scale) + PresentedTextOffset(snapshot, logicalHeight, scale);
    const REAL bottom = (std::min)(static_cast<REAL>((layout.textY + layout.textHeight) * scale),static_cast<REAL>((logicalHeight-SourceTextBottomInset(snapshot))*scale));
    const REAL x = static_cast<REAL>(layout.textX * scale), width = static_cast<REAL>(layout.textWidth * scale);
    if (!DrawTopSourceField(graphics, snapshot.text.primary, font, x, y, width, bottom, 2, 255, static_cast<REAL>(3 * scale)) ||
        !DrawTopSourceField(graphics, snapshot.text.secondary, font, x, y, width, bottom, 2, 153, static_cast<REAL>(3 * scale)) ||
        !DrawTopSourceField(graphics, snapshot.text.badge, font, x, y, width, bottom, 4, 153, 0)) return false;
    const RectF iconBounds(static_cast<REAL>(layout.iconX * scale), static_cast<REAL>(layout.iconY * scale),
        static_cast<REAL>(layout.iconSide * scale), static_cast<REAL>(layout.iconSide * scale));
    if (mainIcon) {
        bool appIconDrawn = false;
        if (snapshot.smtcIndicator == aip::content::MediaIndicatorMode::AppIcon && !snapshot.smtcAppIconPath.empty()) {
            Bitmap icon(snapshot.smtcAppIconPath.c_str());
            if (icon.GetLastStatus() == Ok && icon.GetWidth() <= 256 && icon.GetHeight() <= 256)
                appIconDrawn = graphics.DrawImage(&icon, iconBounds) == Ok;
        }
        if (!appIconDrawn && !aip::tile_visual::DrawTileTopicGlyph(graphics, aip::content::MediaStatusTopic(snapshot.smtcState), iconBounds)) return false;
    }
    const REAL side = iconBounds.Width;
    for (std::size_t index = 0; index < extras.size(); ++index) {
        RectF bounds(iconBounds.X, iconBounds.Y + static_cast<REAL>(index + (mainIcon ? 1 : 0)) *
            (side + static_cast<REAL>(4 * scale)), side, side);
        if (!aip::tile_visual::DrawTileTopicGlyph(graphics, extras[index], bounds)) return false;
    }
    return graphics.GetLastStatus() == Ok;
}

#include "ga_notes_tile_icons.inc"
#include "../game_tile_layout.h"
static bool RenderGameRowsSimulation(Graphics& graphics,const aip::content::Snapshot& snapshot,int width,int height,double scale){
    using namespace aip::content;auto presentation=GameTextPresentation(snapshot.presentation);const auto stacking=presentation.stacking;presentation.stacking=L"top";auto secondaryPresentation=presentation;secondaryPresentation.gapLines=0;
    const auto count=(std::min)(snapshot.detailRows.size(),GameTileRows(width,height));if(!count)return true;
    Font font(L"Segoe UI",static_cast<REAL>(12*scale),FontStyleRegular,UnitPixel);if(font.GetLastStatus()!=Ok)return false;
    const REAL margin=static_cast<REAL>(10*scale),bottom=static_cast<REAL>((height-25)*scale),spacing=static_cast<REAL>(presentation.rowSpacing*scale);
    struct Prepared {const Text* row;std::unique_ptr<Bitmap> icon;GameRowGeometry geometry;REAL height;};std::vector<Prepared> rows;REAL total=0;
    for(size_t index=0;index<count;++index){const auto& row=snapshot.detailRows[index];auto icon=presentation.iconPlacement==L"None"?std::unique_ptr<Bitmap>{}:LoadNotesTileIcon(row.iconTopic);
        const auto geometry=ComputeGameRowGeometry(width,icon!=nullptr,presentation);REAL y=0;const auto textWidth=static_cast<REAL>(geometry.textWidth*scale);
        if(!DrawTopSourceField(graphics,row.primary,font,0,y,textWidth,100000,2,255,static_cast<REAL>(2*scale),&presentation,true)||
            !DrawTopSourceField(graphics,row.secondary,font,0,y,textWidth,100000,2,153,0,&secondaryPresentation,true))return false;
        const REAL rowHeight=(std::max)(y,static_cast<REAL>(geometry.iconSide*scale));const REAL required=total+(rows.empty()?0:spacing)+rowHeight;
        if(required>bottom-margin+.01f)break;total=required;rows.push_back({&row,std::move(icon),geometry,rowHeight});
    }
    REAL y=margin;if(stacking!=L"top")y+=(std::max)(0.0f,bottom-margin-total)*(stacking==L"bottom"?1.0f:.5f);
    for(const auto& row:rows){REAL textY=y;const auto x=static_cast<REAL>(row.geometry.textX*scale),textWidth=static_cast<REAL>(row.geometry.textWidth*scale);
        if(!DrawTopSourceField(graphics,row.row->primary,font,x,textY,textWidth,y+row.height,2,255,static_cast<REAL>(2*scale),&presentation)||
            !DrawTopSourceField(graphics,row.row->secondary,font,x,textY,textWidth,y+row.height,2,153,0,&secondaryPresentation))return false;
        if(row.icon){const REAL side=static_cast<REAL>(row.geometry.iconSide*scale),ratio=(std::min)(side/row.icon->GetWidth(),side/row.icon->GetHeight());
            const REAL iconWidth=row.icon->GetWidth()*ratio,iconHeight=row.icon->GetHeight()*ratio;
            if(graphics.DrawImage(row.icon.get(),RectF(static_cast<REAL>(row.geometry.iconX*scale)+(side-iconWidth)*.5f,y+(row.height-iconHeight)*.5f,iconWidth,iconHeight))!=Ok)return false;}
        y+=row.height+spacing;
    }return graphics.GetLastStatus()==Ok;
}
static bool RenderDetailRowsSimulation(Graphics& graphics, const aip::content::Snapshot& snapshot,
    int logicalWidth, int logicalHeight, double scale)
{
    if(snapshot.gameSource)return RenderGameRowsSimulation(graphics,snapshot,logicalWidth,logicalHeight,scale);
    const auto count = std::min(snapshot.detailRows.size(), aip::content::DetailTileRows(logicalHeight));
    if (!count) return true;
    Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
    if (font.GetLastStatus() != Ok) return false;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& row = snapshot.detailRows[index];
        auto icon = LoadNotesTileIcon(row.iconTopic);
        const auto fallback = !icon && (!row.iconTopic.empty() || index == 0) ? snapshot.detailTopic : std::wstring{};
        const bool hasIcon = icon || !fallback.empty();
        const auto geometry = aip::content::ComputeDetailRowGeometry(logicalWidth, logicalHeight, count, index, hasIcon);
        REAL y = static_cast<REAL>(geometry.textY * scale);
        const REAL x = static_cast<REAL>(geometry.textX * scale), width = static_cast<REAL>(geometry.textWidth * scale), bottom = static_cast<REAL>(geometry.bottom * scale);
        if (!DrawTopSourceField(graphics, row.primary, font, x, y, width, bottom, 1, 255, static_cast<REAL>(2 * scale)) ||
            !DrawTopSourceField(graphics, row.secondary, font, x, y, width, bottom, 1, 153, 0)) return false;
        const RectF bounds(static_cast<REAL>(geometry.iconX * scale), static_cast<REAL>(geometry.iconY * scale),
            static_cast<REAL>(geometry.iconSide * scale), static_cast<REAL>(geometry.iconSide * scale));
        if (icon && graphics.DrawImage(icon.get(), bounds) != Ok) return false;
        if (!icon && hasIcon && !aip::tile_visual::DrawTileTopicGlyph(graphics, fallback, bounds)) return false;
    }
    return graphics.GetLastStatus() == Ok;
}

static bool DrawPrayerClockField(Graphics& graphics, const aip::content::Snapshot& snapshot,
    const std::wstring& text, const wchar_t* defaultStyle, unsigned role,
    REAL x, REAL& y, REAL width, REAL bottom, int maxLines, double scale, bool right = false,bool center=false,bool measureOnly=false,bool nativeWrap=true)
{
    if (text.empty()) return true;
    std::wstring style = defaultStyle;
    const auto& presentation = snapshot.presentation;
    if (presentation.Active() && role < 3) {
        style = role == 0 ? presentation.primaryStyle : role == 1 ? presentation.secondaryStyle : presentation.badgeStyle;
        maxLines = presentation.wrap ? (role == 0 ? presentation.primaryLines : role == 1 ? presentation.secondaryLines : presentation.badgeLines) : 1;
        if (role) y += static_cast<REAL>(presentation.gapLines * 12 * scale);
    }
    if (width <= 0 || y >= bottom) return true;
    const bool light = style.compare(0,5,L"title") == 0 || style.compare(0,9,L"subheader") == 0 || style.compare(0,6,L"header") == 0;
    const bool clockBase=snapshot.clockSource&&style==L"base";
    Font font(clockBase?L"Segoe UI Semibold":light ? L"Segoe UI Light" : L"Segoe UI", static_cast<REAL>(aip::content::TextStylePixels(style) * scale),
        !clockBase&&style.compare(0,4,L"base") == 0 ? FontStyleBold : FontStyleRegular, UnitPixel);
    const REAL lineHeight = font.GetHeight(&graphics);
    if (font.GetLastStatus() != Ok || lineHeight <= 0) return false;
    const REAL available = (std::min)(bottom - y, lineHeight * maxLines + .01f);
    RectF bounds(x,y,width,available), measured;
    StringFormat format;
    format.SetAlignment(center?StringAlignmentCenter:right ? StringAlignmentFar : StringAlignmentNear);
    format.SetTrimming(StringTrimmingEllipsisWord); format.SetFormatFlags(StringFormatFlagsLineLimit);
    if (presentation.Active()) {
        if (!right) format.SetAlignment(presentation.align == L"center" ? StringAlignmentCenter : presentation.align == L"right" ? StringAlignmentFar : StringAlignmentNear);
        if (!presentation.wrap) format.SetFormatFlags(StringFormatFlagsLineLimit | StringFormatFlagsNoWrap);
    }else if(!nativeWrap)format.SetFormatFlags(StringFormatFlagsLineLimit|StringFormatFlagsNoWrap);
    if (graphics.MeasureString(text.c_str(),static_cast<INT>(text.size()),&font,bounds,&format,&measured) != Ok) return false;
    SolidBrush brush(Color(style.find(L"Subtle") == std::wstring::npos ? 255 : 153,255,255,255));
    Status result=Ok;if(!measureOnly){const auto saved = graphics.Save(); graphics.SetClip(bounds,CombineModeIntersect);
        result = graphics.DrawString(text.c_str(),static_cast<INT>(text.size()),&font,bounds,&format,&brush);graphics.Restore(saved);}
    y += (std::min)(available,(std::max)(lineHeight,measured.Height)) + static_cast<REAL>(2 * scale);
    return result == Ok;
}
static bool RenderMailTileSimulation(Graphics& graphics,const aip::content::Snapshot& snapshot,int width,int height,double scale)
{
    const auto rows=aip::content::BuildMailTilePlan(snapshot,width,height);
    REAL y=static_cast<REAL>(10*scale)+PresentedTextOffset(snapshot,height,scale);
    const REAL margin=static_cast<REAL>(10*scale),bottom=static_cast<REAL>((height-30)*scale),textWidth=static_cast<REAL>((width-20)*scale);
    for(const auto& row:rows){
        for(const auto* field:{&row.primary,&row.secondary})
            if(!DrawPrayerClockField(graphics,snapshot,field->text,field->style.c_str(),static_cast<unsigned>(field->role),
                margin,y,textWidth,bottom,field->lines,scale,false,false,false,field->wrap))return false;
        y+=static_cast<REAL>(6*scale);
    }
    return true;
}
static bool RenderPrayerTileSimulation(Graphics& graphics, const aip::content::Snapshot& snapshot,
    int width, int height, double scale)
{
    if (snapshot.prayerClock.Available()) {
        const auto& clock = snapshot.prayerClock;
        auto layout = aip::content::ComputePrayerClockGeometry(width,height,clock,snapshot.detailRows);
        layout.bottom=(std::min)(layout.bottom,height-SourceTextBottomInset(snapshot));layout.clockBottom=(std::min)(layout.clockBottom,layout.bottom);
        const REAL margin = static_cast<REAL>(layout.margin * scale), bottom = static_cast<REAL>(layout.bottom * scale);
        const REAL mainWidth = static_cast<REAL>(layout.mainWidth * scale), headerBottom = static_cast<REAL>(layout.clockBottom * scale);
        REAL y = margin, countY = margin; unsigned role = 0;
        if (!DrawPrayerClockField(graphics,snapshot,clock.time,aip::content::PrayerTimeStyle(layout.countdownColumn),role,
                margin,y,mainWidth,headerBottom,1,scale) ||
            !DrawPrayerClockField(graphics,snapshot,clock.prayer,L"captionSubtle",1,margin,y,mainWidth,headerBottom,1,scale) ||
            !DrawPrayerClockField(graphics,snapshot,aip::content::PrayerLocationText(clock),L"captionSubtle",1,
                margin,y,mainWidth,headerBottom,2,scale)) return false;
        if (layout.countdownColumn) {
            role = 0; // Adaptive subgroup text roles restart in each column.
            const REAL countX = static_cast<REAL>(layout.countdownX * scale), countWidth = static_cast<REAL>(layout.countdownWidth * scale);
            if (!DrawPrayerClockField(graphics,snapshot,L"minutes",L"captionSubtle",3,countX,countY,countWidth,headerBottom,1,scale,true) ||
                !DrawPrayerClockField(graphics,snapshot,clock.minutes,aip::content::PrayerCountdownStyle(clock),2,countX,countY,countWidth,headerBottom,1,scale,true)) return false;
            role = 2;
            y = (std::max)(y,countY);
        } else if (!DrawPrayerClockField(graphics,snapshot,clock.countdown,L"caption",2,margin,y,mainWidth,headerBottom,1,scale)) return false;
        const REAL fullWidth = static_cast<REAL>((width - 2 * layout.margin) * scale);
        if (!DrawPrayerClockField(graphics,snapshot,clock.details,L"captionSubtle",1,margin,y,fullWidth,headerBottom,2,scale)) return false;
        if (layout.schedule) {
            y = (std::min)(y,headerBottom);
            const REAL rowHeight = (bottom - y) / static_cast<REAL>(snapshot.detailRows.size());
            for (const auto& row : snapshot.detailRows) {
                REAL labelY = y, valueY = y;
                role = 1;
                if (!DrawPrayerClockField(graphics,snapshot,row.primary,L"caption",role,margin,labelY,fullWidth * .6f,y + rowHeight,1,scale)) return false;
                role = 1;
                if (!DrawPrayerClockField(graphics,snapshot,row.secondary,L"caption",role,margin + fullWidth * .6f,valueY,fullWidth * .4f,y + rowHeight,1,scale,true)) return false;
                y += rowHeight;
            }
        }
        return graphics.GetLastStatus() == Ok;
    }
    Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
    if (font.GetLastStatus() != Ok) return false;
    const REAL margin = static_cast<REAL>(10 * scale), textWidth = static_cast<REAL>((width - 20) * scale);
    const REAL bottom = static_cast<REAL>((height - SourceTextBottomInset(snapshot)) * scale);
    const bool full = aip::content::PrayerTileShowsSchedule(height, snapshot.detailRows, snapshot.prayerClock);
    const REAL headerBottom = snapshot.presentation.Active() && full ? bottom - (bottom - margin) * .65f : bottom;
    REAL y = margin;
    if (!DrawTopSourceField(graphics, snapshot.text.primary, font, margin, y, textWidth, headerBottom, 2, 255, static_cast<REAL>(2 * scale)) ||
        !DrawTopSourceField(graphics, snapshot.text.secondary, font, margin, y, textWidth, headerBottom, 1, 180, static_cast<REAL>(2 * scale)) ||
        !DrawTopSourceField(graphics, snapshot.text.badge, font, margin, y, textWidth, headerBottom, 2, 180, static_cast<REAL>(3 * scale))) return false;
    if (full) {
        if (snapshot.presentation.Active()) y = (std::min)(y,headerBottom);
        // Reserve equal space for all five obligatory prayers; Sunrise is in
        // the header, never a sixth row that can push Isha out of the tile.
        const REAL rowHeight = (bottom - y) / static_cast<REAL>(snapshot.detailRows.size());
        for (const auto& row : snapshot.detailRows) {
            REAL rowY = y, valueY = y;
            if (!DrawTopSourceField(graphics, row.primary, font, margin, rowY, textWidth * .6f, y + rowHeight, 1, 255, 0) ||
                !DrawTopSourceField(graphics, row.secondary, font, margin + textWidth * .6f, valueY, textWidth * .4f, y + rowHeight, 1, 255, 0)) return false;
            y += rowHeight;
        }
    }
    return graphics.GetLastStatus() == Ok;
}
#include "../datetime_tile_layout.h"
static bool RenderCalendarEventTile(Graphics& graphics,const aip::content::Snapshot& frame,int width,int height,double scale)
{
    const auto plan=aip::content::BuildCalendarTilePlan(frame,width,height);
    const REAL margin=static_cast<REAL>(10*scale),fullWidth=static_cast<REAL>((width-20)*scale);
    const REAL bottom=static_cast<REAL>((height-(aip::content::CalendarUsesDateBranding(frame,width,height)?30:20))*scale);
    const auto pass=[&](bool paint,REAL top,REAL limit,REAL& end){
        REAL y=top;
        for(const auto& group:plan.groups){unsigned total=0;for(const auto& column:group.columns)total+=column.weight;if(!total)continue;
            const REAL gap=static_cast<REAL>(8*scale),available=fullWidth-gap*static_cast<REAL>(group.columns.size()-1);REAL x=margin,next=y;
            for(const auto& column:group.columns){const REAL columnWidth=available*column.weight/total;REAL columnY=y;
                for(const auto& field:column.texts){if(field.spacer){Font spacer(L"Segoe UI",static_cast<REAL>(12*scale),FontStyleRegular,UnitPixel);
                        const auto line=spacer.GetHeight(&graphics);if(spacer.GetLastStatus()!=Ok||line<=0)return false;columnY+=line+static_cast<REAL>(2*scale);continue;}
                    if(!DrawPrayerClockField(graphics,frame,field.text,field.style.c_str(),static_cast<unsigned>(field.role),x,columnY,columnWidth,limit,field.lines,scale,
                        field.align==L"right",field.align==L"center",!paint,field.wrap))return false;}
                next=(std::max)(next,columnY);x+=columnWidth+gap;
            }y=next;
        }end=y;return true;
    };
    REAL y=margin,end=margin;
    if(frame.presentation.Active()&&frame.presentation.stacking!=L"top"){
        REAL measured=0;if(!pass(false,0,100000,measured))return false;
        const REAL spare=(std::max)(0.0f,bottom-margin-measured);y+=frame.presentation.stacking==L"bottom"?spare:spare*.5f;
    }
    if(!pass(true,y,bottom,end))return false;
    if(TileSnapshotUsesRegistration(frame)&&aip::content::CalendarUsesDateBranding(frame,width,height)){
        // Registration manifests expose only one displayName across sizes.
        // Bake this per-size native date label while the manifest suppresses
        // Medium/Large names; Wide keeps the user's ordinary Calendar name.
        const auto label=aip::content::CalendarDateBranding(frame);Font font(L"Segoe UI",static_cast<REAL>(12*scale),FontStyleRegular,UnitPixel);
        SolidBrush ink(Color(255,255,255,255));StringFormat format;format.SetFormatFlags(StringFormatFlagsNoWrap);format.SetTrimming(StringTrimmingEllipsisCharacter);
        const REAL logo=height>200?static_cast<REAL>(16*scale):0;RectF footer(margin,static_cast<REAL>((height-24)*scale),fullWidth-(logo?logo+static_cast<REAL>(8*scale):0),static_cast<REAL>(20*scale));
        if(font.GetLastStatus()!=Ok||graphics.DrawString(label.c_str(),static_cast<INT>(label.size()),&font,footer,&format,&ink)!=Ok)return false;
        if(logo&&!aip::tile_visual::DrawTileTopicGlyph(graphics,L"calendar",RectF(static_cast<REAL>((width-26)*scale),static_cast<REAL>((height-24)*scale),logo,logo)))return false;
    }
    return graphics.GetLastStatus()==Ok;
}
static bool RenderDateTimeTileSimulation(Graphics& graphics,const aip::content::Snapshot& frame,int width,int height,double scale)
{
    const auto& date=frame.dateTime;const REAL margin=static_cast<REAL>(10*scale),bottom=static_cast<REAL>((height-20)*scale);
    const REAL fullWidth=static_cast<REAL>((width-20)*scale);REAL y=margin;
    if(frame.clockSource){
        const auto time=std::wstring(1,date.rightToLeft?L'\x200f':L'\x200e')+date.time;
        return DrawPrayerClockField(graphics,frame,time,aip::content::ClockTimeStyle(width,height),0,margin,y,fullWidth,bottom,1,scale)&&
            DrawPrayerClockField(graphics,frame,date.date.empty()?std::wstring():date.weekday,L"captionSubtle",1,margin,y,fullWidth,bottom,1,scale);
    }
    if(aip::content::CalendarDateOnlyCentered(frame,width,height)){
        const bool isSmall=height<150;const auto& p=frame.presentation;
        const auto& weekday=isSmall&&!date.shortWeekday.empty()?date.shortWeekday:date.weekday;
        const auto dayStyle=isSmall?L"titleNumeral":L"headerNumeral";
        const auto weekdayStyle=aip::content::PrayerFieldStyle(p,aip::content::PrayerFieldRole::Secondary,L"caption");
        const auto numeralStyle=aip::content::PrayerFieldStyle(p,aip::content::PrayerFieldRole::Time,dayStyle);
        Font weekdayFont(L"Segoe UI",static_cast<REAL>(aip::content::TextStylePixels(weekdayStyle)*scale),FontStyleRegular,UnitPixel);
        Font dayFont(L"Segoe UI Light",static_cast<REAL>(aip::content::TextStylePixels(numeralStyle)*scale),FontStyleRegular,UnitPixel);
        const REAL gap=p.Active()?static_cast<REAL>(p.gapLines*12*scale):0;
        const REAL blockHeight=weekdayFont.GetHeight(&graphics)+dayFont.GetHeight(&graphics)+static_cast<REAL>(4*scale)+gap;
        const REAL availableBottom=static_cast<REAL>((isSmall?height:height-10)*scale);
        y=(std::max)(0.0f,(static_cast<REAL>(height*scale)-blockHeight)*.5f);
        if(p.Active()&&p.stacking==L"top")y=isSmall?0:margin;
        else if(p.Active()&&p.stacking==L"bottom")y=(std::max)(0.0f,availableBottom-blockHeight);
        return DrawPrayerClockField(graphics,frame,weekday,L"caption",1,margin,y,fullWidth,availableBottom,1,scale,false,true)&&
            DrawPrayerClockField(graphics,frame,date.dayNumber,dayStyle,0,margin,y,fullWidth,availableBottom,1,scale,false,true);
    }
    if(!frame.calendarEvents.empty())return RenderCalendarEventTile(graphics,frame,width,height,scale);
    if(!DrawPrayerClockField(graphics,frame,date.weekday,L"caption",1,margin,y,fullWidth,bottom,1,scale,true)||
        !DrawPrayerClockField(graphics,frame,date.dayNumber,L"headerNumeral",0,margin,y,fullWidth,bottom,1,scale,true)||
        !DrawPrayerClockField(graphics,frame,date.location,L"captionSubtle",1,margin,y,fullWidth,bottom,1,scale))return false;
    if(!DrawPrayerClockField(graphics,frame,frame.text.secondary,L"captionSubtle",1,margin,y,fullWidth,bottom,2,scale))return false;
    return graphics.GetLastStatus()==Ok;
}
static bool ApplyTileTextOverlay(Bitmap* bitmap, const wchar_t* tileName, int logicalWidth, int logicalHeight)
{
    const bool calendarSmall=g_renderContent&&g_renderContent->calendarSource&&IEquals(tileName?tileName:L"",L"SmallTile")&&
        g_renderContent->textMode!=aip::content::TextMode::Off;
    if (!bitmap || !TileTextOverlayEnabled() || (!calendarSmall&&!TileTextAppliesToTileName(tileName)))
        return true;

    int bitmapWidth = 0;
    int bitmapHeight = 0;
    if (!BitmapDimensionsToInt(bitmap, bitmapWidth, bitmapHeight))
        return false;

    std::wstring primaryText = TileTextPrimaryText();
    std::wstring secondaryText = TileTextSecondaryText();
    std::wstring badgeText = TileTextBadgeText();
    if (primaryText.empty() && secondaryText.empty() && badgeText.empty())
        return true;

    double scale = 1.0;
    if (logicalWidth > 0 && logicalHeight > 0)
    {
        scale = std::min(
            static_cast<double>(bitmapWidth) / logicalWidth,
            static_cast<double>(bitmapHeight) / logicalHeight);
    }
    if (scale <= 0.0)
        scale = 1.0;

    Graphics graphics(bitmap);
    if (!GraphicsOk(graphics, L"create tile text graphics"))
        return false;

    if (!ConfigureTileTextGraphics(graphics))
        return false;
    if(g_renderContent&&!DrawStaticSourceCorner(graphics,*g_renderContent,logicalWidth,logicalHeight,scale,TileSnapshotUsesRegistration(*g_renderContent)))return false;
    if(calendarSmall)return RenderDateTimeTileSimulation(graphics,*g_renderContent,logicalWidth,logicalHeight,scale);
    if (g_renderContent && logicalWidth >= 150 && logicalHeight >= 150) {
        if (g_renderContent->presentation.Active() && !g_renderContent->smtcSource && !g_renderContent->prayerSource && !g_renderContent->clockSource && !g_renderContent->calendarSource && !g_renderContent->mailSource &&
            g_renderContent->detailRows.empty() && g_renderContent->rssHeadlines.empty()) {
            Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
            REAL y = static_cast<REAL>(10 * scale) + PresentedTextOffset(*g_renderContent, logicalHeight, scale);
            const REAL margin = static_cast<REAL>(10 * scale), width = static_cast<REAL>((logicalWidth - 20) * scale), bottom = static_cast<REAL>((logicalHeight - SourceTextBottomInset(*g_renderContent)) * scale);
            return DrawTopSourceField(graphics, primaryText, font, margin, y, width, bottom, 2, 255, 0) &&
                DrawTopSourceField(graphics, secondaryText, font, margin, y, width, bottom, 2, 153, 0) &&
                DrawTopSourceField(graphics, badgeText, font, margin, y, width, bottom, 2, 153, 0);
        }
        if(g_renderContent->mailSource)return RenderMailTileSimulation(graphics,*g_renderContent,logicalWidth,logicalHeight,scale);
        if (g_renderContent->clockSource||g_renderContent->calendarSource) return RenderDateTimeTileSimulation(graphics,*g_renderContent,logicalWidth,logicalHeight,scale);
        if (g_renderContent->prayerSource) return RenderPrayerTileSimulation(graphics, *g_renderContent, logicalWidth, logicalHeight, scale);
        if (g_renderContent->smtcSource) return RenderMediaTileSimulation(graphics, *g_renderContent, logicalWidth, logicalHeight, scale);
        if (!g_renderContent->detailRows.empty()) return RenderDetailRowsSimulation(graphics, *g_renderContent, logicalWidth, logicalHeight, scale);
        if (g_renderContent->wrapSourceText) {
            Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
            if (font.GetLastStatus() != Ok) return false;
            REAL y = static_cast<REAL>(10 * scale);
            const REAL margin = y, bottom = static_cast<REAL>((logicalHeight - SourceTextBottomInset(*g_renderContent)) * scale);
            const REAL width = static_cast<REAL>(bitmapWidth) - margin * 2;
            return DrawTopSourceField(graphics, primaryText, font, margin, y, width, bottom, 2, 255, static_cast<REAL>(3 * scale)) &&
                DrawTopSourceField(graphics, secondaryText, font, margin, y, width, bottom, 5, 153, 0);
        }
    }
    if (g_renderContent && !g_renderContent->rssHeadlines.empty() && logicalHeight >= 150 && logicalWidth >= 150) {
        if (g_renderContent->presentation.Active() || aip::content::RssUsesRichLayout(g_renderContent->presentation)) {
            const bool rich = aip::content::RssUsesRichLayout(g_renderContent->presentation);
            const auto count = (std::min)(g_renderContent->rssHeadlines.size(), rich ? aip::content::RssRichTileRows(logicalWidth, logicalHeight) : aip::content::RssTileRows(logicalHeight));
            const auto presentation = rich ? aip::content::RssRichPresentation(g_renderContent->presentation, logicalWidth, logicalHeight) : g_renderContent->presentation;
            Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
            const REAL margin = static_cast<REAL>(10 * scale), width = static_cast<REAL>((logicalWidth - 20) * scale);
            const REAL rowHeight = static_cast<REAL>((logicalHeight - 30) * scale / count);
            for (std::size_t row = 0; row < count; ++row) {
                REAL y = margin + static_cast<REAL>(row) * rowHeight;
                const REAL bottom = y + rowHeight;
                const auto& item = g_renderContent->rssHeadlines[row];
                const auto title = rich ? item.primary : std::to_wstring(row + 1) + L". " + item.primary;
                if (!DrawTopSourceField(graphics, title, font, margin, y, width, bottom, 3, 255, 0, &presentation) ||
                    ((rich || logicalHeight > 200) && !DrawTopSourceField(graphics, item.secondary, font, margin, y, width, bottom, 2, 153, 0, &presentation))) return false;
            }
            return graphics.GetLastStatus() == Ok;
        }
        const auto count = (std::min)(g_renderContent->rssHeadlines.size(), logicalHeight > 200 ? std::size_t(5) : std::size_t(3));
        const REAL margin = static_cast<REAL>(10 * scale);
        const REAL rowHeight = static_cast<REAL>((logicalHeight - 30) * scale / count);
        Font font(L"Segoe UI", static_cast<REAL>(12 * scale), FontStyleRegular, UnitPixel);
        SolidBrush brush(Color(255, 255, 255, 255));
        StringFormat format; format.SetTrimming(StringTrimmingEllipsisWord);
        const auto saved = graphics.Save();
        for (std::size_t index = 0; index < count; ++index) {
            RectF row(margin, margin + static_cast<REAL>(index) * rowHeight,
                static_cast<REAL>(bitmapWidth) - margin * 2, rowHeight - static_cast<REAL>(4 * scale));
            graphics.SetClip(row, CombineModeReplace);
            const auto title = std::to_wstring(index + 1) + L". " + g_renderContent->rssHeadlines[index].primary;
            graphics.DrawString(title.c_str(), static_cast<INT>(title.size()), &font, row, &format, &brush);
        }
        graphics.Restore(saved);
        return graphics.GetLastStatus() == Ok;
    }
    if (ShouldUseAdaptiveStaticTextFallback()) {
        const auto layout = SimulatedTileTextLayoutForName(tileName);
        if (layout == SimulatedTileTextLayout::Medium)
            return RenderAdaptiveTileTextSimulation(graphics, tiletext::TileSize::Medium, primaryText, secondaryText, badgeText, scale);
        if (layout == SimulatedTileTextLayout::Wide)
            return RenderAdaptiveTileTextSimulation(graphics, tiletext::TileSize::Wide, primaryText, secondaryText, badgeText, scale);
        if (layout == SimulatedTileTextLayout::Large)
            return RenderAdaptiveTileTextSimulation(graphics, tiletext::TileSize::Large, primaryText, secondaryText, badgeText, scale);
        return true;
    }
    switch (SimulatedTileTextLayoutForName(tileName))
    {
    case SimulatedTileTextLayout::Medium:
        return RenderMediumTileTextSimulation(graphics, primaryText, secondaryText, badgeText, scale,
            PreserveContentBackgroundForText());
    case SimulatedTileTextLayout::Wide:
        return RenderWideTileTextSimulation(graphics, primaryText, secondaryText, badgeText, scale,
            PreserveContentBackgroundForText());
    case SimulatedTileTextLayout::Large:
        return RenderLargeTileTextSimulation(graphics, primaryText, secondaryText, badgeText, scale,
            PreserveContentBackgroundForText());
    case SimulatedTileTextLayout::None:
    default:
        return true;
    }
}

static std::wstring TileTextXmlText()
{
    if (g_renderContent && g_renderContent->textMode == aip::content::TextMode::Overlay) return L"";
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextPrimaryText();
}

static std::wstring TileTextXmlSecondaryText()
{
    if (g_renderContent && g_renderContent->textMode == aip::content::TextMode::Overlay) return L"";
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextSecondaryText();
}

static std::wstring TileTextXmlBadgeText()
{
    if (g_renderContent && g_renderContent->textMode == aip::content::TextMode::Overlay) return L"";
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextBadgeText();
}

static Bitmap* ResizeBitmapToMode(Bitmap* src, int w, int h, FitMode mode, Color background)
{
    int sw = 0;
    int sh = 0;
    if (!BitmapDimensionsToInt(src, sw, sh) || w <= 0 || h <= 0)
        return nullptr;

    auto out = std::make_unique<Bitmap>(w, h, PixelFormat32bppARGB);
    if (!BitmapOk(out.get(), L"create resized bitmap"))
        return nullptr;

    Graphics g(out.get());
    if (!GraphicsOk(g, L"create resize graphics"))
        return nullptr;

    if (!GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set resize interpolation") ||
        !GdiOk(g.Clear(background), L"clear resized bitmap"))
        return nullptr;

    double scaleX = (double)w / sw;
    double scaleY = (double)h / sh;

    int dw = w, dh = h;
    int dx = 0, dy = 0;

    switch (mode)
    {
    case FitMode::Fill:
    {
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Fit:
    {
        double s = std::min(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Stretch:
    {
        dw = w;
        dh = h;
        dx = 0;
        dy = 0;
        break;
    }

    case FitMode::Center:
    {
        dw = sw;
        dh = sh;
        dx = (w - sw) / 2;
        dy = (h - sh) / 2;
        break;
    }

    case FitMode::Tile:
        // Handled by ResizeWithFit after building a virtual-desktop-sized tiled canvas.
        break;

    case FitMode::Span:
    {
        // fallback: treat as Fill
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }
    }

    if (!GdiOk(g.DrawImage(src, Rect(dx, dy, dw, dh), 0, 0, sw, sh, UnitPixel), L"draw resized bitmap"))
        return nullptr;

    return out.release();
}

static bool GetVirtualDesktopRect(RECT& rect)
{
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0)
    {
        left = 0;
        top = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
    if (width <= 0 || height <= 0)
        return false;

    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + height;
    return true;
}

static int FirstTileStart(int targetStart, int tileOrigin, int tileSize)
{
    int delta = targetStart - tileOrigin;
    int rem = delta % tileSize;
    if (rem < 0)
        rem += tileSize;
    return targetStart - rem;
}

static Bitmap* BuildTiledPrimaryBitmap(Bitmap* src)
{
    int sw = 0;
    int sh = 0;
    if (!BitmapDimensionsToInt(src, sw, sh))
        return nullptr;

    RECT primaryRect{};
    if (!GetPrimaryMonitorRect(primaryRect))
    {
        primaryRect.left = 0;
        primaryRect.top = 0;
        primaryRect.right = GetSystemMetrics(SM_CXSCREEN);
        primaryRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT virtualRect{};
    if (!GetVirtualDesktopRect(virtualRect))
        virtualRect = primaryRect;

    int primaryW = primaryRect.right - primaryRect.left;
    int primaryH = primaryRect.bottom - primaryRect.top;
    if (primaryW <= 0 || primaryH <= 0)
        return nullptr;

    auto out = std::make_unique<Bitmap>(primaryW, primaryH, PixelFormat32bppARGB);
    if (!BitmapOk(out.get(), L"create tiled bitmap"))
        return nullptr;

    Graphics g(out.get());
    if (!GraphicsOk(g, L"create tiled graphics") ||
        !GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set tiled interpolation") ||
        !GdiOk(g.Clear(Color(255, 0, 0, 0)), L"clear tiled bitmap"))
        return nullptr;

    TextureBrush brush(src, WrapModeTile);
    Matrix transform;
    transform.Translate(
        static_cast<REAL>(virtualRect.left - primaryRect.left),
        static_cast<REAL>(virtualRect.top - primaryRect.top));
    if (brush.GetLastStatus() == Ok &&
        brush.SetTransform(&transform) == Ok &&
        g.FillRectangle(&brush, 0, 0, primaryW, primaryH) == Ok)
    {
        return out.release();
    }

    int firstX = FirstTileStart(primaryRect.left, virtualRect.left, sw);
    int firstY = FirstTileStart(primaryRect.top, virtualRect.top, sh);
    for (int y = firstY; y < primaryRect.bottom; y += sh)
    {
        for (int x = firstX; x < primaryRect.right; x += sw)
        {
            if (!GdiOk(g.DrawImage(src, x - primaryRect.left, y - primaryRect.top, sw, sh), L"draw tiled bitmap"))
                return nullptr;
        }
    }

    return out.release();
}

static Bitmap* BuildSpannedPrimaryBitmap(Bitmap* src)
{
    if (!src) return nullptr;

    RECT primaryRect{};
    if (!GetPrimaryMonitorRect(primaryRect))
    {
        primaryRect.left = 0;
        primaryRect.top = 0;
        primaryRect.right = GetSystemMetrics(SM_CXSCREEN);
        primaryRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT virtualRect{};
    if (!GetVirtualDesktopRect(virtualRect))
        virtualRect = primaryRect;

    int primaryW = primaryRect.right - primaryRect.left;
    int primaryH = primaryRect.bottom - primaryRect.top;
    int virtualW = virtualRect.right - virtualRect.left;
    int virtualH = virtualRect.bottom - virtualRect.top;
    if (primaryW <= 0 || primaryH <= 0 || virtualW <= 0 || virtualH <= 0)
        return nullptr;

    std::unique_ptr<Bitmap> virtualWallpaper(ResizeBitmapToMode(src, virtualW, virtualH, FitMode::Fill, Color(255, 0, 0, 0)));
    if (!virtualWallpaper)
        return nullptr;

    if (primaryW == virtualW && primaryH == virtualH &&
        primaryRect.left == virtualRect.left && primaryRect.top == virtualRect.top)
    {
        return virtualWallpaper.release();
    }

    auto out = std::make_unique<Bitmap>(primaryW, primaryH, PixelFormat32bppARGB);
    if (!BitmapOk(out.get(), L"create spanned bitmap"))
        return nullptr;

    Graphics g(out.get());
    if (!GraphicsOk(g, L"create spanned graphics") ||
        !GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set spanned interpolation") ||
        !GdiOk(g.Clear(Color(255, 0, 0, 0)), L"clear spanned bitmap") ||
        !GdiOk(g.DrawImage(
        virtualWallpaper.get(),
        Rect(0, 0, primaryW, primaryH),
        primaryRect.left - virtualRect.left,
        primaryRect.top - virtualRect.top,
        primaryW,
        primaryH,
        UnitPixel), L"draw spanned bitmap"))
        return nullptr;

    return out.release();
}

Bitmap* ResizeWithFit(Bitmap* src, int w, int h, FitMode mode)
{
    if (mode != FitMode::Tile && mode != FitMode::Span)
        return ResizeBitmapToMode(src, w, h, mode, g_renderContent && g_renderContent->galleryFrame ? Color(0, 0, 0, 0) : Color(255, 0, 0, 0));

    std::unique_ptr<Bitmap> primaryWallpaper((mode == FitMode::Tile)
        ? BuildTiledPrimaryBitmap(src)
        : BuildSpannedPrimaryBitmap(src));
    if (!primaryWallpaper)
        return nullptr;

    return ResizeBitmapToMode(primaryWallpaper.get(), w, h, FitMode::Fill, Color(255, 0, 0, 0));
}

static int DesktopIconVisibleWidthForAsset(int assetWidth, int assetHeight, int logicalWidth, int logicalHeight)
{
    int logicalShortSide = std::min(logicalWidth, logicalHeight);
    int assetShortSide = std::min(assetWidth, assetHeight);
    if (logicalShortSide <= 0 || assetShortSide <= 0)
        return 0;

    int logicalVisibleW = 0;
    if (logicalShortSide <= 60)
        logicalVisibleW = std::max(1, logicalShortSide - 2);
    else if (logicalShortSide <= 80)
        logicalVisibleW = std::max(1, MulDiv(logicalShortSide, 38, 70));
    else
        logicalVisibleW = std::max(1, MulDiv(logicalShortSide, 74, 150));

    return std::max(1, MulDiv(logicalVisibleW, assetShortSide, logicalShortSide));
}

static int DesktopIconCenterYForAsset(int assetHeight, int logicalWidth, int logicalHeight)
{
    int logicalShortSide = std::min(logicalWidth, logicalHeight);
    if (logicalShortSide <= 0 || logicalHeight <= 0)
        return assetHeight / 2;

    int logicalCenterY = (logicalShortSide <= 80)
        ? logicalHeight / 2
        : MulDiv(logicalShortSide, 119, 300);
    return MulDiv(logicalCenterY, assetHeight, logicalHeight);
}

#include "ga_desktop_icon_png.inc"

// Decodes g_desktopIconPng from memory into a GDI+ Bitmap. Caller owns the result.
static Bitmap* LoadEmbeddedDesktopIcon()
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(g_desktopIconPng));
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, g_desktopIconPng, sizeof(g_desktopIconPng));
    GlobalUnlock(hMem);

    IStream* rawStream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &rawStream) != S_OK)
    {
        GlobalFree(hMem);
        return nullptr;
    }
    struct StreamReleaser
    {
        void operator()(IStream* stream) const noexcept
        {
            if (stream)
                stream->Release();
        }
    };
    std::unique_ptr<IStream, StreamReleaser> stream(rawStream);

    // Clone before releasing the stream; GDI+ bitmaps can retain stream-backed state.
    std::unique_ptr<Bitmap> loaded(Bitmap::FromStream(stream.get()));
    std::unique_ptr<Bitmap> bmp;
    if (loaded && loaded->GetLastStatus() == Ok)
    {
        int loadedW = 0;
        int loadedH = 0;
        if (BitmapDimensionsToInt(loaded.get(), loadedW, loadedH))
        {
            Rect bounds(0, 0, loadedW, loadedH);
            bmp.reset(loaded->Clone(bounds, PixelFormat32bppARGB));
        }
    }

    if (!bmp || bmp->GetLastStatus() != Ok)
        return nullptr;

    return bmp.release();
}

static Bitmap* RenderDesktopIconAsset(Bitmap* icon, int assetWidth, int assetHeight, int logicalWidth, int logicalHeight)
{
    int iconW = 0;
    int iconH = 0;
    if (!BitmapDimensionsToInt(icon, iconW, iconH) || assetWidth <= 0 || assetHeight <= 0 || logicalWidth <= 0 || logicalHeight <= 0)
        return nullptr;

    auto out = std::make_unique<Bitmap>(assetWidth, assetHeight, PixelFormat32bppARGB);
    if (!BitmapOk(out.get(), L"create desktop icon asset"))
        return nullptr;

    Graphics g(out.get());
    if (!GraphicsOk(g, L"create desktop icon asset graphics") ||
        !GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set desktop icon interpolation") ||
        !GdiOk(g.SetPixelOffsetMode(PixelOffsetModeHighQuality), L"set desktop icon pixel offset") ||
        !GdiOk(g.Clear(Color(0, 0, 0, 0)), L"clear desktop icon asset"))
        return nullptr;

    int targetVisibleW = DesktopIconVisibleWidthForAsset(assetWidth, assetHeight, logicalWidth, logicalHeight);
    if (targetVisibleW <= 0)
        return nullptr;

    int targetVisibleH = std::max(1, MulDiv(targetVisibleW, iconH, iconW));
    if (targetVisibleW > assetWidth || targetVisibleH > assetHeight)
    {
        double fit = std::min(
            static_cast<double>(assetWidth) / targetVisibleW,
            static_cast<double>(assetHeight) / targetVisibleH);
        targetVisibleW = std::max(1, static_cast<int>(targetVisibleW * fit + 0.5));
        targetVisibleH = std::max(1, static_cast<int>(targetVisibleH * fit + 0.5));
    }

    int dx = (assetWidth - targetVisibleW) / 2;
    int dy = DesktopIconCenterYForAsset(assetHeight, logicalWidth, logicalHeight) - (targetVisibleH / 2);
    dy = ClampInt(dy, 0, std::max(0, assetHeight - targetVisibleH));

    if (!GdiOk(
        g.DrawImage(icon, Rect(dx, dy, targetVisibleW, targetVisibleH), 0, 0, iconW, iconH, UnitPixel),
        L"draw desktop icon asset"))
        return nullptr;

    return out.release();
}

bool SavePNG(Bitmap* b, const wchar_t* f)
{
    const wchar_t* pathForLog = (f && *f) ? f : L"(empty path)";
    if (!b || !f || !*f || !FindPngEncoder())
    {
        Log(g_ui.savePngInvalidInput.c_str(), pathForLog);
        return false;
    }

    std::wstring path(f);
    std::wstring tempPath = MakeTempSiblingPath(path, L"png");
    DeleteFileW(tempPath.c_str());

    Status saveStatus = b->Save(tempPath.c_str(), &g_png, nullptr);
    if (saveStatus != Ok)
    {
        DeleteFileW(tempPath.c_str());
        Log(g_ui.savePngGdiFailure.c_str(), path.c_str(), static_cast<int>(saveStatus));
        return false;
    }

    const int replaceRetries = IniReadClampedI(L"Settings", L"SavePngReplaceRetries", 3, 0, 20);
    const int retryDelayMs = IniReadClampedI(L"Settings", L"SavePngReplaceRetryDelayMs", 75, 0, 5000);
    const int maxAttempts = replaceRetries + 1;
    DWORD finalErr = ERROR_SUCCESS;

    for (int attempt = 1; attempt <= maxAttempts; ++attempt)
    {
        if (MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;

        finalErr = GetLastError();
        if (attempt < maxAttempts && retryDelayMs > 0)
        {
            Log(g_ui.savePngReplaceRetrying.c_str(), attempt, maxAttempts, path.c_str(), finalErr, Win32ErrorString(finalErr).c_str(), retryDelayMs);
            Sleep(static_cast<DWORD>(retryDelayMs));
        }
    }

    if (finalErr == ERROR_SUCCESS)
        finalErr = ERROR_WRITE_FAULT;

    DeleteFileW(tempPath.c_str());
    Log(g_ui.savePngReplaceFailed.c_str(), path.c_str(), tempPath.c_str());
    LogWin32Failure(L"MoveFileExW", finalErr);
    SetLastError(finalErr);
    return false;
}


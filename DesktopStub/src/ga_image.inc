// -----------------------------------------------------------------------------
// Lazy GDI+ lifetime
// -----------------------------------------------------------------------------
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
    int maxLongEdge = 0;
    bool usedWic = false;
    bool downscaled = false;
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

    IWICBitmapSource* source = frame.get();
    UniqueComPtr<IWICBitmapScaler> scaler;
    if (sourceLongEdge > targetLongEdge)
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
    const size_t wicBytes = static_cast<size_t>(wicStride) * static_cast<size_t>(dstH);
    if (wicBytes == 0 || wicBytes > static_cast<size_t>(INT_MAX))
        return nullptr;

    std::vector<BYTE> pixels(wicBytes);
    hr = converter->CopyPixels(nullptr, wicStride, static_cast<UINT>(pixels.size()), pixels.data());
    if (FAILED(hr))
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
        else
        {
            for (UINT y = 0; y < dstH; ++y)
            {
                BYTE* dstRow = data.Stride >= 0
                    ? base + static_cast<size_t>(y) * static_cast<size_t>(data.Stride)
                    : base + static_cast<size_t>(dstH - 1u - y) * static_cast<size_t>(absStride);
                memcpy(dstRow, pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(wicStride), wicStride);
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

    if (IniReadI(L"Settings", L"WallpaperDecodeLowMemory", 1) != 0 && maxLongEdge > 0)
    {
        std::unique_ptr<Bitmap> wicBitmap = TryLoadWicBitmapForRender(path, maxLongEdge, info);
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

static Color ParseTileTextColor(const std::wstring& raw, Color fallback)
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
    return TrimCopy(IniReadS(L"TileText", L"Text", L""));
}

static std::wstring TileTextSecondaryText()
{
    return TrimCopy(IniReadS(L"TileText", L"SecondaryText", L""));
}

static std::wstring TileTextBadgeText()
{
    return TrimCopy(IniReadS(L"TileText", L"BadgeText", L""));
}

static bool TileTextOverlayEnabled()
{
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
    if (IEquals(name, L"SmallTile"))
        return IniReadI(L"TileText", L"ApplyToSmallTile", 0) != 0;
    if (IEquals(name, L"StoreLogo") ||
        IEquals(name, L"Square44x44Logo") ||
        IEquals(name, L"Square30x30Logo"))
        return IniReadI(L"TileText", L"ApplyToLogos", 0) != 0;
    return false;
}

static const wchar_t* NormalizeTileTextHorizontalAlignValue(const std::wstring& raw, const wchar_t* fallback)
{
    std::wstring value = ToLowerCopy(TrimCopy(raw));
    if (value == L"left")
        return L"Left";
    if (value == L"right")
        return L"Right";
    if (value == L"center" || value == L"middle")
        return L"Center";
    return fallback && *fallback ? fallback : L"Center";
}

static const wchar_t* NormalizeTileTextVerticalAlignValue(const std::wstring& raw, const wchar_t* fallback)
{
    std::wstring value = ToLowerCopy(TrimCopy(raw));
    if (value == L"top")
        return L"Top";
    if (value == L"center" || value == L"middle")
        return L"Center";
    if (value == L"bottom")
        return L"Bottom";
    return fallback && *fallback ? fallback : L"Top";
}

static const wchar_t* NormalizedTileTextHorizontalAlign()
{
    return NormalizeTileTextHorizontalAlignValue(IniReadS(L"TileText", L"HorizontalAlign", L"Center"), L"Center");
}

static const wchar_t* NormalizedTileTextVerticalAlign()
{
    return NormalizeTileTextVerticalAlignValue(IniReadS(L"TileText", L"VerticalAlign", L"Top"), L"Top");
}

static const wchar_t* NormalizedTileTextBadgeHorizontalAlign()
{
    return NormalizeTileTextHorizontalAlignValue(IniReadS(L"TileText", L"BadgeHorizontalAlign", L"Right"), L"Right");
}

static const wchar_t* NormalizedTileTextBadgeVerticalAlign()
{
    return NormalizeTileTextVerticalAlignValue(IniReadS(L"TileText", L"BadgeVerticalAlign", L"Top"), L"Top");
}

static std::unique_ptr<FontFamily> CreateTileTextFontFamily()
{
    std::wstring fontName = TrimCopy(IniReadS(L"TileText", L"Font", L"Segoe UI"));
    if (fontName.empty())
        fontName = L"Segoe UI";

    std::unique_ptr<FontFamily> family(new FontFamily(fontName.c_str()));
    if (!family || family->GetLastStatus() != Ok)
        family.reset(new FontFamily(L"Segoe UI"));
    if (!family || family->GetLastStatus() != Ok)
        family.reset(new FontFamily(L"Arial"));
    return family;
}

static int TileTextFontStyle(const wchar_t* boldKey, int boldFallback)
{
    int fontStyle = FontStyleRegular;
    if (IniReadI(L"TileText", boldKey, boldFallback) != 0)
        fontStyle |= FontStyleBold;
    if (IniReadI(L"TileText", L"Italic", 0) != 0)
        fontStyle |= FontStyleItalic;
    return fontStyle;
}

static StringAlignment TileTextHorizontalStringAlignment(const std::wstring& value)
{
    std::wstring align = ToLowerCopy(value);
    if (align == L"left")
        return StringAlignmentNear;
    if (align == L"right")
        return StringAlignmentFar;
    return StringAlignmentCenter;
}

static StringAlignment TileTextVerticalStringAlignment(const std::wstring& value)
{
    std::wstring align = ToLowerCopy(value);
    if (align == L"top")
        return StringAlignmentNear;
    if (align == L"bottom")
        return StringAlignmentFar;
    return StringAlignmentCenter;
}

static bool DrawTileTextString(
    Graphics& graphics,
    const std::wstring& text,
    Font& font,
    const RectF& layout,
    StringFormat& format,
    SolidBrush& brush,
    double scale,
    const wchar_t* operation)
{
    if (text.empty())
        return true;

    if (IniReadI(L"TileText", L"Shadow", 1) != 0)
    {
        int shadowOffset = std::max(1, static_cast<int>(scale + 0.5));
        RectF shadowLayout = layout;
        shadowLayout.X += static_cast<REAL>(shadowOffset);
        shadowLayout.Y += static_cast<REAL>(shadowOffset);
        Color shadowColor = ParseTileTextColor(IniReadS(L"TileText", L"ShadowColor", L"#AA000000"), Color(170, 0, 0, 0));
        SolidBrush shadowBrush(shadowColor);
        if (!GdiOk(graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, shadowLayout, &format, &shadowBrush), operation))
            return false;
    }

    return GdiOk(graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, layout, &format, &brush), operation);
}

static bool ApplyTileTextOverlay(Bitmap* bitmap, const wchar_t* tileName, int logicalWidth, int logicalHeight)
{
    if (!bitmap || !TileTextOverlayEnabled() || !TileTextAppliesToTileName(tileName))
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

    int primaryFontSize = IniReadClampedI(L"TileText", L"FontSize", 18, 6, 256);
    int secondaryFontSize = IniReadClampedI(L"TileText", L"SecondaryFontSize", 12, 6, 256);
    int badgeFontSize = IniReadClampedI(L"TileText", L"BadgeFontSize", 40, 6, 256);
    int marginX = IniReadClampedI(L"TileText", L"MarginX", 8, 0, 1000);
    int marginY = IniReadClampedI(L"TileText", L"MarginY", 10, 0, 1000);
    int lineGap = IniReadClampedI(L"TileText", L"LineGap", 2, 0, 1000);
    int maxSecondaryLines = IniReadClampedI(L"TileText", L"MaxSecondaryLines", 2, 1, 20);

    double scale = 1.0;
    if (logicalWidth > 0 && logicalHeight > 0)
    {
        scale = std::min(
            static_cast<double>(bitmapWidth) / logicalWidth,
            static_cast<double>(bitmapHeight) / logicalHeight);
    }
    if (scale <= 0.0)
        scale = 1.0;

    REAL scaledPrimaryFontSize = static_cast<REAL>(std::max(1, static_cast<int>(primaryFontSize * scale + 0.5)));
    REAL scaledSecondaryFontSize = static_cast<REAL>(std::max(1, static_cast<int>(secondaryFontSize * scale + 0.5)));
    REAL scaledBadgeFontSize = static_cast<REAL>(std::max(1, static_cast<int>(badgeFontSize * scale + 0.5)));
    REAL scaledLineGap = static_cast<REAL>(std::max(0, static_cast<int>(lineGap * scale + 0.5)));
    int scaledMarginX = std::max(0, static_cast<int>(marginX * scale + 0.5));
    int scaledMarginY = std::max(0, static_cast<int>(marginY * scale + 0.5));

    RectF content(
        static_cast<REAL>(scaledMarginX),
        static_cast<REAL>(scaledMarginY),
        static_cast<REAL>(std::max(1, bitmapWidth - scaledMarginX * 2)),
        static_cast<REAL>(std::max(1, bitmapHeight - scaledMarginY * 2)));

    std::unique_ptr<FontFamily> family = CreateTileTextFontFamily();
    if (!family || family->GetLastStatus() != Ok)
        return false;

    Font primaryFont(family.get(), scaledPrimaryFontSize, TileTextFontStyle(L"Bold", 0), UnitPixel);
    Font secondaryFont(family.get(), scaledSecondaryFontSize, TileTextFontStyle(L"SecondaryBold", 0), UnitPixel);
    Font badgeFont(family.get(), scaledBadgeFontSize, TileTextFontStyle(L"BadgeBold", 0), UnitPixel);
    if (primaryFont.GetLastStatus() != Ok || secondaryFont.GetLastStatus() != Ok || badgeFont.GetLastStatus() != Ok)
        return false;

    Graphics graphics(bitmap);
    if (!GraphicsOk(graphics, L"create tile text graphics"))
        return false;

    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);

    std::wstring hAlign = ToLowerCopy(NormalizedTileTextHorizontalAlign());
    std::wstring vAlign = ToLowerCopy(NormalizedTileTextVerticalAlign());
    std::wstring badgeHAlign = ToLowerCopy(NormalizedTileTextBadgeHorizontalAlign());
    std::wstring badgeVAlign = ToLowerCopy(NormalizedTileTextBadgeVerticalAlign());

    REAL primaryLineHeight = scaledPrimaryFontSize * 1.25f;
    REAL secondaryLineHeight = scaledSecondaryFontSize * 1.20f;
    REAL secondaryBlockHeight = secondaryText.empty() ? 0.0f : secondaryLineHeight * static_cast<REAL>(maxSecondaryLines);
    REAL blockHeight = 0.0f;
    if (!primaryText.empty())
        blockHeight += primaryLineHeight;
    if (!primaryText.empty() && !secondaryText.empty())
        blockHeight += scaledLineGap;
    if (!secondaryText.empty())
        blockHeight += secondaryBlockHeight;
    if (blockHeight <= 0.0f)
        blockHeight = primaryLineHeight;

    REAL textY = content.Y;
    if (vAlign == L"center")
        textY = content.Y + std::max(0.0f, (content.Height - blockHeight) / 2.0f);
    else if (vAlign == L"bottom")
        textY = content.Y + std::max(0.0f, content.Height - blockHeight);

    REAL badgeReserve = 0.0f;
    if (!badgeText.empty() && (badgeHAlign == L"right" || badgeHAlign == L"left") && (!primaryText.empty() || !secondaryText.empty()))
        badgeReserve = std::min(content.Width * 0.42f, std::max(scaledBadgeFontSize * 1.35f, content.Width * 0.24f));

    RectF textContent = content;
    if (badgeReserve > 0.0f && badgeHAlign == L"right" && hAlign != L"right")
        textContent.Width = std::max(1.0f, textContent.Width - badgeReserve);
    else if (badgeReserve > 0.0f && badgeHAlign == L"left" && hAlign != L"left")
    {
        textContent.X += badgeReserve;
        textContent.Width = std::max(1.0f, textContent.Width - badgeReserve);
    }

    StringFormat textFormat;
    textFormat.SetAlignment(TileTextHorizontalStringAlignment(hAlign));
    textFormat.SetLineAlignment(StringAlignmentNear);
    textFormat.SetTrimming(StringTrimmingEllipsisWord);
    textFormat.SetFormatFlags(StringFormatFlagsLineLimit);

    Color primaryColor = ParseTileTextColor(IniReadS(L"TileText", L"Color", L"#FFFFFFFF"), Color(255, 255, 255, 255));
    Color secondaryColor = ParseTileTextColor(IniReadS(L"TileText", L"SecondaryColor", L"#CCFFFFFF"), Color(204, 255, 255, 255));
    Color badgeColor = ParseTileTextColor(IniReadS(L"TileText", L"BadgeColor", L"#FFFFFFFF"), Color(255, 255, 255, 255));
    SolidBrush primaryBrush(primaryColor);
    SolidBrush secondaryBrush(secondaryColor);
    SolidBrush badgeBrush(badgeColor);

    if (!primaryText.empty())
    {
        RectF primaryRect(textContent.X, textY, textContent.Width, primaryLineHeight + scaledLineGap);
        if (!DrawTileTextString(graphics, primaryText, primaryFont, primaryRect, textFormat, primaryBrush, scale, L"draw primary tile text"))
            return false;
        textY += primaryLineHeight + scaledLineGap;
    }

    if (!secondaryText.empty())
    {
        RectF secondaryRect(textContent.X, textY, textContent.Width, std::max(1.0f, content.Y + content.Height - textY));
        if (!DrawTileTextString(graphics, secondaryText, secondaryFont, secondaryRect, textFormat, secondaryBrush, scale, L"draw secondary tile text"))
            return false;
    }

    if (!badgeText.empty())
    {
        StringFormat badgeFormat;
        badgeFormat.SetAlignment(TileTextHorizontalStringAlignment(badgeHAlign));
        badgeFormat.SetLineAlignment(TileTextVerticalStringAlignment(badgeVAlign));
        badgeFormat.SetTrimming(StringTrimmingEllipsisCharacter);
        badgeFormat.SetFormatFlags(StringFormatFlagsLineLimit);
        if (!DrawTileTextString(graphics, badgeText, badgeFont, content, badgeFormat, badgeBrush, scale, L"draw tile badge text"))
            return false;
    }

    return true;
}

static std::wstring TileTextXmlText()
{
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextPrimaryText();
}

static std::wstring TileTextXmlSecondaryText()
{
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextSecondaryText();
}

static std::wstring TileTextXmlBadgeText()
{
    if (!TileTextOverlayEnabled())
        return L"";
    return TileTextBadgeText();
}

static std::wstring TileTextXmlHorizontalAlign()
{
    std::wstring value = ToLowerCopy(NormalizedTileTextHorizontalAlign());
    if (value == L"left" || value == L"right")
        return value;
    return L"center";
}

static std::wstring TileTextXmlBadgeHorizontalAlign()
{
    std::wstring value = ToLowerCopy(NormalizedTileTextBadgeHorizontalAlign());
    if (value == L"left" || value == L"right")
        return value;
    return L"center";
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
        return ResizeBitmapToMode(src, w, h, mode, Color(255, 0, 0, 0));

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


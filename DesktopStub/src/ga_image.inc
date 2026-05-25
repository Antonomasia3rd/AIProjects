// -----------------------------------------------------------------------------
// GDI+ PNG save helpers
// -----------------------------------------------------------------------------
static CLSID g_png{};
bool FindPngEncoder()
{
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


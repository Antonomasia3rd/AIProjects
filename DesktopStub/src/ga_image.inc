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

// Embedded 30x30 RGBA PNG — the Desktop icon written to disabled asset slots.
static const BYTE g_desktopIconPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x1E, 0x08, 0x06, 0x00, 0x00, 0x00, 0x3B, 0x30, 0xAE,
    0xA2, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00,
    0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00,
    0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0E, 0xC2, 0x00, 0x00, 0x0E, 0xC2, 0x01, 0x15,
    0x28, 0x4A, 0x80, 0x00, 0x00, 0x00, 0x62, 0x49, 0x44, 0x41, 0x54, 0x48, 0x4B, 0xED, 0xD0, 0xD1,
    0x0A, 0x80, 0x20, 0x10, 0x44, 0xD1, 0xFD, 0xFF, 0x9F, 0x2E, 0x88, 0x0B, 0x31, 0x61, 0x64, 0xC1,
    0xBA, 0x62, 0x73, 0x9E, 0xC4, 0x55, 0x2F, 0x18, 0xB6, 0xBE, 0xAD, 0x48, 0x7D, 0x98, 0x0F, 0x48,
    0x47, 0xCE, 0xE1, 0x3E, 0x5C, 0x79, 0xC4, 0x71, 0xC1, 0xC8, 0x61, 0xC5, 0x48, 0xDC, 0xED, 0xB7,
    0x1C, 0x8F, 0x5C, 0x30, 0x72, 0x58, 0x31, 0x12, 0x8C, 0x3E, 0xE3, 0x99, 0xF7, 0xE1, 0x16, 0x8E,
    0x77, 0xE1, 0xCA, 0xA4, 0xE1, 0x0C, 0xE4, 0xFE, 0x1E, 0x66, 0x99, 0x66, 0xBE, 0xF0, 0x08, 0xE4,
    0xCE, 0xF0, 0x68, 0x75, 0x61, 0x5B, 0x5C, 0xC4, 0x0E, 0xA0, 0xC4, 0x4A, 0xFC, 0x43, 0x87, 0x66,
    0xD5, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

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


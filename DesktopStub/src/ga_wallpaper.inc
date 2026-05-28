// -----------------------------------------------------------------------------
// Wallpaper
// -----------------------------------------------------------------------------

enum class WallpaperDetectionMethod
{
    TranscodedImageCache,
    SystemParametersInfo,
    DesktopWallpaperCOM,
    TranscodedWallpaperFile,
    Auto
};

enum class LiveWallpaperProvider
{
    None,
    Lively,
    WallpaperEngine
};

static std::atomic<bool> g_liveWallpaperCapturePending(false);

static const wchar_t* WallpaperDetectionMethodName(WallpaperDetectionMethod method)
{
    switch (method)
    {
    case WallpaperDetectionMethod::SystemParametersInfo: return L"SystemParametersInfo";
    case WallpaperDetectionMethod::DesktopWallpaperCOM: return L"DesktopWallpaperCOM";
    case WallpaperDetectionMethod::TranscodedWallpaperFile: return L"TranscodedWallpaperFile";
    case WallpaperDetectionMethod::Auto: return L"Auto";
    case WallpaperDetectionMethod::TranscodedImageCache:
    default:
        return L"TranscodedImageCache";
    }
}

static const wchar_t* LiveWallpaperProviderName(LiveWallpaperProvider provider)
{
    switch (provider)
    {
    case LiveWallpaperProvider::Lively:
        return g_ui.liveWallpaperProviderLively.empty() ? L"Lively" : g_ui.liveWallpaperProviderLively.c_str();
    case LiveWallpaperProvider::WallpaperEngine:
        return g_ui.liveWallpaperProviderWallpaperEngine.empty() ? L"Wallpaper Engine" : g_ui.liveWallpaperProviderWallpaperEngine.c_str();
    case LiveWallpaperProvider::None:
    default:
        return g_ui.liveWallpaperProviderNone.empty() ? L"None" : g_ui.liveWallpaperProviderNone.c_str();
    }
}

static WallpaperDetectionMethod ParseWallpaperDetectionMethod(const std::wstring& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(value));
    if (v == L"auto")
        return WallpaperDetectionMethod::Auto;
    if (v == L"systemparametersinfo" || v == L"spi" || v == L"spi_getdeskwallpaper")
        return WallpaperDetectionMethod::SystemParametersInfo;
    if (v == L"desktopwallpapercom" || v == L"desktopwallpaper" || v == L"idesktopwallpaper" || v == L"com")
        return WallpaperDetectionMethod::DesktopWallpaperCOM;
    if (v == L"transcodedwallpaperfile" || v == L"transcodedwallpaper" || v == L"themefile" || v == L"cachedfile")
        return WallpaperDetectionMethod::TranscodedWallpaperFile;
    return WallpaperDetectionMethod::TranscodedImageCache;
}

static WallpaperDetectionMethod CurrentWallpaperDetectionMethod()
{
    return ParseWallpaperDetectionMethod(
        IniReadS(L"Settings", L"WallpaperDetectionMethod", L"TranscodedImageCache"));
}

static bool ShouldLogWallpaperDetectionEvent(const std::wstring& key)
{
    static std::mutex s_mutex;
    static std::vector<std::pair<std::wstring, steady_clock::time_point>> s_recent;

    int repeatMs = g_wallpaperDetectionLogRepeatMs.load();
    if (repeatMs <= 0)
        return true;

    auto now = steady_clock::now();
    std::lock_guard<std::mutex> lk(s_mutex);
    for (auto& recent : s_recent)
    {
        if (!IEquals(recent.first, key))
            continue;

        if (duration_cast<milliseconds>(now - recent.second).count() < repeatMs)
            return false;

        recent.second = now;
        return true;
    }

    s_recent.push_back(std::make_pair(key, now));
    if (s_recent.size() > 64)
        s_recent.erase(s_recent.begin());
    return true;
}

static std::wstring GetWallpaperFromTranscodedImageCache()
{
    HKEY h; if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h)) return L"";

    DWORD t=0, sz=0;
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, &t, nullptr, &sz) || t!=REG_BINARY)
    { RegCloseKey(h); return L""; }

    std::vector<BYTE> buf(sz);
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, nullptr, buf.data(), &sz))
    { RegCloseKey(h); return L""; }

    RegCloseKey(h);
    if (sz <= 24) return L"";
    size_t byteLen = sz - 24;
    if ((byteLen % sizeof(wchar_t)) != 0)
        return L"";
    size_t len = byteLen / sizeof(wchar_t);
    if (len == 0)
        return L"";

    std::wstring value(len, L'\0');
    memcpy(value.data(), buf.data() + 24, len * sizeof(wchar_t));
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

static std::wstring GetWallpaperFromSystemParametersInfo()
{
    std::vector<wchar_t> buf(32768);
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, (UINT)buf.size(), buf.data(), 0))
        return L"";
    return buf[0] ? std::wstring(buf.data()) : L"";
}

static std::wstring GetWallpaperFromDesktopWallpaperCOM()
{
    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        return L"";

    IDesktopWallpaper* desktopWallpaper = nullptr;
    HRESULT createHr = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&desktopWallpaper));

    std::wstring result;
    if (SUCCEEDED(createHr) && desktopWallpaper)
    {
        LPWSTR wallpaper = nullptr;
        HRESULT getHr = desktopWallpaper->GetWallpaper(nullptr, &wallpaper);
        if (SUCCEEDED(getHr) && wallpaper && *wallpaper)
            result = wallpaper;
        CoTaskMemFree(wallpaper);
        desktopWallpaper->Release();
    }

    if (uninitialize)
        CoUninitialize();
    return result;
}

static std::wstring GetWallpaperFromTranscodedWallpaperFile()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        return L"";

    std::wstring path = std::wstring(appData) + L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
    return PathFileExistsW(path.c_str()) ? path : L"";
}

static bool IsUsableWallpaperPath(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool LiveWallpaperCaptureEnabled()
{
    return IniReadI(L"Settings", L"LiveWallpaperCapture", 1) != 0;
}

static bool LiveWallpaperCaptureLivelyEnabled()
{
    return IniReadI(L"Settings", L"LiveWallpaperCaptureLively", 1) != 0;
}

static bool LiveWallpaperCaptureWallpaperEngineEnabled()
{
    return IniReadI(L"Settings", L"LiveWallpaperCaptureWallpaperEngine", 1) != 0;
}

static int LiveWallpaperCaptureDelayMs()
{
    return IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000);
}

static int LiveWallpaperCaptureRefreshMs()
{
    return IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
}

static bool LiveWallpaperCaptureScreenFallbackEnabled()
{
    return IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) != 0;
}

static int LiveWallpaperCaptureStartupRefreshMs()
{
    return IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
}

static int LiveWallpaperCaptureStartupRefreshDurationMs()
{
    return IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
}

static bool IsProcessRunningByImageName(std::initializer_list<const wchar_t*> imageNames)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    {
        for (const wchar_t* imageName : imageNames)
        {
            if (IEquals(pe.szExeFile, imageName))
            {
                CloseHandle(snap);
                return true;
            }
        }
    }

    CloseHandle(snap);
    return false;
}

static bool QueryProcessImagePathForLiveWallpaper(DWORD pid, std::wstring& imagePath)
{
    imagePath.clear();
    if (pid == 0)
        return false;

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;

    wchar_t buf[MAX_PATH * 4] = {};
    DWORD len = _countof(buf);
    BOOL ok = QueryFullProcessImageNameW(process, 0, buf, &len);
    CloseHandle(process);
    if (!ok || len == 0)
        return false;

    imagePath.assign(buf, len);
    return true;
}

static bool ContainsI(const std::wstring& text, const std::wstring& needle)
{
    if (needle.empty())
        return true;
    return ToLowerCopy(text).find(ToLowerCopy(needle)) != std::wstring::npos;
}

static bool LivelyProcessImageLooksLikeCaptureHost(const std::wstring& baseName, const std::wstring& imagePath)
{
    // Lively.UI.WinUI.exe is the configuration UI and must never be used as a
    // capture candidate. Only actual wallpaper player/renderer hosts belong
    // here.
    if (IEquals(baseName, L"Lively.Player.WebView2.exe") ||
        IEquals(baseName, L"Lively.Player.CefSharp.exe"))
        return true;

    // Lively's video wallpaper plugin is the normal mpv.exe under
    // <Lively install>\Plugins\Mpv\mpv.exe, not Lively.Player.Mpv.exe.
    // Keep this path-aware so a user's unrelated mpv.exe is not treated as a
    // live-wallpaper renderer just because Lively is open.
    if (IEquals(baseName, L"mpv.exe"))
    {
        return ContainsI(imagePath, L"\\Lively Wallpaper\\Plugins\\Mpv\\") ||
            ContainsI(imagePath, L"/Lively Wallpaper/Plugins/Mpv/");
    }

    return false;
}

static bool LivelyProcessIndicatesAppRunning(const std::wstring& baseName, const std::wstring& imagePath)
{
    if (IEquals(baseName, L"Lively.exe") ||
        IEquals(baseName, L"Lively Wallpaper.exe") ||
        IEquals(baseName, L"Lively.UI.WinUI.exe"))
        return true;

    return LivelyProcessImageLooksLikeCaptureHost(baseName, imagePath);
}

static bool IsLivelyRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    {
        std::wstring imagePath;
        QueryProcessImagePathForLiveWallpaper(pe.th32ProcessID, imagePath);
        if (LivelyProcessIndicatesAppRunning(pe.szExeFile, imagePath))
        {
            CloseHandle(snap);
            return true;
        }
    }

    CloseHandle(snap);
    return false;
}

static bool IsWallpaperEngineRunning()
{
    return IsProcessRunningByImageName({
        L"wallpaper32.exe",
        L"wallpaper64.exe"
    });
}

static bool WindowProcessMatchesNames(HWND hwnd, std::initializer_list<const wchar_t*> imageNames)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0)
        return false;

    std::wstring imagePath;
    if (!QueryProcessImagePathForLiveWallpaper(pid, imagePath))
        return false;

    const wchar_t* baseName = PathFindFileNameW(imagePath.c_str());
    if (!baseName || !*baseName)
        baseName = imagePath.c_str();

    for (const wchar_t* imageName : imageNames)
    {
        if (IEquals(baseName, imageName))
            return true;
    }

    return false;
}

static bool WindowProcessMatchesLively(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0)
        return false;

    std::wstring imagePath;
    if (!QueryProcessImagePathForLiveWallpaper(pid, imagePath))
        return false;

    const wchar_t* baseName = PathFindFileNameW(imagePath.c_str());
    if (!baseName || !*baseName)
        baseName = imagePath.c_str();

    return LivelyProcessImageLooksLikeCaptureHost(baseName, imagePath);
}

static bool WindowProcessMatchesProvider(HWND hwnd, LiveWallpaperProvider provider)
{
    switch (provider)
    {
    case LiveWallpaperProvider::Lively:
        return WindowProcessMatchesLively(hwnd);
    case LiveWallpaperProvider::WallpaperEngine:
        return WindowProcessMatchesNames(hwnd, {
            L"wallpaper32.exe",
            L"wallpaper64.exe"
        });
    case LiveWallpaperProvider::None:
    default:
        return false;
    }
}

static bool LiveWallpaperCaptureRectLooksLargeEnough(int width, int height, int& minWidth, int& minHeight);

static bool RectLooksPotentialWallpaperSized(const RECT& rc)
{
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int minWidth = 0;
    int minHeight = 0;
    return LiveWallpaperCaptureRectLooksLargeEnough(width, height, minWidth, minHeight);
}

static bool WindowOrDescendantProcessMatchesProvider(HWND root, LiveWallpaperProvider provider)
{
    if (!root || !IsWindow(root))
        return false;

    if (WindowProcessMatchesProvider(root, provider))
        return true;

    struct DescendantProcessContext
    {
        LiveWallpaperProvider provider;
        bool found;
    } context{ provider, false };

    auto enumProc = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* ctx = reinterpret_cast<DescendantProcessContext*>(lParam);
        if (!ctx || ctx->found || !IsWindow(hwnd))
            return FALSE;

        if (WindowProcessMatchesProvider(hwnd, ctx->provider))
        {
            ctx->found = true;
            return FALSE;
        }

        return TRUE;
    };

    EnumChildWindows(root, enumProc, reinterpret_cast<LPARAM>(&context));
    return context.found;
}

static bool HasPotentialLiveWallpaperChildWindow(HWND root)
{
    if (!root || !IsWindow(root))
        return false;

    struct ChildWindowContext
    {
        bool found;
    } context{ false };

    auto enumProc = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* ctx = reinterpret_cast<ChildWindowContext*>(lParam);
        if (!ctx || ctx->found || !IsWindow(hwnd))
            return FALSE;

        wchar_t className[128] = {};
        GetClassNameW(hwnd, className, _countof(className));
        if (IEquals(className, L"SHELLDLL_DefView"))
            return TRUE;

        if (!IsWindowVisible(hwnd))
            return TRUE;

        RECT rc{};
        if (!GetWindowRect(hwnd, &rc))
            return TRUE;

        if (RectLooksPotentialWallpaperSized(rc))
        {
            ctx->found = true;
            return FALSE;
        }

        return TRUE;
    };

    EnumChildWindows(root, enumProc, reinterpret_cast<LPARAM>(&context));
    return context.found;
}

static LiveWallpaperProvider DetectActiveLiveWallpaperProvider()
{
    if (!LiveWallpaperCaptureEnabled())
        return LiveWallpaperProvider::None;

    if (LiveWallpaperCaptureWallpaperEngineEnabled() && IsWallpaperEngineRunning())
        return LiveWallpaperProvider::WallpaperEngine;

    if (LiveWallpaperCaptureLivelyEnabled() && IsLivelyRunning())
        return LiveWallpaperProvider::Lively;

    return LiveWallpaperProvider::None;
}

static bool EnsureDirectoryExistsSimple(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
        return (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;

    if (CreateDirectoryW(path.c_str(), nullptr))
        return true;

    return GetLastError() == ERROR_ALREADY_EXISTS;
}

static std::wstring GetLiveWallpaperSnapshotDirectory()
{
    std::vector<wchar_t> tempPath(MAX_PATH * 4, L'\0');
    DWORD len = GetTempPathW((DWORD)tempPath.size(), tempPath.data());
    std::wstring base;
    if (len > 0 && len < tempPath.size())
        base.assign(tempPath.data(), len);
    else
        base = GetExeDir() + L"\\";

    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();

    std::wstring dir = base + L"\\DesktopTileGenerator";
    if (!EnsureDirectoryExistsSimple(dir))
        return L"";
    return dir;
}

static std::wstring GetLiveWallpaperSnapshotPath(LiveWallpaperProvider provider, unsigned long long sequence)
{
    std::wstring dir = GetLiveWallpaperSnapshotDirectory();
    if (dir.empty())
        return L"";

    // Use rotating filenames instead of a single overwritten file. The poller
    // compares wallpaper paths, so a successful warm-up recapture must return a
    // different path to trigger regeneration after Lively/WebView2 finishes
    // starting its real renderer. Keep the number of temp files bounded.
    std::wstring slot = std::to_wstring(sequence % 8ull);
    std::wstring fileName = L"LiveWallpaperSnapshot_" + slot + L".bmp";
    switch (provider)
    {
    case LiveWallpaperProvider::Lively:
        fileName = L"LiveWallpaperSnapshot_Lively_" + slot + L".bmp";
        break;
    case LiveWallpaperProvider::WallpaperEngine:
        fileName = L"LiveWallpaperSnapshot_WallpaperEngine_" + slot + L".bmp";
        break;
    case LiveWallpaperProvider::None:
    default:
        break;
    }

    return dir + L"\\" + fileName;
}

static bool ReadBitmapPixels32(HBITMAP hBitmap, int width, int height, std::vector<BYTE>& pixels, BITMAPINFOHEADER& bih)
{
    pixels.clear();
    ZeroMemory(&bih, sizeof(bih));
    if (!hBitmap || width <= 0 || height <= 0)
        return false;

    bih.biSize = sizeof(bih);
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;

    size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4u;
    if (imageSize == 0 || imageSize > 512u * 1024u * 1024u)
        return false;

    pixels.resize(imageSize);

    HDC hdc = GetDC(nullptr);
    if (!hdc)
        return false;

    int scanLines = GetDIBits(hdc, hBitmap, 0, static_cast<UINT>(height), pixels.data(), reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
    ReleaseDC(nullptr, hdc);
    if (scanLines != height)
    {
        pixels.clear();
        return false;
    }

    return true;
}

static bool PixelsLookBlankOrBlack(const std::vector<BYTE>& pixels)
{
    if (pixels.size() < 4)
        return true;

    const size_t pixelCount = pixels.size() / 4;
    const size_t step = std::max<size_t>(1, pixelCount / 32768);
    size_t samples = 0;
    size_t nearBlack = 0;
    size_t sameAsFirst = 0;

    BYTE firstB = pixels[0];
    BYTE firstG = pixels[1];
    BYTE firstR = pixels[2];

    unsigned long long sum = 0;
    unsigned long long sumSq = 0;

    for (size_t i = 0; i < pixelCount; i += step)
    {
        size_t p = i * 4;
        BYTE b = pixels[p + 0];
        BYTE g = pixels[p + 1];
        BYTE r = pixels[p + 2];
        unsigned int brightness = (static_cast<unsigned int>(r) + static_cast<unsigned int>(g) + static_cast<unsigned int>(b)) / 3u;

        if (r <= 4 && g <= 4 && b <= 4)
            ++nearBlack;
        if (r == firstR && g == firstG && b == firstB)
            ++sameAsFirst;

        sum += brightness;
        sumSq += static_cast<unsigned long long>(brightness) * brightness;
        ++samples;
    }

    if (samples == 0)
        return true;

    // Reject the exact failure mode shown by Lively/PrintWindow: a fully black
    // but otherwise valid BMP. Also reject almost-solid frames, because they are
    // usually a helper/blank surface rather than the animated wallpaper.
    if (nearBlack * 1000 >= samples * 995)
        return true;
    if (sameAsFirst * 1000 >= samples * 998)
        return true;

    double mean = static_cast<double>(sum) / static_cast<double>(samples);
    double variance = static_cast<double>(sumSq) / static_cast<double>(samples) - mean * mean;
    return mean < 2.0 && variance < 1.0;
}

static void LiveWallpaperMinimumCaptureSize(int& minWidth, int& minHeight)
{
    int primaryWidth = GetSystemMetrics(SM_CXSCREEN);
    int primaryHeight = GetSystemMetrics(SM_CYSCREEN);

    if (primaryWidth <= 0)
        primaryWidth = 1920;
    if (primaryHeight <= 0)
        primaryHeight = 1080;

    // A real live-wallpaper host should be monitor/desktop sized. Tiny Lively
    // helper windows can still contain colorful pixels, so black-frame detection
    // alone is not enough. Use a conservative monitor-derived floor so one
    // monitor in a multi-monitor setup is still accepted, but 136x39/196x204 UI
    // fragments are rejected.
    minWidth = std::max(320, primaryWidth / 3);
    minHeight = std::max(180, primaryHeight / 3);
}

static bool LiveWallpaperCaptureRectLooksLargeEnough(int width, int height, int& minWidth, int& minHeight)
{
    LiveWallpaperMinimumCaptureSize(minWidth, minHeight);
    return width >= minWidth && height >= minHeight;
}

static bool SaveBitmapPixelsAsBmpFile(const std::vector<BYTE>& pixels, const BITMAPINFOHEADER& bih, const std::wstring& path)
{
    if (pixels.empty() || bih.biWidth <= 0 || bih.biHeight <= 0 || path.empty())
        return false;

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    BITMAPFILEHEADER bfh{};
    DWORD pixelBytes = static_cast<DWORD>(pixels.size());
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + pixelBytes;

    DWORD written = 0;
    bool ok = WriteFile(file, &bfh, sizeof(bfh), &written, nullptr) != FALSE && written == sizeof(bfh);
    ok = ok && (WriteFile(file, &bih, sizeof(bih), &written, nullptr) != FALSE && written == sizeof(bih));
    ok = ok && (WriteFile(file, pixels.data(), pixelBytes, &written, nullptr) != FALSE && written == pixelBytes);
    CloseHandle(file);
    return ok;
}

static bool CaptureWindowToBmpFile(
    HWND hwnd,
    const std::wstring& path,
    bool& rejectedBlank,
    bool& rejectedTooSmall,
    int& rejectedWidth,
    int& rejectedHeight,
    int& rejectedMinWidth,
    int& rejectedMinHeight)
{
    rejectedBlank = false;
    rejectedTooSmall = false;
    rejectedWidth = 0;
    rejectedHeight = 0;
    rejectedMinWidth = 0;
    rejectedMinHeight = 0;
    if (!hwnd || path.empty())
        return false;

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc))
        return false;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0)
        return false;

    int minWidth = 0;
    int minHeight = 0;
    if (!LiveWallpaperCaptureRectLooksLargeEnough(width, height, minWidth, minHeight))
    {
        rejectedTooSmall = true;
        rejectedWidth = width;
        rejectedHeight = height;
        rejectedMinWidth = minWidth;
        rejectedMinHeight = minHeight;
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return false;

    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    if (!memDc || !bitmap)
    {
        if (bitmap) DeleteObject(bitmap);
        if (memDc) DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return false;
    }

    HGDIOBJ oldObj = SelectObject(memDc, bitmap);
    BOOL captured = PrintWindow(hwnd, memDc, 2);

    std::vector<BYTE> pixels;
    BITMAPINFOHEADER bih{};
    bool havePixels = captured && ReadBitmapPixels32(bitmap, width, height, pixels, bih);
    bool blank = !havePixels || PixelsLookBlankOrBlack(pixels);

    if (blank && LiveWallpaperCaptureScreenFallbackEnabled())
    {
        // Unsafe/debug fallback: PrintWindow often returns a formally
        // successful but black image for GPU/WebView/DirectX wallpaper
        // surfaces. A screen DC copy can capture what is visible on the
        // desktop, but it can also capture the user's static wallpaper, icons,
        // taskbar, or overlays. Keep it opt-in so it cannot briefly poison the
        // Live Tile with a desktop screenshot.
        if (BitBlt(memDc, 0, 0, width, height, screenDc, rc.left, rc.top, SRCCOPY))
        {
            pixels.clear();
            ZeroMemory(&bih, sizeof(bih));
            havePixels = ReadBitmapPixels32(bitmap, width, height, pixels, bih);
            blank = !havePixels || PixelsLookBlankOrBlack(pixels);
        }
    }

    bool ok = false;
    if (havePixels && !blank)
        ok = SaveBitmapPixelsAsBmpFile(pixels, bih, path);
    else
        rejectedBlank = true;

    SelectObject(memDc, oldObj);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    return ok;
}

static void AddUniqueWindowCandidate(std::vector<HWND>& candidates, HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd))
        return;
    for (HWND existing : candidates)
    {
        if (existing == hwnd)
            return;
    }
    candidates.push_back(hwnd);
}

static bool AddProviderDescendantWindowCandidates(std::vector<HWND>& candidates, HWND root, LiveWallpaperProvider provider)
{
    if (!root || !IsWindow(root))
        return false;

    struct ProviderDescendantContext
    {
        LiveWallpaperProvider provider;
        std::vector<HWND>* candidates;
        bool added;
    } context{ provider, &candidates, false };

    auto enumProc = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* ctx = reinterpret_cast<ProviderDescendantContext*>(lParam);
        if (!ctx || !ctx->candidates || !IsWindow(hwnd))
            return TRUE;

        if (!IsWindowVisible(hwnd))
            return TRUE;

        if (!WindowProcessMatchesProvider(hwnd, ctx->provider))
            return TRUE;

        RECT rc{};
        if (!GetWindowRect(hwnd, &rc) || !RectLooksPotentialWallpaperSized(rc))
            return TRUE;

        AddUniqueWindowCandidate(*ctx->candidates, hwnd);
        ctx->added = true;
        return TRUE;
    };

    EnumChildWindows(root, enumProc, reinterpret_cast<LPARAM>(&context));
    return context.added;
}

static std::vector<HWND> FindLiveWallpaperWindowCandidates(LiveWallpaperProvider provider)
{
    std::vector<HWND> candidates;
    std::vector<HWND> workerCandidates;
    std::vector<HWND> genericWorkerWsWithChildren;

    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr)
    {
        // The WorkerW that owns SHELLDLL_DefView is the desktop icon layer, not
        // the live wallpaper renderer. Capturing it either returns icons or the
        // normal static wallpaper behind Lively.
        if (FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr)
            continue;

        bool addedDescendant = AddProviderDescendantWindowCandidates(candidates, worker, provider);
        if (WindowOrDescendantProcessMatchesProvider(worker, provider))
        {
            // Prefer the actual renderer/host child window over the parent
            // WorkerW. The parent is useful as a last resort, but screen-based
            // fallback on WorkerW is exactly how a static desktop screenshot can
            // leak into the tile.
            if (!addedDescendant)
                AddUniqueWindowCandidate(workerCandidates, worker);
            else
                AddUniqueWindowCandidate(workerCandidates, worker);
            continue;
        }

        // Lively's "Application Wallpaper" mode may embed an arbitrary app whose
        // process name is not known to DesktopStub. Accept a non-icon WorkerW only
        // when it actually contains a visible, monitor-sized child window. A bare
        // empty WorkerW usually just exposes the user's normal wallpaper.
        if (HasPotentialLiveWallpaperChildWindow(worker))
            AddUniqueWindowCandidate(genericWorkerWsWithChildren, worker);
    }

    struct FindProviderWindowContext
    {
        LiveWallpaperProvider provider;
        std::vector<HWND>* candidates;
    } context{ provider, &candidates };

    auto enumProc = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* ctx = reinterpret_cast<FindProviderWindowContext*>(lParam);
        if (!ctx || !ctx->candidates || !IsWindow(hwnd))
            return TRUE;
        if (!WindowProcessMatchesProvider(hwnd, ctx->provider))
            return TRUE;

        AddProviderDescendantWindowCandidates(*ctx->candidates, hwnd, ctx->provider);

        RECT rc{};
        if (IsWindowVisible(hwnd) && GetWindowRect(hwnd, &rc) && RectLooksPotentialWallpaperSized(rc))
            AddUniqueWindowCandidate(*ctx->candidates, hwnd);
        return TRUE;
    };

    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&context));

    for (HWND hwnd : workerCandidates)
        AddUniqueWindowCandidate(candidates, hwnd);
    for (HWND hwnd : genericWorkerWsWithChildren)
        AddUniqueWindowCandidate(candidates, hwnd);

    return candidates;
}

static std::wstring TryGetLiveWallpaperSnapshot()
{
    static std::mutex s_liveWallpaperMutex;
    static LiveWallpaperProvider s_activeProvider = LiveWallpaperProvider::None;
    static steady_clock::time_point s_activeSince{};
    static steady_clock::time_point s_lastCapture{};
    static std::wstring s_snapshotPath;
    static std::wstring s_pendingStartupSnapshotPath;
    static unsigned long long s_captureSequence = 0;
    static bool s_startupSettleActive = false;
    static bool s_waitingForHostWindow = false;

    auto resetStateUnlocked = [&]()
    {
        s_activeProvider = LiveWallpaperProvider::None;
        s_snapshotPath.clear();
        s_pendingStartupSnapshotPath.clear();
        s_activeSince = steady_clock::time_point{};
        s_lastCapture = steady_clock::time_point{};
        s_captureSequence = 0;
        s_startupSettleActive = false;
        s_waitingForHostWindow = false;
        g_liveWallpaperCapturePending = false;
    };

    LiveWallpaperProvider provider = DetectActiveLiveWallpaperProvider();
    if (provider == LiveWallpaperProvider::None)
    {
        std::lock_guard<std::mutex> lk(s_liveWallpaperMutex);
        resetStateUnlocked();
        return L"";
    }

    std::vector<HWND> candidates = FindLiveWallpaperWindowCandidates(provider);

    std::lock_guard<std::mutex> lk(s_liveWallpaperMutex);
    auto now = steady_clock::now();

    int startupRefreshMs = LiveWallpaperCaptureStartupRefreshMs();
    int startupRefreshDurationMs = LiveWallpaperCaptureStartupRefreshDurationMs();
    if (candidates.empty())
    {
        if (provider != s_activeProvider)
        {
            s_activeProvider = provider;
            s_activeSince = now;
            s_lastCapture = steady_clock::time_point{};
            s_snapshotPath.clear();
            s_pendingStartupSnapshotPath.clear();
            s_captureSequence = 0;
            s_startupSettleActive = false;
            s_waitingForHostWindow = true;
        }
        else
        {
            s_waitingForHostWindow = true;
        }

        long long providerAgeMs = duration_cast<milliseconds>(now - s_activeSince).count();
        bool stillStarting = startupRefreshDurationMs > 0 && providerAgeMs < startupRefreshDurationMs;
        g_liveWallpaperCapturePending = stillStarting;
        if (!stillStarting)
        {
            s_snapshotPath.clear();
            s_pendingStartupSnapshotPath.clear();
            s_lastCapture = steady_clock::time_point{};
            s_startupSettleActive = false;
        }

        std::wstring key = L"live-wallpaper-no-window:" + std::wstring(LiveWallpaperProviderName(provider));
        if (ShouldLogWallpaperDetectionEvent(key))
            Log(g_ui.liveWallpaperCaptureNoWindow.c_str(), LiveWallpaperProviderName(provider));
        return L"";
    }

    if (provider != s_activeProvider || s_waitingForHostWindow)
    {
        s_activeProvider = provider;
        s_activeSince = now;
        s_lastCapture = steady_clock::time_point{};
        s_snapshotPath.clear();
        s_pendingStartupSnapshotPath.clear();
        s_captureSequence = 0;
        s_startupSettleActive = startupRefreshMs > 0 && startupRefreshDurationMs > 0;
        s_waitingForHostWindow = false;
        g_liveWallpaperCapturePending = s_startupSettleActive;
    }

    int delayMs = LiveWallpaperCaptureDelayMs();
    if (s_snapshotPath.empty() && s_pendingStartupSnapshotPath.empty() && delayMs > 0)
    {
        long long elapsedMs = duration_cast<milliseconds>(now - s_activeSince).count();
        if (elapsedMs < delayMs)
        {
            Sleep(static_cast<DWORD>(delayMs - elapsedMs));
            now = steady_clock::now();
        }
    }

    auto captureSnapshot = [&](std::wstring& capturedPath) -> bool
    {
        capturedPath.clear();
        std::wstring path = GetLiveWallpaperSnapshotPath(provider, ++s_captureSequence);
        if (path.empty())
        {
            std::wstring key = L"live-wallpaper-temp-path:" + std::wstring(LiveWallpaperProviderName(provider));
            if (ShouldLogWallpaperDetectionEvent(key))
                Log(g_ui.liveWallpaperCaptureSnapshotPathFailed.c_str(), LiveWallpaperProviderName(provider));
            return false;
        }

        bool rejectedBlank = false;
        bool rejectedTooSmall = false;
        int rejectedWidth = 0;
        int rejectedHeight = 0;
        int rejectedMinWidth = 0;
        int rejectedMinHeight = 0;
        bool captured = false;
        for (HWND candidate : candidates)
        {
            bool thisRejectedBlank = false;
            bool thisRejectedTooSmall = false;
            int thisRejectedWidth = 0;
            int thisRejectedHeight = 0;
            int thisRejectedMinWidth = 0;
            int thisRejectedMinHeight = 0;
            if (CaptureWindowToBmpFile(
                candidate,
                path,
                thisRejectedBlank,
                thisRejectedTooSmall,
                thisRejectedWidth,
                thisRejectedHeight,
                thisRejectedMinWidth,
                thisRejectedMinHeight))
            {
                captured = true;
                rejectedBlank = false;
                rejectedTooSmall = false;
                break;
            }
            rejectedBlank = rejectedBlank || thisRejectedBlank;
            if (thisRejectedTooSmall)
            {
                rejectedTooSmall = true;
                rejectedWidth = thisRejectedWidth;
                rejectedHeight = thisRejectedHeight;
                rejectedMinWidth = thisRejectedMinWidth;
                rejectedMinHeight = thisRejectedMinHeight;
            }
        }

        if (!captured)
        {
            std::wstring key = L"live-wallpaper-capture-failed:" + std::wstring(LiveWallpaperProviderName(provider));
            if (ShouldLogWallpaperDetectionEvent(key))
            {
                if (rejectedTooSmall)
                    Log(g_ui.liveWallpaperCaptureSnapshotRejectedTooSmall.c_str(), LiveWallpaperProviderName(provider), rejectedWidth, rejectedHeight, rejectedMinWidth, rejectedMinHeight);
                else if (rejectedBlank)
                    Log(g_ui.liveWallpaperCaptureSnapshotRejectedBlank.c_str(), LiveWallpaperProviderName(provider));
                else
                    Log(g_ui.liveWallpaperCaptureSnapshotFailed.c_str(), LiveWallpaperProviderName(provider));
            }
            return false;
        }

        s_lastCapture = steady_clock::now();
        capturedPath = path;

        std::wstring key = L"live-wallpaper-capture-success:" + std::wstring(LiveWallpaperProviderName(provider)) + L":" + capturedPath;
        if (ShouldLogWallpaperDetectionEvent(key))
            Log(g_ui.liveWallpaperCaptureSnapshotCaptured.c_str(), LiveWallpaperProviderName(provider), capturedPath.c_str());
        return true;
    };

    long long providerAgeMs = duration_cast<milliseconds>(now - s_activeSince).count();
    if (s_startupSettleActive)
    {
        bool settleFinished = providerAgeMs >= startupRefreshDurationMs;
        bool shouldCapture = s_pendingStartupSnapshotPath.empty();
        if (!shouldCapture && startupRefreshMs > 0)
            shouldCapture = duration_cast<milliseconds>(now - s_lastCapture).count() >= startupRefreshMs;
        if (settleFinished && s_pendingStartupSnapshotPath.empty())
            shouldCapture = true;

        if (shouldCapture)
        {
            std::wstring capturedPath;
            if (captureSnapshot(capturedPath))
                s_pendingStartupSnapshotPath = capturedPath;
            now = steady_clock::now();
            providerAgeMs = duration_cast<milliseconds>(now - s_activeSince).count();
            settleFinished = providerAgeMs >= startupRefreshDurationMs;
        }

        if (!settleFinished)
        {
            // Do not publish the warm-up snapshots one by one. Also do not let
            // GetWallpaper fall back to the real static wallpaper while a live
            // wallpaper host is still warming up; that caused a brief static
            // desktop tile before the settled Lively snapshot arrived.
            g_liveWallpaperCapturePending = true;
            return L"";
        }

        s_startupSettleActive = false;
        g_liveWallpaperCapturePending = false;
        if (IsUsableWallpaperPath(s_pendingStartupSnapshotPath))
        {
            s_snapshotPath = s_pendingStartupSnapshotPath;
            s_pendingStartupSnapshotPath.clear();
            return s_snapshotPath;
        }

        s_pendingStartupSnapshotPath.clear();
        return L"";
    }

    if (!s_snapshotPath.empty() && IsUsableWallpaperPath(s_snapshotPath))
    {
        int refreshMs = LiveWallpaperCaptureRefreshMs();
        if (refreshMs <= 0 || duration_cast<milliseconds>(now - s_lastCapture).count() < refreshMs)
        {
            g_liveWallpaperCapturePending = false;
            return s_snapshotPath;
        }
    }

    std::wstring capturedPath;
    if (captureSnapshot(capturedPath))
    {
        s_snapshotPath = capturedPath;
        g_liveWallpaperCapturePending = false;
        return s_snapshotPath;
    }

    // If a live wallpaper is still present but this refresh failed, keep the
    // previous published snapshot. If no snapshot has ever been published, fall
    // back to the real Windows wallpaper.
    g_liveWallpaperCapturePending = false;
    return IsUsableWallpaperPath(s_snapshotPath) ? s_snapshotPath : L"";
}

static std::wstring GetWallpaperByMethod(WallpaperDetectionMethod method)
{
    switch (method)
    {
    case WallpaperDetectionMethod::SystemParametersInfo:
        return GetWallpaperFromSystemParametersInfo();
    case WallpaperDetectionMethod::DesktopWallpaperCOM:
        return GetWallpaperFromDesktopWallpaperCOM();
    case WallpaperDetectionMethod::TranscodedWallpaperFile:
        return GetWallpaperFromTranscodedWallpaperFile();
    case WallpaperDetectionMethod::Auto:
    case WallpaperDetectionMethod::TranscodedImageCache:
    default:
        return GetWallpaperFromTranscodedImageCache();
    }
}

static std::wstring TryWallpaperMethod(WallpaperDetectionMethod method, bool logFailure)
{
    std::wstring wallpaper = GetWallpaperByMethod(method);
    if (IsUsableWallpaperPath(wallpaper))
        return wallpaper;

    if (logFailure)
    {
        const wchar_t* name = WallpaperDetectionMethodName(method);
        std::wstring key = L"wallpaper-method:";
        key += name;
        key += L":";
        key += wallpaper.empty() ? L"(empty)" : wallpaper;
        if (ShouldLogWallpaperDetectionEvent(key))
        {
            if (wallpaper.empty())
                Log(g_ui.wallpaperDetectionMethodFailed.c_str(), name);
            else
                Log(g_ui.wallpaperDetectionMethodReturnedInvalid.c_str(), name, wallpaper.c_str());
        }
    }
    return L"";
}

static std::wstring FindWallpaperByFallback(WallpaperDetectionMethod skip)
{
    const WallpaperDetectionMethod order[] = {
        WallpaperDetectionMethod::TranscodedImageCache,
        WallpaperDetectionMethod::DesktopWallpaperCOM,
        WallpaperDetectionMethod::SystemParametersInfo,
        WallpaperDetectionMethod::TranscodedWallpaperFile
    };

    for (WallpaperDetectionMethod candidate : order)
    {
        if (candidate == skip)
            continue;

        std::wstring wallpaper = TryWallpaperMethod(candidate, true);
        if (!wallpaper.empty())
        {
            std::wstring key = L"wallpaper-fallback:";
            key += WallpaperDetectionMethodName(candidate);
            key += L":";
            key += wallpaper;
            if (ShouldLogWallpaperDetectionEvent(key))
                Log(g_ui.wallpaperDetectionFallbackSelected.c_str(), WallpaperDetectionMethodName(candidate), wallpaper.c_str());
            return wallpaper;
        }
    }

    return L"";
}

std::wstring GetWallpaper()
{
    std::wstring liveWallpaper = TryGetLiveWallpaperSnapshot();
    if (!liveWallpaper.empty())
        return liveWallpaper;
    if (g_liveWallpaperCapturePending)
        return L"";

    WallpaperDetectionMethod method = CurrentWallpaperDetectionMethod();
    switch (method)
    {
    case WallpaperDetectionMethod::Auto:
    {
        std::wstring wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::TranscodedImageCache, true);
        if (!wallpaper.empty()) return wallpaper;
        wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::DesktopWallpaperCOM, true);
        if (!wallpaper.empty()) return wallpaper;
        wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::SystemParametersInfo, true);
        if (!wallpaper.empty()) return wallpaper;
        return TryWallpaperMethod(WallpaperDetectionMethod::TranscodedWallpaperFile, true);
    }
    case WallpaperDetectionMethod::TranscodedImageCache:
    case WallpaperDetectionMethod::SystemParametersInfo:
    case WallpaperDetectionMethod::DesktopWallpaperCOM:
    case WallpaperDetectionMethod::TranscodedWallpaperFile:
    default:
    {
        std::wstring wallpaper = GetWallpaperByMethod(method);
        if (IsUsableWallpaperPath(wallpaper))
            return wallpaper;

        const wchar_t* methodName = WallpaperDetectionMethodName(method);
        std::wstring key = L"wallpaper-selected:";
        key += methodName;
        key += L":";
        key += wallpaper.empty() ? L"(empty)" : wallpaper;
        if (ShouldLogWallpaperDetectionEvent(key))
        {
            if (wallpaper.empty())
                Log(g_ui.wallpaperDetectionMethodFailed.c_str(), methodName);
            else
                Log(g_ui.wallpaperDetectionMethodReturnedInvalid.c_str(), methodName, wallpaper.c_str());
        }

        if (!g_wallpaperDetectionFallbackOnInvalid)
        {
            std::wstring disabledKey = L"wallpaper-fallback-disabled:";
            disabledKey += methodName;
            if (ShouldLogWallpaperDetectionEvent(disabledKey))
                Log(g_ui.wallpaperDetectionFallbackDisabled.c_str(), methodName);
            return wallpaper;
        }

        return FindWallpaperByFallback(method);
    }
    }
}

FitMode GetWallpaperFit()
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h))
        return FitMode::Fill;

    wchar_t style[16] = {};
    wchar_t tile[8] = {};

    DWORD sz = sizeof(style);
    LSTATUS styleStatus = RegQueryValueExW(h, L"WallpaperStyle", nullptr, nullptr, (LPBYTE)style, &sz);

    sz = sizeof(tile);
    LSTATUS tileStatus = RegQueryValueExW(h, L"TileWallpaper", nullptr, nullptr, (LPBYTE)tile, &sz);

    RegCloseKey(h);
    if (styleStatus != ERROR_SUCCESS || tileStatus != ERROR_SUCCESS)
        return FitMode::Fill;

    int s = 0;
    int t = 0;
    if (!TryParseIntStrict(style, s) || !TryParseIntStrict(tile, t))
        return FitMode::Fill;

    if (t == 1) return FitMode::Tile;

    switch (s)
    {
    case 10: return FitMode::Fill;
    case 6:  return FitMode::Fit;
    case 2:  return FitMode::Stretch;
    case 22: return FitMode::Span;
    default: return FitMode::Center;
    }
}

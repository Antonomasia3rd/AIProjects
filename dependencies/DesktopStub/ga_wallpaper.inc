// -----------------------------------------------------------------------------
// Wallpaper
// -----------------------------------------------------------------------------
#include "../live_capture_protocol.h"
#include "../bounded_capture_process.inc"
#include "../tile_policy_scope.h"

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
    Active
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
    case LiveWallpaperProvider::Active:
        return g_ui.liveWallpaperProviderLive.empty() ? L"Live wallpaper" : g_ui.liveWallpaperProviderLive.c_str();
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

static bool WallpaperDetectionFallbackEnabled()
{
    std::wstring value;
    if (aip::content::TryReadTilePolicy(L"Settings", L"WallpaperDetectionFallbackOnInvalid", value)) {
        return value == L"1";
    }
    return g_wallpaperDetectionFallbackOnInvalid.load();
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
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)) ||
        appData == nullptr)
        return L"";

    std::wstring path;
    try
    {
        path = std::wstring(appData) + L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
    }
    catch (...)
    {
        CoTaskMemFree(appData);
        throw;
    }
    CoTaskMemFree(appData);
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
    return IniReadI(L"Settings", L"LiveWallpaperCapture", 0) != 0;
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

static bool LiveWallpaperCaptureRectLooksLargeEnough(int width, int height, int& minWidth, int& minHeight);

static bool RectLooksPotentialWallpaperSized(const RECT& rc)
{
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    int minWidth = 0;
    int minHeight = 0;
    return LiveWallpaperCaptureRectLooksLargeEnough(width, height, minWidth, minHeight);
}

static bool WindowLooksLiveWallpaperCandidate(HWND hwnd)
{
    if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
        return false;

    wchar_t className[128] = {};
    GetClassNameW(hwnd, className, _countof(className));
    if (IEquals(className, L"SHELLDLL_DefView"))
        return false;

    RECT rc{};
    return GetWindowRect(hwnd, &rc) && RectLooksPotentialWallpaperSized(rc);
}

static bool HasPotentialLiveWallpaperChildWindow(HWND root);

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


static bool HasLiveWallpaperHost()
{
    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr)
    {
        if (FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr)
            continue;
        if (HasPotentialLiveWallpaperChildWindow(worker))
            return true;
    }
    return false;
}

static LiveWallpaperProvider DetectActiveLiveWallpaperProvider()
{
    if (!LiveWallpaperCaptureEnabled())
        return LiveWallpaperProvider::None;

    if (HasLiveWallpaperHost())
        return LiveWallpaperProvider::Active;

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
    std::wstring base = aip::GetTemporaryDirectoryPath();
    if (base.empty())
        base = GetExeDir() + L"\\";

    while (!base.empty() && (base.back() == L'\\' || base.back() == L'/'))
        base.pop_back();

    std::wstring productBase = g_exePath;
    size_t slash = productBase.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        productBase.erase(0, slash + 1);
    size_t dot = productBase.find_last_of(L'.');
    if (dot != std::wstring::npos)
        productBase.erase(dot);
    if (productBase.empty())
        productBase = L"DesktopStub";

    std::wstring dir = base + L"\\" + productBase;
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
    // different path triggers regeneration after a WorkerW-hosted live wallpaper
    // finishes painting. Keep the number of temp files bounded.
    std::wstring slot = std::to_wstring(sequence % 8ull);
    std::wstring fileName = L"LiveWallpaperSnapshot_" + slot + L".bmp";
    switch (provider)
    {
    case LiveWallpaperProvider::Active:
        break;
    case LiveWallpaperProvider::None:
    default:
        break;
    }

    return dir + L"\\" + fileName;
}

static bool SurfaceLooksBlankOrBlack(HDC dc, int width, int height)
{
    if (!dc || width <= 0 || height <= 0)
        return true;

    const unsigned long long pixelCount = static_cast<unsigned long long>(width) * static_cast<unsigned long long>(height);
    if (pixelCount == 0)
        return true;

    const unsigned long long step = std::max<unsigned long long>(1, pixelCount / 32768ull);
    size_t samples = 0;
    size_t nearBlack = 0;
    size_t sameAsFirst = 0;
    bool haveFirst = false;
    BYTE firstR = 0;
    BYTE firstG = 0;
    BYTE firstB = 0;

    unsigned long long sum = 0;
    unsigned long long sumSq = 0;

    for (unsigned long long i = 0; i < pixelCount; i += step)
    {
        int x = static_cast<int>(i % static_cast<unsigned long long>(width));
        int y = static_cast<int>(i / static_cast<unsigned long long>(width));
        if (y >= height)
            y = height - 1;

        COLORREF color = GetPixel(dc, x, y);
        if (color == CLR_INVALID)
            continue;

        BYTE r = GetRValue(color);
        BYTE g = GetGValue(color);
        BYTE b = GetBValue(color);
        if (!haveFirst)
        {
            firstR = r;
            firstG = g;
            firstB = b;
            haveFirst = true;
        }

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

    // A real live-wallpaper host should be monitor/desktop sized. Tiny helper
    // windows can still contain colorful pixels, so black-frame detection
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

static bool WriteFileExact(HANDLE file, const void* data, DWORD bytes)
{
    const BYTE* cursor = static_cast<const BYTE*>(data);
    DWORD remaining = bytes;
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0)
            return false;
        cursor += written;
        remaining -= written;
    }
    return true;
}

static bool SaveHBitmapAsBmpHandle(HBITMAP hBitmap, int width, int height, HANDLE file)
{
    if (!hBitmap || width <= 0 || height <= 0 || !file || file == INVALID_HANDLE_VALUE)
        return false;

    const size_t stride = static_cast<size_t>(width) * 4u;
    if (stride / 4u != static_cast<size_t>(width))
        return false;
    if (static_cast<size_t>(height) > static_cast<size_t>(-1) / stride)
        return false;

    size_t imageSize = stride * static_cast<size_t>(height);
    if (imageSize == 0 || imageSize > 512u * 1024u * 1024u || imageSize > static_cast<size_t>(DWORD_MAX))
        return false;

    BITMAPINFOHEADER bih{};
    bih.biSize = sizeof(bih);
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biPlanes = 1;
    bih.biBitCount = 32;
    bih.biCompression = BI_RGB;
    bih.biSizeImage = static_cast<DWORD>(imageSize);

    BITMAPFILEHEADER bfh{};
    bfh.bfType = 0x4D42;
    bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bfh.bfSize = bfh.bfOffBits + static_cast<DWORD>(imageSize);

    bool ok = WriteFileExact(file, &bfh, static_cast<DWORD>(sizeof(bfh))) &&
        WriteFileExact(file, &bih, static_cast<DWORD>(sizeof(bih)));

    HDC hdc = nullptr;
    if (ok)
    {
        hdc = GetDC(nullptr);
        if (!hdc)
            ok = false;
    }

    const size_t maxChunkBytes = 4u * 1024u * 1024u;
    int rowsPerChunk = 1;
    if (stride > 0)
        rowsPerChunk = static_cast<int>(std::max<size_t>(1, std::min<size_t>(static_cast<size_t>(height), maxChunkBytes / stride)));

    std::vector<BYTE> chunk;
    if (ok)
    {
        try
        {
            chunk.resize(stride * static_cast<size_t>(rowsPerChunk));
        }
        catch (...)
        {
            ok = false;
        }
    }

    for (int start = 0; ok && start < height; start += rowsPerChunk)
    {
        int rows = std::min(rowsPerChunk, height - start);
        size_t chunkBytes = stride * static_cast<size_t>(rows);
        int scanLines = GetDIBits(hdc, hBitmap, static_cast<UINT>(start), static_cast<UINT>(rows),
            chunk.data(), reinterpret_cast<BITMAPINFO*>(&bih), DIB_RGB_COLORS);
        if (scanLines != rows || chunkBytes > static_cast<size_t>(DWORD_MAX))
        {
            ok = false;
            break;
        }
        ok = WriteFileExact(file, chunk.data(), static_cast<DWORD>(chunkBytes));
    }

    if (hdc)
        ReleaseDC(nullptr, hdc);
    return ok;
}

// The raw window/screen operations are called only by the isolated helper.
static aip::capture::Reply CaptureWindowToHandle(const aip::capture::HelperRequest& request)
{
    auto reply = aip::capture::MakeReply(request, aip::capture::Status::CaptureFailed);
    const auto hwnd = reinterpret_cast<HWND>(request.window);
    DWORD owner = 0;
    if (!hwnd || !GetWindowThreadProcessId(hwnd, &owner) || owner != request.windowProcessId) {
        reply.status = aip::capture::Status::WindowChanged; reply.error = ERROR_INVALID_WINDOW_HANDLE; return reply;
    }

    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) { reply.error = GetLastError(); return reply; }

    const auto wideWidth = static_cast<long long>(rc.right) - rc.left;
    const auto wideHeight = static_cast<long long>(rc.bottom) - rc.top;
    if (wideWidth <= 0 || wideHeight <= 0 || wideWidth > INT_MAX || wideHeight > INT_MAX) return reply;
    int width = static_cast<int>(wideWidth);
    int height = static_cast<int>(wideHeight);
    if (width <= 0 || height <= 0) return reply;
    reply.width = width; reply.height = height;

    if (width < request.minimumWidth || height < request.minimumHeight)
    {
        reply.flags |= aip::capture::RejectedTooSmall; return reply;
    }
    if (static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 4 > aip::capture::MaximumBitmapBytes)
        { reply.error = ERROR_NOT_ENOUGH_MEMORY; return reply; }

    HDC screenDc = GetDC(nullptr);
    if (!screenDc) { reply.error = GetLastError(); return reply; }

    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, width, height);
    if (!memDc || !bitmap)
    {
        if (bitmap) DeleteObject(bitmap);
        if (memDc) DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return reply;
    }

    HGDIOBJ oldObj = SelectObject(memDc, bitmap);
    if (!GetWindowThreadProcessId(hwnd, &owner) || owner != request.windowProcessId) {
        if (oldObj && oldObj != HGDI_ERROR) SelectObject(memDc, oldObj);
        DeleteObject(bitmap); DeleteDC(memDc); ReleaseDC(nullptr, screenDc);
        reply.status = aip::capture::Status::WindowChanged; reply.error = ERROR_INVALID_WINDOW_HANDLE; return reply;
    }
    BOOL captured = PrintWindow(hwnd, memDc, 2);

    bool haveSurface = captured != FALSE;
    bool blank = !haveSurface || SurfaceLooksBlankOrBlack(memDc, width, height);

    if (blank && request.screenFallback)
    {
        // Unsafe/debug fallback: PrintWindow often returns a formally
        // successful but black image for GPU/WebView/DirectX wallpaper
        // surfaces. A screen DC copy can capture what is visible on the
        // desktop, but it can also capture the user's static wallpaper, icons,
        // taskbar, or overlays. Keep it opt-in so it cannot briefly poison the
        // Live Tile with a desktop screenshot.
        if (BitBlt(memDc, 0, 0, width, height, screenDc, rc.left, rc.top, SRCCOPY))
        {
            haveSurface = true;
            blank = SurfaceLooksBlankOrBlack(memDc, width, height);
        }
    }

    if (oldObj && oldObj != HGDI_ERROR)
    {
        SelectObject(memDc, oldObj);
        oldObj = nullptr;
    }

    bool ok = false;
    if (haveSurface && !blank)
        ok = SaveHBitmapAsBmpHandle(bitmap, width, height, reinterpret_cast<HANDLE>(request.outputHandle));
    else
        reply.flags |= aip::capture::RejectedBlank;

    if (oldObj && oldObj != HGDI_ERROR)
        SelectObject(memDc, oldObj);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
    if (ok) reply.status = aip::capture::Status::Success;
    else reply.error = GetLastError();
    return reply;
}

#ifdef DESKTOPSTUB_INERT_HARDWARE
static std::function<aip::capture::Reply(const aip::capture::HelperRequest&)> g_testLiveCapture;
static aip::capture::ProcessRunner g_testLiveCaptureProcess;
#endif

static int RunLiveWallpaperCaptureHelper(const aip::capture::HelperRequest& request)
{
    const auto output = reinterpret_cast<HANDLE>(request.outputHandle), response = reinterpret_cast<HANDLE>(request.replyHandle);
    if (!output || !response || GetFileType(output) != FILE_TYPE_DISK || GetFileType(response) != FILE_TYPE_PIPE) return 2;
    auto reply = aip::capture::MakeReply(request, aip::capture::Status::CaptureFailed);
    try {
#ifdef DESKTOPSTUB_INERT_HARDWARE
        reply = g_testLiveCapture ? g_testLiveCapture(request) : aip::capture::MakeReply(request, aip::capture::Status::InvalidRequest, ERROR_NOT_SUPPORTED);
#else
        reply = CaptureWindowToHandle(request);
#endif
    } catch (...) { reply = aip::capture::MakeReply(request, aip::capture::Status::CaptureFailed, ERROR_NOT_ENOUGH_MEMORY); }
    DWORD written = 0;
    const bool sent = WriteFile(response, &reply, sizeof(reply), &written, nullptr) && written == sizeof(reply);
    CloseHandle(output); CloseHandle(response);
    return sent && reply.status == aip::capture::Status::Success ? 0 : 1;
}

static bool CaptureWindowToBmpFile(HWND hwnd, const std::wstring& path, bool& rejectedBlank, bool& rejectedTooSmall,
    int& rejectedWidth, int& rejectedHeight, int& rejectedMinWidth, int& rejectedMinHeight, ULONGLONG deadline)
{
    rejectedBlank = rejectedTooSmall = false;
    rejectedWidth = rejectedHeight = rejectedMinWidth = rejectedMinHeight = 0;
    aip::capture::ProcessRequest options;
    options.destination = path; options.deadline = deadline;
    { std::lock_guard<std::mutex> lock(g_pathMutex); options.executable = g_exePath; }
    options.helper.window = reinterpret_cast<std::uintptr_t>(hwnd);
    LiveWallpaperMinimumCaptureSize(options.helper.minimumWidth, options.helper.minimumHeight);
    options.helper.screenFallback = LiveWallpaperCaptureScreenFallbackEnabled();
    static std::atomic<std::uint64_t> sequence{0};
    options.helper.requestId = ++sequence;
    if (!options.helper.requestId) options.helper.requestId = ++sequence;
    options.canceled = [] { return !g_running.load() || g_shutdownState.IsRequested(); };
    aip::capture::Reply reply;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    options.helper.windowProcessId = 1; // No real HWND/PID lookup in an inert host.
    reply = g_testLiveCaptureProcess ? g_testLiveCaptureProcess(options) :
        aip::capture::MakeReply(options.helper, aip::capture::Status::InvalidRequest, ERROR_NOT_SUPPORTED);
#else
    DWORD owner = 0;
    if (!GetWindowThreadProcessId(hwnd, &owner)) return false;
    options.helper.windowProcessId = owner;
    reply = aip::capture::RunOwnedProcess(options);
#endif
    rejectedBlank = (reply.flags & aip::capture::RejectedBlank) != 0;
    rejectedTooSmall = (reply.flags & aip::capture::RejectedTooSmall) != 0;
    if (rejectedTooSmall) {
        rejectedWidth = reply.width; rejectedHeight = reply.height;
        rejectedMinWidth = reply.minimumWidth; rejectedMinHeight = reply.minimumHeight;
    }
    if (reply.status == aip::capture::Status::TimedOut || reply.status == aip::capture::Status::Canceled)
        Log(L"[!] Live wallpaper capture helper stopped: %s (error %lu).", reply.status == aip::capture::Status::TimedOut ? L"deadline" : L"shutdown", static_cast<unsigned long>(reply.error));
    return reply.status == aip::capture::Status::Success;
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

static bool AddLiveWallpaperDescendantWindowCandidates(std::vector<HWND>& candidates, HWND root)
{
    if (!root || !IsWindow(root))
        return false;

    struct LiveWallpaperDescendantContext
    {
        std::vector<HWND>* candidates;
        bool added;
    } context{ &candidates, false };

    auto enumProc = [](HWND hwnd, LPARAM lParam) -> BOOL
    {
        auto* ctx = reinterpret_cast<LiveWallpaperDescendantContext*>(lParam);
        if (!ctx || !ctx->candidates || !IsWindow(hwnd))
            return TRUE;

        if (!WindowLooksLiveWallpaperCandidate(hwnd))
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

    if (provider != LiveWallpaperProvider::Active)
        return candidates;

    HWND worker = nullptr;
    while ((worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr)) != nullptr)
    {
        // The WorkerW that owns SHELLDLL_DefView is the desktop icon layer, not
        // the live wallpaper renderer. Capture is intentionally limited to the
        // remaining WorkerW desktop-host trees so it cannot capture random
        // foreground applications.
        if (FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr) != nullptr)
            continue;

        bool addedDescendant = AddLiveWallpaperDescendantWindowCandidates(candidates, worker);
        if (addedDescendant || HasPotentialLiveWallpaperChildWindow(worker))
            AddUniqueWindowCandidate(workerCandidates, worker);
    }

    for (HWND hwnd : workerCandidates)
        AddUniqueWindowCandidate(candidates, hwnd);

    return candidates;
}

#include "../capture_attempt.h"

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
    static aip::CaptureAttemptSchedule s_captureAttempts;

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
        s_captureAttempts.Reset();
    };

    LiveWallpaperProvider provider = DetectActiveLiveWallpaperProvider();
    if (provider == LiveWallpaperProvider::None)
    {
        std::lock_guard<std::mutex> lk(s_liveWallpaperMutex);
        resetStateUnlocked();
        return L"";
    }

    std::vector<HWND> candidates = FindLiveWallpaperWindowCandidates(provider);

    std::unique_lock<std::mutex> lk(s_liveWallpaperMutex);
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
            s_captureAttempts.Reset();
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
        s_captureAttempts.Reset();
    }

    // Capture releases this mutex while asking another process to paint.
    // A poll/UI/registration caller must never start an overlapping capture.
    // Startup settling still suppresses an early static-wallpaper fallback.
    if (s_captureAttempts.InFlight()) {
        g_liveWallpaperCapturePending = s_startupSettleActive;
        return IsUsableWallpaperPath(s_snapshotPath) ? s_snapshotPath : L"";
    }

    int delayMs = LiveWallpaperCaptureDelayMs();
    if (s_snapshotPath.empty() && s_pendingStartupSnapshotPath.empty() && delayMs > 0)
    {
        long long elapsedMs = duration_cast<milliseconds>(now - s_activeSince).count();
        if (elapsedMs < delayMs)
        {
            // Defer until a later poll instead of sleeping on a UI/registration
            // caller. Pending suppresses premature static wallpaper fallback.
            g_liveWallpaperCapturePending = true;
            return IsUsableWallpaperPath(s_snapshotPath) ? s_snapshotPath : L"";
        }
    }

    auto captureSnapshot = [&](std::wstring& capturedPath) -> bool
    {
        capturedPath.clear();
        const auto clock = []() noexcept { return static_cast<std::uint64_t>(duration_cast<milliseconds>(
            steady_clock::now().time_since_epoch()).count()); };
        const auto token = s_captureAttempts.Begin(clock(), aip::CaptureAttemptRetryMs(
            LiveWallpaperCaptureRefreshMs(), startupRefreshMs, s_startupSettleActive));
        if (!token) return false;
        aip::CaptureAttemptScope<decltype(clock)> attempt(s_captureAttempts, token, lk, clock);
        const ULONGLONG captureDeadline = GetTickCount64() + 3000;
        std::wstring path = GetLiveWallpaperSnapshotPath(provider, ++s_captureSequence);
        steady_clock::time_point activeSinceBeforeCapture = s_activeSince;
        if (path.empty())
        {
            std::wstring key = L"live-wallpaper-temp-path:" + std::wstring(LiveWallpaperProviderName(provider));
            if (ShouldLogWallpaperDetectionEvent(key))
                Log(g_ui.liveWallpaperCaptureSnapshotPathFailed.c_str(), LiveWallpaperProviderName(provider));
            return false;
        }

        lk.unlock();

        bool rejectedBlank = false;
        bool rejectedTooSmall = false;
        int rejectedWidth = 0;
        int rejectedHeight = 0;
        int rejectedMinWidth = 0;
        int rejectedMinHeight = 0;
        bool captured = false;
        for (HWND candidate : candidates)
        {
            if (!g_running.load() || g_shutdownState.IsRequested() || GetTickCount64() >= captureDeadline) break;
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
                thisRejectedMinHeight,
                captureDeadline))
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
            lk.lock();
            return false;
        }

        lk.lock();
        if (provider != s_activeProvider || activeSinceBeforeCapture != s_activeSince)
            return false;

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
            // desktop tile before the settled live wallpaper snapshot arrived.
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

std::wstring GetWallpaper(bool includeLiveWallpaper = true)
{
    std::wstring liveWallpaper = includeLiveWallpaper ? TryGetLiveWallpaperSnapshot() : L"";
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

        if (!WallpaperDetectionFallbackEnabled())
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

static FitMode ReadUncapturedWallpaperFit()
{
    std::wstring scoped;
    if (aip::content::TryReadTilePolicy(L"Settings", L"DisableFitting", scoped)) {
        if (scoped == L"1") return FitMode::Fill;
    }
    if (aip::content::TryReadTilePolicy(L"Settings", L"TileWallpaperFit", scoped)) {
        const auto fit = ToLowerCopy(TrimCopy(scoped));
        if (fit == L"fill") return FitMode::Fill;
        if (fit == L"fit") return FitMode::Fit;
        if (fit == L"stretch") return FitMode::Stretch;
        if (fit == L"center") return FitMode::Center;
        if (fit == L"tile") return FitMode::Tile;
        if (fit == L"span") return FitMode::Span;
        // Auto follows the desktop; explicit modes above never query or
        // modify desktop fitting and cannot alter another tile's settings.
    }
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

FitMode GetWallpaperFit()
{
    FitMode captured;
    if (TryContentWallpaperFit(captured)) return captured;
    return ReadUncapturedWallpaperFit();
}

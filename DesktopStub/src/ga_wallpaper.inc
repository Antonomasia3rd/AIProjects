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


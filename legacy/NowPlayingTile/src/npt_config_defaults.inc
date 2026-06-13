static const wchar_t* DEFAULT_SETTINGS_HEADER =
    L"# NowPlayingTile settings\r\n"
    L"# TileLayout is retained for forward compatibility; current builds render text.\r\n";

static bool EnsureDefaultSettingsFile()
{
    static const aip::IniDefaultValue defaults[] =
    {
        { L"Settings", L"TileLayout", L"Text" },
        { L"Settings", L"UpdateIntervalSeconds", L"2" },
        { L"Settings", L"TileRefreshSeconds", L"60" },
        { L"Settings", L"ShowTrayIcon", L"false" },
    };

    aip::IniConfigStore store(g_iniPath, DEFAULT_SETTINGS_HEADER, 5000);
    if (!store.EnsureDefaults(defaults, ARRAYSIZE(defaults)))
    {
        Log(L"Could not create or repair default settings: %s",
            GetLastErrorText(GetLastError()).c_str());
        return false;
    }
    return true;
}

static std::wstring ReadIniString(const wchar_t* key, const wchar_t* fallback)
{
    return aip::IniConfigStore(g_iniPath).ReadRaw(L"Settings", key, fallback);
}

static int ReadIniInt(const wchar_t* key, int fallback, int minValue, int maxValue)
{
    std::wstring value = Trim(ReadIniString(key, L""));
    if (value.empty())
    {
        return fallback;
    }

    errno = 0;
    wchar_t* end = nullptr;
    long long parsed = wcstoll(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != L'\0' || errno == ERANGE)
    {
        return fallback;
    }
    if (parsed < minValue)
    {
        parsed = minValue;
    }
    if (parsed > maxValue)
    {
        parsed = maxValue;
    }
    return static_cast<int>(parsed);
}

static bool ReadIniBool(const wchar_t* key, bool fallback)
{
    std::wstring value = ReadIniString(key, fallback ? L"true" : L"false");
    value = Trim(value);
    if (EqualsIgnoreCase(value, L"1") || EqualsIgnoreCase(value, L"true") || EqualsIgnoreCase(value, L"yes") || EqualsIgnoreCase(value, L"on"))
    {
        return true;
    }
    if (EqualsIgnoreCase(value, L"0") || EqualsIgnoreCase(value, L"false") || EqualsIgnoreCase(value, L"no") || EqualsIgnoreCase(value, L"off"))
    {
        return false;
    }
    return fallback;
}

static TileLayout ParseTileLayout(const std::wstring& value)
{
    if (EqualsIgnoreCase(value, L"Text"))
    {
        return TileLayout::Text;
    }
    if (EqualsIgnoreCase(value, L"Artwork"))
    {
        return TileLayout::Artwork;
    }
    if (EqualsIgnoreCase(value, L"Combined"))
    {
        return TileLayout::Combined;
    }
    return TileLayout::Text;
}

static const wchar_t* TileLayoutName(TileLayout layout)
{
    switch (layout)
    {
    case TileLayout::Text: return L"Text";
    case TileLayout::Artwork: return L"Artwork";
    case TileLayout::Combined: return L"Combined";
    case TileLayout::Cycle:
    default: return L"Cycle";
    }
}

static AppSettings LoadSettings()
{
    EnsureDefaultSettingsFile();
    AppSettings settings;
    settings.updateIntervalSeconds = ReadIniInt(L"UpdateIntervalSeconds", 2, 1, 60);
    settings.tileRefreshSeconds = ReadIniInt(L"TileRefreshSeconds", 60, 1, 300);
    settings.tileLayout = ParseTileLayout(ReadIniString(L"TileLayout", L"Text"));
    settings.showTrayIcon = ReadIniBool(L"ShowTrayIcon", false);
    return settings;
}

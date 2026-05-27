static const wchar_t* DEFAULT_SETTINGS_TEXT =
    L"# NowPlayingTile settings\r\n"
    L"# TileLayout: Text (artwork/combined layouts temporarily disabled)\r\n"
    L"[Settings]\r\n"
    L"TileLayout=Text\r\n"
    L"UpdateIntervalSeconds=2\r\n"
    L"TileRefreshSeconds=60\r\n"
    L"ShowTrayIcon=false\r\n";

static void EnsureDefaultSettingsFile()
{
    if (FileExists(g_iniPath))
    {
        return;
    }

    EnsureDirectory(GetDirectoryName(g_iniPath));
    HANDLE file = CreateFileW(g_iniPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        Log(L"Could not create default settings: %s", GetLastErrorText(GetLastError()).c_str());
        return;
    }

    int bytesNeeded = WideCharToMultiByte(CP_UTF8, 0, DEFAULT_SETTINGS_TEXT, -1, nullptr, 0, nullptr, nullptr);
    if (bytesNeeded > 1)
    {
        std::vector<char> bytes(static_cast<size_t>(bytesNeeded));
        WideCharToMultiByte(CP_UTF8, 0, DEFAULT_SETTINGS_TEXT, -1, bytes.data(), bytesNeeded, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, bytes.data(), static_cast<DWORD>(bytesNeeded - 1), &written, nullptr);
    }

    CloseHandle(file);
}

static std::wstring ReadIniString(const wchar_t* key, const wchar_t* fallback)
{
    EnsureDefaultSettingsFile();
    wchar_t buffer[512] = {};
    DWORD count = GetPrivateProfileStringW(L"Settings", key, fallback, buffer, ARRAYSIZE(buffer), g_iniPath.c_str());
    if (count == 0 && fallback != nullptr)
    {
        return fallback;
    }
    return buffer;
}

static int ReadIniInt(const wchar_t* key, int fallback, int minValue, int maxValue)
{
    std::wstring value = ReadIniString(key, L"");
    wchar_t* end = nullptr;
    long parsed = wcstol(value.c_str(), &end, 10);
    if (end == value.c_str())
    {
        parsed = fallback;
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
    return TileLayout::Cycle;
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
    AppSettings settings;
    settings.updateIntervalSeconds = ReadIniInt(L"UpdateIntervalSeconds", 2, 1, 60);
    settings.tileRefreshSeconds = ReadIniInt(L"TileRefreshSeconds", 60, 1, 300);
    settings.tileLayout = ParseTileLayout(ReadIniString(L"TileLayout", L"Cycle"));
    settings.showTrayIcon = ReadIniBool(L"ShowTrayIcon", false);
    return settings;
}

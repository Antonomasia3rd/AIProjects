static const wchar_t* DEFAULT_SETTINGS_HEADER =
    L"# NowPlayingTile settings\r\n"
    L"# TileLayout: Text, Artwork, Combined, or Cycle. Missing artwork falls back to Text.\r\n"
    L"# RunAtStartup uses the packaged StartupTask declared in AppxManifest.xml.\r\n";

static bool EnsureDefaultSettingsFile()
{
    static const aip::IniDefaultValue defaults[] =
    {
        { L"Settings", L"TileLayout", L"Text" },
        { L"Settings", L"UpdateIntervalSeconds", L"2" },
        { L"Settings", L"TileRefreshSeconds", L"60" },
        { L"Settings", L"ShowTrayIcon", L"true" },
        { L"Settings", L"RunAtStartup", L"false" },
        { L"Settings", L"ShowMenuAsDropdown", L"true" },
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

    int parsed = 0;
    if (!aip::ParseIntValue(value, parsed))
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
    return parsed;
}

static bool ReadIniBool(const wchar_t* key, bool fallback)
{
    bool parsed = false;
    return aip::ParseBoolValue(
        ReadIniString(key, fallback ? L"true" : L"false"),
        parsed)
        ? parsed
        : fallback;
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
    if (EqualsIgnoreCase(value, L"Cycle"))
    {
        return TileLayout::Cycle;
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
    settings.showTrayIcon = ReadIniBool(L"ShowTrayIcon", true);
    settings.runAtStartup = ReadIniBool(L"RunAtStartup", false);
    settings.showMenuAsDropdown = ReadIniBool(L"ShowMenuAsDropdown", true);
    return settings;
}

static bool NormalizeNowPlayingSetting(
    aip::IniSetting& setting,
    std::wstring& error)
{
    setting.section = Trim(setting.section);
    setting.key = Trim(setting.key);
    if (!EqualsIgnoreCase(setting.section, L"Settings"))
    {
        error = L"NowPlayingTile --set only accepts keys in [Settings].";
        return false;
    }
    setting.section = L"Settings";

    auto normalizeBool = [&](const wchar_t* canonicalKey) -> bool
    {
        bool parsed = false;
        if (!aip::ParseBoolValue(setting.value, parsed))
        {
            error = L"[Settings] " + std::wstring(canonicalKey) +
                L" expects a true/false value.";
            return false;
        }
        setting.key = canonicalKey;
        setting.value = parsed ? L"true" : L"false";
        return true;
    };

    if (EqualsIgnoreCase(setting.key, L"ShowTrayIcon"))
        return normalizeBool(L"ShowTrayIcon");
    if (EqualsIgnoreCase(setting.key, L"RunAtStartup"))
        return normalizeBool(L"RunAtStartup");
    if (EqualsIgnoreCase(setting.key, L"ShowMenuAsDropdown"))
        return normalizeBool(L"ShowMenuAsDropdown");

    if (EqualsIgnoreCase(setting.key, L"TileLayout"))
    {
        if (!EqualsIgnoreCase(setting.value, L"Text") &&
            !EqualsIgnoreCase(setting.value, L"Artwork") &&
            !EqualsIgnoreCase(setting.value, L"Combined") &&
            !EqualsIgnoreCase(setting.value, L"Cycle"))
        {
            error = L"[Settings] TileLayout expects Text, Artwork, Combined, or Cycle.";
            return false;
        }
        setting.key = L"TileLayout";
        setting.value = TileLayoutName(ParseTileLayout(setting.value));
        return true;
    }

    auto normalizeInteger = [&](const wchar_t* canonicalKey, int minimum, int maximum) -> bool
    {
        int parsed = 0;
        if (!aip::ParseIntValueInRange(setting.value, minimum, maximum, parsed))
        {
            error = L"[Settings] " + std::wstring(canonicalKey) + L" expects an integer from " +
                std::to_wstring(minimum) + L" through " + std::to_wstring(maximum) + L".";
            return false;
        }
        setting.key = canonicalKey;
        setting.value = std::to_wstring(parsed);
        return true;
    };

    if (EqualsIgnoreCase(setting.key, L"UpdateIntervalSeconds"))
        return normalizeInteger(L"UpdateIntervalSeconds", 1, 60);
    if (EqualsIgnoreCase(setting.key, L"TileRefreshSeconds"))
        return normalizeInteger(L"TileRefreshSeconds", 1, 300);

    error = L"Unknown NowPlayingTile setting: [Settings] " + setting.key;
    return false;
}

static void AddOrReplaceNowPlayingSetting(
    AppOptions& options,
    const aip::IniSetting& setting)
{
    for (auto& existing : options.settings)
    {
        if (EqualsIgnoreCase(existing.section, setting.section.c_str()) &&
            EqualsIgnoreCase(existing.key, setting.key.c_str()))
        {
            existing = setting;
            return;
        }
    }
    options.settings.push_back(setting);
}

static bool FindNowPlayingStartupPreference(
    const std::vector<aip::IniSetting>& settings,
    bool& found,
    bool& desired)
{
    found = false;
    desired = false;
    for (const auto& setting : settings)
    {
        if (!EqualsIgnoreCase(setting.section, L"Settings") ||
            !EqualsIgnoreCase(setting.key, L"RunAtStartup"))
        {
            continue;
        }
        bool parsed = false;
        if (!aip::ParseBoolValue(setting.value, parsed))
            return false;
        found = true;
        desired = parsed;
    }
    return true;
}

static std::wstring NowPlayingStartupLockIdentity()
{
    return std::wstring(STARTUP_TASK_ID) + L"|" + aip::MakeAbsolutePath(g_iniPath);
}

static bool QueryNowPlayingStartupTask(
    aip::PackagedStartupStatus& status,
    std::wstring& error)
{
    return aip::QueryPackagedStartupTask(STARTUP_TASK_ID, status, &error);
}

static bool SetNowPlayingStartupTask(
    bool desired,
    aip::PackagedStartupStatus& status,
    std::wstring& error)
{
    return aip::SetPackagedStartupTaskEnabled(
        STARTUP_TASK_ID,
        desired,
        status,
        &error);
}

static bool PersistNowPlayingSettingsAtomically(
    const std::vector<aip::IniSetting>& settings,
    std::wstring& error)
{
    error.clear();
    if (settings.empty())
        return true;

    bool startupExplicit = false;
    bool startupDesired = false;
    if (!FindNowPlayingStartupPreference(settings, startupExplicit, startupDesired))
    {
        error = L"[Settings] RunAtStartup expects a true/false value.";
        return false;
    }

    auto mutateSettings = [&](std::wstring& text)
    {
        for (const auto& setting : settings)
        {
            if (!aip::WriteIniValueToText(
                    text,
                    setting.section.c_str(),
                    setting.key.c_str(),
                    setting.value,
                    DEFAULT_SETTINGS_HEADER))
            {
                return false;
            }
        }
        return true;
    };

    aip::PackagedStartupStatus finalStartup;
    const bool saved = startupExplicit && HasPackageIdentity()
        ? aip::CommitPackagedStartupTaskIniState(
            STARTUP_TASK_ID,
            NowPlayingStartupLockIdentity(),
            g_iniPath,
            DEFAULT_SETTINGS_HEADER,
            L"Settings",
            L"RunAtStartup",
            false,
            startupDesired,
            mutateSettings,
            finalStartup,
            &error,
            5000)
        : aip::IniConfigStore(
            g_iniPath,
            DEFAULT_SETTINGS_HEADER,
            5000).MutateFresh(mutateSettings);
    if (saved)
        return true;

    const DWORD saveError = GetLastError();
    if (startupExplicit && HasPackageIdentity() && !error.empty())
    {
        error = L"Could not commit the setting batch with the packaged "
            L"StartupTask: " + error;
        return false;
    }

    error = L"Could not save the setting batch: " + GetLastErrorText(saveError);
    return false;
}

static bool PersistNowPlayingSetting(
    const wchar_t* key,
    const std::wstring& value,
    std::wstring& error)
{
    aip::IniSetting setting{ L"Settings", key, value };
    if (!NormalizeNowPlayingSetting(setting, error))
        return false;
    return PersistNowPlayingSettingsAtomically({ setting }, error);
}

static bool ReconcileNowPlayingStartupFromConfig(std::wstring& error)
{
    error.clear();
    if (!HasPackageIdentity())
        return true;

    auto startupLock = std::make_unique<aip::PackagedStartupTaskMutationLock>(
        NowPlayingStartupLockIdentity());
    if (!startupLock->IsLocked())
    {
        error = L"Could not lock the packaged StartupTask transaction: " +
            GetLastErrorText(startupLock->Error());
        return false;
    }

    aip::PackagedStartupStatus status;
    return SetNowPlayingStartupTask(
        ReadIniBool(L"RunAtStartup", false),
        status,
        error);
}

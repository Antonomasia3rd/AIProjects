// -----------------------------------------------------------------------------
// Command-line parsing and persisted INI setting changes
// -----------------------------------------------------------------------------
struct CommandLineIniSetting
{
    std::wstring section;
    std::wstring key;
    std::wstring value;
};

struct CommandLineOptions
{
    bool helpRequested = false;
    bool parseFailed = false;
    bool exitRequested = false;
    bool generateOnce = false;
    bool forceGenerateOnStartup = false;
    bool regenerateManifest = false;
    bool noMonitor = false;
    bool customIniPath = false;
    bool wallpaperSpecified = false;
    bool comRegistrationHelper = false;
    bool appxActivationServer = false;
    bool launchedFromAppxStub = false;
    bool launchedFromModeSwitch = false;
    DWORD waitForPid = 0;
    std::wstring parseError;
    std::wstring applyError;
    std::wstring iniPath;
    std::wstring wallpaperPath;
    std::wstring comRegistrationHelperManifestPath;
    std::wstring appxActivationServerName;
    std::vector<CommandLineIniSetting> settings;
    bool liveTileModeChanged = false;
    bool trayIconChanged = false;
};

static CommandLineOptions g_commandLine;
void Log(const wchar_t* fmt, ...);
std::wstring FormatWide(const wchar_t* fmt, ...);
int IniReadI(const wchar_t* s, const wchar_t* k, int d);

static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SHOW = 0;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_RELOAD_SETTINGS = 1;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_EXIT = 2;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_GENERATE = 3;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_REGENERATE_MANIFEST = 4;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SHOW_MESSAGEBOX = 5;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_MASK = 0x00FF;
static constexpr WPARAM SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON = 0x0100;
static constexpr WPARAM SINGLE_INSTANCE_FLAG_REGENERATE_MANIFEST = 0x0200;

static void AddCommandLineIniSetting(const std::wstring& section, const std::wstring& key, const std::wstring& value);

static LiveTileUpdateMode ParseLiveTileUpdateModeValue(
    const std::wstring& rawValue,
    LiveTileUpdateMode fallback = LiveTileUpdateMode::Auto)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v.empty())
        return fallback;

    if (v == L"auto" || v == L"automatic")
        return LiveTileUpdateMode::Auto;
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on" ||
        v == L"enable" || v == L"enabled" || v == L"live" ||
        v == L"livetile" || v == L"live_tile" || v == L"live-tile" ||
        v == L"live tile")
        return LiveTileUpdateMode::LiveTile;
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off" ||
        v == L"disable" || v == L"disabled" || v == L"registration" ||
        v == L"register" || v == L"appx" || v == L"manifest")
        return LiveTileUpdateMode::Registration;

    return fallback;
}

static std::wstring LiveTileUpdateModeIniValue(LiveTileUpdateMode mode)
{
    switch (mode)
    {
    case LiveTileUpdateMode::Registration:
        return L"Registration";
    case LiveTileUpdateMode::LiveTile:
        return L"LiveTile";
    case LiveTileUpdateMode::Auto:
    default:
        return L"Auto";
    }
}

static const wchar_t* LiveTileUpdateModeName(LiveTileUpdateMode mode)
{
    switch (mode)
    {
    case LiveTileUpdateMode::Registration:
        return L"Registration";
    case LiveTileUpdateMode::LiveTile:
        return L"Live Tile";
    case LiveTileUpdateMode::Auto:
    default:
        return L"Auto";
    }
}

static bool IsValidLiveTileUpdateMode(LiveTileUpdateMode mode)
{
    return mode == LiveTileUpdateMode::Auto ||
        mode == LiveTileUpdateMode::Registration ||
        mode == LiveTileUpdateMode::LiveTile;
}

static void AddCommandLineLiveTileModeSetting(LiveTileUpdateMode mode)
{
    AddCommandLineIniSetting(L"Settings", L"ExperimentalLiveTileUpdate", LiveTileUpdateModeIniValue(mode));
}

static ManifestCompatibilityTarget ParseManifestCompatibilityTargetValue(
    const std::wstring& rawValue,
    ManifestCompatibilityTarget fallback = ManifestCompatibilityTarget::Windows10)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v.empty())
        return fallback;

    if (v == L"10" || v == L"win10" || v == L"windows10" ||
        v == L"windows 10" || v == L"default")
        return ManifestCompatibilityTarget::Windows10;
    if (v == L"8.1" || v == L"81" || v == L"win81" || v == L"win8.1" ||
        v == L"windows81" || v == L"windows8.1" || v == L"windows 8.1")
        return ManifestCompatibilityTarget::Windows81;
    if (v == L"8" || v == L"8.0" || v == L"80" || v == L"win8" || v == L"win80" ||
        v == L"windows8" || v == L"windows 8" || v == L"windows 8.0")
        return ManifestCompatibilityTarget::Windows8;

    return fallback;
}

static std::wstring ManifestCompatibilityTargetIniValue(ManifestCompatibilityTarget target)
{
    switch (target)
    {
    case ManifestCompatibilityTarget::Windows81:
        return L"Windows81";
    case ManifestCompatibilityTarget::Windows8:
        return L"Windows8";
    case ManifestCompatibilityTarget::Windows10:
    default:
        return L"Windows10";
    }
}

static const wchar_t* ManifestCompatibilityTargetName(ManifestCompatibilityTarget target)
{
    switch (target)
    {
    case ManifestCompatibilityTarget::Windows81:
        return L"Windows 8.1";
    case ManifestCompatibilityTarget::Windows8:
        return L"Windows 8";
    case ManifestCompatibilityTarget::Windows10:
    default:
        return L"Windows 10";
    }
}

static bool IsValidManifestCompatibilityTarget(ManifestCompatibilityTarget target)
{
    return target == ManifestCompatibilityTarget::Windows10 ||
        target == ManifestCompatibilityTarget::Windows81 ||
        target == ManifestCompatibilityTarget::Windows8;
}

static void AddCommandLineManifestTargetSetting(ManifestCompatibilityTarget target)
{
    AddCommandLineIniSetting(L"Settings", L"AppxManifestTarget", ManifestCompatibilityTargetIniValue(target));
}

static std::wstring QuoteCommandLineArg(const std::wstring& arg)
{
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++slashCount;
            continue;
        }

        if (ch == L'"')
        {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(ch);
            slashCount = 0;
            continue;
        }

        if (slashCount)
        {
            out.append(slashCount, L'\\');
            slashCount = 0;
        }
        out.push_back(ch);
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static bool CommandLineOptionHasInlineValue(const std::wstring& arg, const wchar_t* name, std::wstring& value)
{
    std::wstring prefix = std::wstring(name ? name : L"") + L"=";
    if (arg.size() < prefix.size())
        return false;

    if (!IEquals(arg.substr(0, prefix.size()), prefix))
        return false;

    value = arg.substr(prefix.size());
    return true;
}

static bool CommandLineTryGetOptionValue(
    int argc,
    LPWSTR* argv,
    int& index,
    const wchar_t* name,
    std::wstring& value,
    std::wstring& error,
    bool& matched)
{
    matched = false;
    value.clear();
    error.clear();

    std::wstring arg = argv[index] ? argv[index] : L"";
    if (CommandLineOptionHasInlineValue(arg, name, value))
    {
        matched = true;
        return true;
    }

    if (!IEquals(arg, name ? name : L""))
        return true;

    matched = true;
    if (index + 1 >= argc)
    {
        error = L"Missing value after " + std::wstring(name ? name : L"option") + L".";
        return false;
    }

    value = argv[++index] ? argv[index] : L"";
    return true;
}

static bool CommandLineMakeAbsolutePath(const std::wstring& rawPath, std::wstring& out, std::wstring& error)
{
    std::wstring path = TrimCopy(rawPath);
    if (path.empty())
    {
        error = L"Path value cannot be empty.";
        return false;
    }

    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        error = L"GetFullPathNameW failed for path: " + path + L" (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    std::vector<wchar_t> buffer(static_cast<size_t>(needed) + 1, L'\0');
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || static_cast<size_t>(written) >= buffer.size())
    {
        error = L"GetFullPathNameW returned an invalid path for: " + path + L".";
        return false;
    }

    out.assign(buffer.data(), written);
    return true;
}

static void AddCommandLineIniSetting(const std::wstring& section, const std::wstring& key, const std::wstring& value)
{
    for (auto& entry : g_commandLine.settings)
    {
        if (IEquals(entry.section, section) && IEquals(entry.key, key))
        {
            entry.value = value;
            return;
        }
    }

    g_commandLine.settings.push_back({ section, key, value });
}

static void AddCommandLineBoolSetting(const wchar_t* section, const wchar_t* key, bool value)
{
    AddCommandLineIniSetting(section ? section : L"", key ? key : L"", value ? L"1" : L"0");
}

static void AddCommandLineIntSetting(const wchar_t* section, const wchar_t* key, int value)
{
    AddCommandLineIniSetting(section ? section : L"", key ? key : L"", std::to_wstring(value));
}

static bool ParseCommandLineClampedInt(
    const std::wstring& rawValue,
    const wchar_t* optionName,
    int minValue,
    int maxValue,
    int& value,
    std::wstring& error)
{
    std::wstring trimmed = TrimCopy(rawValue);
    int parsed = 0;
    if (!TryParseIntStrict(trimmed, parsed) || parsed < minValue || parsed > maxValue)
    {
        error = std::wstring(optionName ? optionName : L"Option") +
            L" expects an integer from " + std::to_wstring(minValue) +
            L" to " + std::to_wstring(maxValue) + L".";
        return false;
    }
    value = parsed;
    return true;
}

static bool TryParseCommandLineBoolValue(const std::wstring& rawValue, bool& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v == L"1" || v == L"true" || v == L"yes" || v == L"on" || v == L"enable" || v == L"enabled")
    {
        value = true;
        return true;
    }
    if (v == L"0" || v == L"false" || v == L"no" || v == L"off" || v == L"disable" || v == L"disabled")
    {
        value = false;
        return true;
    }
    return false;
}

static bool TryParseCommandLineOverrideSpec(
    const std::wstring& spec,
    std::wstring& section,
    std::wstring& key,
    std::wstring& value,
    std::wstring& error)
{
    std::wstring trimmed = TrimCopy(spec);
    size_t eq = trimmed.find(L'=');
    if (eq == std::wstring::npos)
    {
        error = L"Expected Section.Key=Value: " + spec;
        return false;
    }

    size_t dot = trimmed.rfind(L'.', eq);
    if (dot == std::wstring::npos || dot == 0 || dot + 1 >= eq)
    {
        error = L"Expected Section.Key=Value: " + spec;
        return false;
    }

    section = TrimCopy(trimmed.substr(0, dot));
    key = TrimCopy(trimmed.substr(dot + 1, eq - dot - 1));
    value = trimmed.substr(eq + 1);
    if (section.empty() || key.empty())
    {
        error = L"Section and key cannot be empty: " + spec;
        return false;
    }

    return true;
}

static bool TryParseCommandLineKeyValue(
    const std::wstring& spec,
    std::wstring& key,
    std::wstring& value,
    std::wstring& error)
{
    std::wstring trimmed = TrimCopy(spec);
    size_t eq = trimmed.find(L'=');
    if (eq == std::wstring::npos)
    {
        error = L"Expected Name=Value: " + spec;
        return false;
    }

    key = TrimCopy(trimmed.substr(0, eq));
    value = trimmed.substr(eq + 1);
    if (key.empty())
    {
        error = L"Name cannot be empty: " + spec;
        return false;
    }

    return true;
}

static bool CommandLineParseInteger(const std::wstring& rawValue, int& value)
{
    std::wstring trimmed = TrimCopy(rawValue);
    if (trimmed.empty())
        return false;

    errno = 0;
    wchar_t* end = nullptr;
    long parsed = wcstol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
        return false;

    while (end && *end && iswspace(*end) != 0)
        ++end;
    if (end && *end)
        return false;

    value = static_cast<int>(parsed);
    return true;
}

static bool IsSupportedCommandLineScale(int scale)
{
    return scale == 100 || scale == 125 || scale == 150 || scale == 200 || scale == 400;
}

static std::vector<std::wstring> SplitCommandLineList(const std::wstring& text)
{
    std::vector<std::wstring> values;
    size_t start = 0;
    while (start <= text.size())
    {
        size_t end = text.find_first_of(L",;", start);
        if (end == std::wstring::npos)
            end = text.size();

        values.push_back(TrimCopy(text.substr(start, end - start)));
        if (end == text.size())
            break;
        start = end + 1;
    }
    return values;
}

static bool ApplyCommandLineScales(const std::wstring& rawValue, std::wstring& error)
{
    std::wstring value = ToLowerCopy(TrimCopy(rawValue));
    if (value.empty())
    {
        error = L"Scale list cannot be empty.";
        return false;
    }

    if (value == L"auto")
    {
        AddCommandLineBoolSetting(L"Settings", L"GenerateScaleAuto", true);
        return true;
    }

    bool use100 = false;
    bool use125 = false;
    bool use150 = false;
    bool use200 = false;
    bool use400 = false;

    for (const auto& token : SplitCommandLineList(value))
    {
        if (token.empty())
        {
            error = L"Scale list contains an empty entry.";
            return false;
        }

        if (token == L"all")
        {
            use100 = use125 = use150 = use200 = use400 = true;
            continue;
        }

        int scale = 0;
        if (!CommandLineParseInteger(token, scale) || !IsSupportedCommandLineScale(scale))
        {
            error = L"Unsupported scale: " + token + L". Supported values are auto, all, 100, 125, 150, 200, and 400.";
            return false;
        }

        if (scale == 100) use100 = true;
        else if (scale == 125) use125 = true;
        else if (scale == 150) use150 = true;
        else if (scale == 200) use200 = true;
        else if (scale == 400) use400 = true;
    }

    if (!use100 && !use125 && !use150 && !use200 && !use400)
    {
        error = L"At least one scale must be enabled.";
        return false;
    }

    AddCommandLineBoolSetting(L"Settings", L"GenerateScaleAuto", false);
    AddCommandLineBoolSetting(L"Settings", L"GenerateScale100", use100);
    AddCommandLineBoolSetting(L"Settings", L"GenerateScale125", use125);
    AddCommandLineBoolSetting(L"Settings", L"GenerateScale150", use150);
    AddCommandLineBoolSetting(L"Settings", L"GenerateScale200", use200);
    AddCommandLineBoolSetting(L"Settings", L"GenerateScale400", use400);
    return true;
}

static bool ApplyCommandLineAssetOverride(const std::wstring& rawValue, std::wstring& error)
{
    std::wstring key;
    std::wstring value;
    if (!TryParseCommandLineKeyValue(rawValue, key, value, error))
        return false;

    bool enabled = false;
    if (!TryParseCommandLineBoolValue(value, enabled))
    {
        error = L"Asset value must be a boolean: " + rawValue;
        return false;
    }

    static const wchar_t* assetKeys[] = {
        L"StoreLogo",
        L"MediumTile",
        L"Square44x44Logo",
        L"Square30x30Logo",
        L"SmallTile",
        L"WideTile",
        L"LargeTile",
        L"SplashScreen",
    };

    for (const wchar_t* assetKey : assetKeys)
    {
        if (IEquals(key, assetKey))
        {
            AddCommandLineBoolSetting(L"Assets", assetKey, enabled);
            return true;
        }
    }

    error = L"Unknown asset name: " + key + L".";
    return false;
}

static bool IsLikelyCommandLineOption(const std::wstring& arg)
{
    return !arg.empty() && (arg[0] == L'-' || arg[0] == L'/');
}

static bool CommandLineStartsWithI(const std::wstring& text, const wchar_t* prefix)
{
    std::wstring p = prefix ? prefix : L"";
    return text.size() >= p.size() && IEquals(text.substr(0, p.size()), p);
}

static bool TryParseAppxActivationArgument(const std::wstring& arg, std::wstring& serverName)
{
    serverName.clear();

    static const wchar_t* prefixes[] = {
        L"-ServerName:",
        L"/ServerName:",
        L"--ServerName:",
        L"-ServerName=",
        L"/ServerName=",
        L"--ServerName=",
    };

    for (const wchar_t* prefix : prefixes)
    {
        if (CommandLineStartsWithI(arg, prefix))
        {
            serverName = arg.substr(wcslen(prefix));
            return true;
        }
    }

    return false;
}

static bool IsAppxActivationServerNameOption(const std::wstring& arg)
{
    return IEquals(arg, L"-ServerName") ||
        IEquals(arg, L"/ServerName") ||
        IEquals(arg, L"--ServerName");
}

static bool IsIgnoredAppxActivationArgument(const std::wstring& arg)
{
    return IEquals(arg, L"-Embedding") ||
        IEquals(arg, L"/Embedding") ||
        IEquals(arg, L"--embedding");
}

static bool IsInternalAppActivationActionArgument(const std::wstring& arg)
{
    // IApplicationActivationManager::ActivateApplication appends its argument
    // string to the executable command line. These action tokens are not
    // wallpaper paths; accepting them here prevents a packaged relaunch like
    // "relaunch-live-tile" from being converted into
    // "<exe-dir>\relaunch-live-tile" and rejected by wallpaper validation.
    return IEquals(arg, L"relaunch-live-tile") ||
        IEquals(arg, L"tile-update") ||
        IEquals(arg, L"tile-clear") ||
        IEquals(arg, L"show-messagebox");
}

static bool SetCommandLineWallpaperPath(const std::wstring& rawPath, std::wstring& error)
{
    std::wstring path;
    if (!CommandLineMakeAbsolutePath(rawPath, path, error))
        return false;

    g_commandLine.wallpaperSpecified = true;
    g_commandLine.wallpaperPath = path;
    return true;
}

static bool ParseCommandLineOptions()
{
    g_commandLine = CommandLineOptions{};

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv)
    {
        g_commandLine.parseFailed = true;
        g_commandLine.parseError = L"CommandLineToArgvW failed (error " + std::to_wstring(GetLastError()) + L").";
        return false;
    }

    auto fail = [&](const std::wstring& message) -> bool
    {
        g_commandLine.parseFailed = true;
        g_commandLine.parseError = message;
        LocalFree(argv);
        return false;
    };

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i] ? argv[i] : L"";
        std::wstring value;
        std::wstring error;
        bool matched = false;

        if (IEquals(arg, L"--help") || IEquals(arg, L"-h") || IEquals(arg, L"/?") || IEquals(arg, L"/help"))
        {
            g_commandLine.helpRequested = true;
            continue;
        }
        if (IEquals(arg, L"--exit") || IEquals(arg, L"--quit"))
        {
            g_commandLine.exitRequested = true;
            continue;
        }

        if (TryParseAppxActivationArgument(arg, value))
        {
            g_commandLine.appxActivationServer = true;
            g_commandLine.appxActivationServerName = value;
            continue;
        }
        if (IsAppxActivationServerNameOption(arg))
        {
            g_commandLine.appxActivationServer = true;
            if (i + 1 < argc)
                g_commandLine.appxActivationServerName = argv[++i] ? argv[i] : L"";
            continue;
        }
        if (IsIgnoredAppxActivationArgument(arg))
        {
            g_commandLine.appxActivationServer = true;
            continue;
        }
        if (IEquals(arg, L"--launched-from-appx-stub"))
        {
            g_commandLine.launchedFromAppxStub = true;
            continue;
        }
        if (IEquals(arg, L"--launched-from-mode-switch"))
        {
            g_commandLine.launchedFromModeSwitch = true;
            continue;
        }
        if (IsInternalAppActivationActionArgument(arg))
        {
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--ds-wait-for-pid", value, error, matched))
            return fail(error);
        if (matched)
        {
            int pid = 0;
            if (!CommandLineParseInteger(value, pid) || pid <= 0)
                return fail(L"Invalid process id after --ds-wait-for-pid: " + value + L".");
            g_commandLine.waitForPid = static_cast<DWORD>(pid);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, COM_REGISTRATION_HELPER_ARG, value, error, matched))
            return fail(error);
        if (matched)
        {
            if (TrimCopy(value).empty())
                return fail(L"Missing manifest path after " + std::wstring(COM_REGISTRATION_HELPER_ARG) + L".");
            g_commandLine.comRegistrationHelper = true;
            g_commandLine.comRegistrationHelperManifestPath = value;
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--ini", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring path;
            if (!CommandLineMakeAbsolutePath(value, path, error))
                return fail(error);
            g_commandLine.customIniPath = true;
            g_commandLine.iniPath = path;
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--set", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring section;
            std::wstring key;
            std::wstring overrideValue;
            if (!TryParseCommandLineOverrideSpec(value, section, key, overrideValue, error))
                return fail(error);
            AddCommandLineIniSetting(section, key, overrideValue);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--wallpaper", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!SetCommandLineWallpaperPath(value, error))
                return fail(error);
            g_commandLine.forceGenerateOnStartup = true;
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--wallpaper-detection", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--detect", value, error, matched))
            return fail(error);
        if (matched)
        {
            AddCommandLineIniSetting(L"Settings", L"WallpaperDetectionMethod", value);
            continue;
        }

        if (IEquals(arg, L"--live-wallpaper-capture"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCapture", true);
            continue;
        }
        if (IEquals(arg, L"--no-live-wallpaper-capture"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCapture", false);
            continue;
        }
        if (IEquals(arg, L"--live-wallpaper-screen-fallback"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCaptureScreenFallback", true);
            continue;
        }
        if (IEquals(arg, L"--no-live-wallpaper-screen-fallback"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCaptureScreenFallback", false);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-wallpaper-delay", value, error, matched))
            return fail(error);
        if (matched)
        {
            int parsed = 0;
            if (!ParseCommandLineClampedInt(value, L"--live-wallpaper-delay", 0, 30000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"LiveWallpaperCaptureDelayMs", parsed);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-wallpaper-refresh", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring refreshValue = ToLowerCopy(TrimCopy(value));
            int parsed = 0;
            if (refreshValue == L"once" || refreshValue == L"none" || refreshValue == L"off")
                parsed = 0;
            else if (!ParseCommandLineClampedInt(value, L"--live-wallpaper-refresh", 0, 3600000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"LiveWallpaperCaptureRefreshMs", parsed);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-wallpaper-startup-refresh", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring refreshValue = ToLowerCopy(TrimCopy(value));
            int parsed = 0;
            if (refreshValue == L"none" || refreshValue == L"off")
                parsed = 0;
            else if (!ParseCommandLineClampedInt(value, L"--live-wallpaper-startup-refresh", 0, 3600000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", parsed);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-wallpaper-startup-refresh-duration", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring refreshValue = ToLowerCopy(TrimCopy(value));
            int parsed = 0;
            if (refreshValue == L"none" || refreshValue == L"off")
                parsed = 0;
            else if (!ParseCommandLineClampedInt(value, L"--live-wallpaper-startup-refresh-duration", 0, 3600000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", parsed);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--scales", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--scale", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!ApplyCommandLineScales(value, error))
                return fail(error);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--asset", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!ApplyCommandLineAssetOverride(value, error))
                return fail(error);
            continue;
        }

        if (IEquals(arg, L"--generated-asset-cache"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetCache", true);
            continue;
        }
        if (IEquals(arg, L"--no-generated-asset-cache"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetCache", false);
            continue;
        }
        if (IEquals(arg, L"--generated-asset-precache"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetPrecache", true);
            continue;
        }
        if (IEquals(arg, L"--no-generated-asset-precache"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetPrecache", false);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--generated-asset-cache-max", value, error, matched))
            return fail(error);
        if (matched)
        {
            int parsed = 0;
            if (!ParseCommandLineClampedInt(value, L"--generated-asset-cache-max", 0, 10000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetCacheMaxEntries", parsed);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--generated-asset-precache-max", value, error, matched))
            return fail(error);
        if (matched)
        {
            int parsed = 0;
            if (!ParseCommandLineClampedInt(value, L"--generated-asset-precache-max", 0, 10000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetPrecacheMaxFiles", parsed);
            continue;
        }

        if (IEquals(arg, L"--once"))
        {
            g_commandLine.generateOnce = true;
            g_commandLine.noMonitor = true;
            continue;
        }
        if (IEquals(arg, L"--regenerate-manifest") ||
            IEquals(arg, L"--overwrite-manifest") ||
            IEquals(arg, L"--rebuild-manifest"))
        {
            g_commandLine.regenerateManifest = true;
            continue;
        }
        if (IEquals(arg, L"--generate") || IEquals(arg, L"--generate-now"))
        {
            g_commandLine.forceGenerateOnStartup = true;
            continue;
        }
        if (IEquals(arg, L"--no-monitor") || IEquals(arg, L"--no-poll") || IEquals(arg, L"--no-watch"))
        {
            g_commandLine.noMonitor = true;
            continue;
        }
        if (IEquals(arg, L"--startup-generate"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GenerateOnStartup", true);
            continue;
        }
        if (IEquals(arg, L"--no-startup-generate"))
        {
            AddCommandLineBoolSetting(L"Settings", L"GenerateOnStartup", false);
            continue;
        }
        if (IEquals(arg, L"--tray"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrayIcon", true);
            continue;
        }
        if (IEquals(arg, L"--no-tray"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrayIcon", false);
            continue;
        }
        if (IEquals(arg, L"--console"))
        {
            AddCommandLineBoolSetting(L"Settings", L"ShowConsole", true);
            continue;
        }
        if (IEquals(arg, L"--no-console"))
        {
            AddCommandLineBoolSetting(L"Settings", L"ShowConsole", false);
            continue;
        }
        if (IEquals(arg, L"--logging"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Logging", true);
            continue;
        }
        if (IEquals(arg, L"--no-logging"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Logging", false);
            continue;
        }
        if (IEquals(arg, L"--notifications"))
        {
            AddCommandLineBoolSetting(L"Settings", L"NotificationsEnabled", true);
            continue;
        }
        if (IEquals(arg, L"--no-notifications"))
        {
            AddCommandLineBoolSetting(L"Settings", L"NotificationsEnabled", false);
            continue;
        }
        if (IEquals(arg, L"--powershell") || IEquals(arg, L"--use-powershell"))
        {
            AddCommandLineBoolSetting(L"Settings", L"UsePowerShell", true);
            continue;
        }
        if (IEquals(arg, L"--com") || IEquals(arg, L"--com-registration") || IEquals(arg, L"--no-powershell"))
        {
            AddCommandLineBoolSetting(L"Settings", L"UsePowerShell", false);
            continue;
        }
        if (IEquals(arg, L"--com-helper") || IEquals(arg, L"--com-registration-helper"))
        {
            AddCommandLineBoolSetting(L"Settings", L"ComRegistrationUseHelper", true);
            continue;
        }
        if (IEquals(arg, L"--no-com-helper") || IEquals(arg, L"--no-com-registration-helper"))
        {
            AddCommandLineBoolSetting(L"Settings", L"ComRegistrationUseHelper", false);
            continue;
        }
        if (IEquals(arg, L"--live-tile"))
        {
            AddCommandLineLiveTileModeSetting(LiveTileUpdateMode::LiveTile);
            continue;
        }
        if (IEquals(arg, L"--no-live-tile"))
        {
            AddCommandLineLiveTileModeSetting(LiveTileUpdateMode::Registration);
            continue;
        }
        if (IEquals(arg, L"--live-tile-auto"))
        {
            AddCommandLineLiveTileModeSetting(LiveTileUpdateMode::Auto);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-tile-mode", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring trimmed = TrimCopy(value);
            LiveTileUpdateMode mode = ParseLiveTileUpdateModeValue(trimmed, static_cast<LiveTileUpdateMode>(-1));
            if (!IsValidLiveTileUpdateMode(mode))
            {
                return fail(L"Unsupported Live Tile mode: " + trimmed + L". Supported values are Auto, Registration, and LiveTile.");
            }
            AddCommandLineLiveTileModeSetting(mode);
            continue;
        }

        if (IEquals(arg, L"--manifest-win10"))
        {
            AddCommandLineManifestTargetSetting(ManifestCompatibilityTarget::Windows10);
            g_commandLine.regenerateManifest = true;
            continue;
        }
        if (IEquals(arg, L"--manifest-win81") || IEquals(arg, L"--manifest-win8.1"))
        {
            AddCommandLineManifestTargetSetting(ManifestCompatibilityTarget::Windows81);
            g_commandLine.regenerateManifest = true;
            continue;
        }
        if (IEquals(arg, L"--manifest-win8") || IEquals(arg, L"--manifest-win80") || IEquals(arg, L"--manifest-win8.0"))
        {
            AddCommandLineManifestTargetSetting(ManifestCompatibilityTarget::Windows8);
            g_commandLine.regenerateManifest = true;
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--manifest-target", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring trimmed = TrimCopy(value);
            ManifestCompatibilityTarget target = ParseManifestCompatibilityTargetValue(trimmed, static_cast<ManifestCompatibilityTarget>(-1));
            if (!IsValidManifestCompatibilityTarget(target))
            {
                return fail(L"Unsupported manifest target: " + trimmed + L". Supported values are Windows10, Windows81, and Windows8.");
            }
            AddCommandLineManifestTargetSetting(target);
            g_commandLine.regenerateManifest = true;
            continue;
        }

        if (IEquals(arg, L"--win8-broker"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileBrokerApp", true);
            continue;
        }
        if (IEquals(arg, L"--no-win8-broker"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileBrokerApp", false);
            continue;
        }
        if (IEquals(arg, L"--win8-background-task"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileBackgroundTask", true);
            continue;
        }
        if (IEquals(arg, L"--no-win8-background-task"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileBackgroundTask", false);
            continue;
        }
        if (IEquals(arg, L"--win8-oop-helper"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileOopHelper", true);
            continue;
        }
        if (IEquals(arg, L"--no-win8-oop-helper"))
        {
            AddCommandLineBoolSetting(L"Settings", L"Win8LiveTileOopHelper", false);
            continue;
        }

        if (!IsLikelyCommandLineOption(arg) && !g_commandLine.wallpaperSpecified)
        {
            if (!SetCommandLineWallpaperPath(arg, error))
                return fail(error);
            g_commandLine.forceGenerateOnStartup = true;
            continue;
        }

        return fail(L"Unknown command-line option: " + arg + L". Use --help for supported options.");
    }

    LocalFree(argv);
    return true;
}

static std::wstring CommandLineUsageText()
{
    return
        L"DesktopStub.exe [options] [wallpaper-path]\r\n"
        L"\r\n"
        L"Options:\r\n"
        L"  --help, -h, /?                      Show this help.\r\n"
        L"  --ini <path>                        Use an alternate INI file.\r\n"
        L"  --set Section.Key=Value             Set and save any INI value.\r\n"
        L"  --exit, --quit                      Ask the running instance to exit.\r\n"
        L"  --once                              Generate once, then exit.\r\n"
        L"  --regenerate-manifest               Rewrite AppxManifest.xml once from built-in defaults.\r\n"
        L"  --generate, --generate-now           Force startup generation and keep running.\r\n"
        L"  --wallpaper <path>                  Generate from a specific image path.\r\n"
        L"  --no-monitor                        Do not start wallpaper/fit/DPI monitoring.\r\n"
        L"  --tray | --no-tray                  Set and save tray icon startup visibility.\r\n"
        L"  --console | --no-console            Set and save console visibility.\r\n"
        L"  --logging | --no-logging            Set and save file logging.\r\n"
        L"  --notifications | --no-notifications Set and save tray notifications.\r\n"
        L"  --powershell | --com-registration    Set and save registration command mode.\r\n"
        L"  --com-helper | --no-com-helper      Enable/disable COM isolation helper.\r\n"
        L"  --live-tile | --no-live-tile         Set and save Live Tile update mode.\r\n"
        L"  --live-tile-auto                     Set and save automatic Live Tile update mode.\r\n"
        L"  --live-tile-mode <mode>              Set mode: Auto, Registration, or LiveTile.\r\n"
        L"  --manifest-target <target>            Set and regenerate manifest: Windows10, Windows81, or Windows8.\r\n"
        L"  --manifest-win10|--manifest-win81|--manifest-win8\r\n"
        L"                                      Shortcuts for --manifest-target.\r\n"
        L"  --win8-broker | --no-win8-broker    Enable/disable Win8/8.1 broker helper.\r\n"
        L"  --win8-background-task | --no-win8-background-task\r\n"
        L"                                      Enable/disable experimental background task helper.\r\n"
        L"  --win8-oop-helper | --no-win8-oop-helper\r\n"
        L"                                      Enable/disable experimental OOP helper.\r\n"
        L"  --detect <method>                   Set and save wallpaper detection method.\r\n"
        L"  --live-wallpaper-capture | --no-live-wallpaper-capture\r\n"
        L"                                      Enable/disable live wallpaper snapshots.\r\n"
        L"  --live-wallpaper-screen-fallback | --no-live-wallpaper-screen-fallback\r\n"
        L"                                      Enable/disable unsafe screen-copy fallback.\r\n"
        L"  --live-wallpaper-delay <ms>          Set snapshot startup delay, 0-30000.\r\n"
        L"  --live-wallpaper-refresh <ms|once>   Set snapshot refresh, 0-3600000; once=0.\r\n"
        L"  --live-wallpaper-startup-refresh <ms|off>\r\n"
        L"                                      Recapture during live wallpaper startup; off=0.\r\n"
        L"  --live-wallpaper-startup-refresh-duration <ms|off>\r\n"
        L"                                      Startup recapture window duration; off=0.\r\n"
        L"  --scales auto|all|100,125,150,200,400\r\n"
        L"                                      Set and save generated DPI scales.\r\n"
        L"  --asset Name=0|1                    Set and save an asset toggle.\r\n"
        L"                                      Names: StoreLogo, MediumTile, Square44x44Logo,\r\n"
        L"                                      Square30x30Logo, SmallTile, WideTile, LargeTile, SplashScreen.\r\n"
        L"  --generated-asset-cache | --no-generated-asset-cache\r\n"
        L"                                      Enable/disable generated asset cache.\r\n"
        L"  --generated-asset-precache | --no-generated-asset-precache\r\n"
        L"                                      Enable/disable slideshow pre-cache.\r\n"
        L"  --generated-asset-cache-max <n>      Set cache max entries, 0-10000.\r\n"
        L"  --generated-asset-precache-max <n>   Set pre-cache max files, 0-10000.\r\n"
        L"  --set Settings.Key=Value            Set any other INI-backed setting directly.\r\n"
        L"\r\n"
        L"Examples:\r\n"
        L"  DesktopStub.exe --once\r\n"
        L"  DesktopStub.exe --no-tray --console\r\n"
        L"  DesktopStub.exe --ini D:\\Temp\\DesktopStub.ini --set Settings.PollIntervalMs=5000\r\n"
        L"  DesktopStub.exe --wallpaper D:\\Pictures\\wallpaper.jpg --scales 100,200\r\n"
        L"  DesktopStub.exe --exit\r\n";
}

static bool WriteCommandLineTextToConsole(const std::wstring& text, bool errorOutput)
{
    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached && GetLastError() != ERROR_ACCESS_DENIED)
        return false;

    HANDLE h = GetStdHandle(errorOutput ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (h == INVALID_HANDLE_VALUE || h == nullptr)
    {
        if (attached)
            FreeConsole();
        return false;
    }

    std::wstring output = text;
    if (output.empty() || (output.back() != L'\n' && output.back() != L'\r'))
        output += L"\r\n";

    DWORD mode = 0;
    bool ok = false;
    if (GetConsoleMode(h, &mode))
    {
        DWORD written = 0;
        ok = WriteConsoleW(h, output.c_str(), static_cast<DWORD>(output.size()), &written, nullptr) != FALSE;
    }
    else
    {
        std::vector<BYTE> bytes = EncodeUtf8Text(output, false);
        if (!bytes.empty() || output.empty())
        {
            DWORD written = 0;
            ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != FALSE;
        }
    }

    if (attached)
        FreeConsole();
    return ok;
}

static void ShowCommandLineMessage(const std::wstring& text, bool error)
{
    if (WriteCommandLineTextToConsole(text, error))
        return;

    MessageBoxW(
        nullptr,
        text.c_str(),
        error ? L"DesktopStub command line" : L"DesktopStub",
        MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

static bool CommandLineHasPersistentSettings()
{
    return !g_commandLine.settings.empty();
}

static bool CommandLineShouldExit()
{
    return g_commandLine.exitRequested;
}

static bool CommandLineShouldGenerateNow()
{
    return g_commandLine.forceGenerateOnStartup || g_commandLine.generateOnce;
}

static bool CommandLineShouldRegenerateManifest()
{
    return g_commandLine.regenerateManifest;
}

static bool CommandLineLiveTileModeChanged()
{
    return g_commandLine.liveTileModeChanged;
}

static bool CommandLineTrayIconChanged()
{
    return g_commandLine.trayIconChanged;
}

static bool CommandLineShouldGenerateOnce()
{
    return g_commandLine.generateOnce;
}

static bool CommandLineShouldForceStartupGeneration()
{
    return g_commandLine.forceGenerateOnStartup;
}

static bool CommandLineShouldSkipMonitor()
{
    return g_commandLine.noMonitor;
}

static bool CommandLineWallpaper(std::wstring& wallpaper)
{
    if (!g_commandLine.wallpaperSpecified)
        return false;
    wallpaper = g_commandLine.wallpaperPath;
    return true;
}

static bool CommandLineStringFormat1(const wchar_t* key, const wchar_t* fallback, const std::wstring& value, std::wstring& out)
{
    std::wstring fmt = fallback ? fallback : L"%s";
    if (!g_iniPath.empty())
    {
        std::wstring configured = IniReadS(L"Strings", key, fmt.c_str());
        if (!configured.empty())
            fmt = configured;
    }

    out = FormatWide(fmt.c_str(), value.c_str());
    if (out.empty())
        out = value;
    return true;
}

static bool ValidateCommandLineWallpaperPath()
{
    if (!g_commandLine.wallpaperSpecified)
        return true;

    DWORD attrs = GetFileAttributesW(g_commandLine.wallpaperPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return true;

    CommandLineStringFormat1(
        L"CommandLineWallpaperPathInvalid",
        L"Wallpaper path does not exist or is not a file: %s",
        g_commandLine.wallpaperPath,
        g_commandLine.applyError);
    return false;
}

static bool CommandLineComRegistrationHelper(std::wstring& manifestPath)
{
    if (!g_commandLine.comRegistrationHelper)
        return false;
    manifestPath = g_commandLine.comRegistrationHelperManifestPath;
    return true;
}

static void AppendCommandLineForwardedOptions(std::wstring& cmdline)
{
    if (g_commandLine.customIniPath)
        cmdline += L" --ini " + QuoteCommandLineArg(g_commandLine.iniPath);
}

static void LogCommandLineSummary()
{
    if (g_commandLine.customIniPath)
        Log(L"[i] Command-line INI path: %s", g_commandLine.iniPath.c_str());
    if (g_commandLine.generateOnce)
        Log(L"[i] Command-line mode: generate once, then exit.");
    if (g_commandLine.exitRequested)
        Log(L"[i] Command-line mode: exit running instance.");
    if (g_commandLine.forceGenerateOnStartup && !g_commandLine.generateOnce)
        Log(L"[i] Command-line mode: force startup generation.");
    if (g_commandLine.regenerateManifest)
        Log(L"[i] Command-line mode: regenerate AppxManifest.xml once.");
    if (g_commandLine.noMonitor && !g_commandLine.generateOnce)
        Log(L"[i] Command-line mode: monitoring disabled.");
    if (g_commandLine.wallpaperSpecified)
        Log(L"[i] Command-line wallpaper: %s", g_commandLine.wallpaperPath.c_str());
    if (g_commandLine.appxActivationServer)
    {
        if (!g_commandLine.appxActivationServerName.empty())
            Log(L"[i] AppX activation server argument ignored: %s", g_commandLine.appxActivationServerName.c_str());
        else
            Log(L"[i] AppX activation argument ignored.");
    }
    if (g_commandLine.launchedFromAppxStub)
        Log(L"[i] Command-line launch source: AppX activation stub.");
    if (g_commandLine.launchedFromModeSwitch)
        Log(L"[i] Command-line launch source: Live Tile mode switch relaunch.");
    if (g_commandLine.waitForPid != 0)
        Log(L"[i] Command-line parent wait pid: %lu", static_cast<unsigned long>(g_commandLine.waitForPid));

    for (const auto& entry : g_commandLine.settings)
        Log(L"[i] Command-line setting: [%s] %s = %s", entry.section.c_str(), entry.key.c_str(), entry.value.c_str());
}

static bool CommandLineSettingChangesLiveTileMode(const CommandLineIniSetting& entry)
{
    if (!IEquals(entry.section, L"Settings") || !IEquals(entry.key, L"ExperimentalLiveTileUpdate"))
        return false;

    LiveTileUpdateMode before = ParseLiveTileUpdateModeValue(
        IniReadS(L"Settings", L"ExperimentalLiveTileUpdate", L"Auto"));
    LiveTileUpdateMode after = ParseLiveTileUpdateModeValue(entry.value, LiveTileUpdateMode::Auto);

    return before != after;
}

static bool CommandLineSettingChangesTrayIcon(const CommandLineIniSetting& entry)
{
    return IEquals(entry.section, L"Settings") && IEquals(entry.key, L"TrayIcon");
}

static bool CommandLineSettingChangesManifestTarget(const CommandLineIniSetting& entry)
{
    if (!IEquals(entry.section, L"Settings") || !IEquals(entry.key, L"AppxManifestTarget"))
        return false;

    ManifestCompatibilityTarget before = ParseManifestCompatibilityTargetValue(
        IniReadS(L"Settings", L"AppxManifestTarget", L"Windows10"));
    ManifestCompatibilityTarget after = ParseManifestCompatibilityTargetValue(entry.value, ManifestCompatibilityTarget::Windows10);

    return before != after;
}

static bool CommandLineSettingTargetsManifestEditor(const CommandLineIniSetting& entry)
{
    if (IEquals(entry.section, L"Manifest"))
        return true;

    if (IEquals(entry.section, L"Settings"))
        return ToLowerCopy(TrimCopy(entry.key)).rfind(L"manifest", 0) == 0;

    return false;
}

static bool ApplyCommandLinePersistentSettings()
{
    g_commandLine.applyError.clear();
    g_commandLine.liveTileModeChanged = false;
    g_commandLine.trayIconChanged = false;

    for (const auto& entry : g_commandLine.settings)
    {
        if (CommandLineSettingTargetsManifestEditor(entry))
        {
            std::wstring settingName = entry.section + L"." + entry.key;
            CommandLineStringFormat1(
                L"CommandLineManifestSettingIgnored",
                L"Manifest INI setting is no longer supported: %s. Edit AppxManifest.xml directly or use --regenerate-manifest to restore the built-in default manifest.",
                settingName,
                g_commandLine.applyError);
            return false;
        }

        bool changesLiveTileMode = CommandLineSettingChangesLiveTileMode(entry);
        bool changesTrayIcon = CommandLineSettingChangesTrayIcon(entry);
        bool changesManifestTarget = CommandLineSettingChangesManifestTarget(entry);
        if (!WriteIniValue(entry.section.c_str(), entry.key.c_str(), entry.value.c_str()))
        {
            DWORD err = GetLastError();
            if (err == ERROR_SUCCESS)
                err = ERROR_WRITE_FAULT;
            g_commandLine.applyError = L"Failed to write command-line setting [" +
                entry.section + L"] " + entry.key + L" (error " +
                std::to_wstring(err) + L").";
            return false;
        }

        if (changesLiveTileMode)
            g_commandLine.liveTileModeChanged = true;
        if (changesTrayIcon)
            g_commandLine.trayIconChanged = true;
        if (changesManifestTarget)
            g_commandLine.regenerateManifest = true;
    }

    return true;
}

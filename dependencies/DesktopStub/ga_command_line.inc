// -----------------------------------------------------------------------------
// Command-line parsing and persisted INI setting changes
// -----------------------------------------------------------------------------
using CommandLineIniSetting = aip::IniSetting;
static bool StageExplicitSourceTileNameIntents(std::wstring& text, const std::vector<aip::IniSetting>& settings, std::wstring& error);
#include "../live_capture_protocol.h"
#include "../content_presets.h"
#include "../secondary_tiles.h"
#include "../gallery_engine.h"
#include "../gallery_image_activation.h"
#include "../content_sources/notes_profile.inc"
#include "../content_sources/notes_signin_model.h"
#include "../hardware/hardware_profile_import.inc"
#include "../content_sources/now_playing_profile.inc"
struct CommandLineNotesImport { int resource = -1; std::wstring path; };
struct CommandLineHardwareImport { bool asus = false; std::wstring path; };

struct CommandLineOptions
{
    bool helpRequested = false;
    bool setupDesktop = false;
    bool settingsWindow = false;
    bool profileManager = false;
    std::wstring tileSetupKey;
    std::wstring windowsTileId, mediaAction;
    bool tileSignIn = false;
    std::wstring openUrl, openFolder;
    std::wstring galleryImageToken;
    bool galleryFolderActivation = false;
    bool versionRequested = false;
    bool parseFailed = false;
    bool exitRequested = false;
    bool generateOnce = false;
    bool renderOnly = false;
    bool configureOnly = false;
    bool forceGenerateOnStartup = false;
    bool regenerateManifest = false;
    bool noMonitor = false;
    bool customIniPath = false;
    bool wallpaperSpecified = false;
    bool comRegistrationHelper = false;
    bool appxActivationServer = false;
    bool liveTileClearOnly = false;
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
    int notesRefresh = -1;
    std::vector<CommandLineNotesImport> notesImports;
    std::vector<CommandLineHardwareImport> hardwareImports;
    std::vector<std::wstring> nowPlayingImports;
    std::wstring presetName, presetPreviewDocument;
    bool presetPreviewLoaded = false;
    aip::now_playing_profile::ImportPlan nowPlayingImportPlan;
    std::vector<std::wstring> nowPlayingImportDiagnostics;
    std::vector<aip::IniSetting> discordProfileSettings;
    std::wstring discordCreatePath;
    bool discordPrivateInput = false;
};

static CommandLineOptions g_commandLine;
static bool NormalizeCommandLinePresetName(std::wstring& value)
{
    for (const auto* name : {L"Wallpaper", L"WallpaperRSS", L"WallpaperSMTC"})
        if (aip::content::Lower(aip::content::Trim(value)) == aip::content::Lower(name)) { value = name; return true; }
    return false;
}
static bool ValidateCommandLinePresetCombination(std::wstring& error)
{
    if (g_commandLine.presetName.empty()) return true;
    if (!g_commandLine.notesImports.empty() || !g_commandLine.hardwareImports.empty() || !g_commandLine.nowPlayingImports.empty() ||
        !g_commandLine.discordProfileSettings.empty() || !g_commandLine.discordCreatePath.empty()) {
        error = L"--preset cannot be combined with profile imports or advanced Discord edits. Apply them in separate commands."; return false;
    }
    return true;
}
void Log(const wchar_t* fmt, ...);
std::wstring FormatWide(const wchar_t* fmt, ...);
int IniReadI(const wchar_t* s, const wchar_t* k, int d);
static bool ValidateDesktopStubPackagedStartupPreference(bool desired, std::wstring& error, const wchar_t* targetOverride = nullptr);

static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SHOW = 0;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_RELOAD_SETTINGS = 1;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_EXIT = 2;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_GENERATE = 3;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_REGENERATE_MANIFEST = 4;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SHOW_MESSAGEBOX = 5;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SHOW_CONSOLE = 6;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE = 7;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SETUP_DESKTOP = 11;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_SETTINGS_WINDOW = 12;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_PROFILE_MANAGER = 13;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_FRESH_PROFILE = 14;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_MASK = 0x00FF;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE = 32;
static constexpr WPARAM SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE = 64;
static constexpr WPARAM SINGLE_INSTANCE_FLAG_TILE_SIGN_IN = 0x0400;
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

static ContentSource ParseContentSourceValue(
    const std::wstring& rawValue,
    ContentSource fallback = ContentSource::Wallpaper)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v.empty())
        return fallback;

    if (v == L"wallpaper" || v == L"desktop" || v == L"default")
        return ContentSource::Wallpaper;
    if (v == L"rssfeed" || v == L"rss_feed" || v == L"rss-feed" ||
        v == L"rss" || v == L"feed")
        return ContentSource::RssFeed;

    return fallback;
}

static bool IsValidContentSource(ContentSource source)
{
    return source == ContentSource::Wallpaper ||
        source == ContentSource::RssFeed;
}

static std::wstring ContentSourceIniValue(ContentSource source)
{
    return source == ContentSource::RssFeed ? L"RssFeed" : L"Wallpaper";
}

static const wchar_t* ContentSourceName(ContentSource source)
{
    switch (source)
    {
    case ContentSource::RssFeed:
        return L"RSS Feed";
    case ContentSource::Wallpaper:
    default:
        return L"Wallpaper";
    }
}

static void AddCommandLineLiveTileModeSetting(LiveTileUpdateMode mode)
{
    AddCommandLineIniSetting(L"Settings", L"ExperimentalLiveTileUpdate", LiveTileUpdateModeIniValue(mode));
}

static LiveTileTemplateStyle ParseLiveTileTemplateStyleValue(
    const std::wstring& rawValue,
    LiveTileTemplateStyle fallback = LiveTileTemplateStyle::Adaptive)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v.empty())
        return fallback;

    if (v == L"adaptive" || v == L"windows10" || v == L"windows 10" ||
        v == L"win10" || v == L"default")
        return LiveTileTemplateStyle::Adaptive;
    if (v == L"windows81preset" || v == L"windows81" || v == L"windows8.1" ||
        v == L"windows 8.1" || v == L"win81" || v == L"win8.1" ||
        v == L"preset" || v == L"legacy")
        return LiveTileTemplateStyle::Windows81Preset;

    return fallback;
}

static std::wstring LiveTileTemplateStyleIniValue(LiveTileTemplateStyle style)
{
    switch (style)
    {
    case LiveTileTemplateStyle::Windows81Preset:
        return L"Windows81Preset";
    case LiveTileTemplateStyle::Adaptive:
    default:
        return L"Adaptive";
    }
}

static const wchar_t* LiveTileTemplateStyleName(LiveTileTemplateStyle style)
{
    switch (style)
    {
    case LiveTileTemplateStyle::Windows81Preset:
        return L"Windows 8.1 preset catalog";
    case LiveTileTemplateStyle::Adaptive:
    default:
        return L"Adaptive";
    }
}

static bool IsValidLiveTileTemplateStyle(LiveTileTemplateStyle style)
{
    return style == LiveTileTemplateStyle::Adaptive ||
        style == LiveTileTemplateStyle::Windows81Preset;
}

static void AddCommandLineLiveTileTemplateStyleSetting(LiveTileTemplateStyle style)
{
    AddCommandLineIniSetting(L"Settings", L"LiveTileTemplateStyle", LiveTileTemplateStyleIniValue(style));
}

static LiveTileBranding ParseLiveTileBrandingValue(
    const std::wstring& rawValue,
    LiveTileBranding fallback = LiveTileBranding::Auto)
{
    std::wstring v = ToLowerCopy(TrimCopy(rawValue));
    if (v.empty())
        return fallback;

    if (v == L"auto" || v == L"automatic" || v == L"default")
        return LiveTileBranding::Auto;
    if (v == L"none" || v == L"off" || v == L"disabled")
        return LiveTileBranding::None;
    if (v == L"logo")
        return LiveTileBranding::Logo;
    if (v == L"name")
        return LiveTileBranding::Name;
    if (v == L"nameandlogo" || v == L"name-and-logo" ||
        v == L"name_and_logo" || v == L"name+logo" ||
        v == L"name and logo")
        return LiveTileBranding::NameAndLogo;

    return fallback;
}

static std::wstring LiveTileBrandingIniValue(LiveTileBranding branding)
{
    switch (branding)
    {
    case LiveTileBranding::None:
        return L"None";
    case LiveTileBranding::Logo:
        return L"Logo";
    case LiveTileBranding::Name:
        return L"Name";
    case LiveTileBranding::NameAndLogo:
        return L"NameAndLogo";
    case LiveTileBranding::Auto:
    default:
        return L"Auto";
    }
}

static const wchar_t* LiveTileBrandingName(LiveTileBranding branding)
{
    switch (branding)
    {
    case LiveTileBranding::None:
        return L"None";
    case LiveTileBranding::Logo:
        return L"Logo";
    case LiveTileBranding::Name:
        return L"Name";
    case LiveTileBranding::NameAndLogo:
        return L"Name + logo";
    case LiveTileBranding::Auto:
    default:
        return L"Automatic";
    }
}

static bool IsValidLiveTileBranding(LiveTileBranding branding)
{
    return branding == LiveTileBranding::Auto ||
        branding == LiveTileBranding::None ||
        branding == LiveTileBranding::Logo ||
        branding == LiveTileBranding::Name ||
        branding == LiveTileBranding::NameAndLogo;
}

static void AddCommandLineLiveTileBrandingSetting(LiveTileBranding branding)
{
    AddCommandLineIniSetting(L"Settings", L"LiveTileBranding", LiveTileBrandingIniValue(branding));
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

// QuoteCommandLineArg now lives in the shared dependencies/core.inc (aip::),
// exposed unqualified via the using-declaration in ga_core.inc.

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
    const wchar_t* optionName = name ? name : L"";
    if (!aip::IsOptionOrInlineValue(arg, optionName))
        return true;

    matched = true;
    std::wstring optionArg = arg.find(L'=') == std::wstring::npos ? optionName : arg;
    return aip::TakeCommandLineValue(argc, argv, index, optionArg, value, error);
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
    int parsed = 0;
    if (!aip::ParseIntValueInRange(rawValue, minValue, maxValue, parsed))
    {
        error = std::wstring(optionName ? optionName : L"Option") +
            L" expects an integer from " + std::to_wstring(minValue) +
            L" to " + std::to_wstring(maxValue) + L".";
        return false;
    }
    value = parsed;
    return true;
}

static bool AddCommandLineClampedIntSetting(
    const wchar_t* section,
    const wchar_t* key,
    const wchar_t* optionName,
    const std::wstring& rawValue,
    int minValue,
    int maxValue,
    std::wstring& error)
{
    int parsed = 0;
    if (!ParseCommandLineClampedInt(rawValue, optionName, minValue, maxValue, parsed, error))
        return false;

    AddCommandLineIntSetting(section, key, parsed);
    return true;
}

static bool TryParseCommandLineBoolValue(const std::wstring& rawValue, bool& value)
{
    return aip::ParseBoolValue(rawValue, value);
}

enum class DesktopStubDisplaySettingType { Boolean, LiveTileMode, TemplateStyle, Branding, Target, NotificationApi, NotificationInterval };
struct DesktopStubDisplaySetting {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* fallback;
    DesktopStubDisplaySettingType type;
};
// CLI overrides and complete profiles use the same explicit schema. Integer
// settings must not accidentally accept boolean words as an interval of 0/1.
static constexpr DesktopStubDisplaySetting g_displaySettings[] = {
    {L"Settings", L"Logging", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"TrayIcon", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ShowConsole", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"RunAtStartup", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"RunAtStartupPackaged", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateOnStartup", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateOnStartupOnlyWhenChanged", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ShowMenuAsDropdown", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotificationsEnabled", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings",L"AlreadyRunningProfilePrompt",L"1",DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotificationApi", L"Auto", DesktopStubDisplaySettingType::NotificationApi},
    {L"Settings", L"NotificationProgressIntervalMs", L"750", DesktopStubDisplaySettingType::NotificationInterval},
    {L"Settings", L"NotifyOnOperations", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotificationConfirmations", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnPinProgress", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifySourceErrors", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyGalleryChanges", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"AutoUnpinRemovedGalleryTiles", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ConfirmUnpinRemovedGalleryTiles", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnStart", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnSuccess", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnFailure", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnBusy", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnAlreadyRunning", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"NotifyOnSingleInstanceFailure", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ListenWallpaper", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"WallpaperDetectionFallbackOnInvalid", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"TrimWorkingSetOnIdle", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"CompactCrtHeapOnIdle", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"KeepGdiPlusLoaded", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"Win8LiveTileBrokerApp", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"Win8LiveTileBackgroundTask", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"Win8LiveTileOopHelper", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"LiveWallpaperCapture", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"LiveWallpaperCaptureScreenFallback", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"UsePowerShell", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ComRegistrationUseHelper", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ExperimentalLiveTileUpdate", L"Auto", DesktopStubDisplaySettingType::LiveTileMode},
    {L"Settings", L"LiveTileTemplateStyle", L"Adaptive", DesktopStubDisplaySettingType::TemplateStyle},
    {L"Settings", L"LiveTileBranding", L"Auto", DesktopStubDisplaySettingType::Branding},
    {L"Settings", L"AppxManifestTarget", L"Windows10", DesktopStubDisplaySettingType::Target},
    {L"Settings", L"LiveTileRelaunchOnSwitch", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ClearLiveTileOnShutdown", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"HideDisabledEntries", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateDesktopIconForDisabledEntries", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"DeleteDisabledAssets", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScaleAuto", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScale100", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScale125", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScale150", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScale200", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GenerateScale400", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GeneratedAssetCache", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"GeneratedAssetPrecache", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"WallpaperDecodeLowMemory", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"DisableFitting", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Settings", L"ListenFit", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"TileText", L"Enabled", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"TileText", L"ApplyToMediumTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"TileText", L"ApplyToWideTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"TileText", L"ApplyToLargeTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"StoreLogo", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"MediumTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"Square44x44Logo", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"Square30x30Logo", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"SmallTile", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"WideTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"LargeTile", L"1", DesktopStubDisplaySettingType::Boolean},
    {L"Assets", L"SplashScreen", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"State", L"LiveTileModeReregistrationPending", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"State", L"DesktopHostSetupPending", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"State", L"DesktopHostSetupPreviousPending", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"State", L"LiveTileClearedOnShutdown", L"0", DesktopStubDisplaySettingType::Boolean},
    {L"State", L"ForceShutdownPendingCleanup", L"0", DesktopStubDisplaySettingType::Boolean},
};
static const DesktopStubDisplaySetting* FindDesktopStubDisplaySetting(const std::wstring& section, const std::wstring& key)
{
    for (const auto& setting : g_displaySettings)
        if (IEquals(section, setting.section) && IEquals(key, setting.key)) return &setting;
    return nullptr;
}
static bool IsDesktopStubDisplayBoolSetting(const std::wstring& section, const std::wstring& key)
{
    const auto* setting = FindDesktopStubDisplaySetting(section, key);
    return setting && setting->type == DesktopStubDisplaySettingType::Boolean;
}
static bool NormalizeDesktopStubDisplaySettingValue(const DesktopStubDisplaySetting& setting,
    std::wstring& value, std::wstring& error)
{
    const auto fail = [&](const wchar_t* expected) {
        error = std::wstring(setting.section) + L"." + setting.key + L" expects " + expected + L".";
        return false;
    };
    switch (setting.type) {
    case DesktopStubDisplaySettingType::NotificationInterval: {
        int parsed=0;
        if(!aip::ParseIntValueInRange(value,250,60000,parsed))return fail(L"an integer from 250 to 60000");
        value=std::to_wstring(parsed);break;
    }
    case DesktopStubDisplaySettingType::NotificationApi: {
        value = TrimCopy(value);
        if (IEquals(value,L"Auto")) value=L"Auto";
        else if (IEquals(value,L"Balloon")) value=L"Balloon";
        else if (IEquals(value,L"Toast")) value=L"Toast";
        else return fail(L"Auto, Balloon, or Toast");
        break;
    }
    case DesktopStubDisplaySettingType::Boolean: {
        bool enabled = false;
        if (!TryParseCommandLineBoolValue(value, enabled)) return fail(L"a true/false value");
        value = enabled ? L"1" : L"0";
        break;
    }
    case DesktopStubDisplaySettingType::LiveTileMode: {
        const auto mode = ParseLiveTileUpdateModeValue(value, static_cast<LiveTileUpdateMode>(-1));
        if (!IsValidLiveTileUpdateMode(mode)) return fail(L"Auto, LiveTile, or Registration");
        value = LiveTileUpdateModeIniValue(mode);
        break;
    }
    case DesktopStubDisplaySettingType::TemplateStyle: {
        const auto style = ParseLiveTileTemplateStyleValue(value, static_cast<LiveTileTemplateStyle>(-1));
        if (!IsValidLiveTileTemplateStyle(style)) return fail(L"Adaptive or Windows81Preset");
        value = LiveTileTemplateStyleIniValue(style);
        break;
    }
    case DesktopStubDisplaySettingType::Branding: {
        const auto branding = ParseLiveTileBrandingValue(value, static_cast<LiveTileBranding>(-1));
        if (!IsValidLiveTileBranding(branding)) return fail(L"Auto, None, Logo, Name, or NameAndLogo");
        value = LiveTileBrandingIniValue(branding);
        break;
    }
    case DesktopStubDisplaySettingType::Target: {
        const auto target = ParseManifestCompatibilityTargetValue(value, static_cast<ManifestCompatibilityTarget>(-1));
        if (!IsValidManifestCompatibilityTarget(target)) return fail(L"Windows10, Windows81, or Windows8");
        value = ManifestCompatibilityTargetIniValue(target);
        break;
    }
    }
    error.clear();
    return true;
}

static void AddCommandLinePrimaryTileTextSetting(const std::wstring& value)
{
    AddCommandLineIniSetting(L"TileText", L"Text", value);
    // Clearing the primary field preserves a still-visible secondary/badge,
    // matching the tray editor and the explicit --tile-text-clear action.
    if (!TrimCopy(value).empty()) AddCommandLineBoolSetting(L"TileText", L"Enabled", true);
}

static bool IsDesktopStubStartupSetting(
    const std::wstring& section,
    const std::wstring& key)
{
    return IEquals(section, L"Settings") && IEquals(key, L"RunAtStartup");
}

static bool IsDesktopStubContentSourceSetting(
    const std::wstring& section,
    const std::wstring& key)
{
    return IEquals(section, L"Settings") && IEquals(key, L"ContentSource");
}

static bool IsDesktopStubRssFeedSetting(
    const std::wstring& section,
    const std::wstring& key)
{
    if (!IEquals(section, L"RssFeed"))
        return false;

    return IEquals(key, L"FeedUrl") ||
        IEquals(key, L"UserAgent") ||
        IEquals(key, L"UpdateIntervalSeconds") ||
        IEquals(key, L"MaxItems") ||
        IEquals(key, L"HttpTimeoutSeconds") ||
        IEquals(key, L"MaxFeedBytes");
}

static bool ValidateRssFeedUrlSetting(
    const std::wstring& rawValue,
    bool allowEmpty,
    std::wstring& normalized,
    std::wstring& error)
{
    normalized = TrimCopy(rawValue);
    if (normalized.empty())
    {
        if (allowEmpty)
            return true;
        error = L"RssFeed.FeedUrl is required when Settings.ContentSource=RssFeed.";
        return false;
    }

    if (normalized.size() > 2048 ||
        std::any_of(normalized.begin(), normalized.end(), [](wchar_t ch) { return iswspace(ch) != 0; }))
    {
        error = L"RssFeed.FeedUrl must be a valid http or https URL no longer than 2048 characters.";
        return false;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    components.dwUserNameLength = static_cast<DWORD>(-1);
    components.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(normalized.c_str(), 0, 0, &components) ||
        (components.nScheme != INTERNET_SCHEME_HTTP &&
            components.nScheme != INTERNET_SCHEME_HTTPS) ||
        components.dwHostNameLength == 0 ||
        components.dwHostNameLength > 255)
    {
        error = L"RssFeed.FeedUrl must be a valid http or https URL with a valid host.";
        return false;
    }
    if (components.dwUserNameLength || components.dwPasswordLength)
    {
        error = L"RSS URLs with embedded usernames or passwords are not supported.";
        return false;
    }
    return true;
}

static bool ValidateRssFeedUserAgentSetting(
    const std::wstring& rawValue,
    std::wstring& normalized,
    std::wstring& error)
{
    normalized = TrimCopy(rawValue);
    if (normalized.empty() || normalized.size() > 512 ||
        normalized.find(L'\r') != std::wstring::npos ||
        normalized.find(L'\n') != std::wstring::npos)
    {
        error = L"RssFeed.UserAgent must contain 1 to 512 characters and no line breaks.";
        return false;
    }
    return true;
}

static bool RssFeedIntegerRange(
    const std::wstring& key,
    int& minValue,
    int& maxValue)
{
    if (IEquals(key, L"UpdateIntervalSeconds"))
    {
        minValue = 60;
        maxValue = 86400;
        return true;
    }
    if (IEquals(key, L"MaxItems"))
    {
        minValue = 1;
        maxValue = 20;
        return true;
    }
    if (IEquals(key, L"HttpTimeoutSeconds"))
    {
        minValue = 5;
        maxValue = 120;
        return true;
    }
    if (IEquals(key, L"MaxFeedBytes"))
    {
        minValue = 64 * 1024;
        maxValue = 16 * 1024 * 1024;
        return true;
    }
    return false;
}

#include "../tile_instances.h"
#include "../properties_ui_model.h"
#include "../desktopstub_profiles.h"
static bool NormalizeDesktopStubTypedSetting(
    const std::wstring& section,
    const std::wstring& key,
    std::wstring& value,
    std::wstring& error)
{
    if(IEquals(section,L"Properties"))return aip::properties::NormalizeSetting(key,value,error);
    if(IEquals(section,L"Settings")&&IEquals(key,L"TileIconFont"))return aip::content::NormalizeTilePolicySetting(L"Artwork.IconFont",value,error);
    if(IEquals(section,L"Settings")&&IEquals(key,L"PrimaryRegistration"))return aip::content::NormalizeTilePolicySetting(L"Delivery.PrimaryRegistration",value,error);
    if(IEquals(section,L"Profile"))return aip::profiles::NormalizeSetting(key,value,error);
    if (aip::content::IsTileInstanceSetting(section, key)) {
        if (!aip::content::NormalizeTileInstanceSetting(section, key, value, error)) return false;
        if(const auto* policy=aip::content::FindTilePolicyField(key)){
            if(const auto* mapped=FindDesktopStubDisplaySetting(policy->section,policy->setting)){
                std::wstring issue,mappedValue=value;
                if(!NormalizeDesktopStubDisplaySettingValue(*mapped,mappedValue,issue)){error=section+L"."+key+L": "+issue;return false;}
            }
            return true; // Integer/path policies were validated by the portable table.
        }
        const auto dot = key.find(L'.');
        if (dot != std::wstring::npos && IEquals(key.substr(0, dot), L"RssFeed")) {
            std::wstring issue;
            if (!NormalizeDesktopStubTypedSetting(L"RssFeed", key.substr(dot + 1), value, issue)) {
                error = section + L"." + key + L": " + issue; return false;
            }
        }
        return true;
    }
    if (aip::gallery::KnownGallerySection(section))
        return aip::gallery::NormalizeSetting(section, key, value, error);
    if (aip::notes::KnownSignInSection(section))
        return aip::notes::NormalizeNotesSignInSetting(section, key, value, error);
    if (IEquals(section, L"QuickActions") || IEquals(section, L"MediaActions"))
        return aip::content::NormalizeTileActionSetting(section, key, value, error);
    if (IEquals(section, L"Tiles") || aip::content::Lower(section).compare(0, 5, L"tile.") == 0)
        return aip::content::NormalizeSecondaryTileSetting(section, key, value, error);
    if (aip::content::KnownSection(section) &&
        (!IEquals(section, L"TileText") || aip::content::IsTextPresentationKey(key)))
        return aip::content::NormalizeSetting(section, key, value, error);
    if (const auto* display = FindDesktopStubDisplaySetting(section, key))
        return NormalizeDesktopStubDisplaySettingValue(*display, value, error);
    if (IsDesktopStubStartupSetting(section, key) ||
        (IEquals(section, L"Settings") && IEquals(key, L"RunAtStartupPackaged")))
    {
        bool startup = false;
        if (!TryParseCommandLineBoolValue(value, startup))
        {
            error = L"Settings." + key + L" expects a true/false value.";
            return false;
        }
        value = startup ? L"1" : L"0";
        return true;
    }

    if (IsDesktopStubContentSourceSetting(section, key))
    {
        ContentSource source = ParseContentSourceValue(
            value,
            static_cast<ContentSource>(-1));
        if (!IsValidContentSource(source))
        {
            error = L"Settings.ContentSource expects Wallpaper or RssFeed.";
            return false;
        }
        value = ContentSourceIniValue(source);
        return true;
    }

    if (!IsDesktopStubRssFeedSetting(section, key))
        return true;

    if (IEquals(key, L"FeedUrl"))
        return ValidateRssFeedUrlSetting(value, true, value, error);
    if (IEquals(key, L"UserAgent"))
        return ValidateRssFeedUserAgentSetting(value, value, error);

    int minValue = 0;
    int maxValue = 0;
    if (RssFeedIntegerRange(key, minValue, maxValue))
    {
        int parsed = 0;
        if (!aip::ParseIntValueInRange(value, minValue, maxValue, parsed))
        {
            error = L"RssFeed." + key + L" expects an integer from " +
                std::to_wstring(minValue) + L" to " +
                std::to_wstring(maxValue) + L".";
            return false;
        }
        value = std::to_wstring(parsed);
    }
    return true;
}

static bool TryParseCommandLineOverrideSpec(
    const std::wstring& spec,
    std::wstring& section,
    std::wstring& key,
    std::wstring& value,
    std::wstring& error)
{
    CommandLineIniSetting setting;
    aip::IniSetSpecError errorKind = aip::IniSetSpecError::None;
    std::wstring sharedError;
    if (!aip::ParseIniSetSpec(spec, setting, sharedError, &errorKind))
    {
        error = errorKind == aip::IniSetSpecError::EmptySectionOrKey
            ? L"Section and key cannot be empty: " + spec
            : L"Expected Section.Key=Value: " + spec;
        return false;
    }

    // Instance fields deliberately contain dots (RssFeed.FeedUrl, Notes.UID).
    // The shared grammar retains its last-dot split for all other programs;
    // DesktopStub knows that Tile.<stable-id> is the complete section name.
    if (aip::content::Lower(setting.section).compare(0, 5, L"tile.") == 0) {
        const auto afterId = setting.section.find(L'.', 5);
        if (afterId != std::wstring::npos && aip::content::ValidTileInstanceId(aip::content::Lower(setting.section.substr(5, afterId - 5)))) {
            setting.key = setting.section.substr(afterId + 1) + L"." + setting.key;
            setting.section.resize(afterId);
        }
    }
    // Gallery rows use a numbered section and the same dotted local policies.
    // Preserve their complete policy key while retaining the shared parser's
    // established grammar for unrelated sections.
    if(aip::content::Lower(setting.section).compare(0,13,L"gallery.tile.")==0) {
        const auto afterIndex=setting.section.find(L'.',13);
        if(afterIndex!=std::wstring::npos&&aip::gallery::TileSectionNumber(setting.section.substr(0,afterIndex))) {
            setting.key=setting.section.substr(afterIndex+1)+L"."+setting.key;
            setting.section.resize(afterIndex);
        }
    }

    section = std::move(setting.section);
    key = std::move(setting.key);
    value = std::move(setting.value);
    return true;
}

static bool TryParseCommandLineKeyValue(
    const std::wstring& spec,
    std::wstring& key,
    std::wstring& value,
    std::wstring& error)
{
    size_t eq = spec.find(L'=');
    if (eq == std::wstring::npos)
    {
        error = L"Expected Name=Value: " + spec;
        return false;
    }

    key = TrimCopy(spec.substr(0, eq));
    value = spec.substr(eq + 1);
    if (key.empty())
    {
        error = L"Name cannot be empty: " + spec;
        return false;
    }

    return true;
}

static bool CommandLineParseInteger(const std::wstring& rawValue, int& value)
{
    return aip::ParseIntValue(rawValue, value);
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

    bool desktopSetupOtherOption=false;
    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i] ? argv[i] : L"";
        if(!IEquals(arg,L"--setup-desktop")&&!IEquals(arg,L"--prepare-live-tile-host")&&!IEquals(arg,L"--ini")&&!CommandLineStartsWithI(arg,L"--ini=")&&
            !IEquals(arg,L"--help")&&!IEquals(arg,L"-h")&&!IEquals(arg,L"/?")&&!IEquals(arg,L"/help")&&
            !IEquals(arg,L"--version")&&!IEquals(arg,L"-v")&&!IEquals(arg,L"/version"))desktopSetupOtherOption=true;
        std::wstring value;
        std::wstring error;
        bool matched = false;

        if (IEquals(arg, L"--help") || IEquals(arg, L"-h") || IEquals(arg, L"/?") || IEquals(arg, L"/help"))
        {
            g_commandLine.helpRequested = true;
            continue;
        }
        if (IEquals(arg, L"--version") || IEquals(arg, L"-v") || IEquals(arg, L"/version"))
        {
            g_commandLine.versionRequested = true;
            continue;
        }
        if (IEquals(arg, L"--exit") || IEquals(arg, L"--quit"))
        {
            g_commandLine.exitRequested = true;
            continue;
        }

        if(IEquals(arg,L"--setup-desktop")||IEquals(arg,L"--prepare-live-tile-host")){g_commandLine.setupDesktop=true;continue;}
        if(IEquals(arg,L"--settings")){g_commandLine.settingsWindow=true;continue;}
        if(IEquals(arg,L"--profile-manager")){g_commandLine.profileManager=true;continue;}
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
        if (IEquals(arg, L"tile-clear"))
        {
            g_commandLine.liveTileClearOnly = true;
            continue;
        }
        if (IsInternalAppActivationActionArgument(arg))
        {
            continue;
        }

        const wchar_t* waitForPidArgUsed = nullptr;
        if (!CommandLineTryGetOptionValue(argc, argv, i, WAIT_FOR_PID_ARG, value, error, matched))
            return fail(error);
        if (matched)
        {
            waitForPidArgUsed = WAIT_FOR_PID_ARG;
        }
        else
        {
            if (!CommandLineTryGetOptionValue(argc, argv, i, WAIT_FOR_PID_ARG_LEGACY, value, error, matched))
                return fail(error);
            if (matched)
                waitForPidArgUsed = WAIT_FOR_PID_ARG_LEGACY;
        }
        if (matched)
        {
            int pid = 0;
            if (!CommandLineParseInteger(value, pid) || pid <= 0)
                return fail(L"Invalid process id after " + std::wstring(waitForPidArgUsed ? waitForPidArgUsed : WAIT_FOR_PID_ARG) + L": " + value + L".");
            g_commandLine.waitForPid = static_cast<DWORD>(pid);
            continue;
        }

        const wchar_t* comHelperArgUsed = nullptr;
        if (!CommandLineTryGetOptionValue(argc, argv, i, COM_REGISTRATION_HELPER_ARG, value, error, matched))
            return fail(error);
        if (matched)
        {
            comHelperArgUsed = COM_REGISTRATION_HELPER_ARG;
        }
        else
        {
            if (!CommandLineTryGetOptionValue(argc, argv, i, COM_REGISTRATION_HELPER_ARG_LEGACY, value, error, matched))
                return fail(error);
            if (matched)
                comHelperArgUsed = COM_REGISTRATION_HELPER_ARG_LEGACY;
        }
        if (matched)
        {
            if (TrimCopy(value).empty())
                return fail(L"Missing manifest path after " + std::wstring(comHelperArgUsed ? comHelperArgUsed : COM_REGISTRATION_HELPER_ARG) + L".");
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

        // The desktop secondary-tile launcher appends this Windows metadata.
        // Consume the value; never interpret it as an arbitrary command.
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"/tileid", value, error, matched)) return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--tileid", value, error, matched)) return fail(error);
        if (matched) {
            if (value.empty() || value.size() > 128) return fail(L"Invalid Windows tile id.");
            g_commandLine.windowsTileId = value; continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--media-action", value, error, matched)) return fail(error);
        if (matched) {
            const auto* action = aip::content::FindTileAction(value);
            if (!action) return fail(L"Unknown media action.");
            g_commandLine.mediaAction = action->id; continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--open-url", value, error, matched)) return fail(error);
        if (matched) {
            if (!ValidateRssFeedUrlSetting(value, false, g_commandLine.openUrl, error)) return fail(error);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--open-folder", value, error, matched)) return fail(error);
        if (matched) {
            if (value.empty() || !aip::gallery::detail::Text(value, 32768, false))
                return fail(L"--open-folder requires a nonempty directory path on one line.");
            g_commandLine.openFolder = value;
            g_commandLine.galleryFolderActivation = true;
            continue;
        }
        if(!CommandLineTryGetOptionValue(argc,argv,i,L"--gallery-image",value,error,matched))return fail(error);
        if(matched){aip::gallery::activation::Token token;if(!aip::gallery::activation::Decode(value,token))return fail(L"--gallery-image expects an owned gallery photo token, never a file path.");
            g_commandLine.galleryImageToken=value;g_commandLine.galleryFolderActivation=true;continue;}
        if (IEquals(arg, L"--tile-sign-in")) { g_commandLine.tileSignIn = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--tile-setup", value, error, matched)) return fail(error);
        if (matched) {
            value = aip::content::Lower(aip::content::Trim(value));
            if (!aip::content::ValidTileInstanceId(value)) return fail(L"--tile-setup expects a configured tile ID.");
            g_commandLine.tileSetupKey = value;
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--preset", value, error, matched)) return fail(error);
        if (matched) {
            if (!NormalizeCommandLinePresetName(value)) return fail(L"--preset expects Wallpaper, WallpaperRSS, or WallpaperSMTC.");
            g_commandLine.presetName = value;
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--cycle-driver", value, error, matched)) return fail(error);
        if (matched) {
            if (!aip::content::NormalizeSetting(L"Content", L"CycleDriver", value, error)) return fail(error);
            AddCommandLineIniSetting(L"Content", L"CycleDriver", value);
            continue;
        }

        if (IEquals(arg, L"--discord-private-input")) { g_commandLine.discordPrivateInput = true; g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--discord-create-profile", value, error, matched)) return fail(error);
        if (matched) { if (value.empty()) return fail(L"--discord-create-profile requires a path."); g_commandLine.discordCreatePath = value; g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--discord-set", value, error, matched)) return fail(error);
        if (matched) {
            aip::IniSetting setting;
            if (!aip::ParseIniSetSpec(value, setting, error)) return fail(L"--discord-set expects Section.Key=Value (value not shown).");
            g_commandLine.discordProfileSettings.push_back(std::move(setting)); g_commandLine.configureOnly = true; continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--notes-import-profile", value, error, matched)) return fail(error);
        if (matched) { g_commandLine.notesImports.push_back({-1, value}); g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--caps-import-profile", value, error, matched)) return fail(error);
        if (matched) { g_commandLine.hardwareImports.push_back({false, value}); g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--asus-import-profile", value, error, matched)) return fail(error);
        if (matched) { g_commandLine.hardwareImports.push_back({true, value}); g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--now-playing-import-profile", value, error, matched)) return fail(error);
        if (matched) { g_commandLine.nowPlayingImports.push_back(value); g_commandLine.configureOnly = true; continue; }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--notes-import-cookie", value, error, matched)) return fail(error);
        if (matched) {
            const auto equal = value.find(L'='); int resource = -1;
            if (equal == std::wstring::npos || !aip::notes::ParseHostResource(value.substr(0, equal), resource) || equal + 1 == value.size())
                return fail(L"--notes-import-cookie expects Resin|Stamina|Charge=JSON-path.");
            g_commandLine.notesImports.push_back({resource, value.substr(equal + 1)}); g_commandLine.configureOnly = true; continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--notes-clear", value, error, matched)) return fail(error);
        if (matched) {
            int resource = -1;
            if (!aip::notes::ParseHostResource(value, resource)) return fail(L"--notes-clear expects Resin, Stamina, or Charge.");
            for (const auto& setting : aip::notes::ClearAccountSettings(resource)) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
            g_commandLine.configureOnly = true; continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--notes-refresh", value, error, matched)) return fail(error);
        if (matched) {
            if (!aip::notes::ParseHostResource(value, g_commandLine.notesRefresh, true)) return fail(L"--notes-refresh expects Resin, Stamina, Charge, or All.");
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
            if (IEquals(section, L"AsusBlink")) {
                const auto canonical = aip::hardware_profile::CanonicalAsusKey(key);
                if (!canonical.empty()) key = canonical;
            }
            if (!NormalizeDesktopStubTypedSetting(section, key, overrideValue, error))
                return fail(L"--set " + error);
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

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--content-source", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring normalized = value;
            if (!NormalizeDesktopStubTypedSetting(
                    L"Settings",
                    L"ContentSource",
                    normalized,
                    error))
            {
                return fail(L"--content-source: " + error);
            }
            AddCommandLineIniSetting(L"Settings", L"ContentSource", normalized);
            // This is the dedicated legacy-preset command, not a raw --set
            // assignment. Match the tray action by turning off the numbered
            // content profile; the entries remain in the INI for later reuse.
            AddCommandLineIniSetting(L"Content", L"Enabled", L"0");
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-feed-url", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring normalized;
            if (!ValidateRssFeedUrlSetting(value, false, normalized, error))
                return fail(L"--rss-feed-url: " + error);
            AddCommandLineIniSetting(L"RssFeed", L"FeedUrl", normalized);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-user-agent", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring normalized;
            if (!ValidateRssFeedUserAgentSetting(value, normalized, error))
                return fail(L"--rss-user-agent: " + error);
            AddCommandLineIniSetting(L"RssFeed", L"UserAgent", normalized);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-update-interval", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!AddCommandLineClampedIntSetting(
                    L"RssFeed",
                    L"UpdateIntervalSeconds",
                    L"--rss-update-interval",
                    value,
                    60,
                    86400,
                    error))
            {
                return fail(error);
            }
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-max-items", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!AddCommandLineClampedIntSetting(
                    L"RssFeed",
                    L"MaxItems",
                    L"--rss-max-items",
                    value,
                    1,
                    20,
                    error))
            {
                return fail(error);
            }
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-http-timeout", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!AddCommandLineClampedIntSetting(
                    L"RssFeed",
                    L"HttpTimeoutSeconds",
                    L"--rss-http-timeout",
                    value,
                    5,
                    120,
                    error))
            {
                return fail(error);
            }
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--rss-max-feed-bytes", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!AddCommandLineClampedIntSetting(
                    L"RssFeed",
                    L"MaxFeedBytes",
                    L"--rss-max-feed-bytes",
                    value,
                    64 * 1024,
                    16 * 1024 * 1024,
                    error))
            {
                return fail(error);
            }
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

        if (IEquals(arg, L"--trim-working-set-on-idle"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrimWorkingSetOnIdle", true);
            continue;
        }
        if (IEquals(arg, L"--no-trim-working-set-on-idle"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrimWorkingSetOnIdle", false);
            continue;
        }
        if (IEquals(arg, L"--low-idle-memory"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrimWorkingSetOnIdle", true);
            AddCommandLineBoolSetting(L"Settings", L"CompactCrtHeapOnIdle", true);
            AddCommandLineBoolSetting(L"Settings", L"KeepGdiPlusLoaded", false);
            AddCommandLineIntSetting(L"Settings", L"IdleTrimIntervalMs", 60000);
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCapture", false);
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetPrecache", false);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetCacheMaxEntries", 32);
            AddCommandLineIntSetting(L"Settings", L"LogBufferLines", 128);
            continue;
        }
        if (IEquals(arg, L"--no-low-idle-memory"))
        {
            AddCommandLineBoolSetting(L"Settings", L"TrimWorkingSetOnIdle", false);
            AddCommandLineBoolSetting(L"Settings", L"CompactCrtHeapOnIdle", false);
            AddCommandLineBoolSetting(L"Settings", L"KeepGdiPlusLoaded", true);
            AddCommandLineIntSetting(L"Settings", L"IdleTrimIntervalMs", 0);
            AddCommandLineBoolSetting(L"Settings", L"LiveWallpaperCapture", true);
            AddCommandLineBoolSetting(L"Settings", L"GeneratedAssetPrecache", true);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetCacheMaxEntries", 64);
            AddCommandLineIntSetting(L"Settings", L"LogBufferLines", 1024);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--idle-trim-interval", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--idle-working-set-trim-interval", value, error, matched))
            return fail(error);
        if (matched)
        {
            int parsed = 0;
            std::wstring trimValue = ToLowerCopy(TrimCopy(value));
            if (trimValue == L"none" || trimValue == L"off")
                parsed = 0;
            else if (!ParseCommandLineClampedInt(value, L"--idle-trim-interval", 0, 3600000, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"IdleTrimIntervalMs", parsed);
            continue;
        }

        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--ini-write-lock-wait-ms", value, error, matched))
            return fail(error);
        if (matched)
        {
            if (!AddCommandLineClampedIntSetting(L"Settings", L"IniWriteLockWaitMs", L"--ini-write-lock-wait-ms", value, 0, 60000, error))
                return fail(error);
            continue;
        }

        if (IEquals(arg, L"--compact-crt-heap-on-idle"))
        {
            AddCommandLineBoolSetting(L"Settings", L"CompactCrtHeapOnIdle", true);
            continue;
        }
        if (IEquals(arg, L"--no-compact-crt-heap-on-idle"))
        {
            AddCommandLineBoolSetting(L"Settings", L"CompactCrtHeapOnIdle", false);
            continue;
        }
        if (IEquals(arg, L"--keep-gdiplus-loaded") || IEquals(arg, L"--no-lazy-gdiplus"))
        {
            AddCommandLineBoolSetting(L"Settings", L"KeepGdiPlusLoaded", true);
            continue;
        }
        if (IEquals(arg, L"--no-keep-gdiplus-loaded") || IEquals(arg, L"--lazy-gdiplus"))
        {
            AddCommandLineBoolSetting(L"Settings", L"KeepGdiPlusLoaded", false);
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
        if (IEquals(arg, L"--wallpaper-decode-low-memory") || IEquals(arg, L"--low-memory-wallpaper-decode"))
        {
            AddCommandLineBoolSetting(L"Settings", L"WallpaperDecodeLowMemory", true);
            continue;
        }
        if (IEquals(arg, L"--no-wallpaper-decode-low-memory") || IEquals(arg, L"--no-low-memory-wallpaper-decode"))
        {
            AddCommandLineBoolSetting(L"Settings", L"WallpaperDecodeLowMemory", false);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--wallpaper-decode-max-long-edge", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--wallpaper-decode-max", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring decodeValue = ToLowerCopy(TrimCopy(value));
            int parsed = 0;
            if (decodeValue == L"auto" || decodeValue == L"none" || decodeValue == L"off")
                parsed = 0;
            else if (!ParseCommandLineClampedInt(value, L"--wallpaper-decode-max-long-edge", 0, 32768, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"WallpaperDecodeMaxLongEdge", parsed);
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
            if (!ParseCommandLineClampedInt(value, L"--generated-asset-cache-max", 0, 4096, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetCacheMaxEntries", parsed);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--generated-asset-precache-max", value, error, matched))
            return fail(error);
        if (matched)
        {
            int parsed = 0;
            if (!ParseCommandLineClampedInt(value, L"--generated-asset-precache-max", 0, 256, parsed, error))
                return fail(error);
            AddCommandLineIntSetting(L"Settings", L"GeneratedAssetPrecacheMaxFiles", parsed);
            continue;
        }

        if (IEquals(arg, L"--configure-only"))
        {
            g_commandLine.configureOnly = true;
            g_commandLine.noMonitor = true;
            continue;
        }
        if (IEquals(arg, L"--render-only"))
        {
            g_commandLine.renderOnly = true;
            g_commandLine.noMonitor = true;
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
        if (IEquals(arg, L"--startup"))
        {
            AddCommandLineBoolSetting(L"Settings", L"RunAtStartup", true);
            continue;
        }
        if (IEquals(arg, L"--no-startup"))
        {
            AddCommandLineBoolSetting(L"Settings", L"RunAtStartup", false);
            continue;
        }
        if (IEquals(arg, L"--packaged-startup") || IEquals(arg, L"--no-packaged-startup"))
        {
            AddCommandLineBoolSetting(L"Settings", L"RunAtStartupPackaged", IEquals(arg, L"--packaged-startup"));
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
        if (IEquals(arg, L"--live-tile-adaptive"))
        {
            AddCommandLineLiveTileTemplateStyleSetting(LiveTileTemplateStyle::Adaptive);
            continue;
        }
        if (IEquals(arg, L"--live-tile-windows81-preset") ||
            IEquals(arg, L"--live-tile-win81-preset"))
        {
            AddCommandLineLiveTileTemplateStyleSetting(LiveTileTemplateStyle::Windows81Preset);
            continue;
        }
        if (IEquals(arg, L"--live-tile-relaunch-on-mismatch") ||
            IEquals(arg, L"--relaunch-on-live-tile-mismatch") ||
            IEquals(arg, L"--live-tile-relaunch-on-switch"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveTileRelaunchOnSwitch", true);
            continue;
        }
        if (IEquals(arg, L"--no-live-tile-relaunch-on-mismatch") ||
            IEquals(arg, L"--no-relaunch-on-live-tile-mismatch") ||
            IEquals(arg, L"--no-live-tile-relaunch-on-switch"))
        {
            AddCommandLineBoolSetting(L"Settings", L"LiveTileRelaunchOnSwitch", false);
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
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-tile-template", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring trimmed = TrimCopy(value);
            LiveTileTemplateStyle style = ParseLiveTileTemplateStyleValue(
                trimmed,
                static_cast<LiveTileTemplateStyle>(-1));
            if (!IsValidLiveTileTemplateStyle(style))
            {
                return fail(L"Unsupported Live Tile template style: " + trimmed +
                    L". Supported values are Adaptive and Windows81Preset.");
            }
            AddCommandLineLiveTileTemplateStyleSetting(style);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--live-tile-branding", value, error, matched))
            return fail(error);
        if (matched)
        {
            std::wstring trimmed = TrimCopy(value);
            LiveTileBranding branding = ParseLiveTileBrandingValue(
                trimmed,
                static_cast<LiveTileBranding>(-1));
            if (!IsValidLiveTileBranding(branding))
            {
                return fail(L"Unsupported Live Tile branding: " + trimmed +
                    L". Supported values are Auto, None, Logo, Name, and NameAndLogo.");
            }
            AddCommandLineLiveTileBrandingSetting(branding);
            continue;
        }

        if (IEquals(arg, L"--tile-text-enable"))
        {
            AddCommandLineBoolSetting(L"TileText", L"Enabled", true);
            continue;
        }
        if (IEquals(arg, L"--tile-text-disable"))
        {
            AddCommandLineBoolSetting(L"TileText", L"Enabled", false);
            continue;
        }
        if (IEquals(arg, L"--tile-text-clear"))
        {
            AddCommandLineIniSetting(L"TileText", L"Text", L"");
            continue;
        }
        if (IEquals(arg, L"--tile-text-secondary-clear") || IEquals(arg, L"--tile-subtext-clear"))
        {
            AddCommandLineIniSetting(L"TileText", L"SecondaryText", L"");
            continue;
        }
        if (IEquals(arg, L"--tile-text-badge-clear") || IEquals(arg, L"--tile-badge-clear"))
        {
            AddCommandLineIniSetting(L"TileText", L"BadgeText", L"");
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--tile-text", value, error, matched))
            return fail(error);
        if (matched)
        {
            AddCommandLinePrimaryTileTextSetting(value);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--tile-text-secondary", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--tile-subtext", value, error, matched))
            return fail(error);
        if (matched)
        {
            AddCommandLineIniSetting(L"TileText", L"SecondaryText", value);
            if (!TrimCopy(value).empty())
                AddCommandLineBoolSetting(L"TileText", L"Enabled", true);
            continue;
        }
        if (!CommandLineTryGetOptionValue(argc, argv, i, L"--tile-text-badge", value, error, matched))
            return fail(error);
        if (!matched && !CommandLineTryGetOptionValue(argc, argv, i, L"--tile-badge", value, error, matched))
            return fail(error);
        if (matched)
        {
            AddCommandLineIniSetting(L"TileText", L"BadgeText", value);
            if (!TrimCopy(value).empty())
                AddCommandLineBoolSetting(L"TileText", L"Enabled", true);
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

    std::wstring combinationError;
    if(!g_commandLine.galleryImageToken.empty()&&!g_commandLine.openFolder.empty())return fail(L"--gallery-image and --open-folder cannot be combined.");
    if(g_commandLine.setupDesktop&&!g_commandLine.helpRequested&&!g_commandLine.versionRequested&&desktopSetupOtherOption)
        return fail(L"--setup-desktop is an explicit package setup action. Combine it only with --ini, --help or --version; run settings, imports, Startup and offline commands separately.");
    if (!ValidateCommandLinePresetCombination(combinationError)) return fail(combinationError);
    if (g_commandLine.windowsTileId.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0)
        g_commandLine.galleryFolderActivation = true;
    if (g_commandLine.galleryFolderActivation && !g_commandLine.windowsTileId.empty() &&
        g_commandLine.windowsTileId.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) != 0 &&
        !g_commandLine.helpRequested && !g_commandLine.versionRequested)
        return fail(L"--open-folder cannot be combined with another source's Windows tile id.");
    if (g_commandLine.galleryFolderActivation && !g_commandLine.helpRequested && !g_commandLine.versionRequested &&
        (g_commandLine.renderOnly || g_commandLine.configureOnly || !g_commandLine.settings.empty() || !g_commandLine.presetName.empty() ||
            !g_commandLine.openUrl.empty() || !g_commandLine.mediaAction.empty() || !g_commandLine.tileSetupKey.empty() || g_commandLine.tileSignIn ||
            g_commandLine.notesRefresh >= 0 || !g_commandLine.notesImports.empty() || !g_commandLine.hardwareImports.empty() ||
            !g_commandLine.nowPlayingImports.empty() || !g_commandLine.discordProfileSettings.empty() || !g_commandLine.discordCreatePath.empty() ||
            g_commandLine.generateOnce || g_commandLine.forceGenerateOnStartup || g_commandLine.exitRequested ||
            g_commandLine.regenerateManifest || g_commandLine.appxActivationServer || g_commandLine.comRegistrationHelper))
        return fail(L"--open-folder and gallery tile activation cannot be combined with settings, source actions, or rendering commands.");
    if (g_commandLine.tileSetupKey.empty() && g_commandLine.mediaAction.empty()) {
        const auto& id = g_commandLine.windowsTileId;
        if (id.compare(0, 10, L"AipSource_") == 0) {
            if (aip::content::ValidTileInstanceId(id.substr(10))) g_commandLine.tileSetupKey = id.substr(10);
        } else if (id.compare(0, 10, L"AipAction_") == 0) {
            auto key = id.substr(10); std::replace(key.begin(), key.end(), L'_', L'-');
            if (const auto* action = aip::content::FindTileAction(key)) g_commandLine.mediaAction = action->id;
        }
    }
    LocalFree(argv);
    return true;
}

static std::wstring CommandLineProgramName()
{
    std::wstring name = g_exePath;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name.erase(0, slash + 1);
    return name.empty() ? L"DesktopStub.exe" : name;
}

static std::wstring FormatCommandLineHelpTemplate(const wchar_t* rawTemplate)
{
    std::wstring exe = CommandLineProgramName();
    std::wstring iniExampleName = exe;
    size_t iniExampleDot = iniExampleName.find_last_of(L'.');
    if (iniExampleDot != std::wstring::npos)
        iniExampleName.erase(iniExampleDot);
    if (iniExampleName.empty())
        iniExampleName = L"DesktopStub";

    return aip::FormatTextTemplate(
        rawTemplate ? rawTemplate : g_commandLineHelpDefaults[0].value,
        {
            { L"{exe}", exe },
            { L"{iniExampleName}", iniExampleName }
        });
}

static std::wstring CommandLineUsageText()
{
    std::wstring iniTemplate = IniReadS(
        COMMAND_LINE_HELP_SECTION,
        COMMAND_LINE_HELP_TEMPLATE_KEY,
        g_commandLineHelpDefaults[0].value);
    return FormatCommandLineHelpTemplate(iniTemplate.c_str()) +
        L"\nSettings window: --settings [--ini FILE] opens the native Properties window; the tray menu remains available.\n"
        L"Profiles: --profile-manager; --profile-list; --profile-create NAME [--profile-path FILE]; --profile ID [other options].\n"
        L"  --profile-start ID; --profile-stop ID; --profile-settings ID; --profile-default ID; --profile-autostart ID on|off.\n"
        L"  --profiles-startup on|off; --profiles-unified-tray on|off; --profiles-crash-notifications on|off.\n"
        L"  Each profile has its own INI, accounts, persistent package identity and generated assets; tray icons are separate by default.\n"
        L"\nOptional Live Tile support: --prepare-live-tile-host [--ini FILE] registers the main host, then restarts with package identity. Keeps every tile's delivery choice. --setup-desktop is an alias. Ordinary Registration first run needs no command.\n"
        L"\nNotes: --notes-import-profile INI; --notes-import-cookie GAME=JSON; --notes-clear GAME (configuration only).\n"
        L"       --notes-refresh GAME|All (existing resident only; no new process).\n"
        L"Hardware profiles: --caps-import-profile INI; --asus-import-profile INI (configuration only).\n"
        L"Now playing: --now-playing-import-profile INI (replaces Content.*; donor Startup is reported and deferred).\n"
        L"Presets: --preset Wallpaper|WallpaperRSS|WallpaperSMTC (applied before explicit settings; imports require a separate command).\n"
        L"Rotation: --cycle-driver DesktopStub|Auto|Windows (Windows controls native queue display timing).\n"
        L"Discord advanced profile: --discord-set Section.Key=Value; --discord-create-profile INI (configuration only).\n"
        L"  Credential replacements require masked tray input or explicit --discord-private-input; values are never echoed.\n"
        L"ASUS numbered fields: --set AsusBlink.Event001HddState=128,129 (hardware and disk flags stay explicit).\n"
        L"Gallery: Gallery.Folder, TileCount (1-1024), QueueSize (1-5), RefreshSeconds (60-86400), Order, Fit, Seed; Gallery.Tile.N.Folder/GalleryName select separate folders; ClickAction=Image|Setup. Pinned tiles update automatically.\n"
        L"  Use Tiles > Photo gallery > Set up gallery first to create permanent per-tile ownership history; pin each tile manually.\n"
        L"  --open-folder DIRECTORY opens an existing directory; relative paths are beside the selected INI.\n";
}

static bool AsusEventKey(const std::wstring& key)
{
    return aip::hardware_profile::CanonicalAsusKey(key).compare(0, 5, L"Event") == 0;
}

static std::map<std::wstring, std::wstring> ContentAsusEventsFromDocument(const std::vector<aip::IniSectionData>& document)
{
    std::map<std::wstring, std::wstring> events;
    for (const auto& section : document) if (IEquals(section.name, L"AsusBlink"))
        for (const auto& setting : section.entries) if (AsusEventKey(setting.key)) events[setting.key] = setting.value;
    return events;
}

static bool ReplacesCommandLineAsusEvents()
{
    return std::any_of(g_commandLine.hardwareImports.begin(), g_commandLine.hardwareImports.end(), [](const auto& request) { return request.asus; });
}

static std::map<std::wstring, std::wstring> ReadContentAsusEvents(bool commandLineOverrides = false)
{
    std::map<std::wstring, std::wstring> events;
    if (!commandLineOverrides || !ReplacesCommandLineAsusEvents()) {
        std::vector<IniEntry> entries;
        ReadIniSectionCached(L"AsusBlink", entries);
        for (const auto& entry : entries) if (AsusEventKey(entry.key)) events[entry.key] = entry.value;
    }
    if (commandLineOverrides) for (const auto& setting : g_commandLine.settings)
        if (IEquals(setting.section, L"AsusBlink") && AsusEventKey(setting.key)) {
            const auto canonical = aip::hardware_profile::CanonicalAsusKey(setting.key);
            for (auto entry = events.begin(); entry != events.end(); )
                if (aip::hardware_profile::CanonicalAsusKey(entry->first) == canonical) entry = events.erase(entry);
                else ++entry;
            events[canonical] = setting.value;
        }
    return events;
}

static bool RemoveContentAsusEvents(std::wstring& text)
{
    const auto document = aip::ParseIniDocument(text);
    for (const auto& section : document) if (IEquals(section.name, L"AsusBlink"))
        for (const auto& setting : section.entries) if (AsusEventKey(setting.key))
            if (!aip::RemoveIniValueFromText(text, section.name.c_str(), setting.key.c_str())) return false;
    return true;
}

static bool WriteContentProfileSetting(std::wstring& text, const aip::IniSetting& setting,std::wstring* issue=nullptr)
{
    const auto originalDocument=aip::ParseIniDocument(text);
    std::vector<aip::content::TileInstanceSetting> aliases;std::wstring aliasError;
    if(!aip::content::PlanLegacyDesktopSettingAlias([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
        std::wstring value;return aip::ReadIniValueFromDoc(originalDocument,section,key,value)?value:std::wstring(fallback);
    },setting.section,setting.key,setting.value,aliases,aliasError)){
        if(issue)*issue=aliasError;SetLastError(ERROR_INVALID_DATA);return false;
    }
    if(!aliases.empty()){
        auto candidate=text;
        if(!WriteIniValueToText(candidate,setting.section.c_str(),setting.key.c_str(),setting.value.c_str()))return false;
        for(const auto& alias:aliases)
            if(!WriteIniValueToText(candidate,alias.section.c_str(),alias.key.c_str(),alias.value.c_str()))return false;
        text=std::move(candidate);return true;
    }
    if (IEquals(setting.section, L"AsusBlink") && AsusEventKey(setting.key)) {
        const auto canonical = aip::hardware_profile::CanonicalAsusKey(setting.key);
        const auto document = aip::ParseIniDocument(text);
        for (const auto& section : document) if (IEquals(section.name, L"AsusBlink"))
            for (const auto& entry : section.entries)
                if (aip::hardware_profile::CanonicalAsusKey(entry.key) == canonical && !IEquals(entry.key, canonical))
                    if (!aip::RemoveIniValueFromText(text, section.name.c_str(), entry.key.c_str())) return false;
        return WriteIniValueToText(text, L"AsusBlink", canonical.c_str(), setting.value.c_str());
    }
    return WriteIniValueToText(text, setting.section.c_str(), setting.key.c_str(), setting.value.c_str());
}

static bool ReadContentNowPlayingImport(const std::wstring& path, aip::now_playing_profile::ImportPlan& plan,
    aip::now_playing_profile::ConfigurationProjection& projection, std::wstring& error)
{
    plan = {}; projection = {}; error.clear();
    if (TrimCopy(path).empty()) { error = L"A NowPlayingTile donor INI path is required."; return false; }
    if (aip::ToLower(aip::MakeAbsolutePath(path)) == aip::ToLower(aip::MakeAbsolutePath(g_iniPath))) {
        error = L"The import source must differ from the target INI."; return false;
    }
    return aip::now_playing_profile::ImportProfileFile(path, plan, error) &&
        aip::now_playing_profile::ProjectConfigurationOnly(plan, projection, error);
}

static bool ExpandCommandLineNowPlayingImports(std::wstring& error)
{
    if (g_commandLine.nowPlayingImports.empty()) return true;
    aip::now_playing_profile::ImportPlan plan;
    aip::now_playing_profile::ConfigurationProjection projection;
    // Each donor supplies a complete replacement. The last successful profile
    // wins; only explicit host arguments are folded over its entry batch.
    for (auto path : g_commandLine.nowPlayingImports) {
        if (PathIsRelativeW(path.c_str())) path = aip::GetDirectoryName(g_iniPath) + L"\\" + path;
        if (!ReadContentNowPlayingImport(aip::MakeAbsolutePath(path), plan, projection, error)) return false;
    }
    const auto explicitSettings = g_commandLine.settings;
    g_commandLine.settings.clear();
    for (const auto& setting : projection.settings) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    for (const auto& setting : explicitSettings) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    g_commandLine.nowPlayingImportPlan = std::move(plan);
    g_commandLine.nowPlayingImportDiagnostics = std::move(projection.diagnostics);
    return true;
}

static bool ReadContentHardwareImport(const std::wstring& path, bool asus, std::vector<aip::IniSetting>& changes, std::wstring& error)
{
    std::vector<aip::IniSetting> next;
    if (aip::ToLower(aip::MakeAbsolutePath(path)) == aip::ToLower(aip::MakeAbsolutePath(g_iniPath))) {
        error = L"The import source must differ from the target INI."; return false;
    }
    aip::hardware_profile::Settings mapped;
    if (asus) {
        aip::hardware_profile::AsusProfile imported;
        if (!aip::hardware_profile::ReadAsusProfile(path, imported, error)) return false;
        mapped = aip::hardware_profile::ToHostSettings(imported);
        next.push_back({L"AsusBlink", L"ProfilePath", L""}); // explicit switch to imported inline fields
    } else {
        aip::hardware_profile::CapsProfile imported;
        if (!aip::hardware_profile::ReadCapsProfile(path, imported, error)) return false;
        mapped = aip::hardware_profile::ToHostSettings(imported);
    }
    for (const auto& setting : mapped) {
        const auto split = setting.first.find(L'.');
        next.push_back({setting.first.substr(0, split), setting.first.substr(split + 1), setting.second});
    }
    changes = std::move(next); return true;
}

static bool ExpandCommandLineHardwareImports(std::wstring& error)
{
    if (g_commandLine.hardwareImports.empty()) return true;
    std::vector<CommandLineIniSetting> imported;
    for (const auto& request : g_commandLine.hardwareImports) {
        auto path = request.path;
        if (TrimCopy(path).empty()) { error = L"A hardware donor INI path is required."; return false; }
        if (PathIsRelativeW(path.c_str())) path = aip::GetDirectoryName(g_iniPath) + L"\\" + path;
        std::vector<aip::IniSetting> changes;
        if (!ReadContentHardwareImport(aip::MakeAbsolutePath(path), request.asus, changes, error)) return false;
        if (request.asus) imported.erase(std::remove_if(imported.begin(), imported.end(),
            [](const auto& setting) { return IEquals(setting.section, L"AsusBlink"); }), imported.end());
        imported.insert(imported.end(), changes.begin(), changes.end());
    }
    const auto explicitSettings = g_commandLine.settings;
    g_commandLine.settings.clear();
    for (const auto& setting : imported) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    for (const auto& setting : explicitSettings) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    return true;
}

static bool ExpandCommandLineNotesImports(std::wstring& error)
{
    if (g_commandLine.notesImports.empty()) return true;
    std::vector<CommandLineIniSetting> imported;
    for (const auto& request : g_commandLine.notesImports) {
        auto path = request.path;
        if (PathIsRelativeW(path.c_str())) path = aip::GetDirectoryName(g_iniPath) + L"\\" + path;
        path = aip::MakeAbsolutePath(path);
        if (aip::ToLower(path) == aip::ToLower(aip::MakeAbsolutePath(g_iniPath))) { error = L"The import source must differ from the target INI."; return false; }
        std::wstring text;
        std::vector<CommandLineIniSetting> changes;
        if (!aip::notes::ReadImportFile(path, text, error) ||
            !(request.resource < 0 ? aip::notes::ImportAccountProfile(text, changes, error) : aip::notes::ImportCookieJson(request.resource, text, changes, error))) return false;
        imported.insert(imported.end(), changes.begin(), changes.end());
    }
    const auto explicitSettings = g_commandLine.settings;
    g_commandLine.settings.clear();
    for (const auto& setting : imported) AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    for (const auto& setting : explicitSettings) {
        const auto field = aip::notes::CredentialField(setting.key);
        const bool token = field == L"LToken" || field == L"LTokenProtected";
        const bool user = field == L"LTUid" || field == L"LTUidProtected";
        if (aip::content::Lower(setting.section).compare(0, 6, L"notes.") == 0 && (token || user)) {
            // Explicit credential choice replaces every plain/protected alias
            // in its family, matching tray edits and the resolver's precedence.
            // Canonicalize aliases so empty imported keys cannot mask them.
            for (const auto* key : {L"LToken", L"LTokenV2", L"ltoken_v2", L"LTokenProtected", L"LTokenV2Protected",
                L"LTUid", L"LTuidV2", L"ltuid_v2", L"LTUidProtected", L"LTuidV2Protected"})
                if ((aip::notes::CredentialField(key).compare(0, 6, L"LToken") == 0) == token)
                    AddCommandLineIniSetting(setting.section, key, L"");
            AddCommandLineIniSetting(setting.section, field, setting.value);
        } else AddCommandLineIniSetting(setting.section, setting.key, setting.value);
    }
    return true;
}


static std::wstring CommandLineVersionText()
{
    return DesktopStubVersionDisplayText();
}

static void ShowCommandLineMessage(const std::wstring& text, bool error)
{
    if (aip::WriteCommandLineText(text, error))
        return;
    if (g_commandLine.renderOnly || g_commandLine.configureOnly)
        return; // Offline rendering never opens an interactive message box.

    std::wstring title = error
        ? IniReadS(L"Strings", L"CommandLineErrorTitle", L"DesktopStub command line")
        : IniReadS(L"Strings", L"CommandLineTitle", L"DesktopStub");
    MessageBoxW(
        nullptr,
        text.c_str(),
        title.c_str(),
        MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

static bool CommandLineHasPersistentSettings()
{
    return !g_commandLine.settings.empty() || !g_commandLine.presetName.empty();
}

static bool CommandLineShouldExit()
{
    return g_commandLine.exitRequested;
}

static bool CommandLineShouldShowVersion()
{
    return g_commandLine.versionRequested;
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

static bool CommandLineRequestsLiveTileClearOnly()
{
    return g_commandLine.liveTileClearOnly;
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

static void LogCommandLineSummary();

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

static bool CommandLineSettingChangesManifest(const CommandLineIniSetting& entry)
{
    if (!IEquals(entry.section, L"Settings"))
        return false;

    if (ToLowerCopy(TrimCopy(entry.key)).rfind(L"manifest", 0) == 0)
        return true;

    if (IEquals(entry.key, L"Win8LiveTileBrokerApp") ||
        IEquals(entry.key, L"Win8LiveTileBackgroundTask") ||
        IEquals(entry.key, L"Win8LiveTileOopHelper"))
    {
        return true;
    }

    if (!IEquals(entry.key, L"AppxManifestTarget"))
        return false;

    ManifestCompatibilityTarget before = ParseManifestCompatibilityTargetValue(
        IniReadS(L"Settings", L"AppxManifestTarget", L"Windows10"));
    ManifestCompatibilityTarget after = ParseManifestCompatibilityTargetValue(entry.value, ManifestCompatibilityTarget::Windows10);

    return before != after;
}

static bool CommandLineSettingTargetsObsoleteManifestSection(const CommandLineIniSetting& entry)
{
    return IEquals(entry.section, L"Manifest");
}

static bool ApplyCommandLinePresetToDocument(std::wstring& text, std::wstring& error)
{
    if (g_commandLine.presetName.empty()) return true;
    if (!ValidateCommandLinePresetCombination(error)) return false;
    const auto document = aip::ParseIniDocument(text);
    std::wstring instanceList;
    if(aip::ReadIniValueFromDoc(document,L"Tiles",L"Instances",instanceList)){
        error=L"Legacy --preset does not change independent typed tiles. Use Tiles > Add tile and configure that tile's type instead.";return false;
    }
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    aip::content::Configuration current;
    const bool currentValid = aip::content::ReadConfiguration(read, current, error, ContentAsusEventsFromDocument(document));
    aip::content::PresetDefinition preset;
    if (g_commandLine.presetName == L"Wallpaper") preset = aip::content::WallpaperPreset();
    else if (g_commandLine.presetName == L"WallpaperRSS") {
        int items = 0;
        if (!aip::content::ParseInt(read(L"RssFeed", L"MaxItems", L"5"), 1, 20, items)) {
            error = L"RssFeed.MaxItems must be 1 through 20 before applying WallpaperRSS."; return false;
        }
        preset = aip::content::WallpaperRssPreset(items);
    } else if (g_commandLine.presetName == L"WallpaperSMTC") {
        const aip::content::Entry* wallpaper = nullptr;
        if (currentValid) {
            for (const auto& entry : current.entries)
                if (entry.enabled && aip::content::BackgroundIdFor(entry.background) == aip::content::BackgroundId::Wallpaper) { wallpaper = &entry; break; }
            if (!wallpaper) for (const auto& entry : current.entries)
                if (aip::content::BackgroundIdFor(entry.background) == aip::content::BackgroundId::Wallpaper) { wallpaper = &entry; break; }
        }
        preset = aip::content::WallpaperAndSmtcCycle(wallpaper);
    } else { error = L"--preset expects Wallpaper, WallpaperRSS, or WallpaperSMTC."; return false; }
    // Compare the effective layout, including explicit global overrides, so
    // repeating a preset with the same driver/text preferences cannot consume
    // another pair of preserved slots on every invocation.
    for (const auto& setting : g_commandLine.settings) if (IEquals(setting.section, L"Content")) {
        auto value = setting.value;
        if (!aip::content::NormalizeSetting(setting.section, setting.key, value, error)) return false;
        if (IEquals(setting.key, L"CycleDriver")) preset.cycleDriver = value == L"Auto" ? aip::content::CycleDriver::Auto :
            value == L"Windows" ? aip::content::CycleDriver::Windows : aip::content::CycleDriver::DesktopStub;
        else if (IEquals(setting.key, L"TextMode")) preset.textMode = value == L"Overlay" ? aip::content::TextMode::Overlay :
            value == L"Off" ? aip::content::TextMode::Off : aip::content::TextMode::Auto;
        else if (IEquals(setting.key, L"CycleEnabled")) aip::content::ParseBool(value, preset.cycle);
    }
    if (currentValid && aip::content::ActivePresetMatches(current, preset)) { error.clear(); return true; }
    int count = 0, highest = 0;
    if (!aip::content::ParseInt(read(L"Content", L"Count", L"1"), 1, aip::content::MaxEntries, count)) {
        error = L"Content.Count must be 1 through 32 before applying a preset."; return false;
    }
    for (const auto& section : document) if (aip::content::EntrySection(section.name)) {
        int index = 0; aip::content::ParseInt(section.name.substr(8), 1, aip::content::MaxEntries, index);
        highest = std::max(highest, index);
    }
    aip::content::PresetPlan plan;
    if (!aip::content::PlanAppendPreset(preset, count, highest, plan, error)) return false;
    auto candidate = text;
    for (const auto& setting : plan.settings)
        if (!WriteContentProfileSetting(candidate, {setting.section, setting.key, setting.value})) {
            error = L"The preset could not be represented in the selected INI."; return false;
        }
    text = std::move(candidate); error.clear(); return true;
}

static bool PrepareCommandLinePresetPreview(std::wstring& error)
{
    g_commandLine.presetPreviewLoaded = false;
    g_commandLine.presetPreviewDocument.clear();
    if (g_commandLine.presetName.empty()) return true;
    if (!ValidateCommandLinePresetCombination(error)) return false;
    DesktopStubIniMutationGuard previewLock(CurrentIniWriteLockWaitMs());
    if(!previewLock.IsLocked()){error=L"The preset target INI could not be locked for a read-only preview.";return false;}
    std::wstring text;
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    const bool exists=GetFileAttributesExW(g_iniPath.c_str(),GetFileExInfoStandard,&attributes)!=FALSE;
    if(!exists&&!IsMissingFileError(GetLastError())){error=L"The preset target INI could not be inspected.";return false;}
    if(exists&&(attributes.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)){error=L"The preset target INI is a directory.";return false;}
    // LoadIniText intentionally returns success with empty decoded text for a
    // missing file. Mirror the actual factory's BYTE-empty test here, before
    // any initialization write: a BOM-only existing file remains legacy.
    if(!exists||(!attributes.nFileSizeHigh&&!attributes.nFileSizeLow))text=BuildInitialIniTemplate();
    else if(!ReadIniFileForMutation(text)){error=L"The preset target INI could not be read.";return false;}
    if (!ApplyCommandLinePresetToDocument(text, error)) return false;
    g_commandLine.presetPreviewDocument = std::move(text);
    g_commandLine.presetPreviewLoaded = true;
    return true;
}

static std::wstring DesktopStubContentProfileValue(
    const wchar_t* section,
    const wchar_t* key,
    const wchar_t* fallback,
    bool includeCommandLineOverrides)
{
    // A full NowPlaying import removes all old entry sections during commit.
    // Preflight must use those same fresh-entry defaults, including optional
    // fields such as NotesDetail that are absent from the imported batch.
    std::wstring value = includeCommandLineOverrides && !g_commandLine.nowPlayingImports.empty() &&
        aip::content::Lower(section ? section : L"").compare(0, 8, L"content.") == 0
        ? std::wstring(fallback ? fallback : L"") : IniReadS(section, key, fallback);
    if (!includeCommandLineOverrides)
        return value;

    if (g_commandLine.presetPreviewLoaded) {
        const auto document = aip::ParseIniDocument(g_commandLine.presetPreviewDocument);
        std::wstring preview;
        value = aip::ReadIniValueFromDoc(document, section, key, preview) ? preview : std::wstring(fallback ? fallback : L"");
    }

    for (const auto& entry : g_commandLine.settings)
    {
        if (IEquals(entry.section, section ? section : L"") &&
            IEquals(entry.key, key ? key : L""))
        {
            value = entry.value;
        }
    }
    return value;
}

static bool ValidateDesktopStubContentProfileValues(
    const std::wstring& contentSourceValue,
    const std::wstring& feedUrlValue,
    const std::wstring& userAgentValue,
    const std::wstring& updateIntervalValue,
    const std::wstring& maxItemsValue,
    const std::wstring& httpTimeoutValue,
    const std::wstring& maxFeedBytesValue,
    std::wstring& error)
{
    ContentSource source = ParseContentSourceValue(
        contentSourceValue,
        static_cast<ContentSource>(-1));
    if (!IsValidContentSource(source))
    {
        error = L"[Settings] ContentSource expects Wallpaper or RssFeed.";
        return false;
    }

    std::wstring normalizedFeedUrl;
    if (!ValidateRssFeedUrlSetting(
            feedUrlValue,
            source != ContentSource::RssFeed,
            normalizedFeedUrl,
            error))
    {
        return false;
    }

    std::wstring normalizedUserAgent;
    if (!ValidateRssFeedUserAgentSetting(
            userAgentValue,
            normalizedUserAgent,
            error))
    {
        return false;
    }

    struct IntegerValue
    {
        const wchar_t* key;
        std::wstring value;
    };
    IntegerValue integerValues[] = {
        {L"UpdateIntervalSeconds", updateIntervalValue},
        {L"MaxItems", maxItemsValue},
        {L"HttpTimeoutSeconds", httpTimeoutValue},
        {L"MaxFeedBytes", maxFeedBytesValue},
    };
    for (auto& entry : integerValues)
    {
        if (!NormalizeDesktopStubTypedSetting(
                L"RssFeed",
                entry.key,
                entry.value,
                error))
        {
            return false;
        }
    }
    return true;
}

template<class Reader> static bool ValidateTileInstanceHostConfiguration(Reader read,
    aip::content::TileInstanceConfiguration& cfg, std::wstring& error)
{
    aip::content::TileInstanceConfiguration candidate;
    if (!aip::content::ReadTileInstanceConfiguration(read, candidate, error)) return false;
    for (const auto& tile : candidate.tiles) if (tile.type == aip::content::TileInstanceType::Rss) {
        const auto field = [&](const wchar_t* key, const wchar_t* fallback) {
            return aip::content::TileInstanceRead(tile, read, L"RssFeed", key, fallback);
        };
        if (!ValidateDesktopStubContentProfileValues(L"Wallpaper", field(L"FeedUrl", L""),
            field(L"UserAgent", L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)"),
            field(L"UpdateIntervalSeconds", L"900"), field(L"MaxItems", L"5"),
            field(L"HttpTimeoutSeconds", L"20"), field(L"MaxFeedBytes", L"2097152"), error)) {
            error = aip::content::TileInstanceSection(tile.id) + L": " + error; return false;
        }
    }
    cfg = std::move(candidate); error.clear(); return true;
}
template<class Reader> static bool ValidateTileInstanceProfileIfConfigured(Reader read, bool& configured, std::wstring& error)
{
    aip::content::TileInstanceConfiguration tiles;
    if (!ValidateTileInstanceHostConfiguration(read, tiles, error)) return false;
    configured = tiles.configured;
    if (!configured) return true;
    aip::gallery::Configuration gallery;
    return aip::gallery::ReadConfiguration(read, gallery, error);
}
static bool ValidateDesktopStubProfileDocument(const std::wstring& text, std::wstring& error)
{
    const auto document = aip::ParseIniDocument(text);
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    for (const auto& setting : g_displaySettings) {
        auto value = read(setting.section, setting.key, setting.fallback);
        if (!NormalizeDesktopStubDisplaySettingValue(setting, value, error)) return false;
    }
    if (!aip::notes::ValidateNotesSignInConfiguration(read, error)) return false;
    bool instanceMode = false;
    if (!ValidateTileInstanceProfileIfConfigured(read, instanceMode, error)) return false;
    if (instanceMode) return true;
    aip::content::Configuration content;
    aip::content::SecondaryTileConfiguration tiles;
    aip::content::TileAppearance actions;
    aip::gallery::Configuration gallery;
    if (!aip::content::ReadConfiguration(read, content, error, ContentAsusEventsFromDocument(document)) ||
        !aip::content::ReadSecondaryTileConfiguration(read, content.entries.size(), tiles, error) ||
        !aip::content::ReadTileAppearance(read, L"MediaActions", actions, error) ||
        !aip::gallery::ReadConfiguration(read, gallery, error)) return false;
    const bool rss = content.enabled && std::any_of(content.entries.begin(), content.entries.end(), [](const auto& entry) {
        return entry.enabled && aip::content::EntryUsesSource(entry, aip::content::SourceId::RssFeed);
    });
    return ValidateDesktopStubContentProfileValues(rss && !tiles.enabled ? L"RssFeed" : content.enabled ? L"Wallpaper" : read(L"Settings", L"ContentSource", L"Wallpaper"),
        read(L"RssFeed", L"FeedUrl", L""), read(L"RssFeed", L"UserAgent", L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)"),
        read(L"RssFeed", L"UpdateIntervalSeconds", L"900"), read(L"RssFeed", L"MaxItems", L"5"),
        read(L"RssFeed", L"HttpTimeoutSeconds", L"20"), read(L"RssFeed", L"MaxFeedBytes", L"2097152"), error);
}

static bool ValidateCommandLinePresetDocument(const std::wstring& text, std::wstring& error)
{
    const auto document = aip::ParseIniDocument(text);
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value; return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    bool instanceMode = false;
    if (!ValidateTileInstanceProfileIfConfigured(read, instanceMode, error)) return false;
    if (instanceMode) return true;
    // This callback also validates an ordinary CLI mutation under the INI
    // lock. Preserve action-appearance parity even without a preset request.
    aip::content::TileAppearance actionAppearance;
    if (!aip::content::ReadTileAppearance(read, L"MediaActions", actionAppearance, error)) return false;
    aip::gallery::Configuration gallery;
    if (!aip::gallery::ReadConfiguration(read, gallery, error)) return false;
    if (g_commandLine.presetName.empty()) return true;
    for (const auto& setting : g_displaySettings) {
        auto value = read(setting.section, setting.key, setting.fallback);
        if (!NormalizeDesktopStubDisplaySettingValue(setting, value, error)) return false;
    }
    aip::content::Configuration configuration;
    if (!aip::content::ReadConfiguration(read, configuration, error, ContentAsusEventsFromDocument(document))) return false;
    const bool rss = std::any_of(configuration.entries.begin(), configuration.entries.end(), [&](const auto& entry) {
        return configuration.enabled && entry.enabled && aip::content::EntryUsesSource(entry, aip::content::SourceId::RssFeed);
    });
    return ValidateDesktopStubContentProfileValues(rss ? L"RssFeed" : configuration.enabled ? L"Wallpaper" : read(L"Settings", L"ContentSource", L"Wallpaper"),
        read(L"RssFeed", L"FeedUrl", L""), read(L"RssFeed", L"UserAgent", L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)"),
        read(L"RssFeed", L"UpdateIntervalSeconds", L"900"), read(L"RssFeed", L"MaxItems", L"5"),
        read(L"RssFeed", L"HttpTimeoutSeconds", L"20"), read(L"RssFeed", L"MaxFeedBytes", L"2097152"), error);
}

static bool ValidateDesktopStubDisplayProfile(bool includeCommandLineOverrides, std::wstring& error)
{
    for (const auto& setting : g_displaySettings) {
        auto value = DesktopStubContentProfileValue(setting.section, setting.key, setting.fallback, includeCommandLineOverrides);
        if (!NormalizeDesktopStubDisplaySettingValue(setting, value, error)) return false;
    }
    error.clear();
    return true;
}

static void PrepareCommandLineNotesServerChanges()
{
    for (int resource = 0; resource < 3; ++resource) {
        const auto section = aip::notes::HostAccountSection(resource);
        bool uidChanged = false, serverSpecified = false;
        for (const auto& setting : g_commandLine.settings) {
            if (!IEquals(setting.section, section)) continue;
            if (IEquals(setting.key, L"UID")) uidChanged = setting.value != IniReadS(section.c_str(), L"UID", L"");
            if (IEquals(setting.key, L"Server")) serverSpecified = true;
        }
        // An old explicitly discovered region must not follow a replacement UID.
        // Keep a server supplied anywhere in the same command's atomic batch.
        if (uidChanged && !serverSpecified) AddCommandLineIniSetting(section, L"Server", L"");
    }
}
static bool ValidateDesktopStubContentProfile(
    bool includeCommandLineOverrides,
    std::wstring& error)
{
    if (includeCommandLineOverrides && (!g_commandLine.presetName.empty() || !g_commandLine.nowPlayingImports.empty())) {
        const wchar_t* absent = L"\x1f AIP instance list absent during composition preflight \x1f";
        if (DesktopStubContentProfileValue(L"Tiles", L"Instances", absent, true) != absent) {
            error = L"Legacy --preset and NowPlaying profile imports change global numbered contents and cannot configure independent tiles. "
                L"Use Tiles > Add tile to add a tile of the desired type, or configure an existing Tile.<id> with --set.";
            return false;
        }
    }
    if (includeCommandLineOverrides) PrepareCommandLineNotesServerChanges();
    if (includeCommandLineOverrides && !PrepareCommandLinePresetPreview(error)) return false;
    if (!ValidateDesktopStubDisplayProfile(includeCommandLineOverrides, error)) return false;
    bool instanceMode = false;
    if (!ValidateTileInstanceProfileIfConfigured([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, instanceMode, error)) return false;
    if (instanceMode) return true;
    if (!aip::notes::ValidateNotesSignInConfiguration([includeCommandLineOverrides](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, error)) return false;
    aip::content::Configuration composition;
    if (!aip::content::ReadConfiguration([includeCommandLineOverrides](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, composition, error, ReadContentAsusEvents(includeCommandLineOverrides))) return false;
    aip::content::SecondaryTileConfiguration separate;
    if (!aip::content::ReadSecondaryTileConfiguration([includeCommandLineOverrides](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, composition.entries.size(), separate, error)) return false;
    aip::content::TileAppearance actionAppearance;
    if (!aip::content::ReadTileAppearance([includeCommandLineOverrides](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, L"MediaActions", actionAppearance, error)) return false;
    aip::gallery::Configuration gallery;
    if (!aip::gallery::ReadConfiguration([includeCommandLineOverrides](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        return DesktopStubContentProfileValue(section, key, fallback, includeCommandLineOverrides);
    }, gallery, error)) return false;

    bool usesRss = false;
    for (const auto& entry : composition.entries)
        if (composition.enabled && entry.enabled &&
            aip::content::EntryUsesSource(entry, aip::content::SourceId::RssFeed)) usesRss = true;
    return ValidateDesktopStubContentProfileValues(
        usesRss && !separate.enabled ? L"RssFeed" : composition.enabled ? L"Wallpaper" : DesktopStubContentProfileValue(
            L"Settings",
            L"ContentSource",
            L"Wallpaper",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"FeedUrl",
            L"",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"UserAgent",
            L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"UpdateIntervalSeconds",
            L"900",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"MaxItems",
            L"5",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"HttpTimeoutSeconds",
            L"20",
            includeCommandLineOverrides),
        DesktopStubContentProfileValue(
            L"RssFeed",
            L"MaxFeedBytes",
            L"2097152",
            includeCommandLineOverrides),
        error);
}

static bool ValidateConfiguredRssSettingsForContentSource(
    ContentSource source,
    std::wstring& error)
{
    if (source == ContentSource::Wallpaper)
        return true;

    return ValidateDesktopStubContentProfileValues(
        ContentSourceIniValue(source),
        IniReadS(L"RssFeed", L"FeedUrl", L""),
        IniReadS(
            L"RssFeed",
            L"UserAgent",
            L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)"),
        IniReadS(L"RssFeed", L"UpdateIntervalSeconds", L"900"),
        IniReadS(L"RssFeed", L"MaxItems", L"5"),
        IniReadS(L"RssFeed", L"HttpTimeoutSeconds", L"20"),
        IniReadS(L"RssFeed", L"MaxFeedBytes", L"2097152"),
        error);
}

static bool FindDesktopStubStartupPreference(
    bool& found,
    bool& desired,
    std::wstring& error)
{
    found = false;
    desired = false;
    for (const auto& entry : g_commandLine.settings)
    {
        if (!IsDesktopStubStartupSetting(entry.section, entry.key))
            continue;

        if (!TryParseCommandLineBoolValue(entry.value, desired))
        {
            error = L"Settings.RunAtStartup expects a true/false value.";
            return false;
        }
        found = true;
    }
    return true;
}

static bool ApplyCommandLinePersistentSettings()
{
    g_commandLine.applyError.clear();
    g_commandLine.liveTileModeChanged = false;
    g_commandLine.trayIconChanged = false;
    if (!ValidateDesktopStubContentProfile(true, g_commandLine.applyError))
    {
        g_commandLine.applyError =
            L"Invalid content-source configuration: " +
            g_commandLine.applyError;
        return false;
    }
    if (g_commandLine.settings.empty() && g_commandLine.presetName.empty())
        return true;

    std::wstring packagedStartupTarget = IniReadS(L"Settings", L"AppxManifestTarget", L"Windows10");
    for (const auto& entry : g_commandLine.settings)
        if (IEquals(entry.section, L"Settings") && IEquals(entry.key, L"AppxManifestTarget")) packagedStartupTarget = entry.value;
    for (const auto& entry : g_commandLine.settings)
    {
        if (!IEquals(entry.section, L"Settings") || !IEquals(entry.key, L"RunAtStartupPackaged")) continue;
        bool desired = false;
        if (!TryParseCommandLineBoolValue(entry.value, desired) ||
            !ValidateDesktopStubPackagedStartupPreference(desired, g_commandLine.applyError, packagedStartupTarget.c_str())) return false;
    }

    bool startupExplicit = false;
    bool startupDesired = false;
    if (!FindDesktopStubStartupPreference(
            startupExplicit,
            startupDesired,
            g_commandLine.applyError))
    {
        return false;
    }

    bool changesLiveTileMode = false;
    bool changesTrayIcon = false;
    bool changesManifest = false;
    for (const auto& entry : g_commandLine.settings)
    {
        if (CommandLineSettingTargetsObsoleteManifestSection(entry))
        {
            std::wstring settingName = entry.section + L"." + entry.key;
            CommandLineStringFormat1(
                L"CommandLineManifestSectionIgnored",
                L"Manifest INI section is no longer supported: %s. Use Settings.Manifest* keys or edit AppxManifest.xml directly.",
                settingName,
                g_commandLine.applyError);
            return false;
        }

        changesLiveTileMode = changesLiveTileMode ||
            CommandLineSettingChangesLiveTileMode(entry);
        changesTrayIcon = changesTrayIcon ||
            CommandLineSettingChangesTrayIcon(entry);
        changesManifest = changesManifest ||
            CommandLineSettingChangesManifest(entry);
    }

    auto mutateSettings = [&](std::wstring& text)
    {
        if (!ApplyCommandLinePresetToDocument(text, g_commandLine.applyError)) return false;
        for (const auto& entry : g_commandLine.settings)
        {
            const wchar_t* value = entry.value.c_str();
            if (IsDesktopStubStartupSetting(entry.section, entry.key))
                value = startupDesired ? L"1" : L"0";
            if (!WriteContentProfileSetting(text, {entry.section, entry.key, value},&g_commandLine.applyError))
            {
                return false;
            }
        }
        if (!StageExplicitSourceTileNameIntents(text, g_commandLine.settings, g_commandLine.applyError)) return false;
        if (std::any_of(g_commandLine.settings.begin(), g_commandLine.settings.end(), [](const auto& setting) {
            return IEquals(setting.section, L"AsusBlink") || IEquals(setting.section, L"CapsBlink"); })) {
            const auto document = aip::ParseIniDocument(text);
            aip::content::Configuration prospective; std::wstring issue;
            if (!aip::content::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
                std::wstring read; return aip::ReadIniValueFromDoc(document, section, key, read) ? read : std::wstring(fallback);
            }, prospective, issue, ContentAsusEventsFromDocument(document))) return false;
        }
        return ValidateCommandLinePresetDocument(text, g_commandLine.applyError);
    };

    std::wstring startupTransactionError;
    bool saved = startupExplicit
        ? CommitDesktopStubStartupIniState(
            startupDesired,
            mutateSettings,
            startupTransactionError)
        : MutateDesktopStubIniWithWait(
            mutateSettings,
            CurrentIniWriteLockWaitMs());
    if (!saved)
    {
        DWORD saveError = GetLastError();
        if (startupExplicit && !startupTransactionError.empty())
        {
            g_commandLine.applyError =
                L"Failed to commit command-line settings with this profile's "
                L"shell:startup shortcut: " + startupTransactionError;
            return false;
        }
        if (!g_commandLine.applyError.empty()) return false;
        if (saveError == ERROR_SUCCESS)
            saveError = ERROR_WRITE_FAULT;
        g_commandLine.applyError = L"Failed to save the command-line setting batch (error " +
            std::to_wstring(saveError) + L": " +
            aip::GetLastErrorText(saveError) + L").";
        return false;
    }

    g_commandLine.liveTileModeChanged = changesLiveTileMode;
    g_commandLine.trayIconChanged = changesTrayIcon;
    if (changesManifest)
        g_commandLine.regenerateManifest = true;
    return true;
}

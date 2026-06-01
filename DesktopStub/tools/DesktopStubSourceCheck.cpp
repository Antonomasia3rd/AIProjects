#include <cctype>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <vector>

struct Check
{
    std::string name;
    std::string sourceName;
    std::string sourceText;
    std::string pattern;
    std::string failure;
    bool absent;
};

static int g_checkCount = 0;

static std::string ReadSource(const std::string& relativePath)
{
    std::ifstream in(relativePath, std::ios::binary);
    if (!in)
        throw std::runtime_error("DesktopStub source regression: missing source file '" + relativePath + "'");

    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return text;
}

static std::string JoinSource(const std::vector<std::string>& relativePaths)
{
    std::string joined;
    for (const auto& path : relativePaths)
    {
        if (!joined.empty())
            joined.push_back('\n');
        joined += ReadSource(path);
    }
    return joined;
}

static std::string NormalizeSource(const std::string& text)
{
    std::string normalized;
    normalized.reserve(text.size());
    bool lastWasSpace = false;
    for (unsigned char ch : text)
    {
        if (std::isspace(ch))
        {
            if (!lastWasSpace)
            {
                normalized.push_back(' ');
                lastWasSpace = true;
            }
            continue;
        }

        normalized.push_back(static_cast<char>(ch));
        lastWasSpace = false;
    }
    return normalized;
}

static std::string StripDotAllFlag(std::string pattern)
{
    const std::string flag = "(?s)";
    size_t pos = 0;
    while ((pos = pattern.find(flag, pos)) != std::string::npos)
        pattern.erase(pos, flag.size());
    return pattern;
}

static std::string SliceSource(
    const std::string& sourceText,
    const std::string& startMarker,
    const std::string& endMarker)
{
    size_t start = sourceText.find(startMarker);
    if (start == std::string::npos)
        return std::string();
    size_t end = sourceText.find(endMarker, start + startMarker.size());
    if (end == std::string::npos)
        return sourceText.substr(start);
    return sourceText.substr(start, end - start);
}

static bool RegexMatches(const std::string& sourceText, const std::string& pattern)
{
    std::regex re(StripDotAllFlag(pattern), std::regex_constants::ECMAScript);
    return std::regex_search(NormalizeSource(sourceText), re);
}

static void AssertCheck(const Check& check)
{
    ++g_checkCount;
    bool matched = RegexMatches(check.sourceText, check.pattern);
    if (check.absent ? matched : !matched)
    {
        std::cerr << "DesktopStub source regression: " << check.failure
            << " [" << check.name << " in " << check.sourceName << "]\n";
        throw std::runtime_error("source regression");
    }

    std::cout << "ok - " << check.name << "\n";
}


static void ReportManualCheckFailure(
    const std::string& name,
    const std::string& sourceName,
    const std::string& failure,
    const std::string& detail)
{
    std::cerr << "DesktopStub source regression: " << failure
        << " [" << name << " in " << sourceName << "]";
    if (!detail.empty())
        std::cerr << " missing/forbidden: " << detail;
    std::cerr << "\n";
    throw std::runtime_error("source regression");
}

static void AssertContainsAll(
    const std::string& name,
    const std::string& sourceName,
    const std::string& sourceText,
    const std::vector<std::string>& needles,
    const std::string& failure)
{
    ++g_checkCount;
    for (const auto& needle : needles)
    {
        if (sourceText.find(needle) == std::string::npos)
            ReportManualCheckFailure(name, sourceName, failure, needle);
    }

    std::cout << "ok - " << name << "\n";
}

static void AssertNotContainsAny(
    const std::string& name,
    const std::string& sourceName,
    const std::string& sourceText,
    const std::vector<std::string>& needles,
    const std::string& failure)
{
    ++g_checkCount;
    for (const auto& needle : needles)
    {
        if (sourceText.find(needle) != std::string::npos)
            ReportManualCheckFailure(name, sourceName, failure, needle);
    }

    std::cout << "ok - " << name << "\n";
}

static std::string UiPropertyName(const std::string& key)
{
    if (key.rfind("OK", 0) == 0)
        return "ok" + key.substr(2);
    if (key.rfind("PS", 0) == 0)
        return "ps" + key.substr(2);

    std::string property = key;
    if (!property.empty())
        property[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(property[0])));
    return property;
}

static bool DefaultStringNeedsFormatValidation(const std::string& defaults, const std::string& key)
{
    std::string marker = "{L\"" + key + "\",";
    size_t start = defaults.find(marker);
    if (start == std::string::npos)
        return false;

    size_t valueStart = defaults.find("L\"", start + marker.size());
    if (valueStart == std::string::npos)
        return false;
    valueStart += 2;

    bool escaped = false;
    for (size_t i = valueStart; i < defaults.size(); ++i)
    {
        char ch = defaults[i];
        if (escaped)
        {
            escaped = false;
            continue;
        }
        if (ch == '\\')
        {
            escaped = true;
            continue;
        }
        if (ch == '"')
            return false;
        if (ch == '%' && i + 1 < defaults.size() &&
            std::isalnum(static_cast<unsigned char>(defaults[i + 1])))
            return true;
    }
    return false;
}

static void AssertUiStringWired(const std::string& key, const std::string& defaults, const std::string& uiSources)
{
    std::string property = UiPropertyName(key);
    Check defaultCheck{
        "UI string default exists: " + key,
        "src\\ga_config_defaults.inc",
        defaults,
        key,
        "default string missing for " + key,
        false
    };
    AssertCheck(defaultCheck);

    Check loadCheck{
        "UI string is loaded: " + key,
        "UI/string sources",
        uiSources,
        "g_ui\\." + property,
        "UI field load missing for " + key,
        false
    };
    AssertCheck(loadCheck);

    if (DefaultStringNeedsFormatValidation(defaults, key))
    {
        Check formatCheck{
            "UI format is validated: " + key,
            "UI/string sources",
            uiSources,
            "RequireFormat\\(g_ui\\." + property,
            "format validation missing for " + key,
            false
        };
        AssertCheck(formatCheck);
    }
}

static std::vector<std::string> ExtractUiStringDefaultKeys(const std::string& defaults)
{
    std::string slice = SliceSource(
        defaults,
        "static const StringDefault g_stringDefaults[]",
        "};");
    if (slice.empty())
        throw std::runtime_error("DesktopStub source regression: g_stringDefaults block not found");

    std::vector<std::string> keys;
    std::regex re(R"rx(\{L"([^"]+)",\s*L")rx");
    for (std::sregex_iterator it(slice.begin(), slice.end(), re), end; it != end; ++it)
        keys.push_back((*it)[1].str());

    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    if (keys.empty())
        throw std::runtime_error("DesktopStub source regression: no UI string defaults found");
    return keys;
}

int main(int argc, char** argv)
{
    bool listChecks = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--list" || arg == "/list" || arg == "-l")
            listChecks = true;
        else
        {
            std::cerr << "usage: DesktopStubSourceCheck.exe [--list]\n";
            return 2;
        }
    }

    try
    {
        const std::string generation = ReadSource("src\\ga_generation.inc");
        const std::string app = ReadSource("src\\ga_app.inc");
        const std::string desktopStub = ReadSource("DesktopStub.cpp");
        const std::string buildScript = ReadSource("BuildDesktopStub.cmd");
        const std::string image = ReadSource("src\\ga_image.inc");
        const std::string desktopIcon = ReadSource("src\\ga_desktop_icon_png.inc");
        const std::string registration = ReadSource("src\\ga_registration.inc");
        const std::string liveTile = ReadSource("src\\ga_live_tile.inc");
        const std::string manifest = ReadSource("src\\ga_manifest.inc");
        const std::string version = ReadSource("src\\ga_version.inc");
        const std::string versionResource = ReadSource("src\\DesktopStubVersionResource.rc.inc");
        const std::string desktopStubResource = ReadSource("DesktopStub.rc");
        const std::string brokerResource = ReadSource("LiveTileBroker.rc");
        const std::string tray = ReadSource("src\\ga_tray.inc");
        const std::string runtime = ReadSource("src\\ga_runtime_helpers.inc");
        const std::string commandLine = ReadSource("src\\ga_command_line.inc");
        const std::string defaults = ReadSource("src\\ga_config_defaults.inc");
        const std::string wallpaper = ReadSource("src\\ga_wallpaper.inc");
        const std::string loggingCore = ReadSource("src\\ga_logging_core.inc");
        const std::string brokerApp = ReadSource("src\\ga_livetile_broker_app.inc");
        const std::string backgroundTask = ReadSource("src\\ga_livetile_background_task_dll.inc");
        const std::string gitAttributes = ReadSource("..\\.gitattributes");
        const std::string exportSourceOnly = ReadSource("..\\tools\\ExportSourceOnly.cmd");
        const std::string repoTools = ReadSource("..\\.github\\tools\\RepoTools.cs");
        const std::string buildWorkflow = ReadSource("..\\.github\\workflows\\build-windows.yml");
        const std::string liveTileUpdateBare = SliceSource(
            liveTile,
            "static bool Appx_Update_LiveTileXmlBare",
            "static bool Appx_Clear_LiveTileBare");
        const std::string uiSources = JoinSource({
            "src\\ga_ui_logging.inc",
            "src\\ga_ui_state.inc",
            "src\\ga_logging_core.inc",
            "src\\ga_manifest.inc",
            "src\\ga_ui_strings.inc",
            "src\\ga_runtime_helpers.inc"
        });

        std::vector<Check> checks;

        checks.push_back({"Build script ignores legacy target arguments", "BuildDesktopStub.cmd", buildScript, R"rx(Build policy:.*ignores every argument.*%DESKTOPSTUB_BROKER_EXE_NAME%.*if not "%~1"=="".*One or more arguments were supplied and ignored)rx", "BuildDesktopStub.cmd must accept but ignore old target arguments so every invocation builds the same outputs", false});
        checks.push_back({"Build script always builds host and broker", "BuildDesktopStub.cmd", buildScript, R"rx(echo Building packaged Live Tile broker\.\.\..*LiveTileBroker\.cpp.*echo Building main DesktopStub host\.\.\..*DesktopStub\.cpp)rx", "BuildDesktopStub.cmd must always build both DesktopStub.exe and DesktopStubLiveTileBroker.exe", false});
        checks.push_back({"Build script derives DesktopStub version from configurable CI release tags", "BuildDesktopStub.cmd", buildScript, R"rx(ResolveDesktopStubVersion.*DESKTOPSTUB_VERSION.*DESKTOPSTUB_RELEASE_TAG_PREFIX.*git tag --points-at HEAD --list "%DESKTOPSTUB_RELEASE_TAG_PREFIX%\*".*git tag --list "%DESKTOPSTUB_RELEASE_TAG_PREFIX%\*".*%DESKTOPSTUB_RELEASE_TAG_PREFIX%%NEXT_DESKTOPSTUB_TAG_NUMBER%)rx", "BuildDesktopStub.cmd must derive the default DesktopStub version from a configurable vN release tag family while allowing explicit overrides", false});
        checks.push_back({"Build script embeds DesktopStub version resources", "BuildDesktopStub.cmd", buildScript, R"rx(RC_BROKER_NAME_DEFINES.*LiveTileBroker\.rc.*LiveTileBroker\.cpp.*RC_HOST_NAME_DEFINES.*DesktopStub\.rc.*DesktopStub\.cpp)rx", "DesktopStub executables must compile version resources while allowing output filenames to be parameterized", false});
        checks.push_back({"CI build checkout fetches release tags for DesktopStub versioning", "..\\.github\\workflows\\build-windows.yml", buildWorkflow, R"rx(build-windows:.*Check out repository.*actions/checkout@.*fetch-depth:\s*0)rx", "the CI build job must fetch tags so DesktopStub can embed the same DesktopStub-vN version that release publishing will use", false});
        checks.push_back({"DesktopStub version constants are shared by runtime and manifest", "version/runtime sources", desktopStub + "\n" + version + "\n" + manifest, R"rx(ga_version\.inc.*DesktopStubPackageVersion.*DESKTOPSTUB_VERSION_TEXT_W.*DesktopStubVersionDisplayText.*EffectiveManifestPackageVersion.*DesktopStubPackageVersion)rx", "runtime diagnostics and generated AppxManifest.xml must use the same build version constants", false});
        checks.push_back({"Win32 version resources carry the DesktopStub build version", "resource scripts", versionResource + "\n" + desktopStubResource + "\n" + brokerResource, R"rx(VERSIONINFO.*FILEVERSION DESKTOPSTUB_VERSION_COMMA.*PRODUCTVERSION DESKTOPSTUB_VERSION_COMMA.*FileVersion.*DESKTOPSTUB_VERSION_TEXT.*ProductVersion.*DESKTOPSTUB_VERSION_TEXT.*DesktopStub\.exe.*DesktopStubLiveTileBroker\.exe)rx", "DesktopStub.exe and DesktopStubLiveTileBroker.exe must have FileVersion/ProductVersion resources", false});
        checks.push_back({"Existing manifests are updated to the current package version", "src\\ga_manifest.inc", manifest, R"rx(ReplaceXmlAttributeInFirstTagByLocalName.*expectedPackageVersion = EffectiveManifestPackageVersion\(\).*packageVersionMismatch.*ExtractXmlAttributeFromTag\(xml,\s*L"<Identity ",\s*L"Version".*ReplaceXmlAttributeInFirstTagByLocalName\(updatedXml,\s*L"Identity",\s*L"Version",\s*expectedPackageVersion\).*appxManifestVersionUpdated)rx", "normal startup must keep AppxManifest.xml Identity Version aligned with the DesktopStub build version without resetting custom identity names", false});
        checks.push_back({"DesktopStub version is visible in CLI, tray, and startup logs", "version UI sources", commandLine + "\n" + tray + "\n" + app + "\n" + defaults, R"rx(--version.*CommandLineVersionText.*CommandLineShouldShowVersion.*versionLabel.*DesktopStubVersionDisplayText.*desktopStubVersionSummary.*DesktopStubVersionDisplayText.*DesktopStubVersionSummary)rx", "DesktopStub version must be visible from --version, the tray menu, and startup diagnostics", false});
        checks.push_back({"DesktopStub smoke verifies binary and manifest versions", "..\\.github\\tools\\RepoTools.cs", repoTools, R"rx(VerifyDesktopStubBinaryVersion\(sourceExe.*DesktopStubLiveTileBroker\.exe.*helper version does not match.*--version.*VerifyDesktopStubManifestVersion\(manifestAfterSmoke,\s*desktopStubVersion\))rx", "DesktopStub smoke tests must verify Win32 file/product versions, broker version parity, --version, and AppxManifest.xml version parity", false});
        checks.push_back({"Startup skip validates manifest-resolved assets", "src\\ga_generation.inc", generation, R"rx(static bool StartupGeneratedAssetsPresent.*CurrentManifestDisplayInfo\(\).*ManifestAssetPathForTile\(manifestInfo,\s*t\).*ResolveManifestAssetPath\(exeDir,\s*manifestAssetPath\).*if \(!IsValidGeneratedPngWithDimensions\(basePath,\s*t\.w,\s*t\.h\)\)\s*return false;)rx", "startup skip validation must use manifest-resolved asset paths and require the manifest base asset", false});
        checks.push_back({"COM registration logs async and deployment details", "src\\ga_registration.inc", registration, R"rx(static void LogComRegistrationOperationFailure.*op\.ErrorCode\(\)\.value.*LogComRegistrationDeploymentResult\(op\.GetResults\(\)\))rx", "COM registration failures must log async HRESULT and deployment-result details", false});
        checks.push_back({"COM registration handles AsyncStatus::Error", "src\\ga_registration.inc", registration, R"rx(AsyncStatus::Error)rx", "COM registration must handle failed async status before op.get() can hide status diagnostics", false});
        checks.push_back({"Force shutdown records skipped cleanup first", "src\\ga_generation.inc", generation, R"rx(static void ForceShutdownNow\(\).*RecordForceShutdownPendingCleanup\(\).*ExitProcess\(0\))rx", "force shutdown must persist skipped-cleanup details before bypassing normal shutdown", false});
        checks.push_back({"Startup warns after previous forced shutdown", "src\\ga_app.inc", app, R"rx(ConsumePreviousForceShutdownCleanupWarning\(previousForceShutdownCleanupWarning\).*QueueStartupWarning\(previousForceShutdownCleanupWarning\).*MessageBoxW\()rx", "startup must warn the user when a previous force shutdown skipped cleanup", false});
        checks.push_back({"Live Tile update mode defaults auto", "src\\ga_config_defaults.inc", defaults, R"rx(\{L"Settings",\s*L"ExperimentalLiveTileUpdate",\s*L"Auto"\})rx", "Live Tile update mode must default to Auto so Start tile launches can use package identity while direct launches use registration", false});
        checks.push_back({"Live Tile Auto mode chooses by package identity", "src\\ga_runtime_helpers.inc", runtime, R"rx(ConfiguredLiveTileUpdateMode.*EffectiveLiveTileUpdateMode.*CurrentProcessHasPackageIdentity\(\).*LiveTileUpdateMode::LiveTile.*LiveTileUpdateMode::Registration)rx", "Auto Live Tile mode must choose Live Tile updates only when the process has package identity", false});
        checks.push_back({"INI template does not expose obsolete Manifest section", "src\\ga_config_defaults.inc", defaults, R"rx(BuildInitialIniTemplate(?!.*lines\.push_back\(L"\[Manifest\]"\)))rx", "INI template must not recreate the obsolete [Manifest] editor section", false});
        checks.push_back({"Manifest executable fallback uses Win10 host and Win8 broker", "src\\ga_manifest.inc", manifest, R"rx(EffectiveManifestExecutable.*ConfiguredManifestCompatibilityTarget\(\).*ManifestHostExecutableName\(\).*target\s*!=\s*ManifestCompatibilityTarget::Windows10.*Win8LiveTileBrokerAppEnabled\(\).*ManifestLiveTileBrokerExecutableName\(\).*ManifestAppxActivationStubExecutableName\(\).*ManifestSettingValidated\(L"Executable",\s*fallback\.c_str\(\))rx", "manifest executable fallback must keep Windows 10 on the current host EXE while Windows 8/8.1 targets default to the packaged broker helper", false});
        checks.push_back({"Manifest host executable follows renamed EXE", "src\\ga_manifest.inc", manifest, R"rx(PortableHostBaseName.*AppxStub.*ManifestHostExecutableName\(\).*PortableHostBaseName\(\) \+ L"\.exe".*ManifestAppxActivationStubExecutableName\(\).*PortableHostBaseName\(\) \+ L"AppxStub\.exe")rx", "renaming DesktopStub.exe to GenerateAssets.exe or DeskStub.exe must make the manifest point at the current host EXE and matching AppxStub copy", false});
        checks.push_back({"Existing manifest is regenerated after EXE rename", "src\\ga_manifest.inc", manifest, R"rx(EnsureAppxManifest.*existingExecutable.*expectedExecutable = EffectiveManifestExecutable\(\).*executableMismatch.*BuildDefaultAppxManifest\(\))rx", "renaming the EXE must repair an existing AppxManifest.xml that still points at the old executable name", false});
        checks.push_back({"Custom manifest identity is preserved", "src\\ga_manifest.inc", manifest, R"rx(ExistingManifestIdentityLooksGeneratedDefault.*identityNameMismatch.*ExistingManifestIdentityLooksGeneratedDefault\(existingIdentityName,\s*existingExecutable\).*preservedIdentity.*rebuiltManifest\.replace)rx", "manual AppxManifest.xml package identity edits must not be overwritten by normal startup regeneration", false});
        checks.push_back({"Manifest generation reads Settings manifest defaults", "src\\ga_manifest.inc", manifest, R"rx(ManifestSetting.*legacySettingsKey.*IniReadS\(L"Settings",\s*legacySettingsKey)rx", "generated AppxManifest.xml must be configurable through Settings.Manifest* defaults instead of hardwired project identity", false});
        checks.push_back({"Manifest project identity defaults are in Settings", "src\\ga_config_defaults.inc", defaults, R"rx(ManifestIdentityName.*ManifestPublisher.*ManifestDisplayName.*ManifestLiveTileBrokerExecutable)rx", "manifest project identity and broker defaults must be present as Settings.Manifest* INI defaults", false});
        checks.push_back({"Obsolete Manifest INI section is removed and blocked", "command-line/config sources", commandLine + "\n" + defaults, R"rx(CommandLineSettingTargetsObsoleteManifestSection.*IEquals\(entry\.section,\s*L"Manifest"\).*RemoveObsoleteManifestIniSettings.*IEquals\(sectionName,\s*L"Manifest"\))rx", "the obsolete [Manifest] INI section must be removed from existing INIs and blocked from command-line writes", false});
        checks.push_back({"Manifest executable path rejects traversal segments", "src\\ga_manifest.inc", manifest, R"rx(IsManifestSafeRelativePath.*segment == L"\.".*segment == L"\.\.".*IsManifestExecutableValue.*IsManifestSafeRelativePath)rx", "manifest executable validation must reject . and .. relative path segments", false});
        checks.push_back({"Live Tile update mode skips AppX registration path", "src\\ga_generation.inc", generation, R"rx(else\s+if\s*\(useLiveTileUpdateForThisRun\).*Appx_Update_Or_Request_LiveTile\(exeDir,\s*manifestInfo,\s*liveTileUpdateAssets,\s*appUpdateFailureMessage\).*else\s*\{.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\))rx", "Live Tile update mode must call the tile updater instead of re-registering the AppX manifest", false});
        checks.push_back({"Live Tile mode menu queues one-time re-registration", "src\\ga_tray.inc", tray, R"rx(ID_LIVE_TILE_MODE_AUTO.*ID_LIVE_TILE_MODE_REGISTRATION.*ID_LIVE_TILE_MODE_LIVE_TILE.*IniWrite\(L"Settings",\s*L"ExperimentalLiveTileUpdate".*QueueLiveTileModeReregistration\(\))rx", "changing the Live Tile mode menu must queue a one-time AppX re-registration", false});
        checks.push_back({"Command line exposes Live Tile mode overrides", "src\\ga_command_line.inc", commandLine, R"rx(--live-tile.*--no-live-tile.*--live-tile-auto.*--live-tile-mode.*ExperimentalLiveTileUpdate)rx", "command-line options must expose Auto, Registration, and Live Tile update mode overrides", false});
        checks.push_back({"Command line exposes one-shot manifest regeneration", "command-line/app sources", commandLine + "\n" + app, R"rx(--regenerate-manifest.*CommandLineShouldRegenerateManifest.*RegenerateAppxManifestFromConfig)rx", "manifest regeneration must be available as a one-shot command-line action", false});
        checks.push_back({"Command-line help is INI-backed", "command-line/config sources", commandLine + "\n" + defaults + "\n" + uiSources, R"rx(CommandLineUsageText.*DecodeCommandLineHelpEscapes\(IniReadS\( COMMAND_LINE_HELP_SECTION, COMMAND_LINE_HELP_TEMPLATE_KEY, g_commandLineHelpDefaults\[0\]\.value\)\).*ReplaceAllInPlace\(text, L"\{exe\}".*ReplaceAllInPlace\(text, L"\{iniExampleName\}".*COMMAND_LINE_HELP_SECTION.*g_commandLineHelpDefaults.*EnsureIniDefaults.*g_commandLineHelpDefaults)rx", "command-line help text must be configurable from [CommandLineHelp] Template and repaired into existing INIs", false});
        checks.push_back({"Command-line help prepares INI defaults", "src\\ga_app.inc", app, R"rx(if \(g_commandLine\.helpRequested\).*EnsureIniReadyForCommandLineSettings\(\).*ShowCommandLineMessage\(CommandLineUsageText\(\), false\))rx", "--help must prepare/read the effective INI before rendering the configurable help template", false});
        checks.push_back({"Manifest tray action is one-shot", "src\\ga_tray.inc", tray, R"rx(ID_MANIFEST_REGENERATE.*RegenerateAppxManifestFromConfigAndLog\(\).*QueueGenerate)rx", "tray manifest regeneration must execute once and queue generation instead of toggling a persistent INI setting", false});
        checks.push_back({"Effective INI uses separate single-instance scope", "src\\ga_app.inc", app, R"rx(ConfigureSingleInstanceIdentity\(const std::wstring& exeDir,\s*const std::wstring& iniPath,\s*bool customIniPath\).*Scope by the effective INI path.*!iniPath\.empty\(\)\s*\?\s*iniPath\s*:\s*exeDir.*ConfigureSingleInstanceIdentity\(dir,\s*g_iniPath,\s*g_commandLine\.customIniPath\))rx", "renamed EXE/default INI and alternate --ini runs must not signal each other unless they share the same INI", false});
        checks.push_back({"Command-line requests fail when existing instance cannot be signaled", "src\\ga_app.inc", app, R"rx(if \(!SignalExistingInstance\(singleInstanceRequest\)\).*\(singleInstanceRequest\s*&\s*SINGLE_INSTANCE_REQUEST_MASK\)\s*!=\s*SINGLE_INSTANCE_REQUEST_SHOW.*ShowCommandLineMessage\(.*CommandLineSignalExistingFailed.*true\).*return 2)rx", "command-line exit/generate/reload requests must not report success when the running instance cannot be signaled", false});
        checks.push_back({"Command-line reload only applies tray visibility when TrayIcon changed", "command-line/app sources", commandLine + "\n" + app, R"rx(CommandLineSettingChangesTrayIcon.*IEquals\(entry\.section,\s*L"Settings"\).*IEquals\(entry\.key,\s*L"TrayIcon"\).*ApplySettingsChangedByCommandLine\(bool applyTrayIcon\).*if \(applyTrayIcon\)\s*ApplyTrayIconSettingFromIni\(\).*CommandLineTrayIconChanged\(\).*SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON)rx", "command-line settings reloads must not resurrect or hide the session tray icon unless TrayIcon changed", false});
        checks.push_back({"Live Tile mode change forces registration before Live Tile updates", "src\\ga_generation.inc", generation, R"rx(LiveTileModeReregistrationPending\(\).*StartupGenerationCanSkip.*LiveTileModeReregistrationPending\(\).*return false.*liveTileModeReregistrationPending.*useLiveTileUpdateForThisRun\s*=\s*liveTileUpdateMode\s*&&\s*!liveTileModeReregistrationPending.*RegisterAppxManifest\(manifestPath,\s*appUpdateFailureMessage\).*SetLiveTileModeReregistrationPending\(false\))rx", "Live Tile setting changes must bypass startup skip, force one registration, and clear the pending flag after registration succeeds", false});
        checks.push_back({"Live Tile update requires package identity", "src\\ga_live_tile.inc", liveTile, R"rx(GetCurrentPackageFullName.*liveTileUpdateRequiresIdentity)rx", "Live Tile updater must detect missing package identity and report a clear failure", false});
        checks.push_back({"Live Tile mode disables static wallpaper manifest assets", "src\\ga_generation.inc", generation, R"rx(staticWallpaperAssetEnabled\s*=\s*!liveTileUpdateMode\s*&&\s*IniReadI\(L"Assets",\s*t\.name,\s*0\)\s*!=\s*0.*g_deleteDisabledAssets\s*\|\|\s*liveTileUpdateMode)rx", "Live Tile mode must treat static manifest assets as disabled and remove stale files when desktop-icon fallback is off", false});
        checks.push_back({"Desktop icon placeholder assets use full tile canvases", "src\\ga_generation.inc", generation, R"rx(LoadEmbeddedDesktopIcon\(\).*RenderDesktopIconAsset\(icon\.get\(\),\s*t\.w,\s*t\.h,\s*t\.w,\s*t\.h\).*ScalePixels\(t\.w,\s*scale\).*ScalePixels\(t\.h,\s*scale\).*RenderDesktopIconAsset\(icon\.get\(\),\s*scaledW,\s*scaledH,\s*t\.w,\s*t\.h\))rx", "disabled/static Desktop icon placeholders must be rendered to target tile dimensions instead of saving the raw icon directly", false});
        checks.push_back({"Desktop icon placeholder renderer uses Windows 8 tile placement", "src\\ga_image.inc", image, R"rx(DesktopIconVisibleWidthForAsset.*MulDiv\(logicalShortSide,\s*74,\s*150\).*MulDiv\(logicalVisibleW,\s*assetShortSide,\s*logicalShortSide\).*DesktopIconCenterYForAsset.*MulDiv\(logicalShortSide,\s*119,\s*300\).*MulDiv\(logicalCenterY,\s*assetHeight,\s*logicalHeight\).*static Bitmap\* RenderDesktopIconAsset.*DrawImage\(icon,\s*Rect\(dx,\s*dy,\s*targetVisibleW,\s*targetVisibleH\))rx", "Desktop icon placeholder renderer must use Windows 8-style logical tile sizing and placement without stretching the icon or shrinking high-DPI variants", false});
        checks.push_back({"Desktop icon placeholder uses embedded high-resolution source", "src\\ga_desktop_icon_png.inc", desktopIcon, R"rx(Embedded 864x640 RGBA PNG generated from the Windows 8\.1 Desktop tile logo proportions.*0x00,\s*0x00,\s*0x03,\s*0x60,\s*0x00,\s*0x00,\s*0x02,\s*0x80)rx", "Desktop icon placeholder must use an embedded high-resolution PNG source so the app stays portable and avoids low-resolution upscaling", false});
        checks.push_back({"Generated asset cache restores before loading wallpaper", "src\\ga_generation.inc", generation, R"rx(BuildGenerationStateKey\(wp\).*TryRestoreGeneratedAssetCache\(.*if \(!restoredFromGeneratedAssetCache\).*LoadWallpaperBitmapForRender)rx", "generated asset cache hits must bypass wallpaper loading and PNG rendering", false});
        checks.push_back({"Generated asset cache is populated after successful generation", "src\\ga_generation.inc", generation, R"rx(GeneratedAssetCache.*AddGeneratedAssetCacheFile.*if \(ok\).*SaveGeneratedAssetCache\(exeDir,\s*BuildGenerationStateKey\(wp\),\s*generatedAssetCacheFiles\))rx", "successful generation must save generated PNGs into the bounded asset cache for repeat slideshow wallpapers", false});
        checks.push_back({"Generated asset cache mirrors the active Assets layout", "src\\ga_generation.inc", generation, R"rx(ManifestGeneratedAssetCacheName.*return L"Assets\\\\.*LiveGeneratedAssetCacheName.*return L"Assets\\\\)rx", "generated asset cache entries should mirror the active Assets directory layout instead of using confusing Manifest or Live cache roots", false});
        checks.push_back({"Generated asset cache key includes asset and text settings", "src\\ga_generation.inc", generation, R"rx(BuildGenerationStateKey.*for \(const auto& t : g_tiles\).*IniReadI\(L"Assets",\s*t\.name,\s*0\).*IniReadS\(L"TileText",\s*L"Text",\s*L""\).*IniReadI\(L"TileText",\s*L"ApplyToLogos",\s*0\))rx", "generated asset cache keys must include render-affecting asset toggles and tile text settings", false});
        checks.push_back({"Generated asset pre-cache warms likely slideshow siblings in the background", "src\\ga_generation.inc", generation, R"rx(RenderWallpaperToGeneratedAssetCache.*EnumerateSiblingWallpaperCandidates.*RunGeneratedAssetPrecache.*GeneratedAssetPrecacheThread.*QueueGeneratedAssetPrecache.*MarkGenerationActive\(false\).*QueueGeneratedAssetPrecache)rx", "successful generation must opportunistically pre-cache likely slideshow siblings without blocking the foreground tile update", false});
        checks.push_back({"Generated asset cache settings are exposed in a Caching menu", "src\\ga_tray.inc", tray, R"rx(ID_GENERATED_ASSET_CACHE.*ID_GENERATED_ASSET_PRECACHE.*beginNestedMenu\(appearanceAssetsMenu,\s*L"CreatePopupMenu\(Caching\)".*g_ui\.generatedAssetCache.*g_ui\.generatedAssetPrecache.*g_ui\.generatedAssetCacheMaxEntriesLabel.*g_ui\.generatedAssetPrecacheMaxFilesLabel.*endNestedMenu\(appearanceAssetsMenu,\s*cachingMenu,\s*g_ui\.cachingTitle\).*IniWrite\(L"Settings",\s*L"GeneratedAssetCache".*IniWrite\(L"Settings",\s*L"GeneratedAssetPrecache")rx", "cache and slideshow pre-cache settings must be configurable from a dedicated Caching menu section", false});
        checks.push_back({"Generated asset cache runtime clamps match documented maxima", "src\\ga_generation.inc", generation, R"rx(GeneratedAssetCacheMaxEntries\(\).*IniReadClampedI\(.*4096.*GeneratedAssetPrecacheMaxFiles\(\).*IniReadClampedI\(.*256)rx", "cache max runtime clamps must stay at the documented 4096 entry and 256 pre-cache file limits", false});
        checks.push_back({"Generated asset cache command-line bounds match runtime clamps", "src\\ga_command_line.inc", commandLine, R"rx(--generated-asset-cache-max.*0,\s*4096.*--generated-asset-precache-max.*0,\s*256)rx", "cache max command-line bounds must match runtime clamps", false});
        checks.push_back({"Generated asset cache tray presets match runtime clamps", "src\\ga_tray.inc", tray, R"rx(CycleSettingIntPreset\(L"GeneratedAssetCacheMaxEntries",\s*32,\s*0,\s*4096.*CycleSettingIntPreset\(L"GeneratedAssetPrecacheMaxFiles",\s*16,\s*0,\s*256)rx", "cache max tray presets must match runtime clamps", false});
        checks.push_back({"Idle trim interval is exposed in config, tray, and CLI", "idle-trim sources", defaults + "\n" + tray + "\n" + commandLine + "\n" + app, R"rx(IdleTrimIntervalMs",\s*L"60000".*ID_ADV_IDLE_TRIM_INTERVAL_MS.*CycleSettingIntPreset\(L"IdleTrimIntervalMs",\s*60000,\s*0,\s*3600000.*--idle-trim-interval.*IniReadClampedI\(L"Settings",\s*L"IdleTrimIntervalMs",\s*60000,\s*0,\s*3600000)rx", "IdleTrimIntervalMs must remain configurable through INI defaults, tray menu, command line, and runtime diagnostics", false});
        checks.push_back({"DesktopStub smoke runs from a temporary build directory", "..\\.github\\tools\\RepoTools.cs", repoTools, R"rx(DesktopStubSmoke-.*string exe = Path\.Combine\(tempRoot,\s*Path\.GetFileName\(sourceExe\)\).*File\.Copy\(sourceExe,\s*exe,\s*true\).*DesktopStubLiveTileBroker\.exe.*DesktopStubAppxStub\.exe.*Path\.Combine\(Path\.GetDirectoryName\(exe\),\s*"AppxManifest\.xml"\))rx", "DesktopStub smoke tests must run a copied EXE from a temp directory so they cannot mutate build\\Assets, build\\AssetCache, or build\\AppxManifest.xml", false});
        checks.push_back({"DesktopStub smoke uses unique manifest identity", "..\\.github\\tools\\RepoTools.cs", repoTools, R"rx(dev\.local\.desktopstubsmoke.*SetDesktopStubManifestIdentity.*did not preserve the custom AppxManifest\.xml package identity.*UnregisterDesktopStubSmokePackage.*Remove-AppxPackage)rx", "DesktopStub smoke tests must avoid colliding with a running local DesktopStub package identity, verify custom manifest identity preservation, and clean up the temporary package", false});
        checks.push_back({"TileText style settings are exposed in tray", "src\\ga_tray.inc", tray, R"rx(ID_TILE_TEXT_FONT.*ID_TILE_TEXT_BOLD.*ID_TILE_TEXT_COLOR.*ID_TILE_TEXT_MARGIN_X.*PromptTileTextStringSetting.*PromptTileTextIntSetting)rx", "TileText style settings must be configurable through the tray menu, not only by editing the INI", false});
        checks.push_back({"TileText style settings are exposed in CLI", "src\\ga_command_line.inc", commandLine, R"rx(--tile-text-font.*AddCommandLineIniSetting\(L"TileText", L"Font".*--tile-text-margin-x.*AddCommandLineClampedIntSetting\(L"TileText", L"MarginX".*--tile-text-max-secondary-lines.*AddCommandLineClampedIntSetting\(L"TileText", L"MaxSecondaryLines".*--tile-text-bold.*AddCommandLineBoolSetting\(L"TileText", L"Bold")rx", "TileText style settings must be configurable through command-line options, not only by editing the INI", false});
        checks.push_back({"Live Tile mode writes dedicated notification assets", "src\\ga_generation.inc", generation, R"rx(g_liveTileAssets.*Assets\\\\LiveMediumTile\.png.*Assets\\\\LiveWideTile\.png.*Assets\\\\LiveLargeTile\.png.*NewLiveTileAssetToken.*VersionedLiveTileAssetPath.*if \(liveTileUpdateMode\).*VersionedLiveTileAssetPath\(t,\s*liveTileToken\).*liveTileUpdateAssets\.push_back)rx", "Live Tile mode must write dedicated versioned Live*.png notification assets instead of relying on manifest logo assets or overwriting the displayed tile image in place", false});
        checks.push_back({"Manifest target can generate Windows 10, 8.1, and 8 manifests", "manifest/command-line sources", manifest + "\n" + commandLine + "\n" + tray, R"rx(appx/2010/manifest.*xmlns:m2=.*2013/manifest.*AppxManifestTarget.*--manifest-target.*ID_MANIFEST_TARGET_WINDOWS10.*ID_MANIFEST_TARGET_WINDOWS81.*ID_MANIFEST_TARGET_WINDOWS8)rx", "manifest target selection must cover Windows 10, Windows 8.1, and Windows 8 from command line and tray", false});
        checks.push_back({"Windows 8 broker helper is the default stabilized Live Tile path", "manifest/live-tile/config sources", defaults + "\n" + manifest + "\n" + liveTile, R"rx(\{L"Settings",\s*L"Win8LiveTileOopHelper",\s*L"0"\}.*\{L"Settings",\s*L"Win8LiveTileBackgroundTask",\s*L"0"\}.*\{L"Settings",\s*L"Win8LiveTileBrokerApp",\s*L"1"\}.*Win8LiveTileBrokerApp.*IniReadI\(L"Settings",\s*L"Win8LiveTileBrokerApp",\s*1\).*Packaged WinRT Live Tile broker reported success)rx", "Windows 8/8.1 compatibility mode should default to the packaged WinRT broker while leaving background/OOP experiments disabled", false});
        checks.push_back({"Renamed helper command-line aliases are tolerated", "src\\ga_command_line.inc", commandLine, R"rx(WAIT_FOR_PID_ARG.*WAIT_FOR_PID_ARG_LEGACY.*COM_REGISTRATION_HELPER_ARG.*COM_REGISTRATION_HELPER_ARG_LEGACY)rx", "internal helper command-line parsing must accept both DesktopStub-era and GenerateAssets-era hidden switches", false});
        checks.push_back({"Windows 8 app activation command line arguments are tolerated", "src\\ga_command_line.inc", commandLine, R"rx(TryParseAppxActivationArgument.*-ServerName:.*IsAppxActivationServerNameOption.*IsIgnoredAppxActivationArgument.*-Embedding.*appxActivationServer)rx", "Windows 8/8.1-style AppX activation arguments such as -ServerName:... must not be rejected as unknown command-line options", false});
        checks.push_back({"Legacy Live Tile XML uses Windows 8/8.1 templates", "src\\ga_live_tile.inc", liveTile, R"rx(LiveTileTemplateForTarget.*TileSquare150x150Image.*TileSquareImage.*TileWide310x150Image.*TileWideImage.*TileSquare310x310Image.*visual version=.*2)rx", "Windows 8/8.1 manifest targets must use legacy tile notification templates instead of Windows 10 adaptive templates", false});
        checks.push_back({"Live Tile XML uses dedicated notification assets", "src\\ga_live_tile.inc", liveTile, R"rx(BuildLiveTileXml.*const std::vector<LiveTileUpdateAsset>& liveTileAssets.*for \(const auto& asset : liveTileAssets\).*LiveTileTemplateForTarget\(asset\.binding\).*LiveTileAssetUriIfValid\(exeDir,\s*asset\.file,\s*asset\.w,\s*asset\.h\).*AppendLiveTileImageBinding\(xml,\s*templateName)rx", "Live Tile XML must reference the generated Live*.png update assets", false});
        checks.push_back({"Live wallpaper capture rejects blank PrintWindow frames", "src\\ga_wallpaper.inc", wallpaper, R"rx(SurfaceLooksBlankOrBlack.*rejectedBlank\s*=\s*true)rx", "live wallpaper capture must reject black/blank snapshots instead of generating black tiles", false});
        checks.push_back({"Live wallpaper capture skips desktop icon WorkerW", "src\\ga_wallpaper.inc", wallpaper, R"rx(SHELLDLL_DefView.*desktop icon layer)rx", "live wallpaper capture must skip the WorkerW that owns desktop icons", false});
        checks.push_back({"Live wallpaper capture waits for a real host window", "src\\ga_wallpaper.inc", wallpaper, R"rx(HasPotentialLiveWallpaperChildWindow)rx", "live wallpaper capture should require a real WorkerW host/child window", false});
        checks.push_back({"Live wallpaper capture rejects tiny helper windows", "src\\ga_wallpaper.inc", wallpaper, R"rx(LiveWallpaperCaptureRectLooksLargeEnough)rx", "live wallpaper capture must reject tiny helper windows such as 136x39 UI fragments", false});
        checks.push_back({"Live wallpaper startup recapture is supported", "src\\ga_wallpaper.inc", wallpaper, R"rx(LiveWallpaperCaptureStartupRefreshMs)rx", "live wallpaper capture must retry briefly while WorkerW-hosted renderers are still starting", false});
        checks.push_back({"Live wallpaper snapshots use rotating paths", "src\\ga_wallpaper.inc", wallpaper, R"rx(s_captureSequence)rx", "live wallpaper recaptures should use bounded rotating paths for changed snapshots", false});
        checks.push_back({"Live wallpaper startup settle does not publish every warm-up capture", "src\\ga_wallpaper.inc", wallpaper, R"rx(Do not publish the warm-up snapshots one by one)rx", "startup recapture should settle and publish the latest good snapshot once instead of regenerating repeatedly", false});
        checks.push_back({"WorkerW live wallpaper capture is available", "src\\ga_wallpaper.inc", wallpaper, R"rx(LiveWallpaperProvider::Active)rx", "live wallpaper capture should include a WorkerW provider for unknown WorkerW-hosted live wallpaper apps", false});
        checks.push_back({"Live wallpaper capture is WorkerW-only", "src\\ga_wallpaper.inc", wallpaper, R"rx(Capture is intentionally limited)rx", "live wallpaper capture must not enumerate arbitrary top-level app windows", false});
        checks.push_back({"WorkerW parent fallback is kept for DirectX wallpaper hosts", "src\\ga_wallpaper.inc", wallpaper, R"rx(workerCandidates.*AddUniqueWindowCandidate\(workerCandidates, worker\)|AddUniqueWindowCandidate\(workerCandidates, worker\).*workerCandidates)rx", "WorkerW detection should keep the non-icon WorkerW parent as a fallback when child PrintWindow capture is black", false});
        checks.push_back({"Live wallpaper refresh defaults to ten seconds", "src\\ga_config_defaults.inc", defaults, R"rx(LiveWallpaperCaptureRefreshMs", L"10000)rx", "LiveWallpaperCaptureRefreshMs should default to 10000 ms", false});
        checks.push_back({"Live wallpaper startup recapture duration defaults off", "src\\ga_config_defaults.inc", defaults, R"rx(LiveWallpaperCaptureStartupRefreshDurationMs", L"0)rx", "LiveWallpaperCaptureStartupRefreshDurationMs should default to 0/off", false});
        checks.push_back({"Live wallpaper pending blocks static fallback", "src\\ga_wallpaper.inc", wallpaper, R"rx(g_liveWallpaperCapturePending)rx", "live wallpaper startup should be able to defer fallback to the static wallpaper until capture settles", false});
        checks.push_back({"Live wallpaper screen fallback is opt-in", "src\\ga_wallpaper.inc", wallpaper, R"rx(LiveWallpaperCaptureScreenFallbackEnabled)rx", "screen-DC fallback must be opt-in because it can capture the visible desktop", false});

        checks.push_back({"Generated PNG validation checks full PNG structure", "src\\ga_generation.inc", generation, R"rx(PngCrc32.*sawIhdr.*sawIdat.*sawIend.*expectedCrc.*PngChunkTypeEquals\(chunkType,\s*"IDAT"\).*PngChunkTypeEquals\(chunkType,\s*"IEND"\))rx", "generated PNG validation must reject truncated or header-only cache files", false});
        checks.push_back({"Generated PNG validation records dimensions", "src\\ga_generation.inc", generation, R"rx(TryReadGeneratedPngInfo.*info\.width = width.*info\.height = height.*IsValidGeneratedPngWithDimensions.*expectedWidth.*expectedHeight)rx", "generated PNG validation must expose dimensions so wrong-size tile assets cannot pass as valid", false});
        checks.push_back({"Generated asset cache entries carry expected dimensions", "src\\ga_generation.inc", generation, R"rx(GeneratedAssetCacheFile.*expectedWidth.*expectedHeight)rx", "generated asset cache entries must record expected tile dimensions", false});
        checks.push_back({"Generated asset cache rejects dimensionless entries", "src\\ga_generation.inc", generation, R"rx(AddGeneratedAssetCacheFile.*expectedWidth <= 0 \|\| expectedHeight <= 0.*files\.push_back\(\{ cacheRelativePath,\s*activePath,\s*expectedWidth,\s*expectedHeight \}\))rx", "generated asset cache entries must not be added without expected dimensions", false});
        checks.push_back({"Generated asset cache presence checks expected dimensions", "src\\ga_generation.inc", generation, R"rx(GeneratedAssetCacheFilesPresent.*IsValidGeneratedPngWithDimensions)rx", "generated asset cache restore must reject syntactically valid PNGs that do not match the expected tile dimensions", false});
        checks.push_back({"Startup asset validation checks expected dimensions", "src\\ga_generation.inc", generation, R"rx(StartupGeneratedAssetsPresent.*IsValidGeneratedPngWithDimensions\(basePath,\s*t\.w,\s*t\.h\).*ScalePixels\(t\.w,\s*scale\).*ScalePixels\(t\.h,\s*scale\).*IsValidGeneratedPngWithDimensions\(path,\s*asset\.w,\s*asset\.h\))rx", "startup skip validation must reject wrong-size generated assets", false});
        checks.push_back({"Live Tile XML validates expected asset dimensions", "src\\ga_live_tile.inc", liveTile, R"rx(LiveTileAssetUriIfValid.*expectedWidth.*expectedHeight.*IsValidGeneratedPngWithDimensions\(fullPath,\s*expectedWidth,\s*expectedHeight\).*asset\.w,\s*asset\.h)rx", "Live Tile XML must not reference wrong-size image assets", false});
        checks.push_back({"Missing manifest asset diagnostics include square30 and splash", "src\\ga_generation.inc", generation, R"rx(CurrentManifestAssetPaths.*info\.square44x44Logo.*info\.square30x30Logo.*info\.square71x71Logo.*info\.square310x310Logo.*info\.splashScreenImage)rx", "missing-asset diagnostics should cover every manifest image slot DesktopStub can generate", false});
        checks.push_back({"Generated PNG validation avoids whole-file cache allocation", "src\\ga_generation.inc", generation, R"rx(std::vector<BYTE>\s+png\(static_cast<size_t>\(size\.QuadPart\)\))rx", "generated PNG validation must stream cache files instead of allocating by PNG file size", true});
        checks.push_back({"PNG encoder cache is synchronized", "src\\ga_image.inc", image, R"rx(static std::mutex g_pngMutex.*FindPngEncoder\(\).*std::lock_guard<std::mutex> lk\(g_pngMutex\))rx", "PNG encoder CLSID cache must be protected against foreground/pre-cache races", false});
        checks.push_back({"GDI+ release trims idle memory", "src\\ga_image.inc", image, R"rx(ReleaseGdiPlus\(\).*GdiplusShutdown\(token\).*released = true.*TrimIdleWorkingSetIfConfigured\(\))rx", "GDI+ shutdown should trim idle memory promptly in low-memory mode", false});
        checks.push_back({"LocalAppDataPath avoids large stack buffers", "src\\ga_live_tile.inc", liveTile, R"rx(LocalAppDataPath\(\).*GetEnvironmentVariableW\(L"LOCALAPPDATA",\s*nullptr,\s*0\).*std::vector<wchar_t> buffer)rx", "LocalAppDataPath must not allocate a 32K wchar_t buffer on the stack", false});
        checks.push_back({"RoActivateInstance is guarded against null HSTRING", "src\\ga_live_tile.inc", liveTile, R"rx(WindowsCreateString\(classId\.c_str\(\).*if \(SUCCEEDED\(hr\) && className\).*RoActivateInstance\(className,\s*&instance\).*else if \(SUCCEEDED\(hr\)\).*E_POINTER)rx", "RoActivateInstance must not receive a null HSTRING", false});
        checks.push_back({"CRT heap compaction return is checked", "src\\ga_logging_core.inc", loggingCore, R"rx(int heapStatus = _heapmin\(\).*heapStatus != 0.*errno)rx", "_heapmin return value should be handled intentionally", false});

        checks.push_back({"Build script no longer has argument-selected targets", "BuildDesktopStub.cmd", buildScript, R"rx(BUILD_BROKER|BUILD_BACKGROUND_TASK|unknown build option|:ParseArgs|:ShowHelp)rx", "BuildDesktopStub.cmd must not restore argument-selected target branches", true});
        checks.push_back({"Live Tile update does not clear the existing tile first", "src\\ga_live_tile.inc", liveTileUpdateBare, R"rx(updater\.Clear\s*\()rx", "Live Tile updater must not call Clear before Update because that causes a visible blank tile during refresh", true});
        checks.push_back({"Manifest overwrite is not a persistent default", "src\\ga_config_defaults.inc", defaults, R"rx(\{L"Manifest",\s*L"OverwriteExisting")rx", "manifest overwrite must not be recreated as a persistent INI default", true});
        checks.push_back({"Manifest overwrite is not persisted from tray", "src\\ga_tray.inc", tray, R"rx(IniWrite\(L"Manifest",\s*L"OverwriteExisting")rx", "tray manifest regeneration must not write a persistent OverwriteExisting key", true});
        checks.push_back({"Provider-specific live wallpaper code was removed", "src\\ga_wallpaper.inc", wallpaper, R"rx(LivelyProcess|WallpaperEngineProcess|WindowProcessMatchesLively|WindowProcessMatchesWallpaperEngine|IsLivelyRunning|IsWallpaperEngineRunning|webwallpaper64\.exe|Lively\.Player)rx", "legacy Lively/Wallpaper Engine-specific live wallpaper detection should stay removed; WorkerW detection is the maintained path", true});
        checks.push_back({"Provider-specific live wallpaper settings were removed", "source tree", JoinSource({"src\\ga_config_defaults.inc","src\\ga_command_line.inc","src\\ga_tray.inc","src\\ga_ui_strings.inc","src\\ga_ui_state.inc"}), R"rx(LiveWallpaperCaptureLively|LiveWallpaperCaptureWallpaperEngine|live-wallpaper-lively|live-wallpaper-wallpaper-engine|LiveWallpaperProviderLively|LiveWallpaperProviderWallpaperEngine)rx", "legacy provider-specific live wallpaper settings/strings/command-line switches should stay removed", true});
        checks.push_back({"Legacy named live wallpaper UI was removed", "source tree", JoinSource({"README.md","src\\ga_config_defaults.inc","src\\ga_command_line.inc","src\\ga_tray.inc","src\\ga_ui_strings.inc","src\\ga_ui_state.inc","src\\ga_wallpaper.inc"}), R"rx(Capture generic WorkerW live wallpaper|Generic live wallpaper|Generic=|LiveWallpaperSnapshot_Generic|LiveWallpaperCaptureGeneric|LiveWallpaperProviderGeneric|live-wallpaper-generic)rx", "after provider-specific live wallpaper capture was removed, the remaining WorkerW detector should be presented as the normal live wallpaper detector", true});

        const std::vector<std::string> uiStringKeys = ExtractUiStringDefaultKeys(defaults);

        if (listChecks)
        {
            std::cout << "DesktopStub source regression guardrails:\n";
            for (const auto& check : checks)
                std::cout << "- " << check.name << "\n";
            for (const auto& key : uiStringKeys)
                std::cout << "- UI string default/load/format wiring: " << key << "\n";
            return 0;
        }

        std::cout << "Running DesktopStub maintainer source regression checks...\n";
        std::cout << "This checks source guardrails only; it does not build or launch the app.\n";

        for (const auto& check : checks)
            AssertCheck(check);

        AssertContainsAll(
            "Runtime sidecar file names are product-scoped",
            "Live Tile runtime sources",
            manifest + "\n" + liveTile + "\n" + brokerApp + "\n" + backgroundTask,
            {
                "ProductRuntimeBaseName()",
                "ProductRuntimeFileName(const wchar_t* suffix)",
                "ProductRuntimeFilePath(const wchar_t* suffix)",
                "ProductRuntimeFilePath(L\".livetile.pending.xml\")",
                "ProductRuntimeFilePath(L\".livetile.clear\")",
                "ProductRuntimeFilePath(L\".appxactivation.log\")",
                "const std::wstring suffix = L\"LiveTileBroker\"",
                "const std::wstring suffix = L\"LiveTileTask\""
            },
            "Live Tile pending XML, clear flags, and activation traces must be derived from the product/runtime base name instead of hard-coded DesktopStub file names");
        AssertNotContainsAny(
            "Runtime sidecar files do not hard-code DesktopStub",
            "Live Tile runtime sources",
            liveTile + "\n" + brokerApp + "\n" + backgroundTask,
            {"DesktopStub.livetile", "DesktopStub.appxactivation"},
            "Live Tile runtime sidecar file names must not be hard-coded to DesktopStub");
        AssertContainsAll(
            "Manifest defaults support product tokens",
            "manifest/default sources",
            manifest + "\n" + defaults,
            {
                "ExpandManifestSettingTokens",
                "L\"{ProductRuntimeBaseName}\"",
                "L\"{ProductManifestToken}\"",
                "L\"ManifestLiveTileBrokerExecutable\"",
                "L\"{ProductRuntimeBaseName}LiveTileBroker.exe\"",
                "L\"ManifestLiveTileBrokerEntryPoint\"",
                "L\"{ProductManifestToken}.LiveTileBroker.App\""
            },
            "manifest defaults should use product tokens so copied baseline projects do not inherit DesktopStub package helper identities");
        AssertContainsAll(
            "Single-instance identity is product-scoped",
            "src\\ga_app.inc",
            app,
            {
                "std::wstring productBase = SanitizeManifestToken(ProductRuntimeBaseName(), L\"DesktopStub\");",
                "g_singleInstanceMutexName = L\"Local\\\\\" + productBase + L\".\" + suffix;",
                "g_singleInstanceMessageName = productBase + L\".RestoreRunningInstance.\" + suffix;"
            },
            "single-instance mutex/message names should include the product base so copied baseline projects do not share identity prefixes");
        AssertContainsAll(
            "Build script quotes configurable output paths",
            "BuildDesktopStub.cmd",
            buildScript,
            {
                "/Fe\"%BROKER_EXE%\" /Fo\"%BROKER_OBJ%\"",
                "/Fe\"%OUT_EXE%\" /Fo\"%OBJ_FILE%\"",
                "/fo\"%BROKER_RES%\"",
                "/fo\"%RES_FILE%\""
            },
            "BuildDesktopStub.cmd must quote cl/rc outputs so parameterized output names cannot break the command line");
        AssertNotContainsAny(
            "Build script avoids RC defines with spaces",
            "BuildDesktopStub.cmd",
            buildScript,
            {"DESKTOPSTUB_FILE_DESCRIPTION=\\\"%DESKTOPSTUB_PRODUCT_NAME% tray", "DESKTOPSTUB_FILE_DESCRIPTION=\\\"%DESKTOPSTUB_PRODUCT_NAME% Live Tile"},
            "BuildDesktopStub.cmd must not pass resource string macros containing spaces through rc.exe /d arguments");
        AssertContainsAll(
            "--once survives Live Tile runtime relaunch",
            "src\\ga_app.inc",
            app,
            {
                "ModeSwitchRelaunchArguments(bool forceGenerate, bool waitForThisProcessToExit, bool generateOnce)",
                "if (generateOnce)",
                "args += L\" --once\";",
                "ModeSwitchRelaunchArguments(true, false, CommandLineShouldGenerateOnce())"
            },
            "A one-shot --once run that must bounce between unpackaged/packaged Live Tile runtimes must relaunch with --once, not --generate, so it exits after updating.");
        AssertContainsAll(
            "--once skips tray icon initialization",
            "src\\ga_app.inc",
            app,
            {
                "if (CommandLineShouldGenerateOnce())",
                "g_tray = false;",
                "else if (g_tray)"
            },
            "One-shot --once generation must not call Shell_NotifyIcon before exiting; Explorer can timeout and make the command look hung.");
        AssertContainsAll(
            "Source-only export excludes local artifacts",
            "source export guardrails",
            gitAttributes + "\n" + exportSourceOnly,
            {"build/ export-ignore", "**/*.ini export-ignore", "**/*.exe export-ignore", "git archive --format=zip"},
            "source archives should be created with git archive and export-ignore build/runtime artifacts");
        for (const auto& key : uiStringKeys)
            AssertUiStringWired(key, defaults, uiSources);

        std::cout << "DesktopStub source regression checks passed (" << g_checkCount << " checks).\n";
    }
    catch (const std::regex_error& ex)
    {
        std::cerr << "DesktopStub source regression: invalid regex in C++ source checker: "
            << ex.what() << "\n";
        return 1;
    }
    catch (const std::exception& ex)
    {
        std::string msg = ex.what();
        if (msg != "source regression")
            std::cerr << msg << "\n";
        return 1;
    }

    return 0;
}

#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

static int g_checks = 0;

static std::string ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        throw std::runtime_error("missing source file: " + path);
    }
    return std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());
}

static void RequireContains(
    const std::string& name,
    const std::string& source,
    const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
    {
        std::cerr << "CharmTray source regression: " << name
            << " missing " << needle << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

static void RequireNotContains(
    const std::string& name,
    const std::string& source,
    const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) != std::string::npos)
    {
        std::cerr << "CharmTray source regression: " << name
            << " still contains " << needle << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

int main()
{
    try
    {
        const std::string wrapper = ReadAll("CharmTray.cpp");
        const std::string source = ReadAll("../../dependencies/CharmTray/charm_app.inc");
        const std::string tray = ReadAll("../../dependencies/tray.inc");
        const std::string startup = ReadAll("../../dependencies/startup_shortcut.inc");
        const std::string build = ReadAll("BuildCharmTray.cmd");

        RequireContains(
            "project entry point is a dependency overlay",
            wrapper,
            "../../dependencies/CharmTray/charm_app.inc");
        RequireContains(
            "shared desktop baseline is used",
            source,
            "../desktop_app_baseline.h");
        RequireContains(
            "settings are created through the synchronized INI store",
            source,
            "config.EnsureDefaults(defaults, ARRAYSIZE(defaults))");
        RequireContains(
            "invalid logging booleans are rejected",
            source,
            "aip::ParseBoolValue(rawLogging, loggingEnabled)");
        RequireContains(
            "startup setting is shared by INI command line and tray",
            source,
            "L\"RunAtStartup\"");
        RequireContains(
            "logging uses the shared UTF-8 logger",
            source,
            "aip::Utf8LoggerOptions options;");
        RequireContains(
            "the shared Startup-folder helper is consumed",
            source,
            "../startup_shortcut.inc");
        RequireContains(
            "startup ownership follows the effective INI profile",
            source,
            "spec.identityPath = g_paths.configPath;");
        RequireContains(
            "startup launches preserve the effective INI profile",
            source,
            "spec.arguments = L\"--ini \" + aip::QuoteCommandLineArg(g_paths.configPath);");
        RequireContains(
            "custom INI paths retain executable-side logging",
            source,
            "aip::DefaultLogPathPolicy::BesideExecutable");
        RequireContains(
            "custom INI paths are validated",
            source,
            "aip::TryResolveConfigFilePath");
        RequireContains(
            "command-line settings use one fresh atomic mutation",
            source,
            "ConfigStore().MutateFresh");
        RequireContains(
            "Startup and INI changes use the shared cross-resource transaction",
            source,
            "aip::CommitStartupShortcutIniState(");
        RequireContains(
            "shared Startup transaction uses direction-safe commit and rollback",
            startup,
            "ExecuteCrashConsistentLaunchConfigState(");
        RequireContains(
            "the singleton uses a stable shared path-scoped identity",
            source,
            "aip::BuildPathScopedInstanceIdentity(");
        RequireContains(
            "the singleton is scoped by the effective INI path",
            source,
            "g_paths.configPath);");
        RequireContains(
            "secondary instances receive acknowledged requests",
            source,
            "aip::SendInstanceWindowRequest(");
        RequireContains(
            "the tray exposes Startup preference",
            source,
            "L\"Run at startup\"");
        RequireContains(
            "the tray exposes file logging preference",
            source,
            "L\"File logging\"");
        RequireContains(
            "help works through shared command-line output",
            source,
            "aip::WriteCommandLineText");
        RequireContains(
            "unsupported Windows versions are rejected",
            source,
            "version.dwMajorVersion == 6");
        RequireContains(
            "shell service creation checks HRESULT",
            source,
            "FAILED(result) || *shell == nullptr");
        RequireContains(
            "edge placement checks HRESULT",
            source,
            "IEdgeUiTracker::SetMonitorEdge");
        RequireContains(
            "flyout Show failures are checked",
            source,
            "ICharmFlyout::Show");
        RequireContains(
            "modern tray notification version is selected",
            tray,
            "NOTIFYICON_VERSION_4");
        RequireContains(
            "tray version 4 keeps the standard hover tooltip",
            source,
            "aip::RegisterTrayIcon(");
        RequireContains(
            "Explorer restart restores the tray icon",
            source,
            "msg == g_taskbarCreated");
        RequireContains(
            "tray activation supports keyboard selection",
            source,
            "notification == NIN_KEYSELECT");
        RequireContains(
            "tray menus dispatch returned commands",
            source,
            "TPM_RETURNCMD");
        RequireNotContains(
            "fixed MAX_PATH executable lookup was removed",
            source,
            "wchar_t buffer[MAX_PATH]");
        RequireNotContains(
            "Win32 profile API settings were removed",
            source,
            "WritePrivateProfileStringW");
        RequireNotContains(
            "registry startup writes are forbidden",
            source,
            "RegSetValue");
        RequireNotContains(
            "Task Scheduler startup is forbidden",
            source,
            "schtasks");
        RequireContains(
            "Startup helper uses only the per-user Startup known folder",
            startup,
            "FOLDERID_Startup");
        RequireContains(
            "CharmTray links ShellLink GUID definitions",
            build,
            "uuid.lib");

        std::cout << "CharmTray source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

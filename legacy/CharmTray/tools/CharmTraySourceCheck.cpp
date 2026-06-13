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
        const std::string source = ReadAll("CharmTray.cpp");

        RequireContains(
            "shared desktop baseline is used",
            source,
            "../../dependencies/desktop_app_baseline.h");
        RequireContains(
            "settings are created through the synchronized INI store",
            source,
            "config.EnsureDefaults(defaults, ARRAYSIZE(defaults))");
        RequireContains(
            "invalid logging booleans are rejected",
            source,
            "aip::ParseBoolValue(raw, loggingEnabled)");
        RequireContains(
            "logging uses the shared UTF-8 logger",
            source,
            "aip::Utf8LoggerOptions options;");
        RequireContains(
            "the singleton is scoped to the executable path",
            source,
            "aip::BuildPathScopedInstanceIdentity(");
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
            source,
            "NOTIFYICON_VERSION_4");
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

        std::cout << "CharmTray source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

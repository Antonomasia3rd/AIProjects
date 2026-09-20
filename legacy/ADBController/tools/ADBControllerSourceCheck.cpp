#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
int g_checks = 0;

std::string ReadFile(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error(std::string("could not read ") + path);
    }
    std::string text{
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

void RequireContains(
    const char* name,
    const std::string& source,
    const std::string& expected)
{
    ++g_checks;
    if (source.find(expected) == std::string::npos)
    {
        throw std::runtime_error(std::string(name) + " missing: " + expected);
    }
}

void RequireAbsent(
    const char* name,
    const std::string& source,
    const std::string& rejected)
{
    ++g_checks;
    if (source.find(rejected) != std::string::npos)
    {
        throw std::runtime_error(std::string(name) + " still contains: " + rejected);
    }
}

void RequireBefore(
    const char* name,
    const std::string& source,
    const std::string& first,
    const std::string& second)
{
    ++g_checks;
    size_t firstAt = source.find(first);
    size_t secondAt = source.find(second);
    if (firstAt == std::string::npos || secondAt == std::string::npos || firstAt >= secondAt)
    {
        throw std::runtime_error(std::string(name) + " ordering contract failed");
    }
}
}

int main()
{
    try
    {
        const std::string wrapper = ReadFile("ADBController.cpp");
        const std::string source = ReadFile("../../dependencies/ADBController/adb_controller_app.inc");
        const std::string launcher = ReadFile("TCL ADB.cmd");
        const std::string readme = ReadFile("README.md");
        const std::string resource = ReadFile("ADBController.rc");

        RequireContains(
            "project entry point is a dependency overlay",
            wrapper,
            "../../dependencies/ADBController/adb_controller_app.inc");
        RequireContains("shared baseline", source, "../desktop_app_baseline.h");
        RequireContains("strict CLI parser", source, "bool ParseCommandLine(CommandLineOptions& options");
        RequireContains("help option", source, "aip::EqualsI(arg, L\"--help\")");
        RequireContains("version option", source, "aip::EqualsI(arg, L\"--version\")");
        RequireBefore(
            "help is side-effect-free",
            source,
            "if (commandLine.showHelp)",
            "PrepareApplicationPaths(commandLine");
        RequireBefore(
            "version is side-effect-free",
            source,
            "if (commandLine.showVersion)",
            "PrepareApplicationPaths(commandLine");
        RequireContains("alternate INI", source, "aip::TryBuildCurrentProcessSidecarPaths(");
        RequireContains("typed set", source, "aip::ParseIniSetSpec(value, setting, error)");
        RequireContains("typed setting validation", source, "ValidateCommandLineSetting(setting, error)");
        RequireContains("atomic configuration batch", source, "g_config->MutateFresh(mutateSettings)");
        RequireContains("shared Startup dependency", source, "../startup_shortcut.inc");
        RequireContains("shared Startup/config transaction", source, "aip::CommitStartupShortcutIniState(StartupSpec()");
        RequireContains("shared tray registration", source, "aip::RegisterTrayIcon(g_trayIcon");
        RequireContains("strict loaded settings", source, "ParseApplicationSettings(text, settings, error)");
        RequireContains("atomic setting write", source, "aip::WriteIniValueToText(");
        RequireContains("atomic device removal", source, "aip::RemoveIniValueFromText(");
        RequireContains("keep one device", source, "At least one valid [Devices] entry must remain.");
        RequireContains("configuration-only action", source, "--configure-only");
        RequireContains("configured target selection", source, "FindCommandLineDevice(options.target, device)");
        RequireContains("reboot confirmation", source, "CommandLineAction::Reboot ||");
        RequireContains("disconnect-all confirmation", source, "CommandLineAction::DisconnectAll) && !options.confirmed");
        RequireContains("synchronous action", source, "CommandResult result = ExecuteRequest(request);");
        RequireContains("worker exception containment", source, "ExecuteRequestSafely(CommandRequest& request) noexcept");
        RequireContains("thread creation exception containment", source, "Could not create the ADB background worker thread.");
        RequireContains("synchronized worker completion", source, "std::lock_guard<std::mutex> lock(g_workerCompletionMutex)");
        RequireContains("worker completion timer fallback", source, "SetTimer(g_window, kWorkerCompletionTimer");
        RequireContains("null completion is handled", source, "if (result == nullptr && fallbackError == ERROR_SUCCESS)");
        RequireAbsent("raw worker result allocation", source, "new CommandResult(ExecuteRequest(request))");

        RequireAbsent("unsafe Win32 search", source, "SearchPathW(");
        RequireContains("absolute PATH entries only", source, "!PathIsRelativeW(directory.c_str())");
        RequireContains("oversized PATH fails accurately", source, "ERROR_BUFFER_OVERFLOW");
        RequireContains("resolved path canonicalization", source, "aip::TryMakeAbsolutePath(candidate, absolute");
        RequireContains("ADB application name is explicit", source, "executable.c_str(),\n            commandLine.data()");
        RequireContains("ADB working directory", source, "std::wstring childDirectory = aip::GetDirectoryName(executable)");
        RequireContains("trusted command processor directory", source, "aip::GetSystemDirectoryPath()");
        RequireContains("command processor application name is explicit", source, "cmdPath.c_str(),\n            commandLine.data()");
        RequireContains("shell working directory is ADB directory", source, "std::wstring adbDirectory = aip::GetDirectoryName(adbPath)");
        RequireAbsent("null process application name", source, "CreateProcessW(nullptr");

        RequireContains("profile-scoped instance", source, "g_paths.configPath);");
        RequireContains("profile-scoped control window", source, "g_instanceIdentity.windowTitle.c_str()");
        RequireContains("reload refreshes theme", source, "g_themeMode = g_settings.theme");
        RequireContains("compatibility launcher forwards arguments", launcher, "\"%PROGRAM%\" %*");
        RequireContains("version resource", resource, "FILEVERSION 1,0,0,0");
        RequireContains("CLI documentation", readme, "Command line");
        RequireContains("foreground and tray contract", readme, "foreground window and an optional tray icon");

        std::cout << "ADBController source checks passed: " << g_checks << std::endl;
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "ADBController source check failed after " << g_checks
                  << " checks: " << error.what() << std::endl;
        return 1;
    }
}

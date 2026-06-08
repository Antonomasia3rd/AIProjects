#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static int g_checks = 0;

static std::string NormalizeNewlines(std::string text)
{
    std::string normalized;
    normalized.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\r')
        {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            normalized.push_back('\n');
        }
        else
        {
            normalized.push_back(text[i]);
        }
    }

    return normalized;
}

static std::string ReadAll(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        throw std::runtime_error("missing source file: " + path);
    return NormalizeNewlines(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

static void RequireContains(const std::string& name, const std::string& sourceName, const std::string& source, const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
    {
        std::cerr << "Shared baseline source regression: " << name << " missing " << needle
            << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}


static void RequireOrderedContains(
    const std::string& name,
    const std::string& sourceName,
    const std::string& source,
    const std::vector<std::string>& needles)
{
    ++g_checks;
    size_t pos = 0;
    for (const auto& needle : needles)
    {
        size_t found = source.find(needle, pos);
        if (found == std::string::npos)
        {
            std::cerr << "Shared baseline source regression: " << name << " missing/out-of-order " << needle
                << " in " << sourceName << "\n";
            throw std::runtime_error("source regression");
        }
        pos = found + needle.size();
    }
    std::cout << "ok - " << name << "\n";
}

static void RequireNotContains(const std::string& name, const std::string& sourceName, const std::string& source, const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) != std::string::npos)
    {
        std::cerr << "Shared baseline source regression: " << name << " found forbidden " << needle
            << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

int main()
{
    try
    {
        const std::string appPaths = ReadAll("dependencies/app_paths.inc");
        const std::string baselineApp = ReadAll("dependencies/baseline_app.h");
        const std::string commandLine = ReadAll("dependencies/command_line.inc");
        const std::string sharedCore = ReadAll("dependencies/core.inc");
        const std::string configIni = ReadAll("dependencies/config_ini.inc");
        const std::string logging = ReadAll("dependencies/logging.inc");
        const std::string tray = ReadAll("dependencies/tray.inc");
        const std::string baselineHeader = ReadAll("dependencies/desktop_app_baseline.h");
        const std::string dependenciesReadme = ReadAll("dependencies/README.md");
        const std::string sharedTests = ReadAll("tools/SharedBaselineTests.cpp");
        const std::string discordMain = ReadAll("DiscordRPC/DiscordRPC.cpp");
        const std::string discordCore = ReadAll("DiscordRPC/src/drpc_core.inc");
        const std::string desktopMain = ReadAll("DesktopStub/DesktopStub.cpp");

        RequireContains(
            "supported aggregate include is documented",
            "dependencies/README.md",
            dependenciesReadme,
            "desktop_app_baseline.h` from product translation units. That aggregate");
        RequireContains(
            "individual dependency modules are not promised standalone",
            "dependencies/README.md",
            dependenciesReadme,
            "not guaranteed to be standalone");
        RequireContains(
            "DesktopStub INI dialect is documented",
            "dependencies/README.md",
            dependenciesReadme,
            "write assignments as `\"Name\" = \"Value\"`");

        RequireContains(
            "baseline aggregate owns include order",
            "dependencies/desktop_app_baseline.h",
            baselineHeader,
            "#include \"baseline_app.h\"");
        RequireOrderedContains(
            "baseline aggregate includes shared modules in dependency order",
            "dependencies/desktop_app_baseline.h",
            baselineHeader,
            {
                "#include \"core.inc\"",
                "#include \"app_paths.inc\"",
                "#include \"logging.inc\"",
                "#include \"config_ini.inc\"",
                "#include \"command_line.inc\"",
                "#include \"tray.inc\""
            });

        RequireContains(
            "INI writer emits DesktopStub quoted assignment syntax",
            "dependencies/config_ini.inc",
            configIni,
            "return QuoteIniString(key) + L\" = \" + QuoteIniString(value);");
        RequireContains(
            "shared command-line dependency provides baseline primitives",
            "dependencies/command_line.inc",
            commandLine,
            "ParseIniSetSpec");
        RequireContains(
            "shared command-line dependency provides strict option-value extraction",
            "dependencies/command_line.inc",
            commandLine,
            "TakeCommandLineValue");
        RequireContains(
            "shared tray dependency provides baseline menu primitives",
            "dependencies/tray.inc",
            tray,
            "AppendTrayMenuItem");
        RequireContains(
            "shared tray dependency provides submenu primitives",
            "dependencies/tray.inc",
            tray,
            "BeginTrayNestedMenu");
        RequireContains(
            "shared application baseline exposes reusable subsystem contracts",
            "dependencies/baseline_app.h",
            baselineApp,
            "BuildInstanceIdentity");
        RequireContains(
            "shared application baseline exposes resident shutdown state",
            "dependencies/baseline_app.h",
            baselineApp,
            "ResidentShutdownState");
        RequireContains(
            "shared application baseline exposes stable hash helper",
            "dependencies/baseline_app.h",
            baselineApp,
            "StableHashHex64");
        RequireContains(
            "shared application baseline exposes path-scoped instance identity",
            "dependencies/baseline_app.h",
            baselineApp,
            "BuildPathScopedInstanceIdentity");
        RequireContains(
            "shared config dependency exposes an explicit config store",
            "dependencies/config_ini.inc",
            configIni,
            "class IniConfigStore");
        RequireContains(
            "shared config store supports fresh mutations",
            "dependencies/config_ini.inc",
            configIni,
            "MutateFresh");

        RequireContains(
            "app path helper derives sidecar paths",
            "dependencies/app_paths.inc",
            appPaths,
            "BuildSidecarPathsFromExecutable");
        RequireContains(
            "app path helper supports configured INI overrides",
            "dependencies/app_paths.inc",
            appPaths,
            "configOverride");
        RequireContains(
            "app path helper exposes strict configured INI resolution",
            "dependencies/app_paths.inc",
            appPaths,
            "TryBuildSidecarPathsFromExecutable");
        RequireContains(
            "shared core exposes strict absolute path helper",
            "dependencies/core.inc",
            sharedCore,
            "TryMakeAbsolutePath");
        RequireContains(
            "app path helper uses growable module path lookup",
            "dependencies/app_paths.inc",
            appPaths,
            "GetCurrentExecutablePath");
        RequireContains(
            "app path helper does not use fixed module path buffer",
            "dependencies/app_paths.inc",
            appPaths,
            "buffer.resize(buffer.size() * 2)");
        RequireContains(
            "shared logging helper keeps a bounded recent buffer",
            "dependencies/logging.inc",
            logging,
            "class Utf8Logger");
        RequireContains(
            "shared logging helper writes UTF-8 sidecar log lines",
            "dependencies/logging.inc",
            logging,
            "WideToUtf8(line + L\"\\r\\n\")");
        RequireContains(
            "shared logging helper writes UTF-8 BOM for new files",
            "dependencies/logging.inc",
            logging,
            "{ 0xEF, 0xBB, 0xBF }");
        RequireContains(
            "shared logging helper uses cross-process append locking",
            "dependencies/logging.inc",
            logging,
            "LockFileEx");
        RequireContains(
            "shared logging helper opens append handle with write access for locking",
            "dependencies/logging.inc",
            logging,
            "FILE_APPEND_DATA | FILE_WRITE_DATA | SYNCHRONIZE");
        RequireContains(
            "shared logging helper seeks to EOF after acquiring the append lock",
            "dependencies/logging.inc",
            logging,
            "SetFilePointerEx(file, end, nullptr, FILE_END)");
        RequireContains(
            "shared logging helper loops on partial writes",
            "dependencies/logging.inc",
            logging,
            "WriteAllBytes");
        RequireContains(
            "shared logging helper exposes file write failure state",
            "dependencies/logging.inc",
            logging,
            "LastFileWriteFailed");
        RequireContains(
            "shared logging helper reports write failures to recent log",
            "dependencies/logging.inc",
            logging,
            "Log file write failed");
        RequireContains(
            "shared config mutex uses stable hash helper",
            "dependencies/config_ini.inc",
            configIni,
            "StableHashHex64(MakeAbsolutePath(path))");
        RequireNotContains(
            "shared config dependency does not keep a private mutex hash",
            "dependencies/config_ini.inc",
            configIni,
            std::string("Ini") + "MutexHash");
        RequireNotContains(
            "shared baseline does not expose environment-backed config primitives",
            "dependencies/core.inc",
            sharedCore,
            std::string("Read") + "EnvironmentString");

        RequireContains(
            "INI parser preserves unknown backslash escapes",
            "dependencies/config_ini.inc",
            configIni,
            "out.push_back(ch);");
        RequireContains(
            "INI parser preserves raw Windows-path backslashes by default",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI parser preserves raw Windows path backslashes");
        RequireContains(
            "INI tests lock escaped Windows path write style",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI writer uses DesktopStub quoted assignment and escaped path style");
        RequireContains(
            "INI tests lock app-level template escape separation",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI parser keeps app-level template escapes raw");
        RequireContains(
            "shared tests lock sidecar app path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths derive default INI and log from executable name");
        RequireContains(
            "shared tests lock bounded logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger keeps bounded recent lines");
        RequireContains(
            "shared tests lock concurrent logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger allows concurrent appenders");
        RequireContains(
            "shared tests lock UTF-8 BOM logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger writes BOM for new log files");
        RequireContains(
            "shared tests lock logging failure reporting behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger reports file write failures once");
        RequireContains(
            "shared tests lock logger failure-state reset behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger resets failure state when target changes");
        RequireContains(
            "shared tests lock strict absolute path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "strict absolute path helper rejects empty paths");
        RequireContains(
            "shared tests lock path-scoped identity hashing",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "single-instance identity supports shared path-scoped hashing");

        RequireContains(
            "DiscordRPC help contract allows read-only configured templates",
            "DiscordRPC/DiscordRPC.cpp",
            discordMain,
            "it may read an existing configured");
        RequireContains(
            "DiscordRPC consumes the aggregate desktop baseline",
            "DiscordRPC/DiscordRPC.cpp",
            discordMain,
            "dependencies\\desktop_app_baseline.h");
        RequireContains(
            "DesktopStub consumes the aggregate desktop baseline",
            "DesktopStub/DesktopStub.cpp",
            desktopMain,
            "dependencies\\desktop_app_baseline.h");
        RequireContains(
            "DiscordRPC uses the shared INI store",
            "DiscordRPC/src/drpc_core.inc",
            discordCore,
            "aip::IniConfigStore");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI APIs",
            "DiscordRPC/src/drpc_core.inc",
            discordCore,
            "GetPrivateProfileStringW");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI writers",
            "DiscordRPC/src/drpc_core.inc",
            discordCore,
            "WritePrivateProfileStringW");

        std::cout << "Shared baseline source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

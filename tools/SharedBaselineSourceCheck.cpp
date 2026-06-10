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
        const std::string dpapi = ReadAll("dependencies/dpapi.inc");
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
            "shared command-line dependency provides strict integer parsing",
            "dependencies/command_line.inc",
            commandLine,
            "ParseIntValueInRange");
        RequireContains(
            "shared command-line integer parser checks overflow",
            "dependencies/command_line.inc",
            commandLine,
            "errno == ERANGE");
        RequireContains(
            "shared command-line integer parser uses wide long long parsing",
            "dependencies/command_line.inc",
            commandLine,
            "wcstoll");
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
            "app path helper validates configured INI file paths",
            "dependencies/app_paths.inc",
            appPaths,
            "TryResolveConfigFilePath");
        RequireContains(
            "app path helper rejects directory config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "FILE_ATTRIBUTE_DIRECTORY");
        RequireContains(
            "app path helper rejects reserved device config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "IsReservedWindowsDeviceBaseName");
        RequireContains(
            "app path helper rejects alternate data stream config paths",
            "dependencies/app_paths.inc",
            appPaths,
            "ConfigFileNameContainsColon");
        RequireContains(
            "app path helper documents unchecked raw builder",
            "dependencies/app_paths.inc",
            appPaths,
            "unchecked path builder");
        RequireContains(
            "app path helper exposes safe current-process path builder",
            "dependencies/app_paths.inc",
            appPaths,
            "TryBuildCurrentProcessSidecarPaths");
        RequireContains(
            "safe current-process path builder validates user config overrides",
            "dependencies/app_paths.inc",
            appPaths,
            "TryBuildSidecarPathsFromExecutable(\n        GetCurrentExecutablePath()");
        RequireContains(
            "app path helper exposes executable-side log paths",
            "dependencies/app_paths.inc",
            appPaths,
            "BuildExecutableSidecarLogPath");
        RequireContains(
            "app path helper exposes explicit default log path policy",
            "dependencies/app_paths.inc",
            appPaths,
            "DefaultLogPathPolicy");
        RequireContains(
            "shared core exposes strict absolute path helper",
            "dependencies/core.inc",
            sharedCore,
            "TryMakeAbsolutePath");
        RequireContains(
            "shared core exposes checked UTF-8 conversion",
            "dependencies/core.inc",
            sharedCore,
            "TryWideToUtf8");
        RequireContains(
            "shared core exposes checked UTF-8 decoding",
            "dependencies/core.inc",
            sharedCore,
            "TryUtf8ToWide");
        RequireContains(
            "shared UTF conversion wrappers document empty-on-failure behavior",
            "dependencies/core.inc",
            sharedCore,
            "empty output and conversion failure must be distinguished");
        RequireContains(
            "shared UTF-8 encoder verifies exact second conversion length",
            "dependencies/core.inc",
            sharedCore,
            "if (written != length)");
        RequireContains(
            "shared config decoder verifies exact second decode length",
            "dependencies/config_ini.inc",
            configIni,
            "if (written != need)");
        RequireContains(
            "shared JSON decoding rejects invalid UTF-8",
            "dependencies/core.inc",
            sharedCore,
            "return TryUtf8ToWide(utf8, value);");
        RequireContains(
            "shared JSON decoding rejects raw control characters",
            "dependencies/core.inc",
            sharedCore,
            "uch < 0x20");
        RequireContains(
            "shared JSON string scanner rejects raw control characters",
            "dependencies/core.inc",
            sharedCore,
            "static_cast<unsigned char>(ch) < 0x20");
        RequireContains(
            "shared JSON string scanner rejects invalid escapes",
            "dependencies/core.inc",
            sharedCore,
            "JsonEscapeEnd(json, i, next)");
        RequireContains(
            "shared JSON scanner validates primitive tokens",
            "dependencies/core.inc",
            sharedCore,
            "JsonPrimitiveTokenIsValid");
        RequireContains(
            "shared JSON primitive scanner rejects malformed values",
            "dependencies/core.inc",
            sharedCore,
            "JsonPrimitiveValueEnd");
        RequireContains(
            "shared JSON lookup decodes object keys",
            "dependencies/core.inc",
            sharedCore,
            "DecodeJsonStringUtf8Range(json, pos, stringEnd, decodedKey)");
        RequireContains(
            "shared JSON top-level scanner tracks comma/member state",
            "dependencies/core.inc",
            sharedCore,
            "bool firstMember = true;");
        RequireContains(
            "shared JSON helper exposes try-extract variant",
            "dependencies/core.inc",
            sharedCore,
            "TryExtractJsonStringValue");
        RequireContains(
            "shared core exposes looped file writes",
            "dependencies/core.inc",
            sharedCore,
            "WriteAllBytes");
        RequireContains(
            "shared core exposes lossy UTF-8 conversion for JSON",
            "dependencies/core.inc",
            sharedCore,
            "WideToUtf8Lossy");
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
            "shared logging helper exposes a reusable recent log buffer",
            "dependencies/logging.inc",
            logging,
            "class RecentLogBuffer");
        RequireContains(
            "shared logging helper keeps a bounded recent buffer",
            "dependencies/logging.inc",
            logging,
            "class Utf8Logger");
        RequireContains(
            "shared logging helper writes UTF-8 sidecar log lines",
            "dependencies/logging.inc",
            logging,
            "AppendUtf8LineToFile");
        RequireContains(
            "shared logging helper line appends reuse raw append primitive",
            "dependencies/logging.inc",
            logging,
            "return AppendUtf8TextToFile(filePath, line + L\"\\r\\n\", writeUtf8Bom, lockWaitMs);");
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
            "shared config writer loops on partial writes",
            "dependencies/config_ini.inc",
            configIni,
            "WriteAllBytes(file");
        RequireContains(
            "shared config writer uses checked UTF-8 conversion",
            "dependencies/config_ini.inc",
            configIni,
            "TryWideToUtf8(text, utf8)");
        RequireContains(
            "shared JSON escaping avoids strict conversion data loss",
            "dependencies/core.inc",
            sharedCore,
            "WideToUtf8Lossy(value)");
        RequireContains(
            "shared logging helper exposes bounded append lock wait",
            "dependencies/logging.inc",
            logging,
            "DWORD lockWaitMs = 5000");
        RequireContains(
            "shared JSON field scanner is documented as not a full parser",
            "dependencies/core.inc",
            sharedCore,
            "not a full JSON parser");
        RequireNotContains(
            "shared JSON field scanner does not keep duplicate malformed-string checks",
            "dependencies/core.inc",
            sharedCore,
            "if (stringEnd == std::string::npos)\n        if (stringEnd == std::string::npos)");
        RequireContains(
            "shared logging helper passes bounded lock wait to file appends",
            "dependencies/logging.inc",
            logging,
            "AppendUtf8LineToFile(filePath, line, writeUtf8Bom, lockWaitMs)");
        RequireContains(
            "shared logging helper treats UTF-8 conversion failure as write failure",
            "dependencies/logging.inc",
            logging,
            "TryWideToUtf8(text, utf8)");
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
        RequireContains(
            "shared config mutex supports bounded waits",
            "dependencies/config_ini.inc",
            configIni,
            "WaitForSingleObject(handle_, waitMs)");
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
            "shared DPAPI helper uses checked UTF-8 encoding",
            "dependencies/dpapi.inc",
            dpapi,
            "TryWideToUtf8(secret, utf8)");
        RequireContains(
            "shared DPAPI helper rejects invalid decrypted UTF-8",
            "dependencies/dpapi.inc",
            dpapi,
            "TryUtf8ToWide(utf8, result)");
        RequireContains(
            "shared DPAPI protect handles empty plaintext buffers explicitly",
            "dependencies/dpapi.inc",
            dpapi,
            "emptyPlaintextSentinel");
        RequireContains(
            "shared DPAPI protect guards empty encrypted buffers",
            "dependencies/dpapi.inc",
            dpapi,
            "cipher.cbData == 0 || cipher.pbData == nullptr");
        RequireContains(
            "shared DPAPI helper handles empty decrypted buffers safely",
            "dependencies/dpapi.inc",
            dpapi,
            "plain.cbData != 0");

        RequireContains(
            "shared tests cover bounded INI waits",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI write mutex supports bounded waits");
        RequireContains(
            "shared tests cover strict integer overflow rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "integer parser rejects junk, overflow, and out-of-range values");
        RequireContains(
            "shared tests cover directory config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects existing directories");
        RequireContains(
            "shared tests cover reserved config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects reserved Windows device names");
        RequireContains(
            "shared tests cover alternate data stream config path rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "config path helper rejects alternate data stream names");
        RequireContains(
            "shared tests cover invalid JSON primitive rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON lookup rejects invalid primitive tokens before later fields");
        RequireContains(
            "shared tests cover invalid JSON UTF-8 rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string decoding rejects invalid UTF-8 bytes");
        RequireContains(
            "shared tests cover raw JSON control-character rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string scanning rejects unescaped control characters");
        RequireContains(
            "shared tests cover invalid JSON escape rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON string scanning rejects invalid escape sequences");
        RequireContains(
            "shared tests cover escaped JSON object keys",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON lookup decodes escaped object keys");
        RequireContains(
            "shared tests cover JSON try-extract helper",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON try-extract distinguishes empty strings from missing or invalid fields");
        RequireContains(
            "shared tests cover DPAPI invalid UTF-16 rejection",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI protect rejects invalid UTF-16 before encrypting");
        RequireContains(
            "shared tests cover DPAPI empty-secret round trip",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "DPAPI empty secret round trip is safe");
        RequireContains(
            "shared baseline test script reports test exit code",
            "tools/TestSharedBaseline.cmd",
            ReadAll("tools/TestSharedBaseline.cmd"),
            "SharedBaselineTests exit code");
        RequireContains(
            "shared tests cover UTF-8 conversion failure",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger fails non-empty text it cannot encode");
        RequireContains(
            "shared tests cover INI writer UTF-8 conversion failure",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "INI file writer fails non-empty text it cannot encode");
        RequireContains(
            "shared tests cover JSON invalid UTF-16 behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "JSON escaping does not silently empty invalid UTF-16");
        RequireContains(
            "shared tests cover explicit default log policy",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths expose explicit executable-side default log policy");
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
            "shared tests lock executable-side log path behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "sidecar paths can preserve executable-side default log behavior");
        RequireContains(
            "shared tests lock bounded logging behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger keeps bounded recent lines");
        RequireContains(
            "shared tests lock reusable recent log behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared recent log buffer preserves DesktopStub tray-log behavior");
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
            "shared tests lock logger failure reporting after recovery",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger reports a new failure after recovery");
        RequireContains(
            "shared tests lock bounded logger append wait behavior",
            "tools/SharedBaselineTests.cpp",
            sharedTests,
            "shared UTF-8 logger uses bounded append lock wait");
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

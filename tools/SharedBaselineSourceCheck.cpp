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
        const std::string configIni = ReadAll("dependencies/config_ini.inc");
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
        RequireContains(
            "baseline aggregate includes INI before command-line consumers",
            "dependencies/desktop_app_baseline.h",
            baselineHeader,
            "#include \"config_ini.inc\"\n#include \"command_line.inc\"");

        RequireContains(
            "INI writer emits DesktopStub quoted assignment syntax",
            "dependencies/config_ini.inc",
            configIni,
            "return QuoteIniString(key) + L\" = \" + QuoteIniString(value);");
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

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
    {
        std::string portablePath = path;
        for (char& ch : portablePath)
        {
            if (ch == '\\')
                ch = '/';
        }
        in.open(portablePath, std::ios::binary);
    }
    if (!in)
        throw std::runtime_error("missing source file: " + path);
    return NormalizeNewlines(std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()));
}

static void RequireContains(const std::string& name, const std::string& sourceName, const std::string& source, const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
    {
        std::cerr << "DiscordRPC source regression: " << name << " missing " << needle
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
        std::cerr << "DiscordRPC source regression: " << name << " found forbidden " << needle
            << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

int main()
{
    try
    {
        const std::string sharedCore = ReadAll("..\\dependencies\\core.inc");
        const std::string discordMain = ReadAll("DiscordRPC.cpp");
        const std::string core = ReadAll("src\\drpc_core.inc");
        const std::string commandLine = ReadAll("src\\drpc_command_line.inc");
        const std::string defaults = ReadAll("src\\drpc_config_defaults.inc");
        const std::string app = ReadAll("src\\drpc_app.inc");
        const std::string ipc = ReadAll("src\\drpc_ipc.inc");
        const std::string gateway = ReadAll("src\\drpc_gateway.inc");
        const std::string tray = ReadAll("src\\drpc_tray.inc");
        const std::string types = ReadAll("src\\drpc_types.inc");

        RequireContains(
            "DiscordRPC consumes aggregate desktop baseline",
            "DiscordRPC.cpp",
            discordMain,
            "dependencies\\desktop_app_baseline.h");
        RequireContains(
            "DiscordRPC uses shared INI config store",
            "src\\drpc_core.inc",
            core,
            "aip::IniConfigStore");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI readers",
            "DiscordRPC sources",
            core + "\n" + app + "\n" + defaults,
            "GetPrivateProfileStringW");
        RequireNotContains(
            "DiscordRPC does not use Win32 profile INI writers",
            "DiscordRPC sources",
            core + "\n" + app + "\n" + defaults,
            "WritePrivateProfileStringW");

        RequireContains(
            "DiscordRPC uses shared command-line setting primitive",
            "src\\drpc_types.inc + src\\drpc_command_line.inc",
            types + "\n" + commandLine,
            "using CommandLineSetting = aip::IniSetting");
        RequireContains(
            "DiscordRPC uses shared INI override parser",
            "src\\drpc_command_line.inc",
            commandLine,
            "aip::ParseIniSetSpec");
        RequireContains(
            "DiscordRPC uses shared JSON scanner and decoder",
            "DiscordRPC JSON sources",
            sharedCore + "\n" + core,
            "using aip::ExtractJsonStringValue");
        RequireContains(
            "DiscordRPC uses shared JSON value ending",
            "DiscordRPC JSON sources",
            sharedCore + "\n" + core,
            "using aip::JsonValueEnd");
        RequireNotContains(
            "DiscordRPC does not shadow shared JSON primitives",
            "src\\drpc_core.inc",
            core,
            "static size_t JsonStringEnd");
        RequireNotContains(
            "DiscordRPC does not shadow shared JSON field lookup",
            "src\\drpc_core.inc",
            core,
            "static bool FindJsonFieldValue");

        RequireContains(
            "DiscordRPC uses shared tray append primitive",
            "src\\drpc_tray.inc",
            tray,
            "aip::AppendTrayMenuItem");
        RequireContains(
            "DiscordRPC uses shared tray section layout",
            "src\\drpc_tray.inc",
            tray,
            "aip::TraySectionLayout");
        RequireContains(
            "DiscordRPC supports flat/dropdown tray layout setting",
            "DiscordRPC tray/config sources",
            defaults + "\n" + tray,
            "show_menu_as_dropdown");

        RequireContains(
            "DiscordRPC help remains read-only side-effect-free",
            "DiscordRPC.cpp",
            discordMain,
            "must not create, normalize, repair, or write");
        RequireContains(
            "DiscordRPC help may read configured template",
            "DiscordRPC help/config sources",
            discordMain + "\n" + defaults,
            "DefaultHelpText");
        RequireContains(
            "DiscordRPC registers taskbar recreation message",
            "DiscordRPC app/tray sources",
            app + "\n" + tray,
            "aip::RegisterTaskbarCreatedMessage");
        RequireContains(
            "DiscordRPC signals running instance through shared primitive",
            "DiscordRPC app sources",
            app,
            "aip::SignalInstanceWindow");
        RequireContains(
            "DiscordRPC rejects empty --ini paths",
            "src\\drpc_command_line.inc",
            commandLine,
            "--ini requires a non-empty path.");
        RequireNotContains(
            "DiscordRPC removes environment-backed token CLI",
            "src\\drpc_command_line.inc",
            commandLine,
            std::string("--token-") + "env");
        RequireNotContains(
            "DiscordRPC removes environment-backed token configuration key",
            "DiscordRPC config sources",
            defaults + "\n" + commandLine + "\n" + core + "\n" + app + "\n" + gateway,
            std::string("token_") + "env");
        RequireNotContains(
            "DiscordRPC removes env-token shorthand",
            "src\\drpc_core.inc",
            core,
            std::string("env") + ":");
        RequireNotContains(
            "DiscordRPC does not read environment variables as config",
            "src\\drpc_core.inc",
            core,
            std::string("Read") + "EnvironmentString");
        RequireNotContains(
            "DiscordRPC does not keep environment token resolver",
            "src\\drpc_core.inc",
            core,
            std::string("IsToken") + "EnvironmentReference");
        RequireContains(
            "DiscordRPC Gateway missing-token text is INI-only",
            "DiscordRPC Gateway/app/default strings",
            defaults + "\n" + app + "\n" + gateway,
            "token_protected or token");
        RequireContains(
            "DiscordRPC derives sidecar paths through the shared helper",
            "src\\drpc_core.inc",
            core,
            "aip::BuildCurrentProcessSidecarPaths(APP_NAME)");
        RequireContains(
            "DiscordRPC uses shared UTF-8 logger",
            "DiscordRPC.cpp + src\\drpc_core.inc",
            discordMain + "\n" + core,
            "aip::Utf8Logger");
        RequireContains(
            "DiscordRPC exposes bounded log append wait in INI defaults",
            "src\\drpc_config_defaults.inc",
            defaults,
            "log_append_lock_wait_ms");
        RequireContains(
            "DiscordRPC exposes bounded log append wait on command line",
            "src\\drpc_command_line.inc",
            commandLine,
            "--log-lock-wait-ms");
        RequireContains(
            "DiscordRPC exposes bounded log append wait through tray presets",
            "src\\drpc_tray.inc",
            tray,
            "ID_LOG_LOCK_WAIT_5000");
        RequireContains(
            "DiscordRPC passes bounded lock wait to shared UTF-8 logger",
            "src\\drpc_core.inc",
            core,
            "options.lockWaitMs = g_logAppendLockWaitMs.load()");
        RequireContains(
            "DiscordRPC single-instance scope follows effective INI path",
            "src\\drpc_core.inc",
            core,
            "std::wstring scope = MakeAbsolutePath(g_iniPath);");
        RequireContains(
            "DiscordRPC single-instance identity uses shared path-scoped helper",
            "src\\drpc_core.inc",
            core,
            "aip::BuildPathScopedInstanceIdentity");
        RequireNotContains(
            "DiscordRPC single-instance scope does not add exe base name",
            "src\\drpc_core.inc",
            core,
            "MakeAbsolutePath(g_iniPath) + L\"|\" + g_exeBaseName");

        RequireContains(
            "Discord Gateway connection waits are cancellable",
            "src\\drpc_gateway.inc",
            gateway,
            "RunCancellableWinHttpRequest");
        RequireContains(
            "Discord Gateway cancellation closes pending request handles",
            "src\\drpc_gateway.inc",
            gateway,
            "WinHttpCloseHandle(request)");
        RequireContains(
            "Discord IPC overlapped cancellation drains completion",
            "src\\drpc_ipc.inc",
            ipc,
            "GetOverlappedResult(handle, &overlapped");

        std::cout << "DiscordRPC source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

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
        const std::string presence = ReadAll("src\\drpc_presence.inc");
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
        RequireContains(
            "DiscordRPC integer config reads use shared strict parser",
            "src\\drpc_core.inc",
            core,
            "return aip::ParseIntValue(raw, parsed) ? parsed : fallback;");
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
            "DiscordRPC help documents resident exit",
            "src\\drpc_config_defaults.inc",
            defaults,
            "--exit");
        RequireContains(
            "DiscordRPC help documents advanced logging toggles",
            "src\\drpc_config_defaults.inc",
            defaults,
            "--logging / --no-logging");
        RequireContains(
            "DiscordRPC help documents strict boolean setting helper",
            "src\\drpc_config_defaults.inc",
            defaults,
            "--bool Section.Key=Value");
        RequireContains(
            "DiscordRPC help documents typed set validation",
            "src\\drpc_config_defaults.inc",
            defaults,
            "known typed keys are validated");
        RequireContains(
            "DiscordRPC help documents client-id format",
            "src\\drpc_config_defaults.inc",
            defaults,
            "17-20 decimal digits");
        RequireContains(
            "DiscordRPC help documents update interval range",
            "src\\drpc_config_defaults.inc",
            defaults,
            "1-86400");
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
            "requires a non-empty path.");
        RequireContains(
            "DiscordRPC validates positional config paths like --ini",
            "src\\drpc_command_line.inc",
            commandLine,
            "ResolveCommandLineConfigPath(arg, L\"positional config path\"");
        RequireContains(
            "DiscordRPC rejects directory config paths early",
            "src\\drpc_command_line.inc",
            commandLine,
            "aip::TryResolveConfigFilePath(rawPath, resolvedPath, &pathError)");
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
            "DiscordRPC exposes bounded INI write wait in INI defaults",
            "src\\drpc_config_defaults.inc",
            defaults,
            "ini_write_lock_wait_ms");
        RequireContains(
            "DiscordRPC passes bounded INI wait to shared config store",
            "src\\drpc_core.inc + src\\drpc_config_defaults.inc",
            core + "\n" + defaults,
            "g_iniWriteLockWaitMs.load()");
        RequireContains(
            "DiscordRPC exposes bounded log append wait on command line",
            "src\\drpc_command_line.inc",
            commandLine,
            "--log-lock-wait-ms");
        RequireContains(
            "DiscordRPC validates bounded log append wait command-line values",
            "src\\drpc_command_line.inc",
            commandLine,
            "aip::ParseIntValueInRange(value, 0, 60000, parsed)");
        RequireContains(
            "DiscordRPC validates update interval command-line values",
            "src\\drpc_command_line.inc",
            commandLine,
            "ValidateUpdateIntervalValue(value, error)");
        RequireContains(
            "DiscordRPC bounds update interval command-line values",
            "src\\drpc_command_line.inc",
            commandLine,
            "aip::ParseIntValueInRange(value, 1, 86400, seconds)");
        RequireContains(
            "DiscordRPC validates client-id command-line values",
            "src\\drpc_command_line.inc",
            commandLine,
            "ValidateClientIdValue(value, error)");
        RequireContains(
            "DiscordRPC trims client-id before saving",
            "src\\drpc_command_line.inc",
            commandLine,
            "AddSetting(options, L\"general\", L\"client_id\", Trim(value))");
        RequireContains(
            "DiscordRPC runtime log append wait override ignores invalid integers",
            "src\\drpc_core.inc",
            core,
            "aip::ParseIntValue(setting.value, lockWaitMs)");
        RequireContains(
            "DiscordRPC exposes bounded log append wait through tray presets",
            "src\\drpc_tray.inc",
            tray,
            "ID_LOG_LOCK_WAIT_5000");
        RequireContains(
            "DiscordRPC tray log-lock presets use live reload path",
            "src\\drpc_tray.inc",
            tray,
            "SetTraySetting(L\"app\", L\"log_append_lock_wait_ms\"");
        RequireContains(
            "DiscordRPC declares bounded log append wait tray command identifiers",
            "src\\drpc_tray.inc",
            tray,
            "static constexpr UINT ID_LOG_LOCK_WAIT_0");
        RequireContains(
            "DiscordRPC declares default bounded log append wait tray command identifier",
            "src\\drpc_tray.inc",
            tray,
            "static constexpr UINT ID_LOG_LOCK_WAIT_5000");
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
        RequireContains(
            "DiscordRPC configures control identity before optional single-instance mutex",
            "src\\drpc_core.inc",
            core,
            "ConfigureSingleInstanceIdentity();\n\n    if (!IniReadB(L\"app\", L\"single_instance\", true))");
        RequireContains(
            "DiscordRPC configures control identity before control window path",
            "src\\drpc_app.inc",
            app,
            "ConfigureSingleInstanceIdentity();\n\n    if (options.requestExit)");
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
        RequireContains(
            "DiscordRPC implements gateway supported switch",
            "src\\drpc_app.inc + src\\drpc_config_defaults.inc",
            app + "\n" + defaults,
            "IniReadB(L\"gateway\", L\"supported\", true)");
        RequireContains(
            "DiscordRPC guards Gateway activity JSON object insertion",
            "src\\drpc_gateway.inc",
            gateway,
            "activity.empty() || activity.front() != '{'");
        RequireContains(
            "DiscordRPC exposes shared JSON string decoder",
            "src\\drpc_core.inc",
            core,
            "using aip::DecodeJsonStringRange;");
        RequireContains(
            "DiscordRPC decodes Gateway asset JSON strings before lookup",
            "src\\drpc_gateway.inc",
            gateway,
            "DecodeJsonStringRange(assets, valueStart, valueEnd, decoded)");
        RequireContains(
            "DiscordRPC Gateway asset lookup uses decoded strings",
            "src\\drpc_gateway.inc",
            gateway,
            "assetIdCache_.find(decoded)");
        RequireNotContains(
            "DiscordRPC Gateway asset resolution avoids unchecked UTF-8 conversion",
            "src\\drpc_gateway.inc",
            gateway,
            "Utf8ToWide(raw)");
        RequireNotContains(
            "DiscordRPC does not expose unused rpc_restarted_message default",
            "src\\drpc_config_defaults.inc",
            defaults,
            "rpc_restarted_message");
        RequireContains(
            "DiscordRPC caches compiled censor regex rules",
            "src\\drpc_presence.inc",
            presence,
            "GetCompiledPatternCensorRules");
        RequireContains(
            "DiscordRPC reuses compiled censor regex snapshots without copying regex objects",
            "src\\drpc_presence.inc",
            presence,
            "std::shared_ptr<const std::vector<CompiledCensorRegexRule>>");
        RequireContains(
            "DiscordRPC reuses full/word censor rule snapshots without per-update vector copies",
            "src\\drpc_presence.inc",
            presence,
            "std::shared_ptr<const std::vector<ReplacementRule>>");
        RequireContains(
            "DiscordRPC reuses censor rule-order snapshots without empty-cache startup bugs",
            "src\\drpc_presence.inc",
            presence,
            "std::shared_ptr<const std::vector<CensorRuleKind>>");
        RequireNotContains(
            "DiscordRPC does not compile censor regex inside every title update",
            "src\\drpc_presence.inc",
            presence,
            "std::wregex pattern(rule.first");
        RequireContains(
            "DiscordRPC implements configured censor rule order",
            "src\\drpc_presence.inc",
            presence,
            "GetCachedCensorRuleOrder(IniReadS(L\"censor_map\", L\"rule_order\"");
        RequireContains(
            "DiscordRPC initializes empty censor rule_order through parser fallback",
            "src\\drpc_presence.inc",
            presence,
            "rawOrder != g_cachedRuleOrderRaw || !g_cachedRuleOrder");
        RequireContains(
            "DiscordRPC caches full replacement censor rules",
            "src\\drpc_presence.inc",
            presence,
            "GetCachedFullCensorRules");
        RequireContains(
            "DiscordRPC caches word replacement censor rules",
            "src\\drpc_presence.inc",
            presence,
            "GetCachedWordCensorRules");
        RequireContains(
            "DiscordRPC caches pattern-on-raw censor option",
            "src\\drpc_presence.inc",
            presence,
            "GetCachedApplyPatternOnRaw");
        RequireContains(
            "DiscordRPC pattern-on-raw matching preserves earlier censor stages",
            "src\\drpc_presence.inc",
            presence,
            "if (applyPatternOnRaw && !std::regex_search(rawTitle, rule.pattern))");
        RequireContains(
            "DiscordRPC reports unknown censor rule-order tokens only when config changes",
            "src\\drpc_presence.inc",
            presence,
            "if (rawOrder != g_cachedRuleOrderRaw || !g_cachedRuleOrder)");
        RequireContains(
            "DiscordRPC reports censor regex replacement failures once per config value",
            "src\\drpc_presence.inc",
            presence,
            "ReportPatternRuntimeFailureOnce");

        std::cout << "DiscordRPC source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

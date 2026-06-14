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
            std::cerr << "DiscordRPC source regression: " << name << " missing/out-of-order "
                << needle << " in " << sourceName << "\n";
            throw std::runtime_error("source regression");
        }
        pos = found + needle.size();
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
        const std::string build = ReadAll("build.cmd");
        const std::string resource = ReadAll("DiscordRPC.rc");
        const std::string releaseVersion = ReadAll("..\\dependencies\\release_version.inc");

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
            "DiscordRPC imports shared try-extract JSON helper for protocol logic",
            "DiscordRPC JSON sources",
            sharedCore + "\n" + core,
            "using aip::TryExtractJsonStringValue");
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
        RequireOrderedContains(
            "DiscordRPC uses the complete DesktopStub root header",
            "src\\drpc_tray.inc",
            tray,
            {
                "ReleaseVersionDisplayText",
                "aip::AppendBaselineTrayMenuHeader",
                "ID_TOGGLE_MENU_DROPDOWN",
                "ID_TRAY_REFRESH",
                "aip::TraySectionLayout sectionLayout",
                "HMENU general = beginSection"
            });
        RequireNotContains(
            "DiscordRPC does not put dropdown toggle inside General",
            "src\\drpc_tray.inc",
            tray,
            "AppendChecked(general, ID_TOGGLE_MENU_DROPDOWN");
        RequireNotContains(
            "DiscordRPC removes its redundant root status row",
            "src\\drpc_config_defaults.inc",
            defaults,
            "menu_status");
        RequireContains(
            "DiscordRPC CLI reports the shared release tag and version",
            "DiscordRPC.cpp",
            discordMain,
            "aip::ReleaseVersionDisplayText()");
        RequireContains(
            "DiscordRPC startup log reports the shared release tag and version",
            "src\\drpc_app.inc",
            app,
            "DiscordRPC version:");
        RequireContains(
            "DiscordRPC embeds a Win32 release version resource",
            "DiscordRPC.rc",
            resource,
            "release_version_resource.rc.inc");
        RequireContains(
            "DiscordRPC build resolves tag-derived release metadata",
            "build.cmd",
            build,
            "resolve_release_version.ps1");
        RequireContains(
            "DiscordRPC runtime uses the generated release tag",
            "DiscordRPC.cpp + dependencies/release_version.inc",
            discordMain + "\n" + releaseVersion,
            "DiscordRPCVersionDefines.inc");

        RequireContains(
            "DiscordRPC uses a GUI-subsystem entry point",
            "DiscordRPC.cpp",
            discordMain,
            "int APIENTRY wWinMain");
        RequireContains(
            "DiscordRPC build links the GUI subsystem",
            "build.cmd",
            build,
            "/SUBSYSTEM:WINDOWS");
        RequireNotContains(
            "DiscordRPC build no longer creates a startup console",
            "build.cmd",
            build,
            "/SUBSYSTEM:CONSOLE");
        RequireContains(
            "DiscordRPC allocates its console only when enabled",
            "src\\drpc_core.inc",
            core,
            "if (!AllocConsole())");
        RequireContains(
            "DiscordRPC uses shared command-line output",
            "src\\drpc_core.inc",
            core,
            "aip::WriteCommandLineText(line, error)");
        RequireContains(
            "DiscordRPC mirrors logger output to an enabled console",
            "src\\drpc_core.inc",
            core,
            "options.consoleEnabled = g_consoleEnabled.load()");

        RequireContains(
            "DiscordRPC force shutdown reports cleanup-state write failures",
            "src\\drpc_tray.inc",
            tray,
            "Force shutdown state could not be recorded before process exit.");
        RequireContains(
            "DiscordRPC force shutdown warning clear reports write failures",
            "src\\drpc_app.inc",
            app,
            "Clearing force-shutdown pending cleanup state");

        RequireContains(
            "DiscordRPC help remains read-only side-effect-free",
            "DiscordRPC.cpp",
            discordMain,
            "must not create, normalize, repair, or write");
        RequireContains(
            "DiscordRPC help/version mode detects side-effect-free parsing early",
            "src\\drpc_command_line.inc",
            commandLine,
            "bool sideEffectFreeMode = false;");
        RequireContains(
            "DiscordRPC help/version mode does not reject blocked --set values",
            "src\\drpc_command_line.inc",
            commandLine,
            "!sideEffectFreeMode && !ValidateCommandLineSettingValue(setting, error)");
        RequireContains(
            "DiscordRPC help/version mode does not queue --set writes",
            "src\\drpc_command_line.inc",
            commandLine,
            "if (!sideEffectFreeMode)\n            {\n                AddSetting(options, setting.section, setting.key, setting.value);\n            }");
        RequireContains(
            "DiscordRPC help/version mode does not validate or queue --bool writes",
            "src\\drpc_command_line.inc",
            commandLine,
            "if (!sideEffectFreeMode)\n            {\n                bool parsed = false;");
        RequireContains(
            "DiscordRPC command-line INI setting overrides de-duplicate by key",
            "src\\drpc_command_line.inc",
            commandLine,
            "for (auto& setting : options.settings)");
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
            "DiscordRPC reports taskbar recreation registration failures",
            "src\\drpc_app.inc",
            app,
            "Could not register the TaskbarCreated message:");
        RequireContains(
            "DiscordRPC reports shell-open failures",
            "src\\drpc_tray.inc",
            tray,
            "Could not open shell path");
        RequireContains(
            "DiscordRPC reports tray icon creation failures",
            "src\\drpc_tray.inc",
            tray,
            "Could not create tray icon:");
        RequireContains(
            "DiscordRPC reports tray version negotiation failures",
            "src\\drpc_tray.inc",
            tray,
            "Could not enable tray icon version 4 behavior:");
        RequireContains(
            "DiscordRPC reports tray balloon delivery failures",
            "src\\drpc_tray.inc",
            tray,
            "Could not show tray notification:");
        RequireContains(
            "DiscordRPC validates control-window class registration",
            "src\\drpc_tray.inc",
            tray,
            "GetLastError() != ERROR_CLASS_ALREADY_EXISTS");
        RequireContains(
            "DiscordRPC handles main message-loop failures",
            "src\\drpc_app.inc",
            app,
            "if (messageResult == -1)");
        RequireContains(
            "DiscordRPC reports main message-loop failure as nonzero",
            "src\\drpc_app.inc",
            app,
            "return messageLoopFailed ? 1 : static_cast<int>(ctx.workerExitCode);");
        RequireContains(
            "DiscordRPC worker completion cannot be lost to a full message queue",
            "src\\drpc_app.inc",
            app,
            "SendMessageW(ctx.hwnd, WM_DRPC_WORKER_FINISHED");
        RequireContains(
            "DiscordRPC console shutdown falls back when UI signaling fails",
            "src\\drpc_app.inc",
            app,
            "if (!PostMessageW(hwnd, WM_DRPC_REQUEST_SHUTDOWN, ctrlType, 0))");
        RequireContains(
            "DiscordRPC sends acknowledged running-instance requests",
            "DiscordRPC app sources",
            app,
            "aip::SendInstanceWindowRequest");
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
        RequireContains(
            "DiscordRPC keeps explicit token out of generic command-line INI writes",
            "src\\drpc_command_line.inc + src\\drpc_types.inc",
            commandLine + "\n" + types,
            "tokenToProtect");
        RequireNotContains(
            "DiscordRPC --token does not persist plaintext token before DPAPI protection",
            "src\\drpc_command_line.inc",
            commandLine,
            "AddSetting(options, L\"general\", L\"token\"");
        RequireContains(
            "DiscordRPC --set blocks plaintext token persistence",
            "src\\drpc_command_line.inc",
            commandLine,
            "--set general.token is blocked because it would persist a plaintext Discord token. Use --token instead.");
        RequireContains(
            "DiscordRPC writes explicit tokens directly to DPAPI-protected config",
            "DiscordRPC.cpp + src\\drpc_core.inc",
            discordMain + "\n" + core,
            "ProtectExplicitDiscordTokenInConfig(options.tokenToProtect)");
        RequireNotContains(
            "DiscordRPC runtime token resolver does not fall back to plaintext token",
            "src\\drpc_core.inc",
            core,
            "return Trim(IniReadS(L\"general\", L\"token\", L\"\"));");
        RequireContains(
            "DiscordRPC --bool is restricted to known boolean INI keys",
            "src\\drpc_command_line.inc",
            commandLine,
            "--bool only accepts known boolean INI keys.");
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
            "DiscordRPC Gateway missing-token text points users at protected token storage",
            "DiscordRPC Gateway/app/default strings",
            defaults + "\n" + app + "\n" + gateway,
            "Gateway mode needs DPAPI token_protected");
        RequireContains(
            "DiscordRPC auto Gateway fallback explains missing token after IPC failure",
            "DiscordRPC app/default strings",
            defaults + "\n" + app,
            "Gateway fallback is enabled by transport_mode=auto");
        RequireNotContains(
            "DiscordRPC default INI no longer creates a plaintext token key",
            "src\\drpc_config_defaults.inc",
            defaults,
            "{ L\"general\", L\"token\", L\"\" }");
        RequireContains(
            "DiscordRPC --token help documents direct protected storage",
            "src\\drpc_config_defaults.inc",
            defaults,
            "Store token as DPAPI-protected token_protected");
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
            "DiscordRPC validates known typed --set keys",
            "src\\drpc_command_line.inc",
            commandLine,
            "ValidateCommandLineSettingValue(setting, error)");
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
            "DiscordRPC resolves relative log paths beside the effective INI profile",
            "src\\drpc_core.inc",
            core,
            "std::wstring configDir = GetDirectoryName(MakeAbsolutePath(g_iniPath));");
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
            "DiscordRPC uses a fixed application control message like the shared baseline consumers",
            "DiscordRPC.cpp",
            discordMain,
            "static constexpr UINT WM_DRPC_INSTANCE_CONTROL = WM_APP + 65;");
        RequireNotContains(
            "DiscordRPC does not dynamically register its instance control message",
            "src\\drpc_core.inc",
            core,
            "RegisterWindowMessageW");
        RequireContains(
            "DiscordRPC sends the fixed instance control message through the acknowledged helper",
            "src\\drpc_app.inc",
            app,
            "WM_DRPC_INSTANCE_CONTROL");
        RequireContains(
            "DiscordRPC dispatches the fixed instance control message",
            "src\\drpc_tray.inc",
            tray,
            "if (msg == WM_DRPC_INSTANCE_CONTROL)");
        RequireContains(
            "DiscordRPC acknowledges handled instance control requests",
            "src\\drpc_tray.inc",
            tray,
            "return aip::INSTANCE_REQUEST_HANDLED;");
        RequireContains(
            "DiscordRPC logs when its resident control window is ready",
            "src\\drpc_app.inc",
            app,
            "Resident control window ready.");
        RequireContains(
            "DiscordRPC exits when single-instance mutex creation fails",
            "src\\drpc_core.inc",
            core,
            "Could not create single-instance mutex");
        RequireContains(
            "DiscordRPC does not continue without single-instance protection after mutex creation failure",
            "src\\drpc_core.inc",
            core,
            "LogError(L\"Could not create single-instance mutex: \" + GetLastErrorText(GetLastError()));\n        return false;");
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
            "Discord Gateway optional int parsing avoids sentinel values",
            "src\\drpc_gateway.inc",
            gateway,
            "JsonTryExtractInt");
        RequireNotContains(
            "Discord Gateway optional int parsing does not use INT_MIN sentinel",
            "src\\drpc_gateway.inc",
            gateway,
            "JsonExtractInt(json, key, INT_MIN)");
        RequireContains(
            "Discord Gateway protocol dispatch uses try-extract JSON strings",
            "src\\drpc_gateway.inc",
            gateway,
            "TryExtractJsonStringValue(json, \"t\", eventName)");
        RequireContains(
            "Discord Gateway asset parsing uses try-extract JSON strings",
            "src\\drpc_gateway.inc",
            gateway,
            "TryExtractJsonStringValue(entry, \"id\", assetId)");
        RequireContains(
            "Discord Gateway asset cache rejects non-array responses",
            "src\\drpc_gateway.inc",
            gateway,
            "Gateway asset ID response was not a JSON array");
        RequireContains(
            "Discord Gateway asset cache does not cache malformed responses",
            "src\\drpc_gateway.inc",
            gateway,
            "asset IDs were not cached for application");
        RequireContains(
            "Discord Gateway connection waits are cancellable",
            "src\\drpc_gateway.inc",
            gateway,
            "RunCancellableWinHttpRequest");
        RequireContains(
            "Discord Gateway requires configured WinHTTP timeouts",
            "src\\drpc_gateway.inc",
            gateway,
            "ThrowWinHttp(\"WinHttpSetTimeouts(Gateway)\")");
        RequireContains(
            "Discord asset requests require configured WinHTTP timeouts",
            "src\\drpc_gateway.inc",
            gateway,
            "ThrowWinHttp(\"WinHttpSetTimeouts(asset request)\")");
        RequireContains(
            "Discord Gateway rejects oversized outbound payloads before Discord closes the connection",
            "src\\drpc_gateway.inc",
            gateway,
            "Gateway payload exceeds Discord's 4096-byte limit");
        RequireContains(
            "Discord Gateway bounds inbound message assembly",
            "src\\drpc_gateway.inc",
            gateway,
            "Gateway inbound payload exceeds the 1 MiB safety limit.");
        RequireContains(
            "Discord Gateway bounds REST asset responses",
            "src\\drpc_gateway.inc",
            gateway,
            "Discord asset response exceeds the 4 MiB safety limit.");
        RequireContains(
            "Discord Gateway treats requested-heartbeat send failures as disconnects",
            "src\\drpc_gateway.inc",
            gateway,
            "Gateway-requested heartbeat failed:");
        RequireContains(
            "Discord Gateway randomizes the first heartbeat within the negotiated interval",
            "src\\drpc_gateway.inc",
            gateway,
            "std::uniform_int_distribution<int> initialDelay");
        RequireContains(
            "Discord Gateway cleans up when reader or heartbeat thread startup fails",
            "src\\drpc_gateway.inc",
            gateway,
            "catch (...)\n        {\n            Close();\n            throw;\n        }");
        RequireNotContains(
            "Discord Gateway does not send an unconditional immediate first heartbeat",
            "src\\drpc_gateway.inc",
            gateway,
            "SendHeartbeat();\n        heartbeat_ = std::thread");
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
            "Discord IPC handshake parses READY dispatch structurally",
            "src\\drpc_ipc.inc",
            ipc,
            "TryExtractJsonStringValue(response, \"evt\", eventName)");
        RequireContains(
            "Discord IPC request responses are correlated by nonce",
            "src\\drpc_presence.inc + src\\drpc_ipc.inc",
            presence + "\n" + ipc,
            "ReadResponseForNonce(nonce, options_.responseTimeoutMs)");
        RequireContains(
            "Discord IPC ignores unrelated frames under one response deadline",
            "src\\drpc_ipc.inc",
            ipc,
            "ReadFrameUntil(deadline)");
        RequireContains(
            "Discord IPC bounds outbound frames like inbound frames",
            "src\\drpc_ipc.inc",
            ipc,
            "Discord IPC outbound frame exceeds the 1 MiB safety limit.");
        RequireNotContains(
            "DiscordRPC removes unused JSON object-start scanner",
            "src\\drpc_core.inc",
            core,
            "FindJsonObjectStart");
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
            "foreground process image paths grow beyond MAX_PATH",
            "src\\drpc_presence.inc",
            presence,
            "buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)))");
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

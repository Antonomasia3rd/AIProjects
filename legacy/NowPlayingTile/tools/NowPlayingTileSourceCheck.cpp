#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

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
    std::string portablePath = path;
    for (char& ch : portablePath)
        if (ch == '\\') ch = '/';
    std::ifstream in(portablePath, std::ios::binary);
    if (!in)
        throw std::runtime_error("missing source file: " + path);
    return NormalizeNewlines(std::string(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>()));
}

static void RequireContains(
    const std::string& name,
    const std::string& sourceName,
    const std::string& source,
    const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) == std::string::npos)
    {
        std::cerr << "NowPlayingTile source regression: " << name << " missing "
            << needle << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

static void RequireBefore(
    const std::string& name,
    const std::string& sourceName,
    const std::string& source,
    const std::string& first,
    const std::string& second)
{
    ++g_checks;
    size_t firstPos = source.find(first);
    size_t secondPos = source.find(second);
    if (firstPos == std::string::npos || secondPos == std::string::npos || firstPos >= secondPos)
    {
        std::cerr << "NowPlayingTile source regression: " << name
            << " ordering is invalid in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

static void RequireNotContains(
    const std::string& name,
    const std::string& sourceName,
    const std::string& source,
    const std::string& needle)
{
    ++g_checks;
    if (source.find(needle) != std::string::npos)
    {
        std::cerr << "NowPlayingTile source regression: " << name << " found forbidden "
            << needle << " in " << sourceName << "\n";
        throw std::runtime_error("source regression");
    }
    std::cout << "ok - " << name << "\n";
}

int main()
{
    try
    {
        const std::string mainSource = ReadAll("NowPlayingTile.cpp");
        const std::string app = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_app.inc");
        const std::string commandLine = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_command_line.inc");
        const std::string config = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_config_defaults.inc");
        const std::string core = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_core.inc");
        const std::string manifest = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_manifest.inc");
        const std::string media = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_media.inc");
        const std::string liveTile = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_live_tile.inc");
        const std::string tray = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_tray.inc");
        const std::string widget = ReadAll("..\\..\\dependencies\\NowPlayingTile\\npt_widget.inc");
        const std::string packagedStartup = ReadAll("..\\..\\dependencies\\packaged_startup.inc");
        const std::string sharedPowerShell = ReadAll("..\\..\\dependencies\\powershell_runner.inc");
        const std::string startupManifest = ReadAll("..\\..\\dependencies\\packaged_startup_manifest.h");

        RequireBefore(
            "command line is parsed before GDI+ initialization",
            "src\\npt_app.inc",
            app,
            "AppOptions options = ParseCommandLine();",
            "GdiplusStartup(&g_gdiplusToken");
        RequireBefore(
            "help exits before GDI+ initialization",
            "src\\npt_app.inc",
            app,
            "if (options.showHelp)",
            "GdiplusStartup(&g_gdiplusToken");
        RequireContains(
            "unknown command-line options are rejected",
            "src\\npt_command_line.inc",
            commandLine,
            "options.commandLineError = L\"Unknown option: \" + arg;");
        RequireContains(
            "shared desktop baseline is consumed",
            "NowPlayingTile.cpp",
            mainSource,
            "../../dependencies/desktop_app_baseline.h");
        RequireContains(
            "product implementation is consumed from the dependency overlay",
            "NowPlayingTile.cpp",
            mainSource,
            "../../dependencies/NowPlayingTile/npt_core.inc");
        RequireNotContains(
            "project-local source fragments are not retained",
            "NowPlayingTile.cpp",
            mainSource,
            "#include \"src/");
        RequireContains(
            "sidecar paths use the shared growable module-path helper",
            "src\\npt_core.inc",
            core,
            "aip::BuildCurrentProcessSidecarPaths(APP_NAME)");
        RequireContains(
            "PowerShell discovery uses the growable shared system-directory helper",
            "src\\npt_manifest.inc",
            manifest,
            "aip::DefaultPowerShellExe()");
        RequireContains(
            "PowerShell process creation pins the resolved executable",
            "src\\npt_manifest.inc",
            sharedPowerShell,
            "CreateProcessW(options.powerShellExe.c_str()");
        RequireNotContains(
            "PowerShell process creation does not rely on first-token resolution",
            "src\\npt_manifest.inc",
            manifest,
            "CreateProcessW(nullptr");
        RequireContains(
            "single-instance identity is scoped by the effective sidecar config",
            "src\\npt_core.inc",
            core,
            "aip::BuildPathScopedInstanceIdentity(");
        RequireContains(
            "hidden control window uses its exact scoped title",
            "src\\npt_tray.inc",
            tray,
            "g_instanceIdentity.windowTitle.c_str()");
        RequireContains(
            "visible widget targeting is constrained to the current executable",
            "src\\npt_command_line.inc",
            commandLine,
            "FindWindowForCurrentExecutable(WIDGET_CLASS_NAME");
        RequireContains(
            "logging uses the shared synchronized UTF-8 logger",
            "src\\npt_core.inc",
            core,
            "g_logger.Write(L\"info\", message);");
        RequireContains(
            "incompatible primary modes are rejected",
            "src\\npt_command_line.inc",
            commandLine,
            "if (primaryModes > 1)");
        RequireContains(
            "missing resident exit reports a nonzero status",
            "src\\npt_app.inc",
            app,
            "return SignalExistingInstanceToExit() ? 0 : 2;");
        RequireContains(
            "automatic bootstrap status is propagated",
            "src\\npt_app.inc",
            app,
            "return bootstrapExitCode;");
        RequireContains(
            "packaged bootstrap forwards one-shot intent",
            "src\\npt_manifest.inc",
            manifest,
            "append(L\"--once\");");
        RequireContains(
            "packaged activation accepts forwarded arguments",
            "src\\npt_manifest.inc",
            manifest,
            "activationManager->ActivateApplication(");
        RequireContains(
            "WinRT initialization failure is handled",
            "src\\npt_app.inc",
            app,
            "WinRT apartment initialization failed:");
        RequireContains(
            "single-instance mutex creation is checked",
            "src\\npt_app.inc",
            app,
            "if (mutex == nullptr)");
        RequireContains(
            "integer settings reject trailing junk",
            "src\\npt_config_defaults.inc",
            config,
            "aip::ParseIntValue(value, parsed)");
        RequireContains(
            "default settings use the shared synchronized atomic INI store",
            "src\\npt_config_defaults.inc",
            config,
            "store.EnsureDefaults(defaults, ARRAYSIZE(defaults))");
        RequireContains(
            "PowerShell output has a fixed capture limit",
            "NowPlayingTile.cpp",
            mainSource,
            "POWERSHELL_OUTPUT_LIMIT_BYTES");
        RequireContains(
            "PowerShell output truncation is reported",
            "src\\npt_manifest.inc",
            manifest,
            "PowerShell output was truncated after ");
        RequireContains(
            "manifest writes use the shared atomic UTF-8 writer",
            "src\\npt_manifest.inc",
            manifest,
            "aip::WriteTextFileUtf8Bom(path, text)");
        RequireContains(
            "generated logo files are committed atomically",
            "src\\npt_manifest.inc",
            manifest,
            "CommitTemporaryFile(temporaryPath, path)");
        RequireContains(
            "artwork files are committed atomically",
            "src\\npt_media.inc",
            media,
            "CommitTemporaryFile(temporaryPath, path)");
        RequireContains(
            "artwork layout emits an artwork tile instead of silently rendering text",
            "src\\npt_live_tile.inc",
            liveTile,
            "case TileLayout::Artwork:");
        RequireContains(
            "combined layout emits overlaid artwork and text",
            "src\\npt_live_tile.inc",
            liveTile,
            "BuildCombinedTileXml(snapshot)");
        RequireContains(
            "cycle layout queues distinct tile payloads",
            "src\\npt_live_tile.inc",
            liveTile,
            "updater.EnableNotificationQueue(payloads.size() > 1);");
        RequireContains(
            "missing artwork has a deterministic text fallback",
            "src\\npt_live_tile.inc",
            liveTile,
            "hasArtwork ? BuildArtworkTileXml(snapshot) : BuildTextTileXml(snapshot)");
        RequireContains(
            "Cycle is accepted by the INI parser",
            "src\\npt_config_defaults.inc",
            config,
            "EqualsIgnoreCase(value, L\"Cycle\")");
        RequireContains(
            "tray-launched actions report ShellExecute failures",
            "src\\npt_tray.inc",
            tray,
            "Could not launch tray action");
        RequireContains(
            "worker publication failures clear the running state",
            "src\\npt_tray.inc",
            tray,
            "else\n                {\n                    ctx->updateRunning.store(false);");
        RequireContains(
            "worker running state is atomic across UI and media threads",
            "NowPlayingTile.cpp",
            mainSource,
            "std::atomic<bool> updateRunning{ false };");
        RequireContains(
            "worker exceptions are contained before crossing the thread boundary",
            "src\\npt_tray.inc",
            tray,
            "Media update worker failed with an unknown exception.");
        RequireContains(
            "refresh timer failures are reported",
            "src\\npt_tray.inc",
            tray,
            "Could not update the media refresh timer:");
        RequireContains(
            "background window class registration is checked",
            "src\\npt_tray.inc",
            tray,
            "if (RegisterClassExW(&wc) == 0)");
        RequireContains(
            "background message-loop errors are handled",
            "src\\npt_tray.inc",
            tray,
            "if (result == -1)");
        RequireContains(
            "Explorer restart restores the tray icon",
            "src\\npt_tray.inc",
            tray,
            "msg == g_taskbarCreatedMessage");
        RequireContains(
            "tray visibility is a persisted typed setting",
            "src\\npt_command_line.inc",
            commandLine,
            "AddNowPlayingCommandLineSetting(options, L\"Settings\", L\"ShowTrayIcon\"");
        RequireContains(
            "Explorer restart restores the last dynamic media tooltip",
            "src\\npt_tray.inc",
            tray,
            "SetTrayTip(ctx, ctx->current.title);");
        RequireContains(
            "tray version 4 keeps and updates the standard hover tooltip",
            "src\\npt_tray.inc",
            tray,
            "aip::ModifyTrayIconTooltip(");
        RequireContains(
            "tray construction uses shared menu primitives",
            "src\\npt_tray.inc",
            tray,
            "aip::AppendTrayMenuItem");
        RequireContains(
            "tray keyboard activation is handled",
            "src\\npt_tray.inc",
            tray,
            "LOWORD(lParam) == NIN_KEYSELECT");
        RequireContains(
            "widget window class registration is checked",
            "src\\npt_widget.inc",
            widget,
            "if (RegisterClassExW(&wc) == 0)");
        RequireContains(
            "widget message-loop errors are handled",
            "src\\npt_widget.inc",
            widget,
            "if (result == -1)");
        RequireContains(
            "packaged StartupTask dependency is consumed",
            "NowPlayingTile.cpp",
            mainSource,
            "../../dependencies/packaged_startup.inc");
        RequireContains(
            "manifest declares the desktop StartupTask extension",
            "src\\npt_manifest.inc",
            startupManifest,
            "Category=\\\"windows.startupTask\\\"");
        RequireContains("NPT consumes shared startup XML", "NPT manifest", manifest, "aip::BuildDesktopStartupExtension(STARTUP_TASK_ID");
        RequireContains("NPT consumes shared registration scripts", "NPT manifest", manifest, "aip::BuildAppxRegistrationScript({manifest, true}");
        RequireContains("NPT consumes shared PowerShell runner", "NPT manifest", manifest, "aip::RunPowerShellCaptured(");
        RequireNotContains("NPT does not keep a second PowerShell process loop", "NPT manifest", manifest, "CreateProcessW(");
        RequireContains(
            "manifest StartupTask id matches runtime",
            "NowPlayingTile sources",
            mainSource + manifest,
            "NowPlayingTileStartup");
        RequireContains(
            "packaged Startup uses an MTA worker for synchronous callers",
            "dependencies\\packaged_startup.inc",
            packagedStartup,
            "RunPackagedStartupOperationOnMta");
        RequireContains(
            "packaged Startup preserves user-disabled state",
            "dependencies\\packaged_startup.inc",
            packagedStartup,
            "Re-enable it manually in Task Manager's Startup apps page.");
        RequireNotContains(
            "packaged app does not use a Startup-folder shortcut",
            "NowPlayingTile product sources",
            mainSource + config + commandLine + manifest + tray + app,
            "FOLDERID_Startup");
        RequireContains(
            "Startup setting is present in the generated INI",
            "src\\npt_config_defaults.inc",
            config,
            "{ L\"Settings\", L\"RunAtStartup\", L\"false\" }");
        RequireContains(
            "Startup setting has command-line aliases",
            "src\\npt_command_line.inc",
            commandLine,
            "EqualsIgnoreCase(arg, L\"--startup\") || EqualsIgnoreCase(arg, L\"--no-startup\")");
        RequireContains(
            "Startup setting is exposed in the tray",
            "src\\npt_tray.inc",
            tray,
            "ID_TOGGLE_STARTUP, L\"Run at startup\"");
        RequireContains(
            "known settings are validated before mutation",
            "src\\npt_config_defaults.inc",
            config,
            "NormalizeNowPlayingSetting");
        RequireContains(
            "command-line settings commit in one INI mutation",
            "src\\npt_config_defaults.inc",
            config,
            "PersistNowPlayingSettingsAtomically");
        RequireContains(
            "command-line setting batch uses one fresh mutation",
            "src\\npt_config_defaults.inc",
            config,
            ").MutateFresh(");
        RequireContains(
            "packaged Startup and INI use the shared coupled transaction",
            "src\\npt_config_defaults.inc",
            config,
            "aip::CommitPackagedStartupTaskIniState(");
        RequireContains(
            "packaged Startup transaction uses direction-safe commit and rollback",
            "..\\..\\dependencies\\packaged_startup.inc",
            packagedStartup,
            "ExecuteCrashConsistentLaunchConfigState(");
        RequireContains(
            "packaged bootstrap forwards persistent settings",
            "src\\npt_manifest.inc",
            manifest,
            "setting.section + L\".\" + setting.key + L\"=\" + setting.value");
        RequireContains(
            "help and version use shared command-line output",
            "src\\npt_command_line.inc",
            commandLine,
            "aip::WriteCommandLineText(text, error)");
        RequireContains(
            "tray uses the shared baseline header",
            "src\\npt_tray.inc",
            tray,
            "aip::AppendBaselineTrayMenuHeader(");
        RequireContains(
            "tray exposes every functional INI setting",
            "src\\npt_tray.inc",
            tray,
            "ID_TILE_REFRESH_120");
        RequireContains(
            "settings reload reconciles the packaged StartupTask",
            "src\\npt_tray.inc",
            tray,
            "ReconcileNowPlayingStartupFromConfig(startupError)");

        std::cout << "NowPlayingTile source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

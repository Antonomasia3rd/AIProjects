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
    std::ifstream in(path, std::ios::binary);
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

int main()
{
    try
    {
        const std::string mainSource = ReadAll("NowPlayingTile.cpp");
        const std::string app = ReadAll("src\\npt_app.inc");
        const std::string commandLine = ReadAll("src\\npt_command_line.inc");
        const std::string config = ReadAll("src\\npt_config_defaults.inc");
        const std::string core = ReadAll("src\\npt_core.inc");
        const std::string manifest = ReadAll("src\\npt_manifest.inc");
        const std::string media = ReadAll("src\\npt_media.inc");
        const std::string tray = ReadAll("src\\npt_tray.inc");
        const std::string widget = ReadAll("src\\npt_widget.inc");

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
            "sidecar paths use the shared growable module-path helper",
            "src\\npt_core.inc",
            core,
            "aip::BuildCurrentProcessSidecarPaths(APP_NAME)");
        RequireContains(
            "PowerShell discovery uses the growable shared system-directory helper",
            "src\\npt_manifest.inc",
            manifest,
            "aip::GetSystemDirectoryPath()");
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
            "*end != L'\\0' || errno == ERANGE");
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

        std::cout << "NowPlayingTile source checks passed (" << g_checks << " checks).\n";
        return 0;
    }
    catch (const std::exception& ex)
    {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}

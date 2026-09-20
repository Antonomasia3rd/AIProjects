#include "../dependencies/DiscordRPC/drpc_environment.inc"
#include "../dependencies/DiscordRPC/drpc_types.inc"
#include "../dependencies/DiscordRPC/drpc_core.inc"
#include "../dependencies/DiscordRPC/drpc_config_defaults.inc"
#include "../dependencies/DiscordRPC/drpc_command_line.inc"
#include "../dependencies/DiscordRPC/drpc_presence.inc"
#include "../dependencies/DiscordRPC/service.h"
#include "../dependencies/DiscordRPC/drpc_tray.inc"
#include "../dependencies/DiscordRPC/drpc_app.inc"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInstance;
    InitPaths();

    AppOptions options;
    std::wstring error;
    if (!ParseCommandLine(options, error))
    {
        PrintLine(L"ERROR: " + error, true);
        return 2;
    }

    if (!options.configPath.empty())
    {
        ApplyConfiguredIniPath(options.configPath);
    }

    if (options.showHelp)
    {
        // Keep --help side-effect-free: it may read an existing configured
        // help template, but it must not create, normalize, repair, or write
        // the INI.
        PrintLine(DefaultHelpText());
        return 0;
    }

    if (options.showVersion)
    {
        // Keep --version side-effect-free as well.
        PrintLine(aip::ReleaseVersionDisplayText());
        return 0;
    }

    if (options.requestExit)
    {
        // Keep --exit lightweight and side-effect-free for the target INI.
        // It only needs the resolved INI path to find the existing instance's
        // control window.
        return SignalExistingInstanceToExit() ? 0 : 2;
    }

    if (!EnsureDefaultConfigFile())
    {
        return 1;
    }
    ConfigureRuntimeFromConfig(options);
    ApplyRuntimeLoggingOverridesFromCommandLine(options);
    if (!ApplyCommandLineSettings(options))
    {
        return 1;
    }
    if (!options.settings.empty())
    {
        options.reloadExistingInstance = true;
    }
    bool explicitTokenPresent = options.tokenProvided && !Trim(options.tokenToProtect).empty();
    if (explicitTokenPresent && !ProtectExplicitDiscordTokenInConfig(options.tokenToProtect))
    {
        return 1;
    }
    bool plaintextTokenPresent = !Trim(IniReadS(L"general", L"token", L"")).empty();
    if (!explicitTokenPresent && (options.protectToken || plaintextTokenPresent) && !ProtectDiscordTokenInConfig(plaintextTokenPresent))
    {
        return 1;
    }
    if (options.protectToken || explicitTokenPresent || plaintextTokenPresent)
    {
        options.reloadExistingInstance = true;
    }
    ConfigureRuntimeFromConfig(options);

    if ((options.dryRun || options.dryRunFull || options.once) && ShouldReloadExistingInstance(options))
    {
        SignalExistingInstance(DRPC_INSTANCE_RELOAD);
    }

    if (options.dryRun || options.dryRunFull)
    {
        return RunDryRun(options.dryRunFull);
    }

    return RunApplication(options);
}

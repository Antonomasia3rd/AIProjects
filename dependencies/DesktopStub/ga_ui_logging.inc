// UI/logging aggregation. Keep this order: later fragments depend on helpers and state declared earlier.
#include "ga_ui_state.inc"
#include "ga_logging_core.inc"
#include "ga_runtime_helpers.inc"
#include "ga_manifest.inc"
#include "ga_ui_strings.inc"

static void LogCommandLineSummary()
{
    if (g_commandLine.customIniPath)
        Log(g_ui.commandLineIniPathLog.c_str(), g_commandLine.iniPath.c_str());
    if (g_commandLine.generateOnce)
        LogText(g_ui.commandLineModeGenerateOnce);
    if (g_commandLine.exitRequested)
        LogText(g_ui.commandLineModeExit);
    if (g_commandLine.forceGenerateOnStartup && !g_commandLine.generateOnce)
        LogText(g_ui.commandLineModeForceGenerate);
    if (g_commandLine.regenerateManifest)
        LogText(g_ui.commandLineModeRegenerateManifest);
    if (g_commandLine.noMonitor && !g_commandLine.generateOnce)
        LogText(g_ui.commandLineModeNoMonitor);
    if (g_commandLine.wallpaperSpecified)
        Log(g_ui.commandLineWallpaperLog.c_str(), g_commandLine.wallpaperPath.c_str());
    if (g_commandLine.appxActivationServer)
    {
        if (!g_commandLine.appxActivationServerName.empty())
            Log(g_ui.commandLineAppxServerIgnored.c_str(), g_commandLine.appxActivationServerName.c_str());
        else
            LogText(g_ui.commandLineAppxArgumentIgnored);
    }
    if (g_commandLine.launchedFromAppxStub)
        LogText(g_ui.commandLineLaunchAppxStub);
    if (g_commandLine.launchedFromModeSwitch)
        LogText(g_ui.commandLineLaunchModeSwitch);
    if (g_commandLine.waitForPid != 0)
        Log(g_ui.commandLineParentWaitPid.c_str(), static_cast<unsigned long>(g_commandLine.waitForPid));

    for (const auto& entry : g_commandLine.settings)
        Log(g_ui.commandLineSettingLog.c_str(), entry.section.c_str(), entry.key.c_str(), entry.value.c_str());
}

// compile command: cl /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DiscordRPC.cpp /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib winhttp.lib crypt32.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <wincrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <deque>
#include <exception>
#include <functional>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static constexpr const wchar_t* APP_NAME = L"DiscordRPC";
static constexpr const wchar_t* APP_DISPLAY_NAME = L"DiscordRPC";
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"DiscordRPCTrayWnd";
static constexpr UINT WM_DRPC_TRAY = WM_APP + 61;
static constexpr UINT WM_DRPC_NOTIFY = WM_APP + 62;
static constexpr UINT WM_DRPC_REQUEST_SHUTDOWN = WM_APP + 63;
static constexpr UINT WM_DRPC_WORKER_FINISHED = WM_APP + 64;
static constexpr UINT WM_DRPC_INSTANCE_CONTROL = WM_APP + 65;

static HINSTANCE g_hInst = nullptr;
static std::wstring g_exePath;
static std::wstring g_exeDir;
static std::wstring g_exeBaseName;
static std::wstring g_iniPath;
static std::wstring g_defaultLogPath;
static std::wstring g_effectiveLogPath;
static std::wstring g_singleInstanceMutexName;
static std::wstring g_instanceWindowTitle;
static UINT g_taskbarCreatedMessage = 0;
static HANDLE g_singleInstanceMutex = nullptr;
static std::atomic<HWND> g_runtimeWindow(nullptr);

static std::atomic<bool> g_stopRequested(false);
static std::atomic<bool> g_reloadRequested(false);
static std::atomic<bool> g_refreshRequested(false);
static std::atomic<bool> g_loggingEnabled(true);
static std::atomic<bool> g_fileLoggingEnabled(true);
static std::atomic<bool> g_verboseLogging(false);
static std::atomic<bool> g_notificationsEnabled(true);
static std::atomic<DWORD> g_logAppendLockWaitMs(5000);
static std::atomic<DWORD> g_iniWriteLockWaitMs(5000);
static std::atomic<bool> g_consoleEnabled(false);
static std::mutex g_consoleMutex;
static bool g_consoleAllocated = false;

#include "..\dependencies\desktop_app_baseline.h"

#if __has_include("DiscordRPCVersionDefines.inc")
#include "DiscordRPCVersionDefines.inc"
#endif
#include "..\dependencies\release_version.inc"

static aip::Utf8Logger g_logger;
#include "..\dependencies\dpapi.inc"
#include "src/drpc_types.inc"
#include "src/drpc_core.inc"
#include "src/drpc_config_defaults.inc"
#include "src/drpc_command_line.inc"
#include "src/drpc_presence.inc"
#include "src/drpc_ipc.inc"
#include "src/drpc_gateway.inc"
#include "src/drpc_tray.inc"
#include "src/drpc_app.inc"

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

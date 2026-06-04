// compile command: cl /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DiscordRPC.cpp /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib winhttp.lib crypt32.lib /SUBSYSTEM:CONSOLE
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
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <ctime>
#include <deque>
#include <exception>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
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

static HINSTANCE g_hInst = nullptr;
static std::wstring g_exePath;
static std::wstring g_exeDir;
static std::wstring g_exeBaseName;
static std::wstring g_iniPath;
static std::wstring g_defaultLogPath;
static std::wstring g_effectiveLogPath;
static std::wstring g_singleInstanceMessageName;
static UINT g_singleInstanceMessage = 0;
static HANDLE g_singleInstanceMutex = nullptr;

static std::atomic<bool> g_stopRequested(false);
static std::atomic<bool> g_reloadRequested(false);
static std::atomic<bool> g_refreshRequested(false);
static std::atomic<bool> g_loggingEnabled(true);
static std::atomic<bool> g_fileLoggingEnabled(true);
static std::atomic<bool> g_verboseLogging(false);
static std::atomic<bool> g_notificationsEnabled(true);

static std::mutex g_logMutex;
static std::deque<std::wstring> g_recentLog;

#include "..\dependencies\core.inc"
#include "..\dependencies\config_ini.inc"
#include "..\dependencies\command_line.inc"
#include "..\dependencies\dpapi.inc"
#include "..\dependencies\tray.inc"
#include "src/drpc_types.inc"
#include "src/drpc_core.inc"
#include "src/drpc_config_defaults.inc"
#include "src/drpc_command_line.inc"
#include "src/drpc_presence.inc"
#include "src/drpc_ipc.inc"
#include "src/drpc_gateway.inc"
#include "src/drpc_tray.inc"
#include "src/drpc_app.inc"

int wmain()
{
    g_hInst = GetModuleHandleW(nullptr);
    InitPaths();

    AppOptions options;
    std::wstring error;
    if (!ParseCommandLine(options, error))
    {
        PrintLine(L"ERROR: " + error, true);
        return 2;
    }

    if (options.showHelp)
    {
        // Keep --help side-effect-free: do not create, read, or normalize the INI.
        PrintLine(DefaultHelpText());
        return 0;
    }

    if (options.showVersion)
    {
        // Keep --version side-effect-free as well.
        PrintLine(L"DiscordRPC C++");
        return 0;
    }

    if (!options.configPath.empty())
    {
        g_iniPath = MakeAbsolutePath(options.configPath);
        g_defaultLogPath = PathJoin(GetDirectoryName(g_iniPath), GetFileBaseName(g_iniPath) + L".log");
    }

    EnsureDefaultConfigFile();
    ConfigureRuntimeFromConfig(options);
    ApplyRuntimeLoggingOverridesFromCommandLine(options);
    ApplyCommandLineSettings(options);
    bool plaintextTokenPresent = !Trim(IniReadS(L"general", L"token", L"")).empty();
    if ((options.protectToken || plaintextTokenPresent) && !ProtectDiscordTokenInConfig(plaintextTokenPresent))
    {
        return 1;
    }
    ConfigureRuntimeFromConfig(options);

    if (options.dryRun || options.dryRunFull)
    {
        return RunDryRun(options.dryRunFull);
    }

    return RunApplication(options);
}

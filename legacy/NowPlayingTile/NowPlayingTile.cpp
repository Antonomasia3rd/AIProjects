// compile command: cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE NowPlayingTile.cpp /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <appmodel.h>
#include <stdio.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <memory>
#include <cwchar>
#include <cwctype>
#include <cstdint>
#include <algorithm>
#include <initializer_list>
#include <cstdarg>
#include <limits>
#include <cerrno>

#include "../../dependencies/desktop_app_baseline.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.h>

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

using namespace Gdiplus;
using namespace std::chrono;

static constexpr const wchar_t* APP_NAME = L"NowPlayingTile";
static constexpr const wchar_t* APP_DISPLAY_NAME = L"Now Playing Tile";
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"NowPlayingTileTrayWnd";
static constexpr const wchar_t* WIDGET_CLASS_NAME = L"NowPlayingTileWidgetWnd";
static constexpr const wchar_t* PACKAGE_NAME = L"NowPlayingTile.App";
static constexpr const wchar_t* PACKAGE_PUBLISHER = L"CN=NowPlayingTile";
static constexpr UINT WM_NPT_UPDATE_DONE = WM_APP + 42;
static constexpr UINT_PTR UPDATE_TIMER_ID = 1;
static constexpr DWORD POWERSHELL_COMMAND_TIMEOUT_MS = 120000;
static constexpr DWORD POWERSHELL_POLL_MS = 50;
static constexpr DWORD POWERSHELL_TERMINATE_WAIT_MS = 5000;
static constexpr size_t POWERSHELL_OUTPUT_LIMIT_BYTES = 4u * 1024u * 1024u;

static HINSTANCE g_hInst = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static std::wstring g_exePath;
static std::wstring g_exeDir;
static std::wstring g_exeBaseName;
static std::wstring g_iniPath;
static std::wstring g_logPath;
static aip::InstanceIdentity g_instanceIdentity;
static UINT g_restoreMessage = 0;
static UINT g_taskbarCreatedMessage = 0;
static aip::Utf8Logger g_logger;

struct MediaSnapshot
{
    std::wstring source;
    std::wstring title;
    std::wstring artist;
    std::wstring status;
    std::wstring lastUpdated;
    std::wstring artworkHash;
    std::wstring artworkUri;
    std::wstring artworkPath;

    static MediaSnapshot Empty(const std::wstring& message)
    {
        MediaSnapshot s;
        s.source = L"SMTC";
        s.title = message.empty() ? L"No current media" : message;
        s.artist = L"";
        s.status = L"Idle";
        s.lastUpdated = FormatClockTime();
        return s;
    }

    static std::wstring FormatClockTime();
};

enum class TileLayout
{
    Cycle,
    Text,
    Artwork,
    Combined
};

struct AppSettings
{
    int updateIntervalSeconds = 2;
    int tileRefreshSeconds = 60;
    TileLayout tileLayout = TileLayout::Text;
    bool showTrayIcon = false;
};

struct AppOptions
{
    bool showWidget = false;
    bool updateOnce = false;
    bool allowMultiple = false;
    bool requestExit = false;
    bool registerPackage = false;
    bool unregisterPackage = false;
    bool launchPackaged = false;
    bool regenerateManifest = false;
    bool showHelp = false;
    bool forceTray = false;
    bool forceNoTray = false;
    std::wstring commandLineError;
};

struct RuntimeContext
{
    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid = {};
    bool trayCreated = false;
    std::atomic<bool> updateRunning{ false };
    std::atomic<bool> closing{ false };
    std::thread updateThread;
    AppSettings settings;
    std::wstring lastTileKey;
    steady_clock::time_point lastTileUpdate = steady_clock::time_point::min();
    MediaSnapshot current = MediaSnapshot::Empty(L"Starting...");
};

static void Log(const wchar_t* fmt, ...);
static std::wstring GetLastErrorText(DWORD error);
static std::wstring XmlEscape(const std::wstring& value);
static std::wstring PathJoin(const std::wstring& left, const std::wstring& right);
static std::wstring FilePathToUri(const std::wstring& path);
static std::wstring QuotePowerShellLiteral(const std::wstring& value);
static bool RunPowerShellCommand(const std::wstring& command, std::wstring* output, DWORD* exitCode);
static bool FileExists(const std::wstring& path);
static void EnsureDirectory(const std::wstring& path);
static std::wstring BuildTemporarySiblingPath(const std::wstring& path);
static bool CommitTemporaryFile(const std::wstring& temporaryPath, const std::wstring& destinationPath);
static void InitPaths();
static HWND FindWindowForCurrentExecutable(const wchar_t* windowClass, int retries = 0, int delayMs = 0);
static bool EnsureDefaultSettingsFile();
static AppSettings LoadSettings();
static AppOptions ParseCommandLine();
static MediaSnapshot ReadMediaSnapshot();
static bool TryUpdateLiveTile(const MediaSnapshot& snapshot, const AppSettings& settings);
static bool HasPackageIdentity();
static int RunBackground(const AppOptions& options);
static int RunWidget(const AppOptions& options);
static void BeginUpdate(RuntimeContext* ctx);
static void StopUpdates(RuntimeContext* ctx);
static void MaybeUpdateLiveTile(RuntimeContext* ctx, const MediaSnapshot& snapshot);
static void ShowHelpMessage();
static bool SignalExistingInstanceToExit();
static bool EnsurePackageFiles(bool forceRewrite);
static int RegisterDevelopmentPackage(bool quiet = false);
static int UnregisterDevelopmentPackage(bool quiet = false);
static int LaunchPackagedInstance(bool quiet = false, const std::wstring& arguments = L"");
static bool AutoRegisterAndLaunchIfNeeded(const AppOptions& options, int* exitCode);

#include "src/npt_core.inc"
#include "src/npt_config_defaults.inc"
#include "src/npt_command_line.inc"
#include "src/npt_manifest.inc"
#include "src/npt_media.inc"
#include "src/npt_live_tile.inc"
#include "src/npt_tray.inc"
#include "src/npt_widget.inc"
#include "src/npt_app.inc"

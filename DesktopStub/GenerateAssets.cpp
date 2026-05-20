// compile command: cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE GenerateAssets.cpp /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string>
#include <cwctype>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <exception>
#include <chrono>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <cstdint>
#include <memory>
#include <commdlg.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

INT_PTR CALLBACK RenameDlgProc(HWND, UINT, WPARAM, LPARAM) noexcept;
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) noexcept;

#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
#endif

enum class FitMode {
    Fill,
    Fit,
    Stretch,
    Center,
    Tile,
    Span
};

using namespace Gdiplus;
using namespace std::chrono;

std::wstring IniReadS(const wchar_t* s, const wchar_t* k, const wchar_t* d);
void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v);

enum class ErrorAction
{
    Ignore,
    Warn,
    Exit,
    Crash
};

static const wchar_t* StateEnabled(bool v);
static const wchar_t* StateOn(bool v);
static int ClampInt(int value, int minValue, int maxValue);

static std::wstring g_iniPath, g_logPath, g_exePath;
static std::mutex g_pathMutex;
static std::atomic<int> g_logAppendLockWaitMs(1000);
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"DesktopTileGeneratorTrayWnd";
static constexpr const wchar_t* SINGLE_INSTANCE_MUTEX_BASE = L"Local\\DesktopTileGenerator.GenerateAssets";
static constexpr const wchar_t* SINGLE_INSTANCE_MESSAGE_BASE = L"DesktopTileGenerator.RestoreRunningInstance";
static std::wstring g_singleInstanceMutexName;
static std::wstring g_singleInstanceMessageName;
static std::wstring g_instanceWindowTitle;
static UINT g_singleInstanceMessage = 0;
static UINT g_taskbarCreatedMessage = 0;
static HANDLE g_singleInstanceMutex = nullptr;

#include "src/ga_core.inc"
#include "src/ga_config_defaults.inc"
#include "src/ga_ui_logging.inc"
#include "src/ga_wallpaper.inc"
#include "src/ga_image.inc"
#include "src/ga_registration.inc"
#include "src/ga_generation.inc"
#include "src/ga_tray.inc"
#include "src/ga_app.inc"

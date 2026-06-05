// compile command: cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DesktopStub.cpp /link gdiplus.lib windowscodecs.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <wincodec.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdio.h>
#include <string>
#include <cwctype>
#include <cstring>
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
#include <malloc.h>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <cstdint>
#include <memory>
#include <new>
#include <cmath>
#include <commdlg.h>
#include <appmodel.h>
#include <tlhelp32.h>
#include <roapi.h>
#include <activation.h>
#include <inspectable.h>
#include <winstring.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Management.Deployment.h>
#include <winrt/Windows.UI.Notifications.h>

#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

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
int IniReadI(const wchar_t* s, const wchar_t* k, int d);
void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v);

enum class ErrorAction
{
    Ignore,
    Warn,
    Exit,
    Crash
};

enum class LiveTileUpdateMode
{
    Registration,
    LiveTile,
    Auto
};

enum class ManifestCompatibilityTarget
{
    Windows10,
    Windows81,
    Windows8
};

static const wchar_t* StateEnabled(bool v);
static const wchar_t* StateOn(bool v);
static int ClampInt(int value, int minValue, int maxValue);
static bool TryParseIntStrict(const std::wstring& value, int& parsed);

static std::wstring g_iniPath, g_logPath, g_exePath;
static std::mutex g_pathMutex;
static std::atomic<int> g_logAppendLockWaitMs(1000);
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"DesktopStubTrayWnd";
static constexpr const wchar_t* COM_REGISTRATION_HELPER_ARG = L"--ds-com-register";
static constexpr const wchar_t* COM_REGISTRATION_HELPER_ARG_LEGACY = L"--ga-com-register";
static constexpr const wchar_t* WAIT_FOR_PID_ARG = L"--ds-wait-for-pid";
static constexpr const wchar_t* WAIT_FOR_PID_ARG_LEGACY = L"--ga-wait-for-pid";
static std::wstring g_singleInstanceMutexName;
static std::wstring g_singleInstanceMessageName;
static std::wstring g_instanceWindowTitle;
static UINT g_singleInstanceMessage = 0;
static UINT g_taskbarCreatedMessage = 0;
static HANDLE g_singleInstanceMutex = nullptr;

#include "..\dependencies\core.inc"
#include "..\dependencies\config_ini.inc"
#include "..\dependencies\command_line.inc"
#include "..\dependencies\tray.inc"
#include "src/ga_core.inc"
#if __has_include("DesktopStubVersionDefines.inc")
#include "DesktopStubVersionDefines.inc"
#endif
#include "src/ga_version.inc"
#include "src/ga_config_defaults.inc"
#include "src/ga_command_line.inc"
#include "src/ga_ui_logging.inc"
#include "src/ga_wallpaper.inc"
#include "src/ga_image.inc"
#include "src/ga_registration.inc"
#include "src/ga_generation.inc"
#include "src/ga_live_tile.inc"
#include "src/ga_tray.inc"
#include "src/ga_app.inc"

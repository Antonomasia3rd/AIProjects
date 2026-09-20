// compile command: cl /utf-8 /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE DiscordRPC.cpp /link user32.lib shell32.lib shlwapi.lib advapi32.lib ole32.lib uuid.lib winhttp.lib crypt32.lib /SUBSYSTEM:WINDOWS
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
#ifdef AIP_DISCORD_ENGINE_CONTEXT
static thread_local std::wstring g_iniPath;
#else
static std::wstring g_iniPath;
#endif
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

#include "../desktop_app_baseline.h"
#include "../startup_shortcut.inc"

#if __has_include("DiscordRPCVersionDefines.inc")
#include "DiscordRPCVersionDefines.inc"
#endif
#include "../release_version.inc"

static aip::Utf8Logger g_logger;
#include "../dpapi.inc"

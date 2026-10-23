// Compile-only coverage for the production WinRT adapter. This fixture has no
// entry point and is never linked or executed.
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <appmodel.h>
#include <roapi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <atomic>
#include <mutex>
#include <string>
#include <map>
namespace aip { bool ShowTrayBalloon(const NOTIFYICONDATAW&, const std::wstring&, const std::wstring&, DWORD, UINT, DWORD*); }
static std::atomic<bool> g_notificationsEnabled{true}, g_tray{true};
static std::atomic<bool> g_running{true};
static HWND g_hwnd=nullptr;
static std::atomic<int> g_notificationTimeoutMs{4000};
static struct { bool renderOnly = false, configureOnly = false; } g_commandLine;
static std::mutex g_pathMutex, g_trayMutex;
static std::wstring g_iniPath;
static NOTIFYICONDATAW g_nid{};
static std::wstring IniReadS(const wchar_t*, const wchar_t*, const wchar_t* fallback) { return fallback; }
static int IniReadI(const wchar_t*, const wchar_t*, int fallback) { return fallback; }
static int ClampInt(int value, int minimum, int maximum) { return std::max(minimum, std::min(value, maximum)); }
static void LogWin32Failure(const wchar_t*, DWORD) {}
static void LogText(const std::wstring&) {}
#include "../dependencies/DesktopStub/ga_notifications.inc"
void NotificationCompileWitness() {
    // This symbol is used only by the compiler and this object is never linked.
    NotifyUserOperation(L"compile", L"compile", L"compile");
    NotifyOperationProgress(L"compile", L"compile", L"compile", 1, 2);
    NotifyScheduledUserEvent(L"compile",L"compile",L"compile",aip::notifications::EventType::Alarm,aip::notifications::EventSound::Silent);
    ConfirmUserOperation(nullptr,L"compile",L"compile",L"compile",[]{});
    DrainUserConfirmations();ClearUserConfirmations();
}

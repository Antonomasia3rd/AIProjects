// Experimental Win8/8.1-style Live Tile background task helper.
// Built as a small WinRT component DLL and declared in AppxManifest.xml.
// The task reads GenerateAssets.livetile.pending.xml next to the DLL, updates
// the app tile under package identity, logs the result, and exits.
#define NOMINMAX
#include <windows.h>
#include <winstring.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.Background.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

static HMODULE g_module = nullptr;
static constexpr const wchar_t* BACKGROUND_TASK_CLASS_ID = L"DesktopStub.LiveTileBackgroundTask";

static std::wstring ModulePath()
{
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(g_module, buf.data(), static_cast<DWORD>(buf.size()));
    if (!n || n >= buf.size())
        return L"";
    return std::wstring(buf.data(), n);
}

static std::wstring ModuleDir()
{
    std::wstring path = ModulePath();
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
        return L".";
    return path.substr(0, pos);
}

static std::wstring PendingXmlPath()
{
    return ModuleDir() + L"\\GenerateAssets.livetile.pending.xml";
}

static std::wstring ActivationLogPath()
{
    return ModuleDir() + L"\\GenerateAssets.appxactivation.log";
}

static std::wstring TrimCopy(const std::wstring& s)
{
    size_t first = 0;
    while (first < s.size() && iswspace(s[first]))
        ++first;
    size_t last = s.size();
    while (last > first && iswspace(s[last - 1]))
        --last;
    return s.substr(first, last - first);
}

static std::wstring NowStamp()
{
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u-%02u-%02u %02u:%02u:%02u",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}

static std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return std::string();
    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (need <= 0)
        return std::string();
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &out[0], need, nullptr, nullptr);
    return out;
}

static void AppendActivationLog(const std::wstring& message)
{
    std::wstring line = L"[" + NowStamp() + L"] pid=" + std::to_wstring(GetCurrentProcessId()) + L" " + message + L"\r\n";
    std::string bytes = WideToUtf8(line);
    if (bytes.empty())
        return;

    std::wstring path = ActivationLogPath();
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return;

    LARGE_INTEGER size{};
    if (GetFileSizeEx(h, &size) && size.QuadPart == 0)
    {
        static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        DWORD written = 0;
        WriteFile(h, bom, sizeof(bom), &written, nullptr);
    }

    DWORD written = 0;
    WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    CloseHandle(h);
}

static bool ReadUtf8BomTextFile(const std::wstring& path, std::wstring& out)
{
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER li{};
    if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 || li.QuadPart > 16 * 1024 * 1024)
    {
        CloseHandle(h);
        return false;
    }

    std::string bytes(static_cast<size_t>(li.QuadPart), '\0');
    DWORD read = 0;
    BOOL ok = TRUE;
    if (!bytes.empty())
        ok = ReadFile(h, &bytes[0], static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(h);
    if (!ok)
        return false;
    bytes.resize(read);

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }

    if (bytes.empty())
        return true;

    int need = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (need <= 0)
        return false;
    out.resize(static_cast<size_t>(need));
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), &out[0], need);
    return true;
}

static bool UpdateLiveTileFromPendingXml(std::wstring& failure)
{
    failure.clear();
    std::wstring xmlText;
    if (!ReadUtf8BomTextFile(PendingXmlPath(), xmlText) || TrimCopy(xmlText).empty())
    {
        failure = L"No pending Live Tile XML was found for the background task.";
        return false;
    }

    try
    {
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        XmlDocument xml;
        xml.LoadXml(winrt::hstring(xmlText));
        TileNotification notification(xml);
        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.EnableNotificationQueue(false);
        updater.Update(notification);
        DeleteFileW(PendingXmlPath().c_str());
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        wchar_t buf[128]{};
        swprintf_s(buf, L"Live Tile update failed in background task, hresult=0x%08X", static_cast<unsigned int>(e.code().value));
        failure = buf;
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failure += L", message=" + msg;
        return false;
    }
    catch (...)
    {
        failure = L"Live Tile update failed in background task due to an unknown exception.";
        return false;
    }
}

struct LiveTileBackgroundTask : winrt::implements<LiveTileBackgroundTask, winrt::Windows::ApplicationModel::Background::IBackgroundTask>
{
    void Run(winrt::Windows::ApplicationModel::Background::IBackgroundTaskInstance const& taskInstance)
    {
        auto deferral = taskInstance.GetDeferral();
        AppendActivationLog(L"received background task activation");

        std::wstring failure;
        if (UpdateLiveTileFromPendingXml(failure))
            AppendActivationLog(L"Live Tile update completed under package identity");
        else
            AppendActivationLog(L"Live Tile background task failed: " + failure);

        deferral.Complete();
    }
};

struct LiveTileBackgroundTaskFactory : winrt::implements<LiveTileBackgroundTaskFactory, winrt::Windows::Foundation::IActivationFactory>
{
    winrt::Windows::Foundation::IInspectable ActivateInstance()
    {
        return winrt::make<LiveTileBackgroundTask>();
    }
};

extern "C" HRESULT __stdcall DllGetActivationFactory(HSTRING classId, void** factory) noexcept
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;

    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(classId, &len);
    std::wstring requested = raw ? std::wstring(raw, len) : std::wstring();
    if (_wcsicmp(requested.c_str(), BACKGROUND_TASK_CLASS_ID) != 0)
        return CLASS_E_CLASSNOTAVAILABLE;

    try
    {
        winrt::Windows::Foundation::IActivationFactory f = winrt::make<LiveTileBackgroundTaskFactory>();
        *factory = winrt::detach_abi(f);
        return S_OK;
    }
    catch (...)
    {
        return E_OUTOFMEMORY;
    }
}

STDAPI DllCanUnloadNow(void)
{
    return S_FALSE;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}

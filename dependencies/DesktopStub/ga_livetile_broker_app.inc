// Experimental Win8/8.1-style packaged WinRT broker app for Live Tile updates.
// Unlike DesktopStubAppxStub.exe, this is a real CoreApplication-based
// Windows Runtime app executable. It is intended to satisfy the old RT app
// activation contract, update the tile under package identity, then exit as
// quickly as possible.
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cwctype>
#include <winrt/base.h>
#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Storage.h>

static std::wstring ModulePath()
{
    std::vector<wchar_t> buf(32768);
    DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
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

static std::wstring ModuleBaseName()
{
    std::wstring path = ModulePath();
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path.erase(0, slash + 1);
    size_t dot = path.find_last_of(L'.');
    if (dot != std::wstring::npos)
        path.erase(dot);
    return path.empty() ? L"DesktopStub" : path;
}

static bool EndsWithNoCase(const std::wstring& text, const std::wstring& suffix)
{
    if (suffix.empty() || text.size() < suffix.size())
        return false;

    size_t offset = text.size() - suffix.size();
    for (size_t i = 0; i < suffix.size(); ++i)
    {
        if (towlower(text[offset + i]) != towlower(suffix[i]))
            return false;
    }
    return true;
}

static std::wstring ProductRuntimeBaseName()
{
    std::wstring base = ModuleBaseName();
    const std::wstring suffix = L"LiveTileBroker";
    if (EndsWithNoCase(base, suffix) && base.size() > suffix.size())
        base.resize(base.size() - suffix.size());
    return base.empty() ? L"DesktopStub" : base;
}

static std::wstring ProductRuntimeFileName(const wchar_t* suffix)
{
    std::wstring name = ProductRuntimeBaseName();
    name += suffix ? suffix : L"";
    return name;
}

static std::wstring ProductRuntimeFilePath(const wchar_t* suffix)
{
    return ModuleDir() + L"\\" + ProductRuntimeFileName(suffix);
}

static std::wstring PendingXmlPath()
{
    return ProductRuntimeFilePath(L".livetile.pending.xml");
}

static std::wstring ActivationLogPath()
{
    return ProductRuntimeFilePath(L".appxactivation.log");
}

static std::wstring LocalStateDir()
{
    try
    {
        return winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    }
    catch (...)
    {
        return L"";
    }
}

static std::wstring LocalStatePath(const wchar_t* fileName)
{
    std::wstring dir = LocalStateDir();
    if (dir.empty())
        return L"";
    return dir + L"\\" + fileName;
}

static std::wstring LocalStateActivationLogPath()
{
    return LocalStatePath(ProductRuntimeFileName(L".appxactivation.log").c_str());
}

static std::wstring LocalStatePendingXmlPath()
{
    return LocalStatePath(ProductRuntimeFileName(L".livetile.pending.xml").c_str());
}

static std::wstring PendingClearPath()
{
    return ProductRuntimeFilePath(L".livetile.clear");
}

static std::wstring LocalStatePendingClearPath()
{
    return LocalStatePath(ProductRuntimeFileName(L".livetile.clear").c_str());
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

static bool TryWideToUtf8(const std::wstring& text, std::string& out)
{
    out.clear();
    if (text.empty())
        return true;

    int need = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (need <= 0)
        return false;

    out.resize(static_cast<size_t>(need));
    int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        text.c_str(),
        static_cast<int>(text.size()),
        out.empty() ? nullptr : &out[0],
        need,
        nullptr,
        nullptr);
    if (written != need)
    {
        out.clear();
        return false;
    }
    return true;
}

static bool WriteAllBytes(HANDLE h, const void* data, DWORD size)
{
    const BYTE* ptr = static_cast<const BYTE*>(data);
    DWORD remaining = size;
    while (remaining > 0)
    {
        DWORD chunk = remaining;
        DWORD written = 0;
        if (!WriteFile(h, ptr, chunk, &written, nullptr))
            return false;
        if (written == 0)
        {
            SetLastError(ERROR_WRITE_FAULT);
            return false;
        }
        ptr += written;
        remaining -= written;
    }
    return true;
}

static bool ReadAllBytes(HANDLE h, std::string& bytes)
{
    char* cursor = bytes.empty() ? nullptr : &bytes[0];
    DWORD remaining = static_cast<DWORD>(bytes.size());
    while (remaining > 0)
    {
        DWORD read = 0;
        if (!ReadFile(h, cursor, remaining, &read, nullptr))
            return false;
        if (read == 0)
        {
            SetLastError(ERROR_READ_FAULT);
            return false;
        }
        cursor += read;
        remaining -= read;
    }
    return true;
}

static void AppendActivationLog(const std::wstring& message)
{
    std::wstring line = L"[" + NowStamp() + L"] pid=" + std::to_wstring(GetCurrentProcessId()) + L" " + message + L"\r\n";
    std::string bytes;
    if (!TryWideToUtf8(line, bytes) || bytes.empty())
        return;

    auto appendToPath = [&](const std::wstring& path) -> bool
    {
        if (path.empty())
            return false;

        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;

        bool ok = true;
        LARGE_INTEGER size{};
        if (GetFileSizeEx(h, &size) && size.QuadPart == 0)
        {
            static const unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
            ok = WriteAllBytes(h, bom, static_cast<DWORD>(sizeof(bom)));
        }

        LARGE_INTEGER eof{};
        if (ok && !SetFilePointerEx(h, eof, nullptr, FILE_END))
            ok = false;
        if (ok)
            ok = WriteAllBytes(h, bytes.data(), static_cast<DWORD>(bytes.size()));
        CloseHandle(h);
        return ok;
    };

    std::wstring mutexName = L"Local\\" + ProductRuntimeBaseName() + L"LiveTileBrokerActivationLogMutex";
    HANDLE logMutex = CreateMutexW(nullptr, FALSE, mutexName.c_str());
    bool logLocked = false;
    if (logMutex != nullptr)
    {
        DWORD wait = WaitForSingleObject(logMutex, 5000);
        logLocked = (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED);
    }

    if (logMutex != nullptr && !logLocked)
    {
        CloseHandle(logMutex);
        return;
    }

    bool wroteLocalState = appendToPath(LocalStateActivationLogPath());
    bool wroteModuleDir = appendToPath(ActivationLogPath());

    if (logLocked)
        ReleaseMutex(logMutex);
    if (logMutex != nullptr)
        CloseHandle(logMutex);

    (void)wroteLocalState;
    (void)wroteModuleDir;
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
    bool ok = ReadAllBytes(h, bytes);
    CloseHandle(h);
    if (!ok)
        return false;

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF)
    {
        bytes.erase(0, 3);
    }

    if (bytes.empty())
        return true;

    int need = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (need <= 0)
        return false;
    out.resize(static_cast<size_t>(need));
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), &out[0], need);
    if (written != need)
    {
        out.clear();
        return false;
    }
    return true;
}

static bool FileExists(const std::wstring& path)
{
    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool ClearLiveTileIfRequested(bool& handled, std::wstring& failure)
{
    handled = false;
    failure.clear();

    std::wstring localClear = LocalStatePendingClearPath();
    bool requested = (!localClear.empty() && FileExists(localClear)) || FileExists(PendingClearPath());
    if (!requested)
        return true;

    handled = true;
    try
    {
        using namespace winrt::Windows::UI::Notifications;
        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        updater.Clear();
        DeleteFileW(PendingClearPath().c_str());
        if (!localClear.empty())
            DeleteFileW(localClear.c_str());
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        wchar_t buf[128]{};
        swprintf_s(buf, L"Live Tile clear failed in packaged broker, hresult=0x%08X", static_cast<unsigned int>(e.code().value));
        failure = buf;
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failure += L", message=" + msg;
        return false;
    }
    catch (...)
    {
        failure = L"Live Tile clear failed in packaged broker due to an unknown exception.";
        return false;
    }
}

static bool UpdateLiveTileFromPendingXml(std::wstring& failure)
{
    failure.clear();
    std::wstring xmlText;
    std::wstring localPending = LocalStatePendingXmlPath();
    std::wstring sourcePath = localPending.empty() ? PendingXmlPath() : localPending;
    if (!ReadUtf8BomTextFile(sourcePath, xmlText) || TrimCopy(xmlText).empty())
    {
        sourcePath = PendingXmlPath();
        if (!ReadUtf8BomTextFile(sourcePath, xmlText) || TrimCopy(xmlText).empty())
        {
            failure = L"No pending Live Tile XML was found for the packaged broker.";
            return false;
        }
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
        std::wstring localPendingAfterUpdate = LocalStatePendingXmlPath();
        if (!localPendingAfterUpdate.empty())
            DeleteFileW(localPendingAfterUpdate.c_str());
        return true;
    }
    catch (const winrt::hresult_error& e)
    {
        wchar_t buf[128]{};
        swprintf_s(buf, L"Live Tile update failed in packaged broker, hresult=0x%08X", static_cast<unsigned int>(e.code().value));
        failure = buf;
        std::wstring msg = e.message().c_str();
        if (!msg.empty())
            failure += L", message=" + msg;
        return false;
    }
    catch (...)
    {
        failure = L"Live Tile update failed in packaged broker due to an unknown exception.";
        return false;
    }
}

struct LiveTileBrokerView : winrt::implements<LiveTileBrokerView, winrt::Windows::ApplicationModel::Core::IFrameworkView>
{
    void Initialize(winrt::Windows::ApplicationModel::Core::CoreApplicationView const&)
    {
        AppendActivationLog(L"packaged WinRT broker Initialize");
        // Do not subscribe to CoreApplicationView::Activated here. The broker has no UI
        // and does not need activation args; avoiding the event handler also keeps this
        // minimal CoreApplication host compatible with older VS/C++/WinRT projections.
    }

    void SetWindow(winrt::Windows::UI::Core::CoreWindow const&)
    {
        AppendActivationLog(L"packaged WinRT broker SetWindow");
    }

    void Load(winrt::hstring const&)
    {
    }

    void Run()
    {
        AppendActivationLog(L"packaged WinRT broker Run");
        std::wstring failure;
        bool clearHandled = false;
        if (!ClearLiveTileIfRequested(clearHandled, failure))
        {
            AppendActivationLog(L"Live Tile packaged broker failed: " + failure);
        }
        else if (clearHandled)
        {
            AppendActivationLog(L"Live Tile cleared under package identity");
        }
        else if (UpdateLiveTileFromPendingXml(failure))
        {
            AppendActivationLog(L"Live Tile update completed under package identity");
        }
        else
        {
            AppendActivationLog(L"Live Tile packaged broker failed: " + failure);
        }

        winrt::Windows::ApplicationModel::Core::CoreApplication::Exit();
    }

    void Uninitialize()
    {
        AppendActivationLog(L"packaged WinRT broker Uninitialize");
    }

};

struct LiveTileBrokerViewSource : winrt::implements<LiveTileBrokerViewSource, winrt::Windows::ApplicationModel::Core::IFrameworkViewSource>
{
    winrt::Windows::ApplicationModel::Core::IFrameworkView CreateView()
    {
        return winrt::make<LiveTileBrokerView>();
    }
};

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    try
    {
        // CoreApplication activation for this Win8/8.1 broker is entered on an MTA path
        // on Windows 10. Initializing as STA/ASTA makes CoreApplication::Run fail with
        // RO_E_MUST_BE_MTA (0x8001010E): "The Application Object must initially be
        // accessed from the multi-thread apartment."
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        AppendActivationLog(L"packaged WinRT broker process started: " + std::wstring(GetCommandLineW()));
        winrt::Windows::ApplicationModel::Core::CoreApplication::Run(winrt::make<LiveTileBrokerViewSource>());
        AppendActivationLog(L"packaged WinRT broker process exiting");
    }
    catch (const winrt::hresult_error& e)
    {
        wchar_t buf[256]{};
        swprintf_s(buf, L"packaged WinRT broker fatal hresult=0x%08X", static_cast<unsigned int>(e.code().value));
        std::wstring message = buf;
        std::wstring detail = e.message().c_str();
        if (!detail.empty())
            message += L", message=" + detail;
        AppendActivationLog(message);
    }
    catch (...)
    {
        AppendActivationLog(L"packaged WinRT broker fatal unknown exception");
    }
    return 0;
}

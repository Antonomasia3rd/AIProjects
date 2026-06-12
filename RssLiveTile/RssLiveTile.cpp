// compile command: cl /std:c++17 /EHsc /W4 /DUNICODE /D_UNICODE RssLiveTile.cpp /link gdiplus.lib winhttp.lib gdi32.lib user32.lib shell32.lib shlwapi.lib ole32.lib windowsapp.lib runtimeobject.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <appmodel.h>
#include <objidl.h>
#include <propidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cwctype>
#include <new>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <winrt/base.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include "..\dependencies\desktop_app_baseline.h"

#ifndef ARRAYSIZE
#define ARRAYSIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

using namespace Gdiplus;
using namespace std::chrono;

static constexpr const wchar_t* APP_NAME = L"RssLiveTile";
static constexpr const wchar_t* APP_DISPLAY_NAME = L"RSS Live Tile";
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"RssLiveTileTrayWnd";
static constexpr UINT WM_RLT_TRAY = WM_APP + 10;
static constexpr UINT WM_RLT_UPDATE_DONE = WM_APP + 11;
static constexpr UINT WM_RLT_CONTROL = WM_APP + 12;
static constexpr WPARAM RLT_CONTROL_EXIT = 1;
static constexpr WPARAM RLT_CONTROL_REFRESH = 2;
static constexpr UINT_PTR UPDATE_TIMER_ID = 1;
static constexpr UINT TRAY_UID = 1;
static constexpr DWORD POWERSHELL_TIMEOUT_MS = 120000;
static constexpr DWORD POWERSHELL_POLL_MS = 50;
static constexpr DWORD POWERSHELL_TERMINATE_WAIT_MS = 5000;

static HINSTANCE g_hInst = nullptr;
static ULONG_PTR g_gdiplusToken = 0;
static aip::SidecarPaths g_paths;
static aip::Utf8Logger g_logger;
static aip::InstanceIdentity g_identity;
static HANDLE g_singleInstanceMutex = nullptr;
static UINT g_taskbarCreatedMessage = 0;

struct AppSettings
{
    std::wstring feedUrl;
    std::wstring userAgent;
    std::wstring manifestDisplayName;
    std::wstring manifestDescription;
    std::wstring manifestIdentityName;
    std::wstring manifestPublisher;
    std::wstring manifestVersion;
    std::wstring manifestBackgroundColor;
    int updateIntervalSeconds = 300;
    int tileRefreshSeconds = 900;
    int maxItems = 5;
    int httpTimeoutSeconds = 30;
    int maxFeedBytes = 1024 * 1024;
    bool showTrayIcon = true;
    bool bootstrapPackageOnLaunch = true;
};

struct AppOptions
{
    bool showHelp = false;
    bool once = false;
    bool requestExit = false;
    bool registerPackage = false;
    bool unregisterPackage = false;
    bool launchPackaged = false;
    bool regenerateManifest = false;
    bool allowMultiple = false;
    bool noBootstrap = false;
    bool forceTray = false;
    bool forceNoTray = false;
    bool manifestSettingChanged = false;
    std::wstring configOverride;
    std::wstring openUrl;
    std::wstring parseError;
    std::vector<aip::IniSetting> writes;
};

struct FeedItem
{
    std::wstring title;
    std::wstring summary;
    std::wstring link;
    std::wstring date;
};

struct FeedSnapshot
{
    bool ok = false;
    std::wstring sourceTitle;
    std::wstring status;
    std::vector<FeedItem> items;
};

struct UpdateResult
{
    bool ok = false;
    std::wstring status;
    FeedSnapshot snapshot;
};

struct RuntimeContext
{
    HWND hwnd = nullptr;
    NOTIFYICONDATAW nid = {};
    bool trayCreated = false;
    std::atomic<bool> closing{ false };
    std::atomic<bool> updateRunning{ false };
    std::atomic<HINTERNET> activeRequest{ nullptr };
    std::thread updateThread;
    std::mutex mutex;
    AppSettings settings;
    FeedSnapshot latest;
    std::unique_ptr<UpdateResult> pendingResult;
    std::wstring lastTileKey;
    steady_clock::time_point lastTileUpdate = steady_clock::time_point::min();
};

static std::wstring VFormatString(const wchar_t* fmt, va_list args)
{
    if (fmt == nullptr)
    {
        return L"";
    }

    va_list copy;
    va_copy(copy, args);
    int needed = _vscwprintf(fmt, copy);
    va_end(copy);
    if (needed <= 0)
    {
        return fmt;
    }

    std::wstring out(static_cast<size_t>(needed), L'\0');
    vswprintf_s(out.data(), out.size() + 1, fmt, args);
    return out;
}

static std::wstring FormatString(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    std::wstring out = VFormatString(fmt, args);
    va_end(args);
    return out;
}

static void LogText(const std::wstring& message)
{
    g_logger.Write(L"info", message);
}

static void LogWarn(const std::wstring& message)
{
    g_logger.Write(L"warn", message);
}

static void LogError(const std::wstring& message)
{
    g_logger.Write(L"error", message);
}

static void Logf(const wchar_t* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    g_logger.Write(L"info", VFormatString(fmt, args));
    va_end(args);
}

static bool WriteCommandLineTextToHandle(HANDLE handle, const std::wstring& output)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode))
    {
        DWORD written = 0;
        return WriteConsoleW(
            handle,
            output.c_str(),
            static_cast<DWORD>(output.size()),
            &written,
            nullptr) != FALSE;
    }

    std::string utf8;
    if (!aip::TryWideToUtf8(output, utf8))
    {
        return false;
    }
    return utf8.empty() ||
        aip::WriteAllBytes(handle, utf8.data(), static_cast<DWORD>(utf8.size()));
}

static bool WriteCommandLineText(const std::wstring& text, bool errorOutput)
{
    std::wstring output = text;
    if (output.empty() || (output.back() != L'\r' && output.back() != L'\n'))
    {
        output += L"\r\n";
    }

    HANDLE handle = GetStdHandle(errorOutput ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (WriteCommandLineTextToHandle(handle, output))
    {
        return true;
    }

    bool attached = AttachConsole(ATTACH_PARENT_PROCESS) != FALSE;
    if (!attached && GetLastError() != ERROR_ACCESS_DENIED)
    {
        return false;
    }

    handle = GetStdHandle(errorOutput ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    bool ok = WriteCommandLineTextToHandle(handle, output);
    if (attached)
    {
        FreeConsole();
    }
    return ok;
}

static void ShowCommandLineMessage(const std::wstring& text, bool error)
{
    if (!WriteCommandLineText(text, error))
    {
        MessageBoxW(
            nullptr,
            text.c_str(),
            APP_DISPLAY_NAME,
            MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
    }
}

static std::wstring GetFileName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring XmlEscape(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

static std::wstring CollapseWhitespace(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    bool inWhitespace = false;
    for (wchar_t ch : value)
    {
        if (iswspace(ch))
        {
            if (!out.empty())
            {
                inWhitespace = true;
            }
            continue;
        }
        if (inWhitespace && !out.empty())
        {
            out.push_back(L' ');
        }
        out.push_back(ch);
        inWhitespace = false;
    }
    return aip::Trim(out);
}

static std::wstring StripHtmlTags(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    bool inTag = false;
    for (wchar_t ch : value)
    {
        if (ch == L'<')
        {
            inTag = true;
            out.push_back(L' ');
            continue;
        }
        if (ch == L'>')
        {
            inTag = false;
            out.push_back(L' ');
            continue;
        }
        if (!inTag)
        {
            out.push_back(ch);
        }
    }
    return CollapseWhitespace(out);
}

static std::wstring LimitText(std::wstring value, size_t maxChars)
{
    value = CollapseWhitespace(value);
    if (value.size() <= maxChars)
    {
        return value;
    }
    if (maxChars <= 3)
    {
        return value.substr(0, maxChars);
    }
    value.resize(maxChars - 3);
    while (!value.empty() && iswspace(value.back()))
    {
        value.pop_back();
    }
    value += L"...";
    return value;
}

static std::wstring QuoteCommandLineArg(const std::wstring& value)
{
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : value)
    {
        if (ch == L'\\')
        {
            ++slashCount;
            continue;
        }
        if (ch == L'"')
        {
            out.append(slashCount * 2 + 1, L'\\');
            out.push_back(ch);
            slashCount = 0;
            continue;
        }
        out.append(slashCount, L'\\');
        slashCount = 0;
        out.push_back(ch);
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
}

static std::wstring PowerShellSingleQuotedString(const std::wstring& value)
{
    std::wstring out = L"'";
    for (wchar_t ch : value)
    {
        if (ch == L'\'')
        {
            out += L"''";
        }
        else
        {
            out.push_back(ch);
        }
    }
    out.push_back(L'\'');
    return out;
}

static std::wstring Base64EncodeBytes(const BYTE* data, size_t len)
{
    static const wchar_t table[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        uint32_t b0 = data[i];
        uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
        uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
        uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(table[(triple >> 18) & 0x3F]);
        out.push_back(table[(triple >> 12) & 0x3F]);
        out.push_back(i + 1 < len ? table[(triple >> 6) & 0x3F] : L'=');
        out.push_back(i + 2 < len ? table[triple & 0x3F] : L'=');
    }
    return out;
}

static std::wstring PowerShellEncodedCommand(const std::wstring& command)
{
    return Base64EncodeBytes(
        reinterpret_cast<const BYTE*>(command.data()),
        command.size() * sizeof(wchar_t));
}

static std::wstring PowerShellUtf8Preamble()
{
    return L"[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false); "
        L"$OutputEncoding = [Console]::OutputEncoding; ";
}

static std::wstring DefaultPowerShellExe()
{
    wchar_t systemDirectory[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    if (len > 0 && len < ARRAYSIZE(systemDirectory))
    {
        return std::wstring(systemDirectory) + L"\\WindowsPowerShell\\v1.0\\powershell.exe";
    }
    return L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe";
}

static std::wstring DecodeOutputBytes(const std::vector<BYTE>& bytes)
{
    std::wstring out;
    if (bytes.empty())
    {
        return out;
    }
    if (aip::DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, 0))
    {
        return out;
    }
    if (aip::DecodeCodePageText(CP_ACP, bytes.data(), bytes.size(), out, 0))
    {
        return out;
    }
    return L"";
}

static std::wstring LastNonEmptyLine(const std::wstring& text)
{
    std::wstring current;
    std::wstring last;
    for (wchar_t ch : text)
    {
        if (ch == L'\r' || ch == L'\n')
        {
            current = aip::Trim(current);
            if (!current.empty())
            {
                last = current;
            }
            current.clear();
            continue;
        }
        current.push_back(ch);
    }
    current = aip::Trim(current);
    if (!current.empty())
    {
        last = current;
    }
    return last;
}

static bool RunPowerShellCommand(const std::wstring& command, std::wstring& output, DWORD& exitCode)
{
    output.clear();
    exitCode = 1;

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0))
    {
        output = L"CreatePipe failed: " + aip::GetLastErrorText(GetLastError());
        return false;
    }
    if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
    {
        DWORD err = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        output = L"SetHandleInformation failed: " + aip::GetLastErrorText(err);
        return false;
    }

    HANDLE nulIn = CreateFileW(
        L"NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &sa,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (nulIn == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        output = L"CreateFileW(NUL) failed: " + aip::GetLastErrorText(err);
        return false;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = nulIn;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = QuoteCommandLineArg(DefaultPowerShellExe()) +
        L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand " +
        PowerShellEncodedCommand(PowerShellUtf8Preamble() + command);

    BOOL started = CreateProcessW(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &si,
        &pi);

    CloseHandle(writePipe);
    CloseHandle(nulIn);
    if (!started)
    {
        DWORD err = GetLastError();
        CloseHandle(readPipe);
        output = L"CreateProcessW(PowerShell) failed: " + aip::GetLastErrorText(err);
        return false;
    }

    std::vector<BYTE> outBytes;
    auto drainPipe = [&]()
    {
        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(readPipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
            {
                break;
            }

            BYTE buffer[4096];
            DWORD toRead = std::min<DWORD>(available, static_cast<DWORD>(sizeof(buffer)));
            DWORD read = 0;
            if (!ReadFile(readPipe, buffer, toRead, &read, nullptr) || read == 0)
            {
                break;
            }
            outBytes.insert(outBytes.end(), buffer, buffer + read);
        }
    };

    ULONGLONG start = GetTickCount64();
    bool timedOut = false;
    bool waitFailed = false;
    DWORD waitError = ERROR_SUCCESS;
    for (;;)
    {
        DWORD wait = WaitForSingleObject(pi.hProcess, POWERSHELL_POLL_MS);
        drainPipe();
        if (wait == WAIT_OBJECT_0)
        {
            break;
        }
        if (wait == WAIT_FAILED)
        {
            waitFailed = true;
            waitError = GetLastError();
            TerminateProcess(pi.hProcess, waitError);
            WaitForSingleObject(pi.hProcess, POWERSHELL_TERMINATE_WAIT_MS);
            break;
        }
        if (GetTickCount64() - start >= POWERSHELL_TIMEOUT_MS)
        {
            timedOut = true;
            TerminateProcess(pi.hProcess, 1460);
            WaitForSingleObject(pi.hProcess, POWERSHELL_TERMINATE_WAIT_MS);
            break;
        }
    }

    drainPipe();
    if (!GetExitCodeProcess(pi.hProcess, &exitCode))
    {
        exitCode = 1;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    CloseHandle(readPipe);

    output = aip::Trim(DecodeOutputBytes(outBytes));
    if (timedOut)
    {
        if (!output.empty())
        {
            output += L"\r\n";
        }
        output += L"PowerShell command timed out.";
        return false;
    }
    if (waitFailed)
    {
        if (!output.empty())
        {
            output += L"\r\n";
        }
        output += L"PowerShell process wait failed: " + aip::GetLastErrorText(waitError);
        return false;
    }
    return exitCode == 0;
}

static bool CurrentProcessHasPackageIdentity()
{
    UINT32 length = 0;
    LONG rc = GetCurrentPackageFullName(&length, nullptr);
    return rc == ERROR_INSUFFICIENT_BUFFER;
}

static std::wstring ManifestPath()
{
    return aip::PathJoin(g_paths.exeDir, L"AppxManifest.xml");
}

static std::wstring AssetsDir()
{
    return aip::PathJoin(g_paths.exeDir, L"Assets");
}

static std::wstring AssetPath(const wchar_t* fileName)
{
    return aip::PathJoin(AssetsDir(), fileName);
}

static bool FindPngEncoder(CLSID& clsid)
{
    UINT count = 0;
    UINT bytes = 0;
    if (GetImageEncodersSize(&count, &bytes) != Ok || count == 0 || bytes == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(bytes);
    ImageCodecInfo* info = reinterpret_cast<ImageCodecInfo*>(buffer.data());
    if (GetImageEncoders(count, bytes, info) != Ok)
    {
        return false;
    }

    for (UINT i = 0; i < count; ++i)
    {
        if (info[i].MimeType != nullptr && wcscmp(info[i].MimeType, L"image/png") == 0)
        {
            clsid = info[i].Clsid;
            return true;
        }
    }
    return false;
}

static bool SavePng(Bitmap& bitmap, const std::wstring& path)
{
    aip::EnsureDirectory(aip::GetDirectoryName(path));
    CLSID png{};
    if (!FindPngEncoder(png))
    {
        SetLastError(ERROR_NOT_FOUND);
        return false;
    }

    std::wstring tempPath = path +
        L".tmp." +
        std::to_wstring(GetCurrentProcessId()) +
        L"." +
        std::to_wstring(GetCurrentThreadId());
    DeleteFileW(tempPath.c_str());
    Status status = bitmap.Save(tempPath.c_str(), &png, nullptr);
    if (status != Ok)
    {
        DeleteFileW(tempPath.c_str());
        SetLastError(ERROR_WRITE_FAULT);
        return false;
    }

    if (!MoveFileExW(
            tempPath.c_str(),
            path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD error = GetLastError();
        DeleteFileW(tempPath.c_str());
        SetLastError(error);
        return false;
    }
    return true;
}

static bool IsExpectedPng(const std::wstring& path, UINT width, UINT height)
{
    if (!aip::FileExists(path))
    {
        return false;
    }
    Bitmap bitmap(path.c_str(), FALSE);
    return bitmap.GetLastStatus() == Ok &&
        bitmap.GetWidth() == width &&
        bitmap.GetHeight() == height;
}

static bool RenderLogoPng(const std::wstring& path, int width, int height, const wchar_t* label)
{
    Bitmap bitmap(width, height, PixelFormat32bppARGB);
    if (bitmap.GetLastStatus() != Ok)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return false;
    }

    Graphics graphics(&bitmap);
    graphics.SetSmoothingMode(SmoothingModeAntiAlias);
    graphics.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
    graphics.Clear(Color(255, 0, 90, 158));

    SolidBrush accent(Color(255, 255, 185, 45));
    SolidBrush textBrush(Color(255, 255, 255, 255));
    Pen ringPen(Color(210, 255, 255, 255), std::max(2.0f, width / 28.0f));

    const float unit = static_cast<float>(std::min(width, height));
    graphics.FillEllipse(&accent, RectF(unit * 0.11f, unit * 0.11f, unit * 0.22f, unit * 0.22f));
    graphics.DrawArc(&ringPen, RectF(unit * 0.05f, unit * 0.05f, unit * 0.54f, unit * 0.54f), 0.0f, 90.0f);
    graphics.DrawArc(&ringPen, RectF(unit * -0.08f, unit * -0.08f, unit * 0.86f, unit * 0.86f), 0.0f, 90.0f);

    FontFamily family(L"Segoe UI");
    const bool wide = width > height * 2;
    REAL fontSize = wide ? std::max<REAL>(18.0f, height * 0.30f) : std::max<REAL>(16.0f, unit * 0.28f);
    Font font(&family, fontSize, FontStyleBold, UnitPixel);
    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
    RectF box(0.0f, wide ? height * 0.10f : unit * 0.22f, static_cast<REAL>(width), static_cast<REAL>(height) * 0.82f);
    graphics.DrawString(label, -1, &font, box, &format, &textBrush);

    return SavePng(bitmap, path);
}

static bool EnsureDefaultAssets(bool force)
{
    struct Asset
    {
        const wchar_t* name;
        int width;
        int height;
        const wchar_t* label;
    };

    static const Asset assets[] = {
        { L"StoreLogo.png", 50, 50, L"RSS" },
        { L"Square44x44Logo.png", 44, 44, L"R" },
        { L"Square71x71Logo.png", 71, 71, L"R" },
        { L"Square150x150Logo.png", 150, 150, L"RSS" },
        { L"Wide310x150Logo.png", 310, 150, L"RSS Live Tile" },
        { L"Square310x310Logo.png", 310, 310, L"RSS" },
    };

    for (const Asset& asset : assets)
    {
        std::wstring path = AssetPath(asset.name);
        if (!force &&
            IsExpectedPng(
                path,
                static_cast<UINT>(asset.width),
                static_cast<UINT>(asset.height)))
        {
            continue;
        }
        if (!RenderLogoPng(path, asset.width, asset.height, asset.label))
        {
            LogError(L"Could not write asset " + path + L": " + aip::GetLastErrorText(GetLastError()));
            return false;
        }
    }
    return true;
}

static std::wstring BuildAppxManifest(const AppSettings& settings)
{
    std::wstring displayName = XmlEscape(settings.manifestDisplayName);
    std::wstring description = XmlEscape(settings.manifestDescription);
    std::wstring identity = XmlEscape(settings.manifestIdentityName);
    std::wstring publisher = XmlEscape(settings.manifestPublisher);
    std::wstring version = XmlEscape(settings.manifestVersion);
    std::wstring background = XmlEscape(settings.manifestBackgroundColor);
    std::wstring executable = XmlEscape(GetFileName(g_paths.exePath));

    std::wstring manifest;
    manifest += L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    manifest += L"<Package\r\n";
    manifest += L"  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\r\n";
    manifest += L"  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\r\n";
    manifest += L"  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n";
    manifest += L"  IgnorableNamespaces=\"uap rescap\">\r\n\r\n";
    manifest += L"  <Identity Name=\"" + identity + L"\" Publisher=\"" + publisher + L"\" Version=\"" + version + L"\" />\r\n\r\n";
    manifest += L"  <Properties>\r\n";
    manifest += L"    <DisplayName>" + displayName + L"</DisplayName>\r\n";
    manifest += L"    <PublisherDisplayName>" + displayName + L"</PublisherDisplayName>\r\n";
    manifest += L"    <Logo>Assets\\StoreLogo.png</Logo>\r\n";
    manifest += L"  </Properties>\r\n\r\n";
    manifest += L"  <Dependencies>\r\n";
    manifest += L"    <TargetDeviceFamily Name=\"Windows.Desktop\" MinVersion=\"10.0.10240.0\" MaxVersionTested=\"10.0.26100.0\" />\r\n";
    manifest += L"  </Dependencies>\r\n\r\n";
    manifest += L"  <Resources>\r\n";
    manifest += L"    <Resource Language=\"en-us\" />\r\n";
    manifest += L"  </Resources>\r\n\r\n";
    manifest += L"  <Applications>\r\n";
    manifest += L"    <Application Id=\"App\" Executable=\"" + executable + L"\" EntryPoint=\"Windows.FullTrustApplication\">\r\n";
    manifest += L"      <uap:VisualElements DisplayName=\"" + displayName + L"\" Description=\"" + description + L"\" Square150x150Logo=\"Assets\\Square150x150Logo.png\" Square44x44Logo=\"Assets\\Square44x44Logo.png\" BackgroundColor=\"" + background + L"\">\r\n";
    manifest += L"        <uap:DefaultTile Square71x71Logo=\"Assets\\Square71x71Logo.png\" Wide310x150Logo=\"Assets\\Wide310x150Logo.png\" Square310x310Logo=\"Assets\\Square310x310Logo.png\">\r\n";
    manifest += L"          <uap:ShowNameOnTiles>\r\n";
    manifest += L"            <uap:ShowOn Tile=\"square150x150Logo\" />\r\n";
    manifest += L"            <uap:ShowOn Tile=\"wide310x150Logo\" />\r\n";
    manifest += L"            <uap:ShowOn Tile=\"square310x310Logo\" />\r\n";
    manifest += L"          </uap:ShowNameOnTiles>\r\n";
    manifest += L"        </uap:DefaultTile>\r\n";
    manifest += L"      </uap:VisualElements>\r\n";
    manifest += L"    </Application>\r\n";
    manifest += L"  </Applications>\r\n\r\n";
    manifest += L"  <Capabilities>\r\n";
    manifest += L"    <rescap:Capability Name=\"runFullTrust\" />\r\n";
    manifest += L"  </Capabilities>\r\n";
    manifest += L"</Package>\r\n";
    return manifest;
}

static const aip::IniDefaultValue kDefaultSettings[] = {
    { L"Settings", L"FeedUrl", L"https://blogs.windows.com/windowsexperience/feed/" },
    { L"Settings", L"UpdateIntervalSeconds", L"300" },
    { L"Settings", L"TileRefreshSeconds", L"900" },
    { L"Settings", L"MaxItems", L"5" },
    { L"Settings", L"ShowTrayIcon", L"1" },
    { L"Settings", L"BootstrapPackageOnLaunch", L"1" },
    { L"Settings", L"UserAgent", L"RssLiveTile/1.0" },
    { L"Settings", L"HttpTimeoutSeconds", L"30" },
    { L"Settings", L"MaxFeedBytes", L"1048576" },
    { L"Manifest", L"DisplayName", L"RSS Live Tile" },
    { L"Manifest", L"Description", L"RSS feed Live Tile updater" },
    { L"Manifest", L"IdentityName", L"RssLiveTile.App" },
    { L"Manifest", L"Publisher", L"CN=RssLiveTile" },
    { L"Manifest", L"Version", L"1.0.0.0" },
    { L"Manifest", L"BackgroundColor", L"#005A9E" },
};

static const wchar_t* kIniHeader =
    L"# RSS Live Tile settings\r\n"
    L"# FeedUrl can point to an RSS 2.0 or Atom feed. Live Tile updates require package identity,\r\n"
    L"# so a normal first launch registers AppxManifest.xml and relaunches the packaged entry.\r\n";

static bool EnsureDefaultSettingsFile()
{
    aip::IniConfigStore store(g_paths.configPath, kIniHeader, 5000);
    if (!store.EnsureDefaults(kDefaultSettings, ARRAYSIZE(kDefaultSettings)))
    {
        LogError(L"Could not create default INI " + g_paths.configPath + L": " + aip::GetLastErrorText(GetLastError()));
        return false;
    }
    return true;
}

static std::wstring IniRead(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    return aip::IniReadRaw(g_paths.configPath, section, key, fallback);
}

static int IniReadInt(const wchar_t* section, const wchar_t* key, int fallback, int minValue, int maxValue)
{
    int parsed = 0;
    if (aip::ParseIntValueInRange(IniRead(section, key, L""), minValue, maxValue, parsed))
    {
        return parsed;
    }
    return fallback;
}

static bool IniReadBool(const wchar_t* section, const wchar_t* key, bool fallback)
{
    bool parsed = false;
    if (aip::ParseBoolValue(IniRead(section, key, L""), parsed))
    {
        return parsed;
    }
    return fallback;
}

static AppSettings LoadSettings()
{
    AppSettings settings;
    settings.feedUrl = aip::Trim(IniRead(L"Settings", L"FeedUrl", L""));
    settings.updateIntervalSeconds = IniReadInt(L"Settings", L"UpdateIntervalSeconds", 300, 15, 86400);
    settings.tileRefreshSeconds = IniReadInt(L"Settings", L"TileRefreshSeconds", 900, 0, 86400);
    settings.maxItems = IniReadInt(L"Settings", L"MaxItems", 5, 1, 5);
    settings.showTrayIcon = IniReadBool(L"Settings", L"ShowTrayIcon", true);
    settings.bootstrapPackageOnLaunch = IniReadBool(L"Settings", L"BootstrapPackageOnLaunch", true);
    settings.userAgent = aip::Trim(IniRead(L"Settings", L"UserAgent", L"RssLiveTile/1.0"));
    settings.httpTimeoutSeconds = IniReadInt(L"Settings", L"HttpTimeoutSeconds", 30, 5, 300);
    settings.maxFeedBytes = IniReadInt(L"Settings", L"MaxFeedBytes", 1024 * 1024, 4096, 5 * 1024 * 1024);
    settings.manifestDisplayName = aip::Trim(IniRead(L"Manifest", L"DisplayName", L"RSS Live Tile"));
    settings.manifestDescription = aip::Trim(IniRead(L"Manifest", L"Description", L"RSS feed Live Tile updater"));
    settings.manifestIdentityName = aip::Trim(IniRead(L"Manifest", L"IdentityName", L"RssLiveTile.App"));
    settings.manifestPublisher = aip::Trim(IniRead(L"Manifest", L"Publisher", L"CN=RssLiveTile"));
    settings.manifestVersion = aip::Trim(IniRead(L"Manifest", L"Version", L"1.0.0.0"));
    settings.manifestBackgroundColor = aip::Trim(IniRead(L"Manifest", L"BackgroundColor", L"#005A9E"));

    if (settings.userAgent.empty())
    {
        settings.userAgent = L"RssLiveTile/1.0";
    }
    if (settings.manifestDisplayName.empty())
    {
        settings.manifestDisplayName = APP_DISPLAY_NAME;
    }
    if (settings.manifestDescription.empty())
    {
        settings.manifestDescription = L"RSS feed Live Tile updater";
    }
    if (settings.manifestIdentityName.empty())
    {
        settings.manifestIdentityName = L"RssLiveTile.App";
    }
    if (settings.manifestPublisher.empty())
    {
        settings.manifestPublisher = L"CN=RssLiveTile";
    }
    if (settings.manifestVersion.empty())
    {
        settings.manifestVersion = L"1.0.0.0";
    }
    if (settings.manifestBackgroundColor.empty())
    {
        settings.manifestBackgroundColor = L"#005A9E";
    }
    return settings;
}

static bool ValidateFeedUrl(const std::wstring& value, std::wstring& error)
{
    std::wstring trimmed = aip::Trim(value);
    if (trimmed.empty())
    {
        error = L"FeedUrl cannot be empty.";
        return false;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(trimmed.c_str(), 0, 0, &components) ||
        (components.nScheme != INTERNET_SCHEME_HTTP &&
            components.nScheme != INTERNET_SCHEME_HTTPS) ||
        components.dwHostNameLength == 0)
    {
        error = L"FeedUrl must be a valid http or https URL.";
        return false;
    }
    return true;
}

static bool IsHttpUrl(const std::wstring& value)
{
    std::wstring error;
    return ValidateFeedUrl(value, error);
}

static bool ValidateManifestIdentity(const std::wstring& value)
{
    std::wstring trimmed = aip::Trim(value);
    if (trimmed.size() < 3 || trimmed.size() > 50)
    {
        return false;
    }
    return std::all_of(trimmed.begin(), trimmed.end(), [](wchar_t ch)
    {
        return
            (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'a' && ch <= L'z') ||
            (ch >= L'0' && ch <= L'9') ||
            ch == L'.' ||
            ch == L'-';
    });
}

static bool ValidateManifestVersion(const std::wstring& value)
{
    size_t start = 0;
    int components = 0;
    while (start <= value.size())
    {
        size_t end = value.find(L'.', start);
        std::wstring part = end == std::wstring::npos
            ? value.substr(start)
            : value.substr(start, end - start);
        int number = 0;
        if (!aip::ParseIntValueInRange(part, 0, 65535, number))
        {
            return false;
        }
        ++components;
        if (end == std::wstring::npos)
        {
            break;
        }
        start = end + 1;
    }
    return components == 4;
}

static bool ValidateSetting(const aip::IniSetting& setting, std::wstring& error)
{
    auto matches = [&](const wchar_t* section, const wchar_t* key)
    {
        return aip::EqualsI(setting.section, section) && aip::EqualsI(setting.key, key);
    };
    auto requireInt = [&](int minValue, int maxValue)
    {
        int parsed = 0;
        return aip::ParseIntValueInRange(setting.value, minValue, maxValue, parsed);
    };
    auto requireBool = [&]()
    {
        bool parsed = false;
        return aip::ParseBoolValue(setting.value, parsed);
    };

    if (matches(L"Settings", L"FeedUrl"))
    {
        return ValidateFeedUrl(setting.value, error);
    }
    if (matches(L"Settings", L"UpdateIntervalSeconds") && !requireInt(15, 86400))
    {
        error = L"UpdateIntervalSeconds must be between 15 and 86400.";
        return false;
    }
    if (matches(L"Settings", L"TileRefreshSeconds") && !requireInt(0, 86400))
    {
        error = L"TileRefreshSeconds must be between 0 and 86400.";
        return false;
    }
    if (matches(L"Settings", L"MaxItems") && !requireInt(1, 5))
    {
        error = L"MaxItems must be between 1 and 5.";
        return false;
    }
    if (matches(L"Settings", L"HttpTimeoutSeconds") && !requireInt(5, 300))
    {
        error = L"HttpTimeoutSeconds must be between 5 and 300.";
        return false;
    }
    if (matches(L"Settings", L"MaxFeedBytes") && !requireInt(4096, 5 * 1024 * 1024))
    {
        error = L"MaxFeedBytes must be between 4096 and 5242880.";
        return false;
    }
    if ((matches(L"Settings", L"ShowTrayIcon") ||
            matches(L"Settings", L"BootstrapPackageOnLaunch")) &&
        !requireBool())
    {
        error = setting.key + L" expects a boolean value.";
        return false;
    }
    if (matches(L"Settings", L"UserAgent") && aip::Trim(setting.value).empty())
    {
        error = L"UserAgent cannot be empty.";
        return false;
    }
    if (matches(L"Manifest", L"IdentityName") && !ValidateManifestIdentity(setting.value))
    {
        error = L"Manifest.IdentityName must be 3-50 letters, digits, periods, or hyphens.";
        return false;
    }
    if (matches(L"Manifest", L"Version") && !ValidateManifestVersion(aip::Trim(setting.value)))
    {
        error = L"Manifest.Version must contain four numbers between 0 and 65535.";
        return false;
    }
    if ((matches(L"Manifest", L"DisplayName") ||
            matches(L"Manifest", L"Description") ||
            matches(L"Manifest", L"Publisher") ||
            matches(L"Manifest", L"BackgroundColor")) &&
        aip::Trim(setting.value).empty())
    {
        error = setting.section + L"." + setting.key + L" cannot be empty.";
        return false;
    }
    return true;
}

static bool WriteIniSetting(const aip::IniSetting& setting)
{
    aip::IniConfigStore store(g_paths.configPath, kIniHeader, 5000);
    if (!store.WriteRaw(setting.section.c_str(), setting.key.c_str(), setting.value))
    {
        LogError(L"Could not write INI setting " + setting.section + L"." + setting.key + L": " + aip::GetLastErrorText(GetLastError()));
        return false;
    }
    return true;
}

static bool ValidatePackageSettings(const AppSettings& settings, std::wstring& error)
{
    if (!ValidateManifestIdentity(settings.manifestIdentityName))
    {
        error = L"Manifest.IdentityName must be 3-50 ASCII letters, digits, periods, or hyphens.";
        return false;
    }
    if (!ValidateManifestVersion(settings.manifestVersion))
    {
        error = L"Manifest.Version must contain four numbers between 0 and 65535.";
        return false;
    }
    if (settings.manifestDisplayName.empty() ||
        settings.manifestDescription.empty() ||
        settings.manifestPublisher.empty() ||
        settings.manifestBackgroundColor.empty())
    {
        error = L"Manifest display name, description, publisher, and background color cannot be empty.";
        return false;
    }
    return true;
}

static bool EnsurePackageFiles(bool force)
{
    AppSettings settings = LoadSettings();
    std::wstring settingsError;
    if (!ValidatePackageSettings(settings, settingsError))
    {
        LogError(L"Cannot generate package files: " + settingsError);
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }
    if (!EnsureDefaultAssets(force))
    {
        return false;
    }

    std::wstring manifestPath = ManifestPath();
    std::wstring expectedManifest = BuildAppxManifest(settings);
    if (!force && aip::FileExists(manifestPath))
    {
        std::wstring existingManifest;
        if (aip::ReadTextFileUtf8BomAware(manifestPath, existingManifest) &&
            existingManifest == expectedManifest)
        {
            return true;
        }
        LogText(L"Regenerating stale package manifest.");
    }

    if (!aip::WriteTextFileUtf8Bom(manifestPath, expectedManifest))
    {
        LogError(L"Could not write " + manifestPath + L": " + aip::GetLastErrorText(GetLastError()));
        return false;
    }
    LogText(L"Wrote " + manifestPath);
    return true;
}

static bool RegisterPackage(std::wstring* packageFamilyName)
{
    std::wstring manifest = ManifestPath();
    std::wstring output;
    DWORD exitCode = 1;
    std::wstring identity = LoadSettings().manifestIdentityName;
    std::wstring ps =
        L"$ErrorActionPreference='Stop'; "
        L"Add-AppxPackage -Register " + PowerShellSingleQuotedString(manifest) + L" -ForceUpdateFromAnyVersion; "
        L"(Get-AppxPackage -Name " + PowerShellSingleQuotedString(identity) + L" | Select-Object -First 1 -ExpandProperty PackageFamilyName)";

    LogText(L"Registering loose Appx package.");
    bool ok = RunPowerShellCommand(ps, output, exitCode);
    if (!output.empty())
    {
        LogText(L"PowerShell output: " + output);
    }
    if (!ok)
    {
        LogError(L"Package registration failed with exit code " + std::to_wstring(exitCode) + L".");
        return false;
    }

    std::wstring family = LastNonEmptyLine(output);
    if (family.empty())
    {
        LogError(L"Package registration did not return a PackageFamilyName.");
        return false;
    }
    if (packageFamilyName != nullptr)
    {
        *packageFamilyName = family;
    }
    LogText(L"Registered package family: " + family);
    return true;
}

static bool LookupPackageFamilyName(std::wstring& packageFamilyName)
{
    packageFamilyName.clear();
    std::wstring output;
    DWORD exitCode = 1;
    std::wstring identity = LoadSettings().manifestIdentityName;
    std::wstring ps =
        L"$ErrorActionPreference='Stop'; "
        L"(Get-AppxPackage -Name " + PowerShellSingleQuotedString(identity) + L" | Select-Object -First 1 -ExpandProperty PackageFamilyName)";

    if (!RunPowerShellCommand(ps, output, exitCode))
    {
        LogError(L"Package lookup failed with exit code " + std::to_wstring(exitCode) + L". " + output);
        return false;
    }
    packageFamilyName = LastNonEmptyLine(output);
    if (packageFamilyName.empty())
    {
        LogError(L"Package lookup did not find " + identity + L".");
        return false;
    }
    return true;
}

static bool UnregisterPackage()
{
    std::wstring output;
    DWORD exitCode = 1;
    std::wstring identity = LoadSettings().manifestIdentityName;
    std::wstring ps =
        L"$ErrorActionPreference='Stop'; "
        L"$packages = @(Get-AppxPackage -Name " + PowerShellSingleQuotedString(identity) + L"); "
        L"foreach ($package in $packages) { Remove-AppxPackage -Package $package.PackageFullName }; "
        L"Write-Output ('Removed {0} package(s).' -f $packages.Count)";

    LogText(L"Unregistering loose Appx package.");
    bool ok = RunPowerShellCommand(ps, output, exitCode);
    if (!output.empty())
    {
        LogText(L"PowerShell output: " + output);
    }
    if (!ok)
    {
        LogError(L"Package removal failed with exit code " + std::to_wstring(exitCode) + L".");
        return false;
    }
    return true;
}

static bool ActivatePackageFamily(const std::wstring& packageFamilyName, const wchar_t* arguments)
{
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool shouldUninit = SUCCEEDED(hrInit);
    if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE)
    {
        LogError(FormatString(L"CoInitializeEx failed: 0x%08X", static_cast<unsigned int>(hrInit)));
        return false;
    }

    IApplicationActivationManager* manager = nullptr;
    HRESULT hr = CoCreateInstance(
        CLSID_ApplicationActivationManager,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&manager));
    if (FAILED(hr) || manager == nullptr)
    {
        if (shouldUninit)
        {
            CoUninitialize();
        }
        LogError(FormatString(L"CoCreateInstance(IApplicationActivationManager) failed: 0x%08X", static_cast<unsigned int>(hr)));
        return false;
    }

    std::wstring appUserModelId = packageFamilyName + L"!App";
    DWORD pid = 0;
    hr = manager->ActivateApplication(appUserModelId.c_str(), arguments ? arguments : L"", AO_NOERRORUI, &pid);
    manager->Release();
    if (shouldUninit)
    {
        CoUninitialize();
    }

    if (FAILED(hr))
    {
        LogError(FormatString(L"ActivateApplication(%s) failed: 0x%08X", appUserModelId.c_str(), static_cast<unsigned int>(hr)));
        return false;
    }

    Logf(L"Activated %s as pid %lu.", appUserModelId.c_str(), static_cast<unsigned long>(pid));
    return true;
}

static bool LaunchPackagedInstance(const std::wstring& arguments)
{
    std::wstring family;
    return LookupPackageFamilyName(family) &&
        ActivatePackageFamily(family, arguments.c_str());
}

class WinrtApartment
{
public:
    WinrtApartment()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            initialized_ = true;
        }
        catch (const winrt::hresult_error& ex)
        {
            if (ex.code().value != RPC_E_CHANGED_MODE)
            {
                throw;
            }
        }
    }

    ~WinrtApartment()
    {
        if (initialized_)
        {
            winrt::uninit_apartment();
        }
    }

    WinrtApartment(const WinrtApartment&) = delete;
    WinrtApartment& operator=(const WinrtApartment&) = delete;

private:
    bool initialized_ = false;
};

static std::wstring ExtractCharset(const std::wstring& value)
{
    std::wstring lowered = aip::ToLower(value);
    size_t marker = lowered.find(L"charset");
    if (marker == std::wstring::npos)
    {
        return L"";
    }
    size_t equals = lowered.find(L'=', marker + 7);
    if (equals == std::wstring::npos)
    {
        return L"";
    }
    size_t start = equals + 1;
    while (start < lowered.size() && iswspace(lowered[start]))
    {
        ++start;
    }
    wchar_t quote = L'\0';
    if (start < lowered.size() &&
        (lowered[start] == L'\'' || lowered[start] == L'"'))
    {
        quote = lowered[start++];
    }
    size_t end = start;
    while (end < lowered.size())
    {
        wchar_t ch = lowered[end];
        if ((quote != L'\0' && ch == quote) ||
            (quote == L'\0' && (ch == L';' || iswspace(ch))))
        {
            break;
        }
        ++end;
    }
    return lowered.substr(start, end - start);
}

static std::wstring XmlDeclaredEncoding(const std::vector<BYTE>& bytes)
{
    size_t count = std::min<size_t>(bytes.size(), 512);
    std::wstring prefix;
    prefix.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        BYTE ch = bytes[i];
        prefix.push_back(ch < 0x80 ? static_cast<wchar_t>(ch) : L' ');
    }
    std::wstring lowered = aip::ToLower(prefix);
    size_t declaration = lowered.find(L"<?xml");
    size_t declarationEnd = lowered.find(L"?>", declaration);
    if (declaration == std::wstring::npos || declarationEnd == std::wstring::npos)
    {
        return L"";
    }
    std::wstring xmlDeclaration =
        lowered.substr(declaration, declarationEnd - declaration);
    size_t marker = xmlDeclaration.find(L"encoding");
    size_t equals = marker == std::wstring::npos
        ? std::wstring::npos
        : xmlDeclaration.find(L'=', marker + 8);
    if (equals == std::wstring::npos)
    {
        return L"";
    }
    size_t start = equals + 1;
    while (start < xmlDeclaration.size() && iswspace(xmlDeclaration[start]))
    {
        ++start;
    }
    wchar_t quote = L'\0';
    if (start < xmlDeclaration.size() &&
        (xmlDeclaration[start] == L'\'' || xmlDeclaration[start] == L'"'))
    {
        quote = xmlDeclaration[start++];
    }
    size_t end = start;
    while (end < xmlDeclaration.size())
    {
        wchar_t ch = xmlDeclaration[end];
        if ((quote != L'\0' && ch == quote) ||
            (quote == L'\0' && iswspace(ch)))
        {
            break;
        }
        ++end;
    }
    return xmlDeclaration.substr(start, end - start);
}

static UINT CharsetCodePage(const std::wstring& charset)
{
    std::wstring value = aip::ToLower(aip::Trim(charset));
    if (value == L"utf-8" || value == L"utf8")
    {
        return CP_UTF8;
    }
    if (value == L"windows-1252" || value == L"cp1252")
    {
        return 1252;
    }
    if (value == L"iso-8859-1" || value == L"latin1" || value == L"latin-1")
    {
        return 28591;
    }
    if (value == L"us-ascii" || value == L"ascii")
    {
        return 20127;
    }
    if (value == L"shift_jis" || value == L"shift-jis" || value == L"sjis")
    {
        return 932;
    }
    if (value == L"gb18030")
    {
        return 54936;
    }
    return 0;
}

static bool DecodeFeedBytes(
    const std::vector<BYTE>& bytes,
    const std::wstring& contentType,
    std::wstring& text)
{
    if ((bytes.size() >= 2 &&
            ((bytes[0] == 0xFF && bytes[1] == 0xFE) ||
                (bytes[0] == 0xFE && bytes[1] == 0xFF))) ||
        (bytes.size() >= 3 &&
            bytes[0] == 0xEF &&
            bytes[1] == 0xBB &&
            bytes[2] == 0xBF))
    {
        return aip::DecodeTextBytes(bytes, text);
    }

    std::wstring charset = XmlDeclaredEncoding(bytes);
    if (charset.empty())
    {
        charset = ExtractCharset(contentType);
    }
    UINT codePage = CharsetCodePage(charset);
    if (codePage != 0 &&
        aip::DecodeCodePageText(
            codePage,
            bytes.data(),
            bytes.size(),
            text,
            codePage == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0))
    {
        return true;
    }
    return aip::DecodeTextBytes(bytes, text);
}

static bool HttpGetText(
    const AppSettings& settings,
    std::wstring& text,
    std::wstring& error,
    std::atomic<bool>* cancelRequested = nullptr,
    std::atomic<HINTERNET>* activeRequest = nullptr)
{
    text.clear();
    error.clear();

    auto isCancelled = [&]()
    {
        return cancelRequested != nullptr && cancelRequested->load();
    };

    if (settings.feedUrl.empty())
    {
        error = L"FeedUrl is empty. Set [Settings] FeedUrl in " + g_paths.configPath + L".";
        return false;
    }

    URL_COMPONENTSW components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(settings.feedUrl.c_str(), 0, 0, &components))
    {
        error = L"Invalid FeedUrl: " + aip::GetLastErrorText(GetLastError());
        return false;
    }

    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS)
    {
        error = L"FeedUrl must use http or https.";
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    std::wstring host(
        components.lpszHostName,
        components.dwHostNameLength);
    HINTERNET session = WinHttpOpen(
        settings.userAgent.c_str(),
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session == nullptr)
    {
        error = L"WinHttpOpen failed: " + aip::GetLastErrorText(GetLastError());
        return false;
    }

    HINTERNET connection = nullptr;
    HINTERNET request = nullptr;
    auto closeRequest = [&]()
    {
        if (request == nullptr)
        {
            return;
        }

        bool shouldClose = true;
        if (activeRequest != nullptr)
        {
            HINTERNET expected = request;
            shouldClose = activeRequest->compare_exchange_strong(expected, nullptr);
        }
        if (shouldClose)
        {
            WinHttpCloseHandle(request);
        }
        request = nullptr;
    };
    auto closeHandles = [&]()
    {
        closeRequest();
        if (connection != nullptr)
        {
            WinHttpCloseHandle(connection);
            connection = nullptr;
        }
        if (session != nullptr)
        {
            WinHttpCloseHandle(session);
            session = nullptr;
        }
    };
    auto fail = [&](const std::wstring& message, DWORD lastError)
    {
        closeHandles();
        if (isCancelled())
        {
            error = L"Feed update cancelled.";
            SetLastError(ERROR_CANCELLED);
        }
        else
        {
            error = message;
            SetLastError(lastError);
        }
        return false;
    };

    DWORD timeoutMs = static_cast<DWORD>(settings.httpTimeoutSeconds * 1000);
    if (!WinHttpSetTimeouts(session, timeoutMs, timeoutMs, timeoutMs, timeoutMs))
    {
        DWORD lastError = GetLastError();
        return fail(L"WinHttpSetTimeouts failed: " + aip::GetLastErrorText(lastError), lastError);
    }

    if (isCancelled())
    {
        return fail(L"Feed update cancelled.", ERROR_CANCELLED);
    }

    connection = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (connection == nullptr)
    {
        DWORD lastError = GetLastError();
        return fail(L"WinHttpConnect failed: " + aip::GetLastErrorText(lastError), lastError);
    }

    std::wstring objectName;
    if (components.lpszUrlPath != nullptr && components.dwUrlPathLength > 0)
    {
        objectName.assign(components.lpszUrlPath, components.dwUrlPathLength);
    }
    if (components.lpszExtraInfo != nullptr && components.dwExtraInfoLength > 0)
    {
        objectName.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (objectName.empty())
    {
        objectName = L"/";
    }

    const wchar_t* acceptTypes[] = {
        L"application/rss+xml",
        L"application/atom+xml",
        L"application/xml",
        L"text/xml",
        L"*/*",
        nullptr
    };

    DWORD flags = components.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    request = WinHttpOpenRequest(
        connection,
        L"GET",
        objectName.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        acceptTypes,
        flags);
    if (request == nullptr)
    {
        DWORD lastError = GetLastError();
        return fail(L"WinHttpOpenRequest failed: " + aip::GetLastErrorText(lastError), lastError);
    }

    if (activeRequest != nullptr)
    {
        HINTERNET previous = activeRequest->exchange(request);
        if (previous != nullptr)
        {
            WinHttpCloseHandle(previous);
        }
    }
    if (isCancelled())
    {
        return fail(L"Feed update cancelled.", ERROR_CANCELLED);
    }

    const wchar_t* headers = L"Accept-Encoding: identity\r\nCache-Control: no-cache\r\n";
    bool ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1), nullptr, 0, 0, 0) != FALSE &&
        WinHttpReceiveResponse(request, nullptr) != FALSE;
    if (!ok)
    {
        DWORD lastError = GetLastError();
        return fail(L"HTTP request failed: " + aip::GetLastErrorText(lastError), lastError);
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        DWORD lastError = GetLastError();
        return fail(L"Could not read the HTTP status: " + aip::GetLastErrorText(lastError), lastError);
    }
    if (status < 200 || status >= 300)
    {
        return fail(L"HTTP status " + std::to_wstring(status) + L".", ERROR_BAD_NET_RESP);
    }

    std::wstring contentType;
    DWORD contentTypeSize = 0;
    WinHttpQueryHeaders(
        request,
        WINHTTP_QUERY_CONTENT_TYPE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER,
        &contentTypeSize,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
        contentTypeSize >= sizeof(wchar_t))
    {
        std::vector<wchar_t> buffer(contentTypeSize / sizeof(wchar_t));
        if (WinHttpQueryHeaders(
                request,
                WINHTTP_QUERY_CONTENT_TYPE,
                WINHTTP_HEADER_NAME_BY_INDEX,
                buffer.data(),
                &contentTypeSize,
                WINHTTP_NO_HEADER_INDEX))
        {
            contentType.assign(buffer.data());
        }
    }

    std::vector<BYTE> bytes;
    for (;;)
    {
        if (isCancelled())
        {
            return fail(L"Feed update cancelled.", ERROR_CANCELLED);
        }

        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available))
        {
            DWORD lastError = GetLastError();
            return fail(
                L"WinHttpQueryDataAvailable failed: " + aip::GetLastErrorText(lastError),
                lastError);
        }
        if (available == 0)
        {
            break;
        }
        if (bytes.size() + available > static_cast<size_t>(settings.maxFeedBytes))
        {
            return fail(L"Feed response exceeded MaxFeedBytes.", ERROR_FILE_TOO_LARGE);
        }

        size_t oldSize = bytes.size();
        bytes.resize(oldSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request, bytes.data() + oldSize, available, &read))
        {
            DWORD lastError = GetLastError();
            return fail(L"WinHttpReadData failed: " + aip::GetLastErrorText(lastError), lastError);
        }
        bytes.resize(oldSize + read);
    }

    closeHandles();

    if (!DecodeFeedBytes(bytes, contentType, text))
    {
        error = L"Could not decode feed bytes as text.";
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    return true;
}

static std::wstring LocalXmlName(const std::wstring& name)
{
    size_t colon = name.rfind(L':');
    std::wstring local = colon == std::wstring::npos ? name : name.substr(colon + 1);
    return aip::ToLower(local);
}

static bool NameMatches(const std::wstring& localName, std::initializer_list<const wchar_t*> names)
{
    for (const wchar_t* name : names)
    {
        if (localName == name)
        {
            return true;
        }
    }
    return false;
}

static std::wstring ChildText(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& node,
    std::initializer_list<const wchar_t*> names)
{
    auto children = node.ChildNodes();
    for (uint32_t i = 0; i < children.Length(); ++i)
    {
        auto child = children.Item(i);
        std::wstring local = LocalXmlName(child.NodeName().c_str());
        if (NameMatches(local, names))
        {
            return CollapseWhitespace(child.InnerText().c_str());
        }
    }
    return L"";
}

static std::wstring ChildHtmlText(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& node,
    std::initializer_list<const wchar_t*> names)
{
    return StripHtmlTags(ChildText(node, names));
}

static std::wstring ChildLink(const winrt::Windows::Data::Xml::Dom::IXmlNode& node)
{
    std::wstring fallback;
    auto children = node.ChildNodes();
    for (uint32_t i = 0; i < children.Length(); ++i)
    {
        auto child = children.Item(i);
        if (LocalXmlName(child.NodeName().c_str()) != L"link")
        {
            continue;
        }

        auto attrs = child.Attributes();
        if (attrs != nullptr)
        {
            auto href = attrs.GetNamedItem(L"href");
            if (href != nullptr)
            {
                std::wstring value = CollapseWhitespace(href.InnerText().c_str());
                if (!value.empty())
                {
                    auto rel = attrs.GetNamedItem(L"rel");
                    std::wstring relation = rel == nullptr
                        ? L""
                        : aip::ToLower(CollapseWhitespace(rel.InnerText().c_str()));
                    if (relation.empty() || relation == L"alternate")
                    {
                        return value;
                    }
                    if (fallback.empty())
                    {
                        fallback = value;
                    }
                }
            }
        }

        std::wstring text = CollapseWhitespace(child.InnerText().c_str());
        if (!text.empty())
        {
            return text;
        }
    }
    return fallback;
}

static std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> FindDescendantsByLocalName(
    const winrt::Windows::Data::Xml::Dom::IXmlNode& root,
    std::initializer_list<const wchar_t*> names,
    size_t maxResults)
{
    std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> matches;
    if (root == nullptr || maxResults == 0)
    {
        return matches;
    }

    std::vector<winrt::Windows::Data::Xml::Dom::IXmlNode> pending;
    pending.push_back(root);
    size_t visited = 0;
    static constexpr size_t MAX_XML_NODES = 100000;

    while (!pending.empty() && matches.size() < maxResults && visited < MAX_XML_NODES)
    {
        auto node = pending.back();
        pending.pop_back();
        ++visited;

        if (NameMatches(LocalXmlName(node.NodeName().c_str()), names))
        {
            matches.push_back(node);
            if (matches.size() >= maxResults)
            {
                break;
            }
        }

        auto children = node.ChildNodes();
        for (uint32_t i = children.Length(); i > 0; --i)
        {
            pending.push_back(children.Item(i - 1));
        }
    }
    return matches;
}

static FeedSnapshot ParseFeedXml(const std::wstring& xml, const AppSettings& settings)
{
    FeedSnapshot snapshot;

    try
    {
        WinrtApartment apartment;
        using namespace winrt::Windows::Data::Xml::Dom;

        XmlDocument doc;
        XmlLoadSettings loadSettings;
        loadSettings.ProhibitDtd(true);
        loadSettings.ResolveExternals(false);
        loadSettings.ElementContentWhiteSpace(false);
        loadSettings.MaxElementDepth(64);
        doc.LoadXml(winrt::hstring(xml), loadSettings);

        auto root = doc.DocumentElement();
        auto channels = FindDescendantsByLocalName(root, { L"channel" }, 1);
        if (!channels.empty())
        {
            snapshot.sourceTitle = ChildText(channels.front(), { L"title" });
        }
        if (snapshot.sourceTitle.empty() && root != nullptr)
        {
            snapshot.sourceTitle = ChildText(root, { L"title" });
        }
        if (snapshot.sourceTitle.empty())
        {
            snapshot.sourceTitle = L"RSS";
        }

        auto items = FindDescendantsByLocalName(
            root,
            { L"item", L"entry" },
            static_cast<size_t>(settings.maxItems));
        for (const auto& node : items)
        {
            FeedItem item;
            item.title = LimitText(ChildText(node, { L"title" }), 180);
            item.summary = LimitText(ChildHtmlText(node, { L"description", L"summary", L"content" }), 240);
            item.date = LimitText(ChildText(node, { L"pubdate", L"published", L"updated", L"date" }), 80);
            item.link = ChildLink(node);

            if (item.title.empty())
            {
                item.title = item.summary.empty() ? L"(untitled)" : LimitText(item.summary, 120);
            }
            if (item.summary.empty())
            {
                item.summary = item.date.empty() ? snapshot.sourceTitle : item.date;
            }
            snapshot.items.push_back(std::move(item));
        }

        if (snapshot.items.empty())
        {
            snapshot.status = L"No RSS or Atom entries were found.";
            return snapshot;
        }

        snapshot.ok = true;
        snapshot.status = L"Loaded " + std::to_wstring(snapshot.items.size()) + L" item(s) from " + snapshot.sourceTitle + L".";
        return snapshot;
    }
    catch (const winrt::hresult_error& ex)
    {
        snapshot.status = FormatString(L"Feed XML parse failed: 0x%08X %s", static_cast<unsigned int>(ex.code().value), ex.message().c_str());
        return snapshot;
    }
    catch (...)
    {
        snapshot.status = L"Feed XML parse failed with an unknown exception.";
        return snapshot;
    }
}

static FeedSnapshot ReadFeedSnapshot(
    const AppSettings& settings,
    std::atomic<bool>* cancelRequested = nullptr,
    std::atomic<HINTERNET>* activeRequest = nullptr)
{
    std::wstring xml;
    std::wstring error;
    if (!HttpGetText(settings, xml, error, cancelRequested, activeRequest))
    {
        FeedSnapshot snapshot;
        snapshot.status = error;
        return snapshot;
    }
    return ParseFeedXml(xml, settings);
}

static std::wstring BuildTileXml(const FeedSnapshot& snapshot, const FeedItem& item)
{
    std::wstring source = XmlEscape(LimitText(snapshot.sourceTitle, 80));
    std::wstring title = XmlEscape(LimitText(item.title, 160));
    std::wstring summary = XmlEscape(LimitText(item.summary, 220));
    std::wstring date = XmlEscape(LimitText(item.date, 80));
    std::wstring activationArguments;
    if (IsHttpUrl(item.link))
    {
        activationArguments = XmlEscape(
            L"--open-url " + QuoteCommandLineArg(item.link));
    }

    std::wstring xml;
    xml += L"<tile>\r\n";
    xml += L"  <visual branding=\"nameAndLogo\"";
    if (!activationArguments.empty())
    {
        xml += L" arguments=\"" + activationArguments + L"\"";
    }
    xml += L">\r\n";
    xml += L"    <binding template=\"TileMedium\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    if (!date.empty())
    {
        xml += L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + date + L"</text>\r\n";
    }
    xml += L"    </binding>\r\n";
    xml += L"    <binding template=\"TileWide\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + summary + L"</text>\r\n";
    xml += L"    </binding>\r\n";
    xml += L"    <binding template=\"TileLarge\">\r\n";
    xml += L"      <text hint-style=\"captionSubtle\">" + source + L"</text>\r\n";
    xml += L"      <text hint-style=\"title\" hint-wrap=\"true\">" + title + L"</text>\r\n";
    xml += L"      <text hint-style=\"caption\" hint-wrap=\"true\">" + summary + L"</text>\r\n";
    if (!date.empty())
    {
        xml += L"      <text hint-style=\"captionSubtle\" hint-wrap=\"true\">" + date + L"</text>\r\n";
    }
    xml += L"    </binding>\r\n";
    xml += L"  </visual>\r\n";
    xml += L"</tile>";
    return xml;
}

static std::wstring BuildSnapshotKey(const FeedSnapshot& snapshot)
{
    std::wstring key = snapshot.sourceTitle;
    for (const FeedItem& item : snapshot.items)
    {
        key += L"\n";
        key += item.title;
        key += L"\n";
        key += item.date;
        key += L"\n";
        key += item.link;
    }
    return key;
}

static bool UpdateLiveTile(const FeedSnapshot& snapshot, std::wstring& failure)
{
    failure.clear();
    if (!CurrentProcessHasPackageIdentity())
    {
        failure = L"Live Tile update requires package identity. Launch normally so the app can register and relaunch the packaged entry.";
        return false;
    }
    if (!snapshot.ok || snapshot.items.empty())
    {
        failure = snapshot.status.empty() ? L"No feed items are available." : snapshot.status;
        return false;
    }

    try
    {
        WinrtApartment apartment;
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        TileUpdater updater = TileUpdateManager::CreateTileUpdaterForApplication();
        std::vector<TileNotification> notifications;
        notifications.reserve(snapshot.items.size());
        for (const FeedItem& item : snapshot.items)
        {
            XmlDocument doc;
            doc.LoadXml(winrt::hstring(BuildTileXml(snapshot, item)));
            notifications.emplace_back(doc);
        }

        updater.EnableNotificationQueue(true);
        updater.Clear();
        for (const TileNotification& notification : notifications)
        {
            updater.Update(notification);
        }
        return true;
    }
    catch (const winrt::hresult_error& ex)
    {
        failure = FormatString(L"Live Tile update failed: 0x%08X %s", static_cast<unsigned int>(ex.code().value), ex.message().c_str());
        return false;
    }
    catch (...)
    {
        failure = L"Live Tile update failed with an unknown exception.";
        return false;
    }
}

static bool ShouldUpdateTile(RuntimeContext* ctx, const FeedSnapshot& snapshot, bool force)
{
    std::lock_guard<std::mutex> lock(ctx->mutex);
    std::wstring key = BuildSnapshotKey(snapshot);
    auto now = steady_clock::now();
    bool stale = ctx->lastTileUpdate == steady_clock::time_point::min() ||
        (ctx->settings.tileRefreshSeconds > 0 &&
            duration_cast<seconds>(now - ctx->lastTileUpdate).count() >= ctx->settings.tileRefreshSeconds);
    return force || key != ctx->lastTileKey || stale;
}

static void MarkTileUpdated(RuntimeContext* ctx, const FeedSnapshot& snapshot)
{
    std::lock_guard<std::mutex> lock(ctx->mutex);
    ctx->lastTileKey = BuildSnapshotKey(snapshot);
    ctx->lastTileUpdate = steady_clock::now();
}

static void CancelActiveUpdate(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }
    HINTERNET request = ctx->activeRequest.exchange(nullptr);
    if (request != nullptr)
    {
        WinHttpCloseHandle(request);
    }
}

static void BeginUpdate(RuntimeContext* ctx, bool force)
{
    if (ctx == nullptr || ctx->closing.load())
    {
        return;
    }
    if (ctx->updateRunning.exchange(true))
    {
        return;
    }
    if (ctx->updateThread.joinable())
    {
        ctx->updateThread.join();
    }

    AppSettings settings;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        settings = ctx->settings;
    }
    try
    {
        ctx->updateThread = std::thread([ctx, settings, force]()
        {
            std::unique_ptr<UpdateResult> result(new (std::nothrow) UpdateResult());
            if (!result)
            {
                ctx->updateRunning.store(false);
                LogError(L"Could not allocate a feed update result.");
                return;
            }
            try
            {
                result->snapshot = ReadFeedSnapshot(
                    settings,
                    &ctx->closing,
                    &ctx->activeRequest);
                result->ok = result->snapshot.ok;
                result->status = result->snapshot.status;

                if (result->snapshot.ok && ShouldUpdateTile(ctx, result->snapshot, force))
                {
                    std::wstring failure;
                    if (UpdateLiveTile(result->snapshot, failure))
                    {
                        MarkTileUpdated(ctx, result->snapshot);
                        result->status += L" Live Tile updated.";
                    }
                    else
                    {
                        result->ok = false;
                        result->status += L" " + failure;
                    }
                }
            }
            catch (const std::exception& ex)
            {
                result->ok = false;
                std::string message = ex.what() ? ex.what() : "unknown exception";
                result->status = L"Feed update failed: " +
                    std::wstring(message.begin(), message.end());
            }
            catch (...)
            {
                result->ok = false;
                result->status = L"Feed update failed with an unknown exception.";
            }

            ctx->updateRunning.store(false);
            if (!ctx->closing.load())
            {
                {
                    std::lock_guard<std::mutex> lock(ctx->mutex);
                    ctx->pendingResult = std::move(result);
                }
                PostMessageW(ctx->hwnd, WM_RLT_UPDATE_DONE, 0, 0);
            }
        });
    }
    catch (const std::exception& ex)
    {
        ctx->updateRunning.store(false);
        std::string message = ex.what() ? ex.what() : "unknown exception";
        LogError(L"Could not start feed update worker: " +
            std::wstring(message.begin(), message.end()));
    }
    catch (...)
    {
        ctx->updateRunning.store(false);
        LogError(L"Could not start feed update worker.");
    }
}

static void UpdateTrayTip(RuntimeContext* ctx, const std::wstring& tip)
{
    if (ctx == nullptr || !ctx->trayCreated)
    {
        return;
    }
    ctx->nid.uFlags = NIF_TIP;
    wcsncpy_s(ctx->nid.szTip, ARRAYSIZE(ctx->nid.szTip), tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);
}

static bool AddTrayIcon(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->hwnd == nullptr)
    {
        return false;
    }
    if (ctx->trayCreated)
    {
        return true;
    }

    ctx->nid = {};
    ctx->nid.cbSize = sizeof(ctx->nid);
    ctx->nid.hWnd = ctx->hwnd;
    ctx->nid.uID = TRAY_UID;
    ctx->nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    ctx->nid.uCallbackMessage = WM_RLT_TRAY;
    ctx->nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(ctx->nid.szTip, ARRAYSIZE(ctx->nid.szTip), APP_DISPLAY_NAME, _TRUNCATE);
    ctx->trayCreated = Shell_NotifyIconW(NIM_ADD, &ctx->nid) != FALSE;
    if (ctx->trayCreated)
    {
        ctx->nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &ctx->nid);
    }
    return ctx->trayCreated;
}

static void RemoveTrayIcon(RuntimeContext* ctx)
{
    if (ctx != nullptr && ctx->trayCreated)
    {
        Shell_NotifyIconW(NIM_DELETE, &ctx->nid);
        ctx->trayCreated = false;
    }
}

enum TrayCommand : UINT
{
    IDM_REFRESH = 1001,
    IDM_RELOAD = 1002,
    IDM_OPEN_LATEST = 1003,
    IDM_OPEN_LOG = 1004,
    IDM_OPEN_INI = 1005,
    IDM_REGISTER = 1006,
    IDM_LAUNCH_PACKAGED = 1007,
    IDM_EXIT = 1008,
};

static bool LaunchSelfAction(const wchar_t* action)
{
    std::wstring commandLine =
        QuoteCommandLineArg(g_paths.exePath) +
        L" --ini " +
        QuoteCommandLineArg(g_paths.configPath) +
        L" " +
        action;

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(
            g_paths.exePath.c_str(),
            commandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            g_paths.exeDir.c_str(),
            &startup,
            &process))
    {
        LogError(L"Could not launch maintenance action: " + aip::GetLastErrorText(GetLastError()));
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

static bool ResetUpdateTimer(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->hwnd == nullptr)
    {
        return false;
    }
    KillTimer(ctx->hwnd, UPDATE_TIMER_ID);
    int updateIntervalSeconds = 300;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        updateIntervalSeconds = ctx->settings.updateIntervalSeconds;
    }
    UINT timerMs = static_cast<UINT>(std::min<long long>(
        static_cast<long long>(updateIntervalSeconds) * 1000ll,
        static_cast<long long>(UINT_MAX)));
    if (SetTimer(ctx->hwnd, UPDATE_TIMER_ID, timerMs, nullptr) == 0)
    {
        LogError(L"SetTimer failed: " + aip::GetLastErrorText(GetLastError()));
        return false;
    }
    return true;
}

static void ReloadRuntimeSettings(RuntimeContext* ctx)
{
    if (ctx == nullptr)
    {
        return;
    }

    AppSettings reloaded = LoadSettings();
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->settings = reloaded;
    }
    if (!ResetUpdateTimer(ctx))
    {
        return;
    }

    if (reloaded.showTrayIcon && !ctx->trayCreated)
    {
        if (!AddTrayIcon(ctx))
        {
            LogWarn(L"Could not restore the tray icon after configuration reload.");
        }
    }
    else if (!reloaded.showTrayIcon && ctx->trayCreated)
    {
        RemoveTrayIcon(ctx);
    }

    LogText(L"Configuration reloaded.");
    BeginUpdate(ctx, true);
}

static void ShowTrayMenu(RuntimeContext* ctx)
{
    if (ctx == nullptr || ctx->hwnd == nullptr)
    {
        return;
    }

    HMENU menu = aip::CreateTrayPopupMenu();
    if (menu == nullptr)
    {
        return;
    }

    std::wstring status;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        status = ctx->latest.status.empty() ? L"Waiting for first update" : ctx->latest.status;
    }
    aip::AppendTrayMenuItem(menu, 0, LimitText(status, 80), false, false);
    aip::AppendMenuSeparator(menu);
    aip::AppendTrayMenuItem(menu, IDM_REFRESH, L"Refresh now");
    aip::AppendTrayMenuItem(menu, IDM_RELOAD, L"Reload configuration");
    bool hasLatestLink = false;
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        hasLatestLink =
            !ctx->latest.items.empty() &&
            IsHttpUrl(ctx->latest.items.front().link);
    }
    aip::AppendTrayMenuItem(menu, IDM_OPEN_LATEST, L"Open latest article", false, hasLatestLink);
    aip::AppendTrayMenuItem(menu, IDM_OPEN_LOG, L"Open log");
    aip::AppendTrayMenuItem(menu, IDM_OPEN_INI, L"Open settings");
    aip::AppendMenuSeparator(menu);
    aip::AppendTrayMenuItem(menu, IDM_REGISTER, L"Register package");
    aip::AppendTrayMenuItem(menu, IDM_LAUNCH_PACKAGED, L"Launch packaged entry");
    aip::AppendMenuSeparator(menu);
    aip::AppendTrayMenuItem(menu, IDM_EXIT, L"Exit");

    POINT pt{};
    GetCursorPos(&pt);
    SetForegroundWindow(ctx->hwnd);
    aip::TrackPostAndDestroyTrayMenu(menu, ctx->hwnd, pt, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN);
}

static void HandleTrayCommand(RuntimeContext* ctx, UINT id)
{
    if (ctx == nullptr)
    {
        return;
    }
    switch (id)
    {
    case IDM_REFRESH:
        BeginUpdate(ctx, true);
        break;
    case IDM_RELOAD:
        ReloadRuntimeSettings(ctx);
        break;
    case IDM_OPEN_LATEST:
    {
        std::wstring url;
        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            if (!ctx->latest.items.empty())
            {
                url = ctx->latest.items.front().link;
            }
        }
        if (IsHttpUrl(url))
        {
            ShellExecuteW(ctx->hwnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        break;
    }
    case IDM_OPEN_LOG:
        ShellExecuteW(ctx->hwnd, L"open", g_paths.defaultLogPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case IDM_OPEN_INI:
        ShellExecuteW(ctx->hwnd, L"open", g_paths.configPath.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        break;
    case IDM_REGISTER:
        LaunchSelfAction(L"--register");
        break;
    case IDM_LAUNCH_PACKAGED:
        LaunchSelfAction(L"--launch-packaged");
        break;
    case IDM_EXIT:
        DestroyWindow(ctx->hwnd);
        break;
    default:
        break;
    }
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    RuntimeContext* ctx = reinterpret_cast<RuntimeContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    if (g_taskbarCreatedMessage != 0 && msg == g_taskbarCreatedMessage)
    {
        if (ctx != nullptr && ctx->settings.showTrayIcon)
        {
            ctx->trayCreated = false;
            AddTrayIcon(ctx);
        }
        return 0;
    }

    switch (msg)
    {
    case WM_TIMER:
        if (wParam == UPDATE_TIMER_ID)
        {
            BeginUpdate(ctx, false);
            return 0;
        }
        break;
    case WM_COMMAND:
        HandleTrayCommand(ctx, LOWORD(wParam));
        return 0;
    case WM_RLT_TRAY:
        if (LOWORD(lParam) == WM_CONTEXTMENU || LOWORD(lParam) == WM_RBUTTONUP)
        {
            ShowTrayMenu(ctx);
            return 0;
        }
        if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            BeginUpdate(ctx, true);
            return 0;
        }
        break;
    case WM_RLT_CONTROL:
        if (wParam == RLT_CONTROL_EXIT)
        {
            DestroyWindow(hwnd);
            return 0;
        }
        if (wParam == RLT_CONTROL_REFRESH)
        {
            BeginUpdate(ctx, true);
            return 0;
        }
        break;
    case WM_RLT_UPDATE_DONE:
    {
        std::unique_ptr<UpdateResult> result;
        if (ctx != nullptr)
        {
            std::lock_guard<std::mutex> lock(ctx->mutex);
            result = std::move(ctx->pendingResult);
        }
        if (ctx != nullptr && result)
        {
            {
                std::lock_guard<std::mutex> lock(ctx->mutex);
                ctx->latest = result->snapshot;
                ctx->latest.status = result->status;
            }
            if (result->ok)
            {
                LogText(result->status);
            }
            else
            {
                LogWarn(result->status);
            }
            UpdateTrayTip(ctx, LimitText(result->status, 120));
        }
        return 0;
    }
    case WM_DESTROY:
        if (ctx != nullptr)
        {
            ctx->closing.store(true);
            CancelActiveUpdate(ctx);
            KillTimer(hwnd, UPDATE_TIMER_ID);
            RemoveTrayIcon(ctx);
        }
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool RegisterWindowClass()
{
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = g_hInst;
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    ATOM atom = RegisterClassW(&wc);
    if (atom == 0)
    {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS)
        {
            LogError(L"RegisterClassW failed: " + aip::GetLastErrorText(err));
            return false;
        }
    }
    return true;
}

static int RunBackground(const AppOptions& options)
{
    RuntimeContext ctx;
    ctx.settings = LoadSettings();
    if (options.forceTray)
    {
        ctx.settings.showTrayIcon = true;
    }
    if (options.forceNoTray)
    {
        ctx.settings.showTrayIcon = false;
    }

    if (!RegisterWindowClass())
    {
        return 1;
    }

    HWND hwnd = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        g_identity.windowTitle.c_str(),
        WS_OVERLAPPED,
        0,
        0,
        0,
        0,
        nullptr,
        nullptr,
        g_hInst,
        &ctx);
    if (hwnd == nullptr)
    {
        LogError(L"CreateWindowExW failed: " + aip::GetLastErrorText(GetLastError()));
        return 1;
    }
    ctx.hwnd = hwnd;

    if (ctx.settings.showTrayIcon)
    {
        if (!AddTrayIcon(&ctx))
        {
            LogWarn(L"Could not create the tray icon; command-line resident control remains available.");
        }
    }

    if (!ResetUpdateTimer(&ctx))
    {
        LogError(L"SetTimer failed: " + aip::GetLastErrorText(GetLastError()));
        DestroyWindow(hwnd);
        return 1;
    }
    LogText(L"Resident control window ready.");
    BeginUpdate(&ctx, true);

    MSG msg{};
    int exitCode = 0;
    for (;;)
    {
        BOOL messageResult = GetMessageW(&msg, nullptr, 0, 0);
        if (messageResult == 0)
        {
            exitCode = static_cast<int>(msg.wParam);
            break;
        }
        if (messageResult == -1)
        {
            LogError(L"GetMessageW failed: " + aip::GetLastErrorText(GetLastError()));
            exitCode = 1;
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ctx.closing.store(true);
    CancelActiveUpdate(&ctx);
    if (ctx.updateThread.joinable())
    {
        ctx.updateThread.join();
    }
    RemoveTrayIcon(&ctx);
    return exitCode;
}

static int RunOnce()
{
    AppSettings settings = LoadSettings();
    FeedSnapshot snapshot = ReadFeedSnapshot(settings);
    if (!snapshot.ok)
    {
        LogError(snapshot.status);
        return 1;
    }

    std::wstring failure;
    if (!UpdateLiveTile(snapshot, failure))
    {
        LogError(failure);
        return 1;
    }

    LogText(snapshot.status + L" Live Tile updated.");
    return 0;
}

static bool TakeValue(int argc, wchar_t** argv, int& i, const std::wstring& arg, std::wstring& value, std::wstring& error)
{
    return aip::TakeCommandLineValue(argc, argv, i, arg, value, error);
}

static void AddWrite(AppOptions& options, const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    options.writes.push_back(aip::IniSetting{ section, key, value });
}

static AppOptions ParseCommandLine()
{
    AppOptions options;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr)
    {
        options.parseError = L"CommandLineToArgvW failed.";
        return options;
    }

    for (int i = 1; i < argc; ++i)
    {
        std::wstring arg = argv[i] ? argv[i] : L"";
        std::wstring value;
        std::wstring error;

        if (aip::EqualsI(arg, L"--help") || aip::EqualsI(arg, L"-h") || aip::EqualsI(arg, L"/?"))
        {
            options.showHelp = true;
        }
        else if (aip::EqualsI(arg, L"--once"))
        {
            options.once = true;
        }
        else if (aip::EqualsI(arg, L"--exit") || aip::EqualsI(arg, L"--quit"))
        {
            options.requestExit = true;
        }
        else if (aip::EqualsI(arg, L"--register"))
        {
            options.registerPackage = true;
        }
        else if (aip::EqualsI(arg, L"--unregister"))
        {
            options.unregisterPackage = true;
        }
        else if (aip::EqualsI(arg, L"--launch-packaged") || aip::EqualsI(arg, L"--launch"))
        {
            options.launchPackaged = true;
        }
        else if (aip::EqualsI(arg, L"--regenerate-manifest"))
        {
            options.regenerateManifest = true;
        }
        else if (aip::EqualsI(arg, L"--allow-multiple"))
        {
            options.allowMultiple = true;
        }
        else if (aip::EqualsI(arg, L"--no-bootstrap"))
        {
            options.noBootstrap = true;
        }
        else if (aip::EqualsI(arg, L"--tray"))
        {
            options.forceTray = true;
        }
        else if (aip::EqualsI(arg, L"--no-tray"))
        {
            options.forceNoTray = true;
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--ini"))
        {
            if (!TakeValue(argc, argv, i, arg, options.configOverride, options.parseError))
            {
                break;
            }
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--set"))
        {
            std::wstring spec;
            if (!TakeValue(argc, argv, i, arg, spec, options.parseError))
            {
                break;
            }
            aip::IniSetting setting;
            if (!aip::ParseIniSetSpec(spec, setting, options.parseError))
            {
                break;
            }
            options.writes.push_back(setting);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--feed-url"))
        {
            if (!TakeValue(argc, argv, i, arg, value, options.parseError))
            {
                break;
            }
            AddWrite(options, L"Settings", L"FeedUrl", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--interval"))
        {
            if (!TakeValue(argc, argv, i, arg, value, options.parseError))
            {
                break;
            }
            AddWrite(options, L"Settings", L"UpdateIntervalSeconds", value);
        }
        else if (aip::IsOptionOrInlineValue(arg, L"--open-url"))
        {
            if (!TakeValue(argc, argv, i, arg, options.openUrl, options.parseError))
            {
                break;
            }
        }
        else
        {
            options.parseError = L"Unknown option: " + arg;
            break;
        }
    }

    if (options.parseError.empty() && !options.showHelp)
    {
        if (!options.openUrl.empty() && !IsHttpUrl(options.openUrl))
        {
            options.parseError = L"--open-url expects an http or https URL.";
        }
    }

    if (options.parseError.empty() && !options.showHelp)
    {
        int actionCount =
            (options.once ? 1 : 0) +
            (options.registerPackage ? 1 : 0) +
            (options.unregisterPackage ? 1 : 0) +
            (options.launchPackaged ? 1 : 0) +
            (options.regenerateManifest ? 1 : 0) +
            (options.requestExit ? 1 : 0);
        if (actionCount > 1)
        {
            options.parseError = L"Choose only one action: --once, --register, --unregister, --launch-packaged, --regenerate-manifest, or --exit.";
        }
    }

    if (options.parseError.empty() && !options.showHelp)
    {
        for (const auto& setting : options.writes)
        {
            if (!ValidateSetting(setting, options.parseError))
            {
                break;
            }
            if (aip::EqualsI(setting.section, L"Manifest"))
            {
                options.manifestSettingChanged = true;
            }
        }
    }

    LocalFree(argv);
    return options;
}

static std::wstring BuildHelpText()
{
    return
        L"RSS Live Tile\r\n\r\n"
        L"Usage:\r\n"
        L"  RssLiveTile.exe\r\n"
        L"  RssLiveTile.exe --feed-url <url>\r\n"
        L"  RssLiveTile.exe --once\r\n"
        L"  RssLiveTile.exe --register\r\n"
        L"  RssLiveTile.exe --launch-packaged\r\n"
        L"  RssLiveTile.exe --unregister\r\n"
        L"  RssLiveTile.exe --regenerate-manifest\r\n"
        L"  RssLiveTile.exe --exit\r\n"
        L"  RssLiveTile.exe --set Section.Key=Value\r\n\r\n"
        L"Options:\r\n"
        L"  --ini <path>          Use a separate INI and resident-instance scope.\r\n"
        L"  --feed-url <url>      Save an RSS or Atom feed URL.\r\n"
        L"  --interval <seconds>  Save a polling interval from 15 to 86400.\r\n"
        L"  --tray | --no-tray    Override tray visibility for this run.\r\n"
        L"  --no-bootstrap        Do not register/relaunch through package identity.\r\n"
        L"  --allow-multiple      Disable the path-scoped single-instance guard.\r\n\r\n"
        L"Settings and logs stay beside the executable:\r\n"
        L"  " + g_paths.configPath + L"\r\n"
        L"  " + g_paths.defaultLogPath + L"\r\n\r\n"
        L"Live Tile updates require package identity. A normal launch writes AppxManifest.xml, registers the loose package, relaunches the packaged entry, and exits the bootstrap process.";
}

static bool InitPathsAndLogging(const std::wstring& configOverride)
{
    std::wstring error;
    if (!aip::TryBuildCurrentProcessSidecarPaths(
            APP_NAME,
            configOverride,
            g_paths,
            aip::DefaultLogPathPolicy::BesideExecutable,
            &error))
    {
        MessageBoxW(nullptr, error.c_str(), APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return false;
    }

    aip::Utf8LoggerOptions options;
    options.filePath = g_paths.defaultLogPath;
    options.maxRecentLines = 200;
    g_logger.Configure(options);

    g_identity = aip::BuildPathScopedInstanceIdentity(
        L"RssLiveTile.App",
        L"RssLiveTile.Instance",
        WINDOW_CLASS_NAME,
        APP_NAME,
        g_paths.configPath);
    g_taskbarCreatedMessage = aip::RegisterTaskbarCreatedMessage();
    return true;
}

static bool SignalExistingInstance(WPARAM request)
{
    return aip::SignalInstanceWindow(
        WINDOW_CLASS_NAME,
        g_identity.windowTitle.c_str(),
        WM_RLT_CONTROL,
        request,
        20,
        100,
        false);
}

enum class SingleInstanceResult
{
    Acquired,
    ExistingSignaled,
    Failed,
};

static SingleInstanceResult AcquireSingleInstance(const AppOptions& options)
{
    if (options.allowMultiple)
    {
        return SingleInstanceResult::Acquired;
    }

    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, g_identity.mutexName.c_str());
    if (g_singleInstanceMutex == nullptr)
    {
        LogError(L"CreateMutexW failed: " + aip::GetLastErrorText(GetLastError()));
        return SingleInstanceResult::Failed;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        bool signaled = SignalExistingInstance(
            options.requestExit ? RLT_CONTROL_EXIT : RLT_CONTROL_REFRESH);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        if (!signaled)
        {
            LogError(L"An existing instance owns the mutex, but its control window could not be signaled.");
            return SingleInstanceResult::Failed;
        }
        return SingleInstanceResult::ExistingSignaled;
    }
    return SingleInstanceResult::Acquired;
}

static bool InitGdiplus()
{
    GdiplusStartupInput input;
    Status status = GdiplusStartup(&g_gdiplusToken, &input, nullptr);
    if (status != Ok)
    {
        LogError(L"GdiplusStartup failed.");
        return false;
    }
    return true;
}

static void ShutdownGdiplus()
{
    if (g_gdiplusToken != 0)
    {
        GdiplusShutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
}

static int ActionRegister()
{
    if (!EnsurePackageFiles(true))
    {
        return 1;
    }
    std::wstring family;
    return RegisterPackage(&family) ? 0 : 1;
}

static std::wstring BuildPackagedArguments(const AppOptions& options)
{
    std::wstring arguments =
        L"--ini " + QuoteCommandLineArg(g_paths.configPath) +
        L" --no-bootstrap";
    if (options.once)
    {
        arguments += L" --once";
    }
    if (options.forceTray)
    {
        arguments += L" --tray";
    }
    if (options.forceNoTray)
    {
        arguments += L" --no-tray";
    }
    return arguments;
}

static int BootstrapAndLaunchPackaged(const AppOptions& options)
{
    std::wstring bootstrapMutexName = g_identity.mutexName + L".Bootstrap";
    HANDLE bootstrapMutex = CreateMutexW(nullptr, TRUE, bootstrapMutexName.c_str());
    if (bootstrapMutex == nullptr)
    {
        LogError(L"Could not create the package-bootstrap mutex: " + aip::GetLastErrorText(GetLastError()));
        return 1;
    }

    bool ownsMutex = GetLastError() != ERROR_ALREADY_EXISTS;
    if (!ownsMutex)
    {
        DWORD wait = WaitForSingleObject(
            bootstrapMutex,
            POWERSHELL_TIMEOUT_MS + POWERSHELL_TERMINATE_WAIT_MS);
        if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
        {
            DWORD error = wait == WAIT_TIMEOUT ? WAIT_TIMEOUT : GetLastError();
            CloseHandle(bootstrapMutex);
            LogError(L"Timed out waiting for package bootstrap: " + aip::GetLastErrorText(error));
            return 1;
        }
        ownsMutex = true;
    }

    auto closeBootstrapMutex = [&]()
    {
        if (ownsMutex)
        {
            ReleaseMutex(bootstrapMutex);
            ownsMutex = false;
        }
        CloseHandle(bootstrapMutex);
    };

    if (!EnsurePackageFiles(false))
    {
        closeBootstrapMutex();
        return 1;
    }

    std::wstring family;
    if (!RegisterPackage(&family))
    {
        closeBootstrapMutex();
        MessageBoxW(nullptr, L"RSS Live Tile could not register its loose Appx package. Check the log next to the executable for details.", APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }
    std::wstring arguments = BuildPackagedArguments(options);
    if (!ActivatePackageFamily(family, arguments.c_str()))
    {
        closeBootstrapMutex();
        MessageBoxW(nullptr, L"RSS Live Tile registered the package but could not launch the packaged entry. Check the log next to the executable for details.", APP_DISPLAY_NAME, MB_OK | MB_ICONERROR);
        return 1;
    }
    closeBootstrapMutex();
    return 0;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    g_hInst = hInstance;
    AppOptions options = ParseCommandLine();

    if (!InitPathsAndLogging(options.configOverride))
    {
        return 1;
    }

    if (!options.parseError.empty())
    {
        ShowCommandLineMessage(options.parseError, true);
        return 2;
    }

    if (options.showHelp)
    {
        ShowCommandLineMessage(BuildHelpText(), false);
        return 0;
    }

    LogText(std::wstring(L"Starting ") + APP_NAME + L" from " + g_paths.exePath);

    if (!EnsureDefaultSettingsFile())
    {
        return 1;
    }

    for (const auto& write : options.writes)
    {
        if (!WriteIniSetting(write))
        {
            return 1;
        }
    }

    if (options.manifestSettingChanged &&
        !options.registerPackage &&
        !options.regenerateManifest &&
        !options.unregisterPackage)
    {
        if (!EnsurePackageFiles(true))
        {
            return 1;
        }
        LogText(L"Manifest settings changed; package files were regenerated. Registration will be refreshed by the next unpackaged launch or explicit --register.");
    }

    if (!options.openUrl.empty())
    {
        HINSTANCE result = ShellExecuteW(
            nullptr,
            L"open",
            options.openUrl.c_str(),
            nullptr,
            nullptr,
            SW_SHOWNORMAL);
        if (reinterpret_cast<INT_PTR>(result) <= 32)
        {
            LogWarn(L"Could not open article URL: " + options.openUrl);
        }
    }

    if (options.requestExit)
    {
        return SignalExistingInstance(RLT_CONTROL_EXIT) ? 0 : 2;
    }

    if (!InitGdiplus())
    {
        return 1;
    }

    int exitCode = 0;
    if (options.unregisterPackage)
    {
        exitCode = UnregisterPackage() ? 0 : 1;
    }
    else if (options.registerPackage)
    {
        exitCode = ActionRegister();
    }
    else if (options.regenerateManifest)
    {
        exitCode = EnsurePackageFiles(true) ? 0 : 1;
    }
    else if (options.launchPackaged)
    {
        exitCode = LaunchPackagedInstance(BuildPackagedArguments(options)) ? 0 : 1;
    }
    else
    {
        AppSettings settings = LoadSettings();
        if (!options.noBootstrap &&
            settings.bootstrapPackageOnLaunch &&
            !CurrentProcessHasPackageIdentity())
        {
            exitCode = BootstrapAndLaunchPackaged(options);
        }
        else
        {
            SingleInstanceResult singleInstance = AcquireSingleInstance(options);
            if (singleInstance == SingleInstanceResult::Failed)
            {
                exitCode = 1;
            }
            else if (singleInstance == SingleInstanceResult::ExistingSignaled)
            {
                exitCode = 0;
            }
            else if (options.once)
            {
                exitCode = RunOnce();
            }
            else
            {
                exitCode = RunBackground(options);
            }
        }
    }

    if (g_singleInstanceMutex != nullptr)
    {
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
    ShutdownGdiplus();
    LogText(L"Exiting.");
    return exitCode;
}

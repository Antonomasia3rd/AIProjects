// compile command: cl /std:c++17 /EHsc /DUNICODE /D_UNICODE GenerateAssets.cpp /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <stdio.h>
#include <string>
#include <cwctype>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <chrono>
#include <climits>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <commdlg.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

INT_PTR CALLBACK RenameDlgProc(HWND, UINT, WPARAM, LPARAM);

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

static std::wstring g_iniPath, g_logPath, g_exePath;
static std::mutex g_pathMutex;
static constexpr const wchar_t* WINDOW_CLASS_NAME = L"DesktopTileGeneratorTrayWnd";
static constexpr const wchar_t* SINGLE_INSTANCE_MUTEX_NAME = L"Local\\DesktopTileGenerator.GenerateAssets";
static constexpr const wchar_t* SINGLE_INSTANCE_MESSAGE_NAME = L"DesktopTileGenerator.RestoreRunningInstance";
static UINT g_singleInstanceMessage = 0;
static HANDLE g_singleInstanceMutex = nullptr;

static std::wstring TrimCopy(std::wstring s)
{
    auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

static std::wstring ToLowerCopy(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t ch) { return (wchar_t)towlower(ch); });
    return s;
}

static bool IEquals(const std::wstring& a, const std::wstring& b)
{
    return ToLowerCopy(a) == ToLowerCopy(b);
}

static bool ReadWholeFileBytes(const std::wstring& path, std::vector<BYTE>& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > 0x7fffffff)
    {
        CloseHandle(h);
        return false;
    }

    bytes.resize((size_t)size.QuadPart);
    DWORD read = 0;
    BOOL ok = TRUE;
    if (!bytes.empty())
        ok = ReadFile(h, bytes.data(), (DWORD)bytes.size(), &read, nullptr);
    CloseHandle(h);
    if (!ok || read != bytes.size())
        return false;
    return true;
}

static bool WriteWholeFileBytes(const std::wstring& path, const std::vector<BYTE>& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    DWORD wrote = 0;
    BOOL ok = TRUE;
    if (!bytes.empty())
        ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == bytes.size();
}

static bool HasUtf8Bom(const std::vector<BYTE>& bytes)
{
    return bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF;
}

static bool DecodeCodePageText(UINT codePage, const BYTE* data, size_t size, std::wstring& out, DWORD flags = 0)
{
    if (size > INT_MAX)
        return false;
    if (size == 0)
    {
        out.clear();
        return true;
    }

    int need = MultiByteToWideChar(codePage, flags, (LPCCH)data, (int)size, nullptr, 0);
    if (need <= 0)
        return false;

    out.resize(need);
    return MultiByteToWideChar(codePage, flags, (LPCCH)data, (int)size, &out[0], need) > 0;
}

static bool DecodeTextBytes(const std::vector<BYTE>& bytes, std::wstring& out)
{
    if (bytes.empty())
    {
        out.clear();
        return true;
    }

    if (HasUtf8Bom(bytes))
        return DecodeCodePageText(CP_UTF8, bytes.data() + 3, bytes.size() - 3, out, MB_ERR_INVALID_CHARS);

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
    {
        if ((bytes.size() - 2) % 2 != 0)
            return false;
        size_t n = (bytes.size() - 2) / 2;
        out.resize(n);
        if (n)
            memcpy(out.data(), bytes.data() + 2, n * sizeof(wchar_t));
        return true;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
    {
        if ((bytes.size() - 2) % 2 != 0)
            return false;
        size_t n = (bytes.size() - 2) / 2;
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            BYTE hi = bytes[2 + i * 2 + 0];
            BYTE lo = bytes[2 + i * 2 + 1];
            out[i] = (wchar_t)((hi << 8) | lo);
        }
        return true;
    }

    return DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, MB_ERR_INVALID_CHARS);
}

static std::vector<BYTE> EncodeUtf8Text(const std::wstring& text, bool includeBom)
{
    std::vector<BYTE> out;
    if (text.size() > INT_MAX)
        return out;

    int need = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    if (need <= 0 && !text.empty())
        return out;

    if (includeBom)
    {
        static const BYTE bom[] = { 0xEF, 0xBB, 0xBF };
        out.insert(out.end(), bom, bom + sizeof(bom));
    }

    size_t offset = out.size();
    out.resize(offset + (size_t)need);
    if (need > 0)
        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), (LPSTR)(out.data() + offset), need, nullptr, nullptr);
    return out;
}

static bool ReadTextFile(const std::wstring& path, std::wstring& out)
{
    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(path, bytes))
        return false;
    return DecodeTextBytes(bytes, out);
}

static bool WriteUtf8BomTextFile(const std::wstring& path, const std::wstring& text)
{
    return WriteWholeFileBytes(path, EncodeUtf8Text(text, true));
}

static bool AppendUtf8TextFile(const std::wstring& path, const std::wstring& text)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    LARGE_INTEGER sz{};
    if (GetFileSizeEx(h, &sz) && sz.QuadPart == 0)
    {
        std::vector<BYTE> bom = EncodeUtf8Text(L"", true);
        if (!bom.empty())
        {
            DWORD wrote = 0;
            WriteFile(h, bom.data(), (DWORD)bom.size(), &wrote, nullptr);
        }
    }

    SetFilePointer(h, 0, nullptr, FILE_END);
    std::vector<BYTE> bytes = EncodeUtf8Text(text, false);
    BOOL ok = TRUE;
    DWORD wrote = 0;
    if (!bytes.empty())
        ok = WriteFile(h, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr);
    CloseHandle(h);
    return ok && wrote == bytes.size();
}

static bool NormalizeTextFileToUtf8Bom(const std::wstring& path)
{
    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(path, bytes) || bytes.empty() || HasUtf8Bom(bytes))
        return false;

    std::wstring text;
    if (!DecodeTextBytes(bytes, text))
        return false;

    return WriteUtf8BomTextFile(path, text);
}

static void ConfigureConsoleCP()
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
}

static void ConfigureDpiAwareness()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setContext = (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }

    SetProcessDPIAware();
}

static std::wstring GetModulePath()
{
    DWORD size = 1024;
    for (;;)
    {
        std::vector<wchar_t> buf(size);
        SetLastError(ERROR_SUCCESS);
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), (DWORD)buf.size());
        if (!n)
            return L"";
        if (n < buf.size() - 1)
            return std::wstring(buf.data(), n);
        if (size >= 32768)
            return L"";
        size *= 2;
    }
}

static std::vector<std::wstring> SplitLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    if (text.empty())
        return lines;

    size_t i = 0;
    while (i <= text.size())
    {
        size_t j = i;
        while (j < text.size() && text[j] != L'\r' && text[j] != L'\n') ++j;
        lines.push_back(text.substr(i, j - i));
        if (j >= text.size()) break;
        if (text[j] == L'\r' && j + 1 < text.size() && text[j + 1] == L'\n') j += 2;
        else ++j;
        i = j;
    }
    return lines;
}

static std::wstring JoinLines(const std::vector<std::wstring>& lines)
{
    std::wstring text;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i) text += L"\r\n";
        text += lines[i];
    }
    return text;
}

static bool ReadIniFileAuto(std::wstring& text);
static bool ReadIniValueFromText(const std::wstring& text, const wchar_t* s, const wchar_t* k, std::wstring& out);
static bool WriteIniValueToText(std::wstring& text, const wchar_t* s, const wchar_t* k, const wchar_t* v);

static void StripLeadingBom(std::wstring& s)
{
    if (!s.empty() && s.front() == 0xFEFF)
        s.erase(s.begin());
}

struct IniEntry
{
    std::wstring key;
    std::wstring value;
};

struct IniSectionData
{
    std::wstring name;
    std::vector<IniEntry> entries;
};

static std::vector<IniSectionData> ParseIniDocument(const std::wstring& text)
{
    std::vector<IniSectionData> doc;
    IniSectionData* current = nullptr;

    auto findSection = [&](const std::wstring& name) -> IniSectionData*
    {
        for (auto& sec : doc)
        {
            if (IEquals(sec.name, name))
                return &sec;
        }
        return nullptr;
    };

    auto findKey = [](IniSectionData& sec, const std::wstring& key) -> IniEntry*
    {
        for (auto& e : sec.entries)
        {
            if (IEquals(e.key, key))
                return &e;
        }
        return nullptr;
    };

    auto lines = SplitLines(text);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring line = lines[i];
        if (i == 0)
            StripLeadingBom(line);

        std::wstring trimmed = TrimCopy(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#')
            continue;

        if (trimmed.front() == L'[' && trimmed.back() == L']')
        {
            std::wstring name = TrimCopy(trimmed.substr(1, trimmed.size() - 2));
            if (IniSectionData* existing = findSection(name))
            {
                current = existing;
            }
            else
            {
                doc.push_back(IniSectionData{ name, {} });
                current = &doc.back();
            }
            continue;
        }

        if (!current)
            continue;

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = TrimCopy(line.substr(0, eq));
        std::wstring value = line.substr(eq + 1);

        if (IniEntry* existing = findKey(*current, key))
            existing->value = value;
        else
            current->entries.push_back(IniEntry{ key, value });
    }

    return doc;
}

static bool ReadIniFileAuto(std::wstring& text)
{
    return ReadTextFile(g_iniPath, text);
}

static bool ReadIniValueFromText(const std::wstring& text, const wchar_t* s, const wchar_t* k, std::wstring& out)
{
    std::wstring sectionName = s ? s : L"";
    std::wstring keyName = k ? k : L"";

    auto doc = ParseIniDocument(text);
    for (const auto& sec : doc)
    {
        if (!IEquals(sec.name, sectionName))
            continue;

        for (const auto& entry : sec.entries)
        {
            if (IEquals(entry.key, keyName))
            {
                out = entry.value;
                return true;
            }
        }
    }

    return false;
}

static bool WriteIniValueToText(std::wstring& text, const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    std::wstring sectionName = s ? s : L"";
    std::wstring keyName = k ? k : L"";
    std::wstring value = v ? v : L"";
    std::vector<std::wstring> lines = SplitLines(text);
    if (!lines.empty())
        StripLeadingBom(lines[0]);

    bool inTargetSection = false;
    bool sawTargetSection = false;
    bool updated = false;
    size_t insertPos = lines.size();

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring trimmed = TrimCopy(lines[i]);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#')
            continue;

        if (trimmed.front() == L'[' && trimmed.back() == L']')
        {
            if (inTargetSection && insertPos == lines.size())
                insertPos = i;

            std::wstring name = TrimCopy(trimmed.substr(1, trimmed.size() - 2));
            inTargetSection = IEquals(name, sectionName);
            if (inTargetSection && !sawTargetSection)
            {
                sawTargetSection = true;
                insertPos = lines.size();
            }
            continue;
        }

        if (!inTargetSection)
            continue;

        size_t eq = lines[i].find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = TrimCopy(lines[i].substr(0, eq));
        if (IEquals(key, keyName))
        {
            size_t valueStart = eq + 1;
            while (valueStart < lines[i].size() && (lines[i][valueStart] == L' ' || lines[i][valueStart] == L'\t'))
                ++valueStart;

            lines[i] = lines[i].substr(0, valueStart) + value;
            updated = true;
        }
    }

    if (!updated)
    {
        std::wstring newEntry = keyName + L"=" + value;
        if (sawTargetSection)
        {
            if (insertPos == lines.size())
                lines.push_back(newEntry);
            else
                lines.insert(lines.begin() + insertPos, newEntry);
        }
        else
        {
            if (!lines.empty() && !lines.back().empty())
                lines.push_back(L"");
            lines.push_back(L"[" + sectionName + L"]");
            lines.push_back(newEntry);
        }
    }

    text = JoinLines(lines);
    if (!text.empty())
        text += L"\r\n";
    return true;
}

static bool WriteIniValue(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    std::wstring text;
    if (!ReadIniFileAuto(text))
        text.clear();

    if (!WriteIniValueToText(text, s, k, v))
        return false;

    return WriteUtf8BomTextFile(g_iniPath, text);
}

struct IniDefault {
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static std::atomic<bool> g_running(true), g_console(false), g_logging(true), g_tray(true);
static std::atomic<bool> g_listenWallpaper(true);
static std::atomic<bool> g_listenFit(true);
static std::atomic<bool> g_disableFitting(false);
static std::atomic<bool> g_generateOnStartup(true);
static std::atomic<bool> g_hideDisabled(false);
static std::atomic<bool> g_showMenuAsDropdown(true);
static std::atomic<bool> g_notificationsEnabled(true);
static std::atomic<bool> g_notifyOnStart(false);
static std::atomic<bool> g_notifyOnSuccess(false);
static std::atomic<bool> g_notifyOnFailure(true);
static std::atomic<bool> g_notifyOnBusy(false);
static std::atomic<bool> g_notifyOnAlreadyRunning(true);
static std::atomic<bool> g_notifyOnSingleInstanceFailure(true);
static std::atomic<bool> g_generateDesktopIconForDisabledEntries(true);
static std::atomic<bool> g_generateScaleAuto(true);
static std::atomic<bool> g_generateScale100(true);
static std::atomic<bool> g_generateScale125(true);
static std::atomic<bool> g_generateScale150(true);
static std::atomic<bool> g_generateScale200(false);
static std::atomic<bool> g_generateScale400(false);
static std::mutex g_generationMutex;
static std::mutex g_generationQueueMutex;
static std::condition_variable g_generationQueueCv;
static std::thread g_generationWorker;
static bool g_generationWorkerStarted = false;
static bool g_generationWorkerStop = false;
static bool g_generationQueued = false;
static std::wstring g_generationQueuedWallpaper;
static std::wstring g_generationQueuedReason;
static const IniDefault g_defaults[] = {
    // Settings
    {L"Settings", L"PollIntervalMs", L"2000"},
    {L"Settings", L"ConfirmMs", L"800"},
    {L"Settings", L"DebounceMinMs", L"1200"},
    {L"Settings", L"Logging", L"1"},
    {L"Settings", L"LogPath", L""},
    {L"Settings", L"TrayIcon", L"1"},
    {L"Settings", L"ShowConsole", L"0"},
    {L"Settings", L"GenerateOnStartup", L"1"},
    {L"Settings", L"ListenWallpaper", L"1"},
    {L"Settings", L"ListenFit", L"1"},
    {L"Settings", L"DisableFitting", L"0"},
    {L"Settings", L"UsePowerShell", L"1"},
    {L"Settings", L"HideDisabledEntries", L"0"},
    {L"Settings", L"GenerateDesktopIconForDisabledEntries", L"1"},
    {L"Settings", L"GenerateScaleAuto", L"1"},
    {L"Settings", L"GenerateScale100", L"1"},
    {L"Settings", L"GenerateScale125", L"1"},
    {L"Settings", L"GenerateScale150", L"1"},
    {L"Settings", L"GenerateScale200", L"0"},
    {L"Settings", L"GenerateScale400", L"0"},
    {L"Settings", L"ShowMenuAsDropdown", L"1"},
    {L"Settings", L"NotificationsEnabled", L"1"},
    {L"Settings", L"NotifyOnStart", L"0"},
    {L"Settings", L"NotifyOnSuccess", L"0"},
    {L"Settings", L"NotifyOnFailure", L"1"},
    {L"Settings", L"NotifyOnBusy", L"0"},
    {L"Settings", L"NotifyOnAlreadyRunning", L"1"},
    {L"Settings", L"NotifyOnSingleInstanceFailure", L"1"},
    {L"Settings", L"SingleInstanceFailureAction", L"Warn"},

    // Assets (IMPORTANT: correct defaults)
    {L"Assets", L"StoreLogo", L"0"},
    {L"Assets", L"MediumTile", L"1"},
    {L"Assets", L"Square44x44Logo", L"0"},
    {L"Assets", L"SmallTile", L"0"},
    {L"Assets", L"WideTile", L"1"},
    {L"Assets", L"LargeTile", L"1"},

};

struct StringDefault {
    const wchar_t* key;
    const wchar_t* value;
};

static const StringDefault g_stringDefaults[] = {
    {L"EnabledText", L"enabled"},
    {L"DisabledText", L"disabled"},
    {L"OnText", L"on"},
    {L"OffText", L"off"},

    {L"TrayTip", L"Desktop Tile Generator"},
    {L"ExeLabel", L"[i] EXE: %s"},
    {L"IniLabel", L"[i] INI: %s"},
    {L"LogFileLabel", L"[i] Log file: %s"},
    {L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s"},
    {L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)"},
    {L"TrayIconSummary", L"[i] Tray icon: %s"},
    {L"ConsoleSummary", L"[i] Console %s."},
    {L"LoggingSummary", L"[i] Logging %s."},
    {L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s"},
    {L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s"},
    {L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s"},
    {L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s"},
    {L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s"},
    {L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s"},
    {L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)"},
    {L"PowerShellEnabledMode", L"PowerShell enabled"},
    {L"ComPreferredMode", L"COM preferred with PowerShell fallback"},
    {L"ConsoleAllocated", L"[i] Console allocated."},
    {L"StartupGenerationEnabled", L"[i] Startup generation enabled."},
    {L"StartupGenerationSkippedNoWallpaper", L"[!] Startup generation skipped: no wallpaper detected."},
    {L"ManualGenerationRequested", L"[i] Manual generation requested from the tray menu."},
    {L"GenerationRequestBusy", L"[!] Generation request ignored: another operation is already running."},
    {L"EmptyWallpaper", L"[!] Empty wallpaper."},
    {L"IniEncodingNormalized", L"[i] INI file normalized to UTF-8 with BOM."},
    {L"FutureLogEntriesNewPath", L"[i] Future log entries will be written to the new path."},
    {L"FutureLogEntriesRenamedPath", L"[i] Future log entries will be written to the renamed path."},
    {L"FutureLogEntriesDefaultPath", L"[i] Future log entries will be written to the default path."},
    {L"ListenWallpaperState", L"[i] ListenWallpaper = %d"},
    {L"ListenFitState", L"[i] ListenFit = %d"},
    {L"DisableFittingState", L"[i] DisableFitting = %d"},
    {L"AssetToggleState", L"[i] Asset %s %s."},
    {L"ComRegistrationStatusFailed", L"[!] COM registration failed (status=%d)"},
    {L"GdiPlusStartupFailedTitle", L"GDI+"},
    {L"GdiPlusStartupFailedMessage", L"GDI+ startup failed"},
    {L"Win32FailureFmt", L"[!] %s failed (error %lu: %s)"},
    {L"LogPathChanged", L"[i] Log path changed to: %s"},
    {L"LogPathRenamed", L"[i] Log path renamed to: %s"},
    {L"LogPathReset", L"[i] Log path reset to default: %s"},
    {L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting."},

    {L"FitFill", L"Fill"},
    {L"FitFit", L"Fit"},
    {L"FitStretch", L"Stretch"},
    {L"FitCenter", L"Center"},
    {L"FitTile", L"Tile"},
    {L"FitSpan", L"Span"},

    {L"NotifyOnStartState", L"[i] NotifyOnStart = %d"},
    {L"NotifyOnSuccessState", L"[i] NotifyOnSuccess = %d"},
    {L"NotifyOnFailureState", L"[i] NotifyOnFailure = %d"},
    {L"NotifyOnBusyState", L"[i] NotifyOnBusy = %d"},
    {L"NotifyOnAlreadyRunningState", L"[i] NotifyOnAlreadyRunning = %d"},
    {L"NotifyOnSingleInstanceFailureState", L"[i] NotifyOnSingleInstanceFailure = %d"},
    {L"UnspecifiedReason", L"an unspecified reason"},
    {L"ChangeDetected", L"change detected"},
    {L"AdvancedTitle", L"Advanced:"},
    {L"PollIntervalLabel", L"Poll interval: %d ms"},
    {L"ConfirmMsLabel", L"Confirm: %d ms"},
    {L"DebounceMinMsLabel", L"Debounce min: %d ms"},
    {L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s"},
    {L"SingleInstanceActionIgnore", L"Ignore"},
    {L"SingleInstanceActionWarn", L"Warn"},
    {L"SingleInstanceActionExit", L"Exit"},
    {L"SingleInstanceActionCrash", L"Crash"},
    {L"UnknownError", L"Unknown error"},
    {L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||"},
    {L"AllFilesFilter", L"All files (*.*)|*.*||"},

    {L"GeneralTitle", L"General:"},
    {L"LoggingTitle", L"Logging:"},
    {L"WallpaperFittingTitle", L"Wallpaper Fitting:"},
    {L"AssetsTitle", L"Assets:"},
    {L"DpiScalesTitle", L"DPI Scales:"},
    {L"ListenWallpaper", L"Listen Wallpaper"},
    {L"TrayIcon", L"Tray Icon"},
    {L"UsePowerShell", L"Use PowerShell"},
    {L"GenerateOnStartup", L"Generate on Startup"},
    {L"HideDisabledEntries", L"Hide disabled entries"},
    {L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries"},
    {L"GenerateScaleAuto", L"Auto: current DPI scale"},
    {L"GenerateScale100", L"100%"},
    {L"GenerateScale125", L"125%"},
    {L"GenerateScale150", L"150%"},
    {L"GenerateScale200", L"200%"},
    {L"GenerateScale400", L"400%"},
    {L"CurrentDpiScaleLabel", L"Current DPI: %d%% (asset scale: %d%%)"},
    {L"ShowMenuAsDropdown", L"Show menu as dropdown"},
    {L"NotificationsTitle", L"Notifications:"},
    {L"NotificationsEnabled", L"Enable all tray notifications"},
    {L"NotifyOnStart", L"Notify when generation starts"},
    {L"NotifyOnSuccess", L"Notify when generation succeeds"},
    {L"NotifyOnFailure", L"Notify when generation, wallpaper, or registration fails"},
    {L"NotifyOnBusy", L"Notify when generation is already running"},
    {L"NotifyOnAlreadyRunning", L"Notify when the app is already running"},
    {L"NotifyOnSingleInstanceFailure", L"Notify when single-instance protection fails"},
    {L"ShowConsole", L"Show Console"},
    {L"GenerateNow", L"Generate now"},
    {L"LoggingEnable", L"Enable"},
    {L"ChangePath", L"Change path..."},
    {L"RenamePath", L"Rename path..."},
    {L"OpenInExplorer", L"Open in Explorer"},
    {L"ResetDefault", L"Reset to default"},
    {L"ShowPowerShellLog", L"Show PowerShell Log"},
    {L"LogPathPrefix", L"Path: "},
    {L"LogPathTitle", L"Log Path"},
    {L"PSStatusPrefix", L"PS Status: "},
    {L"PSStatusTitle", L"PS Status"},
    {L"DisableFitting", L"Disable Fitting"},
    {L"ListenFit", L"Listen Fit Mode"},
    {L"FitModePrefix", L"Fit Mode: "},
    {L"FitModeForced", L"Fit Mode: Fill (forced)"},
    {L"Exit", L"Exit"},
    {L"RenameDialogTitle", L"Rename Log Path"},
    {L"OKText", L"OK"},
    {L"CancelText", L"Cancel"},
    {L"ErrorTitle", L"Error"},
    {L"PathCannotBeEmpty", L"Path cannot be empty."},
    {L"NoPowerShellOutputTitle", L"PowerShell Output"},
    {L"NoPowerShellOutput", L"No PowerShell output."},
    {L"GenerateNowTitle", L"Generate Now"},
    {L"WallpaperNotFound", L"Could not detect current wallpaper."},
    {L"CurrentWallpaperNotFound", L"Could not detect current wallpaper."},
    {L"ProgramStarting", L"Program starting..."},
    {L"NotificationTitle", L"Desktop Tile Generator"},
    {L"NotificationGenerationStarted", L"Wallpaper generation started."},
    {L"NotificationGenerationSuccess", L"Wallpaper generation completed successfully."},
    {L"NotificationGenerationFailed", L"Wallpaper generation failed."},
    {L"NotificationGenerationBusy", L"Wallpaper generation is already running."},
    {L"NotificationAlreadyRunning", L"Desktop Tile Generator is already running."},
    {L"NotificationAlreadyRunningTrayPersistent", L"Desktop Tile Generator is already running. Tray icon is enabled in the config, so it will stay visible after restart."},
    {L"NotificationAlreadyRunningTraySession", L"Desktop Tile Generator is already running. Tray icon is visible for this session only; it will be hidden again after restart because TrayIcon=0 in the config."},
    {L"NotificationSingleInstanceFailure", L"Single-instance protection failed; another copy may run."},
    {L"NotificationTrayIconHidden", L"Tray icon hidden for this session. Run GenerateAssets.exe again to restore it."},
    {L"ManualGenerateTriggered", L"Manual generate triggered."},
    {L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s."},
    {L"WallpaperSource", L"Wallpaper source: %s"},
    {L"FailedLoadWallpaper", L"Failed to load wallpaper image."},
    {L"SkippingAssetDisabled", L"Skipping %s (disabled in settings)."},
    {L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s"},
    {L"ActiveScales", L"[i] Active scales: %s"},
    {L"SavedAsset", L"Saved %s"},
    {L"SavedScaledAsset", L"Saved %s (scale %d%%)"},
    {L"FailedSaveAsset", L"Failed to save %s"},
    {L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)"},
    {L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets."},
    {L"ManifestPath", L"Manifest path: %s"},
    {L"UsingComRegistration", L"Using COM Appx registration..."},
    {L"InvalidManifestPath", L"Invalid manifest path."},
    {L"ComRegistrationSuccess", L"COM registration success."},
    {L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration."},
    {L"ComRegistrationException", L"COM registration threw exception: 0x%08X"},
    {L"ComExceptionMessage", L"COM message: %s"},
    {L"ComFallbackToPowerShell", L"COM failed, falling back to PowerShell..."},
    {L"LaunchingPowerShellRegistration", L"Launching PowerShell registration..."},
    {L"PowerShellCommand", L"Command: %s"},
    {L"PowerShellCompleted", L"PowerShell registration completed successfully."},
    {L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!"},
    {L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X."},
    {L"PowerShellOutputFollows", L"PowerShell output follows:"},
    {L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully."},
    {L"AssetGenerationFinished", L"Asset generation and registration finished successfully."},
    {L"AssetGenerationFailed", L"Asset generation did not complete successfully."},
    {L"AppRegistrationFailed", L"App registration did not complete successfully."},
    {L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce."},
    {L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected."},
    {L"WallpaperChangeDetected", L"Wallpaper change detected."},
    {L"FitModeChangeDetected", L"Fit mode change detected."},
    {L"DpiScaleChangeDetected", L"DPI scale change detected."},
    {L"RegenerationAllowedAfterDebounce", L"Regeneration allowed after debounce."},
    {L"SingleInstanceMutexFailure", L"[!] Single-instance mutex creation failed (error %lu: %s); action=%s."},
};

static void EnsureIniStringDefaults()
{
    for (const auto& d : g_stringDefaults)
    {
        if (IniReadS(L"Strings", d.key, L"").empty())
        {
            IniWrite(L"Strings", d.key, d.value);
        }
    }
}

// Logging buffer
static std::mutex g_logMutex;
static std::vector<std::wstring> g_logBuf;
static const size_t LOGMAX = 1024;

// PowerShell status
static std::mutex g_psMutex;
static std::wstring g_psOut, g_psMsg;
static DWORD g_psCode = 0;
static bool g_psErr = false;

// Tray
static std::mutex g_trayMutex;
static NOTIFYICONDATAW g_nid{};
static HWND g_hwnd = nullptr;


struct UiStrings
{
    std::wstring generalTitle;
    std::wstring loggingTitle;
    std::wstring wallpaperFittingTitle;
    std::wstring assetsTitle;
    std::wstring dpiScalesTitle;

    std::wstring listenWallpaper;
    std::wstring trayIcon;
    std::wstring usePowerShell;
    std::wstring generateOnStartup;
    std::wstring hideDisabledEntries;
    std::wstring generateDesktopIconForDisabledEntries;
    std::wstring generateScaleAuto;
    std::wstring generateScale100;
    std::wstring generateScale125;
    std::wstring generateScale150;
    std::wstring generateScale200;
    std::wstring generateScale400;
    std::wstring currentDpiScaleLabel;
    std::wstring showMenuAsDropdown;
    std::wstring notificationsTitle;
    std::wstring notificationsEnabled;
    std::wstring notifyOnStart;
    std::wstring notifyOnSuccess;
    std::wstring notifyOnFailure;
    std::wstring notifyOnBusy;
    std::wstring notifyOnAlreadyRunning;
    std::wstring notifyOnSingleInstanceFailure;
    std::wstring showConsole;
    std::wstring generateNow;

    std::wstring loggingEnable;
    std::wstring changePath;
    std::wstring renamePath;
    std::wstring openInExplorer;
    std::wstring resetDefault;
    std::wstring showPowerShellLog;

    std::wstring logPathPrefix;
    std::wstring logPathTitle;
    std::wstring psStatusPrefix;
    std::wstring psStatusTitle;

    std::wstring disableFitting;
    std::wstring listenFit;
    std::wstring fitModePrefix;
    std::wstring fitModeForced;

    std::wstring exitText;

    std::wstring renameDialogTitle;
    std::wstring okText;
    std::wstring cancelText;
    std::wstring errorTitle;
    std::wstring pathCannotBeEmpty;

    std::wstring noPowerShellOutputTitle;
    std::wstring noPowerShellOutput;

    std::wstring generateNowTitle;
    std::wstring wallpaperNotFound;
    std::wstring currentWallpaperNotFound;

    std::wstring programStarting;
    std::wstring notificationTitle;
    std::wstring notificationGenerationStarted;
    std::wstring notificationGenerationSuccess;
    std::wstring notificationGenerationFailed;
    std::wstring notificationGenerationBusy;
    std::wstring notificationAlreadyRunning;
    std::wstring notificationAlreadyRunningTrayPersistent;
    std::wstring notificationAlreadyRunningTraySession;
    std::wstring notificationSingleInstanceFailure;
    std::wstring notificationTrayIconHidden;
    std::wstring manualGenerateTriggered;
    std::wstring startingWallpaperGeneration;
    std::wstring wallpaperSource;
    std::wstring failedLoadWallpaper;
    std::wstring iniEncodingNormalized;
    std::wstring skippingAssetDisabled;
    std::wstring generatedDesktopIconAsset;
    std::wstring activeScales;
    std::wstring savedAsset;
    std::wstring savedScaledAsset;
    std::wstring failedSaveAsset;
    std::wstring failedSaveScaledAsset;
    std::wstring reRegisteringManifest;
    std::wstring manifestPath;
    std::wstring usingComRegistration;
    std::wstring invalidManifestPath;
    std::wstring comRegistrationSuccess;
    std::wstring comRegistrationFailed;
    std::wstring comRegistrationException;
    std::wstring comExceptionMessage;
    std::wstring comFallbackToPowerShell;
    std::wstring launchingPowerShellRegistration;
    std::wstring powerShellCommand;
    std::wstring powerShellCompleted;
    std::wstring powerShellErrorSideloadDisabled;
    std::wstring powerShellErrorCode;
    std::wstring powerShellOutputFollows;
    std::wstring powerShellRegistrationFailed;
    std::wstring assetGenerationFinished;
    std::wstring assetGenerationFailed;
    std::wstring appRegistrationFailed;

    std::wstring changeConfirmed;
    std::wstring wallpaperAndFitChangeDetected;
    std::wstring wallpaperChangeDetected;
    std::wstring fitModeChangeDetected;
    std::wstring dpiScaleChangeDetected;
    std::wstring regenerationAllowedAfterDebounce;

    std::wstring enabledText;
    std::wstring disabledText;
    std::wstring onText;
    std::wstring offText;

    std::wstring trayTip;
    std::wstring exeLabel;
    std::wstring iniLabel;
    std::wstring logFileLabel;
    std::wstring powerShellRegistrationSummary;
    std::wstring usePowerShellSummary;
    std::wstring trayIconSummary;
    std::wstring consoleSummary;
    std::wstring loggingSummary;
    std::wstring generateOnStartupSummary;
    std::wstring hideDisabledEntriesSummary;
    std::wstring generateDesktopIconForDisabledEntriesSummary;
    std::wstring generateScaleAutoSummary;
    std::wstring generateScaleSummary;
    std::wstring showMenuAsDropdownSummary;
    std::wstring notificationsSummary;
    std::wstring powerShellEnabledMode;
    std::wstring comPreferredMode;
    std::wstring consoleAllocated;
    std::wstring startupGenerationEnabled;
    std::wstring startupGenerationSkippedNoWallpaper;
    std::wstring manualGenerationRequested;
    std::wstring generationRequestBusy;
    std::wstring emptyWallpaper;
    std::wstring futureLogEntriesNewPath;
    std::wstring futureLogEntriesRenamedPath;
    std::wstring futureLogEntriesDefaultPath;
    std::wstring listenWallpaperState;
    std::wstring listenFitState;
    std::wstring disableFittingState;
    std::wstring assetToggleState;
    std::wstring comRegistrationStatusFailed;
    std::wstring gdiPlusStartupFailedTitle;
    std::wstring gdiPlusStartupFailedMessage;
    std::wstring win32FailureFmt;
    std::wstring logPathChanged;
    std::wstring logPathRenamed;
    std::wstring logPathReset;
    std::wstring listenFitAutoDisabled;
    std::wstring singleInstanceMutexFailure;

    std::wstring fitFill;
    std::wstring fitFit;
    std::wstring fitStretch;
    std::wstring fitCenter;
    std::wstring fitTile;
    std::wstring fitSpan;

    std::wstring notifyOnStartState;
    std::wstring notifyOnSuccessState;
    std::wstring notifyOnFailureState;
    std::wstring notifyOnBusyState;
    std::wstring notifyOnAlreadyRunningState;
    std::wstring notifyOnSingleInstanceFailureState;
    std::wstring unspecifiedReason;

    std::wstring changeDetected;
    std::wstring advancedTitle;
    std::wstring pollIntervalLabel;
    std::wstring confirmMsLabel;
    std::wstring debounceMinMsLabel;
    std::wstring singleInstanceFailureActionLabel;
    std::wstring singleInstanceActionIgnore;
    std::wstring singleInstanceActionWarn;
    std::wstring singleInstanceActionExit;
    std::wstring singleInstanceActionCrash;
    std::wstring unknownError;

    std::wstring logFileFilter;
    std::wstring allFilesFilter;
};

static UiStrings g_ui;
static const wchar_t* StateEnabled(bool v) { return v ? g_ui.enabledText.c_str() : g_ui.disabledText.c_str(); }
static const wchar_t* StateOn(bool v) { return v ? g_ui.onText.c_str() : g_ui.offText.c_str(); }
// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static void WriteConsoleLine(const std::wstring& line)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
    {
        std::wstring s = line;
        s += L"\r\n";
        DWORD written = 0;
        if (WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr))
            return;
    }

    fwprintf(stdout, L"%ls\n", line.c_str());
    fflush(stdout);
}

static std::wstring GetLogPathCopy()
{
    std::lock_guard<std::mutex> lk(g_pathMutex);
    return g_logPath;
}

static void SetLogPathCopy(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(g_pathMutex);
    g_logPath = path;
}

void Log(const wchar_t* fmt, ...)
{
    wchar_t msg[2048];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(msg, _countof(msg), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[64];
    swprintf(ts, 64, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    std::wstring line = ts + std::wstring(msg);

    std::lock_guard<std::mutex> lk(g_logMutex);
    if (g_logBuf.size() >= LOGMAX) g_logBuf.erase(g_logBuf.begin());
    g_logBuf.push_back(line);

    if (g_console) { WriteConsoleLine(line); }

    if (g_logging)
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
            AppendUtf8TextFile(logPath, line + L"\r\n");
    }
}

void LogText(const std::wstring& text)
{
    Log(L"%ls", text.c_str());
}

int IniReadI(const wchar_t* s, const wchar_t* k, int d)
{
    std::wstring text;
    std::wstring value;
    if (!ReadIniFileAuto(text) || !ReadIniValueFromText(text, s, k, value))
        return d;
    return _wtoi(value.c_str());
}

static int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

static int IniReadClampedI(const wchar_t* s, const wchar_t* k, int d, int minValue, int maxValue)
{
    return ClampInt(IniReadI(s, k, d), minValue, maxValue);
}

static std::wstring IntToWString(int value)
{
    wchar_t buf[32];
    swprintf(buf, _countof(buf), L"%d", value);
    return buf;
}

std::wstring IniReadS(const wchar_t* s, const wchar_t* k, const wchar_t* d)
{
    std::wstring text;
    std::wstring out;
    if (ReadIniFileAuto(text) && ReadIniValueFromText(text, s, k, out))
        return out;
    return d ? d : L"";
}

void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    WriteIniValue(s, k, v);
}

std::wstring GetExeDir()
{
    size_t pos = g_exePath.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".";
    return g_exePath.substr(0, pos);
}

std::wstring MakeFileUri(const std::wstring& path)
{
    DWORD need = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!need) return L"";

    std::vector<wchar_t> full(need);
    DWORD n = GetFullPathNameW(path.c_str(), (DWORD)full.size(), full.data(), nullptr);
    if (!n || n >= full.size()) return L"";

    std::wstring p(full.data(), n);
    if (p.rfind(L"\\\\?\\UNC\\", 0) == 0)
        p = L"\\\\" + p.substr(8);
    else if (p.rfind(L"\\\\?\\", 0) == 0)
        p.erase(0, 4);
    std::replace(p.begin(), p.end(), L'\\', L'/');

    std::wstring esc;
    esc.reserve(p.size() * 3);
    for (wchar_t ch : p)
    {
        switch (ch)
        {
        case L' ':  esc += L"%20"; break;
        case L'#':  esc += L"%23"; break;
        case L'%':  esc += L"%25"; break;
        case L'?':  esc += L"%3F"; break;
        case L'"':  esc += L"%22"; break;
        case L'<':  esc += L"%3C"; break;
        case L'>':  esc += L"%3E"; break;
        case L'|':  esc += L"%7C"; break;
        default:    esc.push_back(ch); break;
        }
    }

    if (esc.size() >= 2 && esc[1] == L':')
        esc.insert(esc.begin(), L'/');

    if (esc.rfind(L"//", 0) == 0)
        return L"file:" + esc;

    return L"file://" + esc;
}

std::wstring Win32ErrorString(DWORD err)
{
    wchar_t* buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);

    std::wstring msg;
    if (len && buf)
    {
        msg.assign(buf, buf + len);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L'.' || msg.back() == L' '))
            msg.pop_back();
    }
    else
    {
        msg = g_ui.unknownError.empty() ? L"Unknown error" : g_ui.unknownError;
    }

    if (buf) LocalFree(buf);
    return msg;
}

void LogWin32Failure(const wchar_t* what, DWORD err = GetLastError())
{
    Log(g_ui.win32FailureFmt.c_str(), what, err, Win32ErrorString(err).c_str());
}

std::wstring FormatWide(const wchar_t* fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    return std::wstring(buf);
}

template <size_t N>
static void CopyTruncated(wchar_t (&dest)[N], const std::wstring& text)
{
    wcsncpy_s(dest, N, text.c_str(), _TRUNCATE);
}

void NotifyTrayBalloon(const std::wstring& title, const std::wstring& msg, DWORD icon = NIIF_INFO, bool force = false)
{
    NOTIFYICONDATAW nid{};
    {
        std::lock_guard<std::mutex> lk(g_trayMutex);
        if ((!force && !g_tray) || (!force && !g_notificationsEnabled) || !g_nid.hWnd)
            return;
        nid = g_nid;
    }
    nid.uFlags = NIF_INFO;
    CopyTruncated(nid.szInfoTitle, title);
    CopyTruncated(nid.szInfo, msg);
    nid.dwInfoFlags = icon;
    nid.uTimeout = 4000;
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static bool NormalizeIniToUtf8BomIfNeeded()
{
    return NormalizeTextFileToUtf8Bom(g_iniPath);
}

void EnsureIniDefaults()
{
    std::wstring iniText;
    bool haveText = ReadIniFileAuto(iniText);

    for (const auto& d : g_defaults)
    {
        std::wstring value;
        if (!haveText || !ReadIniValueFromText(iniText, d.section, d.key, value))
            IniWrite(d.section, d.key, d.value);
    }
}


void LoadUiStrings()
{
    g_ui.generalTitle = IniReadS(L"Strings", L"GeneralTitle", L"General:");
    g_ui.loggingTitle = IniReadS(L"Strings", L"LoggingTitle", L"Logging:");
    g_ui.wallpaperFittingTitle = IniReadS(L"Strings", L"WallpaperFittingTitle", L"Wallpaper Fitting:");
    g_ui.assetsTitle = IniReadS(L"Strings", L"AssetsTitle", L"Assets:");
    g_ui.dpiScalesTitle = IniReadS(L"Strings", L"DpiScalesTitle", L"DPI Scales:");

    g_ui.listenWallpaper = IniReadS(L"Strings", L"ListenWallpaper", L"Listen Wallpaper");
    g_ui.trayIcon = IniReadS(L"Strings", L"TrayIcon", L"Tray Icon");
    g_ui.usePowerShell = IniReadS(L"Strings", L"UsePowerShell", L"Use PowerShell");
    g_ui.generateOnStartup = IniReadS(L"Strings", L"GenerateOnStartup", L"Generate on Startup");
    g_ui.hideDisabledEntries = IniReadS(L"Strings", L"HideDisabledEntries", L"Hide disabled entries");
    g_ui.generateDesktopIconForDisabledEntries = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries");
    g_ui.generateScaleAuto = IniReadS(L"Strings", L"GenerateScaleAuto", L"Auto: current DPI scale");
    g_ui.generateScale100 = IniReadS(L"Strings", L"GenerateScale100", L"100%");
    g_ui.generateScale125 = IniReadS(L"Strings", L"GenerateScale125", L"125%");
    g_ui.generateScale150 = IniReadS(L"Strings", L"GenerateScale150", L"150%");
    g_ui.generateScale200 = IniReadS(L"Strings", L"GenerateScale200", L"200%");
    g_ui.generateScale400 = IniReadS(L"Strings", L"GenerateScale400", L"400%");
    g_ui.currentDpiScaleLabel = IniReadS(L"Strings", L"CurrentDpiScaleLabel", L"Current DPI: %d%% (asset scale: %d%%)");
    g_ui.showMenuAsDropdown = IniReadS(L"Strings", L"ShowMenuAsDropdown", L"Show menu as dropdown");
    g_ui.notificationsTitle = IniReadS(L"Strings", L"NotificationsTitle", L"Notifications:");
    g_ui.notificationsEnabled = IniReadS(L"Strings", L"NotificationsEnabled", L"Enable all tray notifications");
    g_ui.notifyOnStart = IniReadS(L"Strings", L"NotifyOnStart", L"Notify when generation starts");
    g_ui.notifyOnSuccess = IniReadS(L"Strings", L"NotifyOnSuccess", L"Notify when generation succeeds");
    g_ui.notifyOnFailure = IniReadS(L"Strings", L"NotifyOnFailure", L"Notify when generation, wallpaper, or registration fails");
    g_ui.notifyOnBusy = IniReadS(L"Strings", L"NotifyOnBusy", L"Notify when generation is already running");
    g_ui.notifyOnAlreadyRunning = IniReadS(L"Strings", L"NotifyOnAlreadyRunning", L"Notify when the app is already running");
    g_ui.notifyOnSingleInstanceFailure = IniReadS(L"Strings", L"NotifyOnSingleInstanceFailure", L"Notify when single-instance protection fails");
    g_ui.showConsole = IniReadS(L"Strings", L"ShowConsole", L"Show Console");
    g_ui.generateNow = IniReadS(L"Strings", L"GenerateNow", L"Generate now");

    g_ui.loggingEnable = IniReadS(L"Strings", L"LoggingEnable", L"Enable");
    g_ui.changePath = IniReadS(L"Strings", L"ChangePath", L"Change path...");
    g_ui.renamePath = IniReadS(L"Strings", L"RenamePath", L"Rename path...");
    g_ui.openInExplorer = IniReadS(L"Strings", L"OpenInExplorer", L"Open in Explorer");
    g_ui.resetDefault = IniReadS(L"Strings", L"ResetDefault", L"Reset to default");
    g_ui.showPowerShellLog = IniReadS(L"Strings", L"ShowPowerShellLog", L"Show PowerShell Log");

    g_ui.logPathPrefix = IniReadS(L"Strings", L"LogPathPrefix", L"Path: ");
    g_ui.logPathTitle = IniReadS(L"Strings", L"LogPathTitle", L"Log Path");
    g_ui.psStatusPrefix = IniReadS(L"Strings", L"PSStatusPrefix", L"PS Status: ");
    g_ui.psStatusTitle = IniReadS(L"Strings", L"PSStatusTitle", L"PS Status");

    g_ui.disableFitting = IniReadS(L"Strings", L"DisableFitting", L"Disable Fitting");
    g_ui.listenFit = IniReadS(L"Strings", L"ListenFit", L"Listen Fit Mode");
    g_ui.fitModePrefix = IniReadS(L"Strings", L"FitModePrefix", L"Fit Mode: ");
    g_ui.fitModeForced = IniReadS(L"Strings", L"FitModeForced", L"Fit Mode: Fill (forced)");

    g_ui.exitText = IniReadS(L"Strings", L"Exit", L"Exit");

    g_ui.renameDialogTitle = IniReadS(L"Strings", L"RenameDialogTitle", L"Rename Log Path");
    g_ui.okText = IniReadS(L"Strings", L"OKText", L"OK");
    g_ui.cancelText = IniReadS(L"Strings", L"CancelText", L"Cancel");
    g_ui.errorTitle = IniReadS(L"Strings", L"ErrorTitle", L"Error");
    g_ui.pathCannotBeEmpty = IniReadS(L"Strings", L"PathCannotBeEmpty", L"Path cannot be empty.");

    g_ui.noPowerShellOutputTitle = IniReadS(L"Strings", L"NoPowerShellOutputTitle", L"PowerShell Output");
    g_ui.noPowerShellOutput = IniReadS(L"Strings", L"NoPowerShellOutput", L"No PowerShell output.");

    g_ui.generateNowTitle = IniReadS(L"Strings", L"GenerateNowTitle", L"Generate Now");
    g_ui.wallpaperNotFound = IniReadS(L"Strings", L"WallpaperNotFound", L"Could not detect current wallpaper.");
    g_ui.currentWallpaperNotFound = IniReadS(L"Strings", L"CurrentWallpaperNotFound", L"Could not detect current wallpaper.");

    g_ui.programStarting = IniReadS(L"Strings", L"ProgramStarting", L"Program starting...");
    g_ui.notificationTitle = IniReadS(L"Strings", L"NotificationTitle", L"Desktop Tile Generator");
    g_ui.notificationGenerationStarted = IniReadS(L"Strings", L"NotificationGenerationStarted", L"Wallpaper generation started.");
    g_ui.notificationGenerationSuccess = IniReadS(L"Strings", L"NotificationGenerationSuccess", L"Wallpaper generation completed successfully.");
    g_ui.notificationGenerationFailed = IniReadS(L"Strings", L"NotificationGenerationFailed", L"Wallpaper generation failed.");
    g_ui.notificationGenerationBusy = IniReadS(L"Strings", L"NotificationGenerationBusy", L"Wallpaper generation is already running.");
    g_ui.notificationAlreadyRunning = IniReadS(L"Strings", L"NotificationAlreadyRunning", L"Desktop Tile Generator is already running.");
    g_ui.notificationAlreadyRunningTrayPersistent = IniReadS(L"Strings", L"NotificationAlreadyRunningTrayPersistent", L"Desktop Tile Generator is already running. Tray icon is enabled in the config, so it will stay visible after restart.");
    g_ui.notificationAlreadyRunningTraySession = IniReadS(L"Strings", L"NotificationAlreadyRunningTraySession", L"Desktop Tile Generator is already running. Tray icon is visible for this session only; it will be hidden again after restart because TrayIcon=0 in the config.");
    g_ui.notificationSingleInstanceFailure = IniReadS(L"Strings", L"NotificationSingleInstanceFailure", L"Single-instance protection failed; another copy may run.");
    g_ui.notificationTrayIconHidden = IniReadS(L"Strings", L"NotificationTrayIconHidden", L"Tray icon hidden for this session. Run GenerateAssets.exe again to restore it.");
    g_ui.manualGenerateTriggered = IniReadS(L"Strings", L"ManualGenerateTriggered", L"Manual generate triggered.");
    g_ui.startingWallpaperGeneration = IniReadS(L"Strings", L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s.");
    g_ui.wallpaperSource = IniReadS(L"Strings", L"WallpaperSource", L"Wallpaper source: %s");
    g_ui.failedLoadWallpaper = IniReadS(L"Strings", L"FailedLoadWallpaper", L"Failed to load wallpaper image.");
    g_ui.iniEncodingNormalized = IniReadS(L"Strings", L"IniEncodingNormalized", L"[i] INI file normalized to UTF-8 with BOM.");
    g_ui.skippingAssetDisabled = IniReadS(L"Strings", L"SkippingAssetDisabled", L"Skipping %s (disabled in settings).");
    g_ui.generatedDesktopIconAsset = IniReadS(L"Strings", L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s");
    g_ui.activeScales = IniReadS(L"Strings", L"ActiveScales", L"[i] Active scales: %s");
    g_ui.savedAsset = IniReadS(L"Strings", L"SavedAsset", L"Saved %s");
    g_ui.savedScaledAsset = IniReadS(L"Strings", L"SavedScaledAsset", L"Saved %s (scale %d%%)");
    g_ui.failedSaveAsset = IniReadS(L"Strings", L"FailedSaveAsset", L"Failed to save %s");
    g_ui.failedSaveScaledAsset = IniReadS(L"Strings", L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)");

    g_ui.reRegisteringManifest = IniReadS(L"Strings", L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets.");
    g_ui.manifestPath = IniReadS(L"Strings", L"ManifestPath", L"Manifest path: %s");
    g_ui.usingComRegistration = IniReadS(L"Strings", L"UsingComRegistration", L"Using COM Appx registration...");
    g_ui.invalidManifestPath = IniReadS(L"Strings", L"InvalidManifestPath", L"Invalid manifest path.");
    g_ui.comRegistrationSuccess = IniReadS(L"Strings", L"ComRegistrationSuccess", L"COM registration success.");
    g_ui.comRegistrationFailed = IniReadS(L"Strings", L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration.");
    g_ui.comRegistrationException = IniReadS(L"Strings", L"ComRegistrationException", L"COM registration threw exception: 0x%08X");
    g_ui.comExceptionMessage = IniReadS(L"Strings", L"ComExceptionMessage", L"COM message: %s");
    g_ui.comFallbackToPowerShell = IniReadS(L"Strings", L"ComFallbackToPowerShell", L"COM failed, falling back to PowerShell...");
    g_ui.launchingPowerShellRegistration = IniReadS(L"Strings", L"LaunchingPowerShellRegistration", L"Launching PowerShell registration...");
    g_ui.powerShellCommand = IniReadS(L"Strings", L"PowerShellCommand", L"Command: %s");
    g_ui.powerShellCompleted = IniReadS(L"Strings", L"PowerShellCompleted", L"PowerShell registration completed successfully.");
    g_ui.powerShellErrorSideloadDisabled = IniReadS(L"Strings", L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!");
    g_ui.powerShellErrorCode = IniReadS(L"Strings", L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X.");
    g_ui.powerShellOutputFollows = IniReadS(L"Strings", L"PowerShellOutputFollows", L"PowerShell output follows:");
    g_ui.powerShellRegistrationFailed = IniReadS(L"Strings", L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully.");
    g_ui.assetGenerationFinished = IniReadS(L"Strings", L"AssetGenerationFinished", L"Asset generation and registration finished successfully.");
    g_ui.assetGenerationFailed = IniReadS(L"Strings", L"AssetGenerationFailed", L"Asset generation did not complete successfully.");
    g_ui.appRegistrationFailed = IniReadS(L"Strings", L"AppRegistrationFailed", L"App registration did not complete successfully.");

    g_ui.changeConfirmed = IniReadS(L"Strings", L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce.");
    g_ui.wallpaperAndFitChangeDetected = IniReadS(L"Strings", L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected.");
    g_ui.wallpaperChangeDetected = IniReadS(L"Strings", L"WallpaperChangeDetected", L"Wallpaper change detected.");
    g_ui.fitModeChangeDetected = IniReadS(L"Strings", L"FitModeChangeDetected", L"Fit mode change detected.");
    g_ui.dpiScaleChangeDetected = IniReadS(L"Strings", L"DpiScaleChangeDetected", L"DPI scale change detected.");
    g_ui.regenerationAllowedAfterDebounce = IniReadS(L"Strings", L"RegenerationAllowedAfterDebounce", L"Regeneration allowed after debounce.");
    g_ui.enabledText = IniReadS(L"Strings", L"EnabledText", L"enabled");
    g_ui.disabledText = IniReadS(L"Strings", L"DisabledText", L"disabled");
    g_ui.onText = IniReadS(L"Strings", L"OnText", L"on");
    g_ui.offText = IniReadS(L"Strings", L"OffText", L"off");

    g_ui.trayTip = IniReadS(L"Strings", L"TrayTip", L"Desktop Tile Generator");
    g_ui.exeLabel = IniReadS(L"Strings", L"ExeLabel", L"[i] EXE: %s");
    g_ui.iniLabel = IniReadS(L"Strings", L"IniLabel", L"[i] INI: %s");
    g_ui.logFileLabel = IniReadS(L"Strings", L"LogFileLabel", L"[i] Log file: %s");
    g_ui.powerShellRegistrationSummary = IniReadS(L"Strings", L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s");
    g_ui.usePowerShellSummary = IniReadS(L"Strings", L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)");
    g_ui.trayIconSummary = IniReadS(L"Strings", L"TrayIconSummary", L"[i] Tray icon: %s");
    g_ui.consoleSummary = IniReadS(L"Strings", L"ConsoleSummary", L"[i] Console %s.");
    g_ui.loggingSummary = IniReadS(L"Strings", L"LoggingSummary", L"[i] Logging %s.");
    g_ui.generateOnStartupSummary = IniReadS(L"Strings", L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s");
    g_ui.hideDisabledEntriesSummary = IniReadS(L"Strings", L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s");
    g_ui.generateDesktopIconForDisabledEntriesSummary = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s");
    g_ui.generateScaleAutoSummary = IniReadS(L"Strings", L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s");
    g_ui.generateScaleSummary = IniReadS(L"Strings", L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s");
    g_ui.showMenuAsDropdownSummary = IniReadS(L"Strings", L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s");
    g_ui.notificationsSummary = IniReadS(L"Strings", L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)");
    g_ui.powerShellEnabledMode = IniReadS(L"Strings", L"PowerShellEnabledMode", L"PowerShell enabled");
    g_ui.comPreferredMode = IniReadS(L"Strings", L"ComPreferredMode", L"COM preferred with PowerShell fallback");
    g_ui.consoleAllocated = IniReadS(L"Strings", L"ConsoleAllocated", L"[i] Console allocated.");
    g_ui.startupGenerationEnabled = IniReadS(L"Strings", L"StartupGenerationEnabled", L"[i] Startup generation enabled.");
    g_ui.startupGenerationSkippedNoWallpaper = IniReadS(L"Strings", L"StartupGenerationSkippedNoWallpaper", L"[!] Startup generation skipped: no wallpaper detected.");
    g_ui.manualGenerationRequested = IniReadS(L"Strings", L"ManualGenerationRequested", L"[i] Manual generation requested from the tray menu.");
    g_ui.generationRequestBusy = IniReadS(L"Strings", L"GenerationRequestBusy", L"[!] Generation request ignored: another operation is already running.");
    g_ui.emptyWallpaper = IniReadS(L"Strings", L"EmptyWallpaper", L"[!] Empty wallpaper.");
    g_ui.futureLogEntriesNewPath = IniReadS(L"Strings", L"FutureLogEntriesNewPath", L"[i] Future log entries will be written to the new path.");
    g_ui.futureLogEntriesRenamedPath = IniReadS(L"Strings", L"FutureLogEntriesRenamedPath", L"[i] Future log entries will be written to the renamed path.");
    g_ui.futureLogEntriesDefaultPath = IniReadS(L"Strings", L"FutureLogEntriesDefaultPath", L"[i] Future log entries will be written to the default path.");
    g_ui.listenWallpaperState = IniReadS(L"Strings", L"ListenWallpaperState", L"[i] ListenWallpaper = %d");
    g_ui.listenFitState = IniReadS(L"Strings", L"ListenFitState", L"[i] ListenFit = %d");
    g_ui.disableFittingState = IniReadS(L"Strings", L"DisableFittingState", L"[i] DisableFitting = %d");
    g_ui.assetToggleState = IniReadS(L"Strings", L"AssetToggleState", L"[i] Asset %s %s.");
    g_ui.comRegistrationStatusFailed = IniReadS(L"Strings", L"ComRegistrationStatusFailed", L"[!] COM registration failed (status=%d)");
    g_ui.gdiPlusStartupFailedTitle = IniReadS(L"Strings", L"GdiPlusStartupFailedTitle", L"GDI+");
    g_ui.gdiPlusStartupFailedMessage = IniReadS(L"Strings", L"GdiPlusStartupFailedMessage", L"GDI+ startup failed");
    g_ui.win32FailureFmt = IniReadS(L"Strings", L"Win32FailureFmt", L"[!] %s failed (error %lu: %s)");
    g_ui.logPathChanged = IniReadS(L"Strings", L"LogPathChanged", L"[i] Log path changed to: %s");
    g_ui.logPathRenamed = IniReadS(L"Strings", L"LogPathRenamed", L"[i] Log path renamed to: %s");
    g_ui.logPathReset = IniReadS(L"Strings", L"LogPathReset", L"[i] Log path reset to default: %s");
    g_ui.listenFitAutoDisabled = IniReadS(L"Strings", L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting.");
    g_ui.singleInstanceMutexFailure = IniReadS(L"Strings", L"SingleInstanceMutexFailure", L"[!] Single-instance mutex creation failed (error %lu: %s); action=%s.");

    g_ui.fitFill = IniReadS(L"Strings", L"FitFill", L"Fill");
    g_ui.fitFit = IniReadS(L"Strings", L"FitFit", L"Fit");
    g_ui.fitStretch = IniReadS(L"Strings", L"FitStretch", L"Stretch");
    g_ui.fitCenter = IniReadS(L"Strings", L"FitCenter", L"Center");
    g_ui.fitTile = IniReadS(L"Strings", L"FitTile", L"Tile");
    g_ui.fitSpan = IniReadS(L"Strings", L"FitSpan", L"Span");

    g_ui.notifyOnStartState = IniReadS(L"Strings", L"NotifyOnStartState", L"[i] NotifyOnStart = %d");
    g_ui.notifyOnSuccessState = IniReadS(L"Strings", L"NotifyOnSuccessState", L"[i] NotifyOnSuccess = %d");
    g_ui.notifyOnFailureState = IniReadS(L"Strings", L"NotifyOnFailureState", L"[i] NotifyOnFailure = %d");
    g_ui.notifyOnBusyState = IniReadS(L"Strings", L"NotifyOnBusyState", L"[i] NotifyOnBusy = %d");
    g_ui.notifyOnAlreadyRunningState = IniReadS(L"Strings", L"NotifyOnAlreadyRunningState", L"[i] NotifyOnAlreadyRunning = %d");
    g_ui.notifyOnSingleInstanceFailureState = IniReadS(L"Strings", L"NotifyOnSingleInstanceFailureState", L"[i] NotifyOnSingleInstanceFailure = %d");
    g_ui.unspecifiedReason = IniReadS(L"Strings", L"UnspecifiedReason", L"an unspecified reason");
    g_ui.changeDetected = IniReadS(L"Strings", L"ChangeDetected", L"change detected");
    g_ui.advancedTitle = IniReadS(L"Strings", L"AdvancedTitle", L"Advanced:");
    g_ui.pollIntervalLabel = IniReadS(L"Strings", L"PollIntervalLabel", L"Poll interval: %d ms");
    g_ui.confirmMsLabel = IniReadS(L"Strings", L"ConfirmMsLabel", L"Confirm: %d ms");
    g_ui.debounceMinMsLabel = IniReadS(L"Strings", L"DebounceMinMsLabel", L"Debounce min: %d ms");
    g_ui.singleInstanceFailureActionLabel = IniReadS(L"Strings", L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s");
    g_ui.singleInstanceActionIgnore = IniReadS(L"Strings", L"SingleInstanceActionIgnore", L"Ignore");
    g_ui.singleInstanceActionWarn = IniReadS(L"Strings", L"SingleInstanceActionWarn", L"Warn");
    g_ui.singleInstanceActionExit = IniReadS(L"Strings", L"SingleInstanceActionExit", L"Exit");
    g_ui.singleInstanceActionCrash = IniReadS(L"Strings", L"SingleInstanceActionCrash", L"Crash");
    g_ui.unknownError = IniReadS(L"Strings", L"UnknownError", L"Unknown error");

    g_ui.logFileFilter = IniReadS(L"Strings", L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||");
    g_ui.allFilesFilter = IniReadS(L"Strings", L"AllFilesFilter", L"All files (*.*)|*.*||");
}

static const wchar_t* FindStringDefault(const wchar_t* key, const wchar_t* fallback)
{
    for (const auto& d : g_stringDefaults)
    {
        if (IEquals(d.key, key))
            return d.value;
    }
    return fallback;
}

static std::vector<std::wstring> ExtractFormatTokens(const std::wstring& fmt)
{
    std::vector<std::wstring> tokens;
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        if (fmt[i] != L'%')
            continue;

        if (i + 1 < fmt.size() && fmt[i + 1] == L'%')
        {
            ++i;
            continue;
        }

        if (fmt.compare(i, 4, L"%08X") == 0)
        {
            tokens.push_back(L"%08X");
            i += 3;
        }
        else if (fmt.compare(i, 3, L"%lu") == 0)
        {
            tokens.push_back(L"%lu");
            i += 2;
        }
        else if (fmt.compare(i, 2, L"%s") == 0)
        {
            tokens.push_back(L"%s");
            i += 1;
        }
        else if (fmt.compare(i, 2, L"%d") == 0)
        {
            tokens.push_back(L"%d");
            i += 1;
        }
        else
        {
            tokens.push_back(L"<invalid>");
        }
    }
    return tokens;
}

static bool FormatTokensMatch(const std::wstring& fmt, std::initializer_list<const wchar_t*> expected)
{
    std::vector<std::wstring> actual = ExtractFormatTokens(fmt);
    if (actual.size() != expected.size())
        return false;

    size_t i = 0;
    for (const wchar_t* token : expected)
    {
        if (actual[i++] != token)
            return false;
    }
    return true;
}

static void RequireFormat(std::wstring& text, const wchar_t* key, std::initializer_list<const wchar_t*> expected)
{
    if (!FormatTokensMatch(text, expected))
        text = FindStringDefault(key, L"");
}

static void ValidateFormatStrings()
{
    RequireFormat(g_ui.exeLabel, L"ExeLabel", { L"%s" });
    RequireFormat(g_ui.iniLabel, L"IniLabel", { L"%s" });
    RequireFormat(g_ui.logFileLabel, L"LogFileLabel", { L"%s" });
    RequireFormat(g_ui.powerShellRegistrationSummary, L"PowerShellRegistrationSummary", { L"%s" });
    RequireFormat(g_ui.usePowerShellSummary, L"UsePowerShellSummary", { L"%d", L"%s" });
    RequireFormat(g_ui.trayIconSummary, L"TrayIconSummary", { L"%s" });
    RequireFormat(g_ui.consoleSummary, L"ConsoleSummary", { L"%s" });
    RequireFormat(g_ui.loggingSummary, L"LoggingSummary", { L"%s" });
    RequireFormat(g_ui.generateOnStartupSummary, L"GenerateOnStartupSummary", { L"%s" });
    RequireFormat(g_ui.hideDisabledEntriesSummary, L"HideDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.generateDesktopIconForDisabledEntriesSummary, L"GenerateDesktopIconForDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.generateScaleAutoSummary, L"GenerateScaleAutoSummary", { L"%s" });
    RequireFormat(g_ui.generateScaleSummary, L"GenerateScaleSummary", { L"%d", L"%s" });
    RequireFormat(g_ui.currentDpiScaleLabel, L"CurrentDpiScaleLabel", { L"%d", L"%d" });
    RequireFormat(g_ui.showMenuAsDropdownSummary, L"ShowMenuAsDropdownSummary", { L"%s" });
    RequireFormat(g_ui.notificationsSummary, L"NotificationsSummary", { L"%s", L"%s", L"%s", L"%s", L"%s", L"%s", L"%s" });
    RequireFormat(g_ui.listenWallpaperState, L"ListenWallpaperState", { L"%d" });
    RequireFormat(g_ui.listenFitState, L"ListenFitState", { L"%d" });
    RequireFormat(g_ui.disableFittingState, L"DisableFittingState", { L"%d" });
    RequireFormat(g_ui.assetToggleState, L"AssetToggleState", { L"%s", L"%s" });
    RequireFormat(g_ui.comRegistrationStatusFailed, L"ComRegistrationStatusFailed", { L"%d" });
    RequireFormat(g_ui.win32FailureFmt, L"Win32FailureFmt", { L"%s", L"%lu", L"%s" });
    RequireFormat(g_ui.logPathChanged, L"LogPathChanged", { L"%s" });
    RequireFormat(g_ui.logPathRenamed, L"LogPathRenamed", { L"%s" });
    RequireFormat(g_ui.logPathReset, L"LogPathReset", { L"%s" });
    RequireFormat(g_ui.singleInstanceMutexFailure, L"SingleInstanceMutexFailure", { L"%lu", L"%s", L"%s" });
    RequireFormat(g_ui.notifyOnStartState, L"NotifyOnStartState", { L"%d" });
    RequireFormat(g_ui.notifyOnSuccessState, L"NotifyOnSuccessState", { L"%d" });
    RequireFormat(g_ui.notifyOnFailureState, L"NotifyOnFailureState", { L"%d" });
    RequireFormat(g_ui.notifyOnBusyState, L"NotifyOnBusyState", { L"%d" });
    RequireFormat(g_ui.notifyOnAlreadyRunningState, L"NotifyOnAlreadyRunningState", { L"%d" });
    RequireFormat(g_ui.notifyOnSingleInstanceFailureState, L"NotifyOnSingleInstanceFailureState", { L"%d" });
    RequireFormat(g_ui.pollIntervalLabel, L"PollIntervalLabel", { L"%d" });
    RequireFormat(g_ui.confirmMsLabel, L"ConfirmMsLabel", { L"%d" });
    RequireFormat(g_ui.debounceMinMsLabel, L"DebounceMinMsLabel", { L"%d" });
    RequireFormat(g_ui.singleInstanceFailureActionLabel, L"SingleInstanceFailureActionLabel", { L"%s" });

    RequireFormat(g_ui.startingWallpaperGeneration, L"StartingWallpaperGeneration", { L"%s" });
    RequireFormat(g_ui.wallpaperSource, L"WallpaperSource", { L"%s" });
    RequireFormat(g_ui.skippingAssetDisabled, L"SkippingAssetDisabled", { L"%s" });
    RequireFormat(g_ui.generatedDesktopIconAsset, L"GeneratedDesktopIconAsset", { L"%s" });
    RequireFormat(g_ui.activeScales, L"ActiveScales", { L"%s" });
    RequireFormat(g_ui.savedAsset, L"SavedAsset", { L"%s" });
    RequireFormat(g_ui.savedScaledAsset, L"SavedScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.failedSaveAsset, L"FailedSaveAsset", { L"%s" });
    RequireFormat(g_ui.failedSaveScaledAsset, L"FailedSaveScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.manifestPath, L"ManifestPath", { L"%s" });
    RequireFormat(g_ui.comRegistrationException, L"ComRegistrationException", { L"%08X" });
    RequireFormat(g_ui.comExceptionMessage, L"ComExceptionMessage", { L"%s" });
    RequireFormat(g_ui.powerShellCommand, L"PowerShellCommand", { L"%s" });
    RequireFormat(g_ui.powerShellErrorCode, L"PowerShellErrorCode", { L"%08X" });
}

static std::wstring EnsureTrailingSpace(std::wstring s)
{
    if (!s.empty() && s.back() != L' ')
        s.push_back(L' ');
    return s;
}

static void NormalizeSeparatorStrings()
{
    g_ui.logPathPrefix = EnsureTrailingSpace(g_ui.logPathPrefix);
    g_ui.psStatusPrefix = EnsureTrailingSpace(g_ui.psStatusPrefix);
    g_ui.fitModePrefix = EnsureTrailingSpace(g_ui.fitModePrefix);
}

bool UsePowerShell()
{
    return IniReadI(L"Settings", L"UsePowerShell", 1) != 0;
}

static ErrorAction ParseErrorAction(const std::wstring& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(value));
    if (v == L"ignore" || v == L"none" || v == L"do nothing" || v == L"do_nothing")
        return ErrorAction::Ignore;
    if (v == L"exit" || v == L"exit program" || v == L"exit_program")
        return ErrorAction::Exit;
    if (v == L"crash" || v == L"crash program" || v == L"crash_program" || v == L"failfast" || v == L"fail fast")
        return ErrorAction::Crash;
    return ErrorAction::Warn;
}

static const wchar_t* ErrorActionName(ErrorAction action)
{
    switch (action)
    {
    case ErrorAction::Ignore: return g_ui.singleInstanceActionIgnore.c_str();
    case ErrorAction::Exit: return g_ui.singleInstanceActionExit.c_str();
    case ErrorAction::Crash: return g_ui.singleInstanceActionCrash.c_str();
    case ErrorAction::Warn:
    default:
        return g_ui.singleInstanceActionWarn.c_str();
    }
}

static ErrorAction CurrentSingleInstanceFailureAction()
{
    return ParseErrorAction(IniReadS(L"Settings", L"SingleInstanceFailureAction", L"Warn"));
}

static int SnapToSupportedScale(int percent)
{
    static const int supported[] = { 100, 125, 150, 200, 400 };
    for (int scale : supported)
    {
        if (percent <= scale)
            return scale;
    }
    return 400;
}

static int CurrentDpiPercent()
{
    UINT dpiX = 96;
    UINT dpiY = 96;

    HMODULE shcore = LoadLibraryW(L"shcore.dll");
    if (shcore)
    {
        using GetDpiForMonitorFn = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
        auto getDpiForMonitor = (GetDpiForMonitorFn)GetProcAddress(shcore, "GetDpiForMonitor");
        if (getDpiForMonitor)
        {
            POINT pt{ 0, 0 };
            HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
            if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)) && dpiX > 0)
            {
                FreeLibrary(shcore);
                return MulDiv((int)dpiX, 100, 96);
            }
        }
        FreeLibrary(shcore);
    }

    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (user32)
    {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        auto getDpiForSystem = (GetDpiForSystemFn)GetProcAddress(user32, "GetDpiForSystem");
        if (getDpiForSystem)
        {
            dpiX = getDpiForSystem();
            if (dpiX > 0)
                return MulDiv((int)dpiX, 100, 96);
        }
    }

    HDC dc = GetDC(nullptr);
    if (dc)
    {
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpi > 0)
            return MulDiv(dpi, 100, 96);
    }

    return 100;
}

static int CurrentAssetScale()
{
    return SnapToSupportedScale(CurrentDpiPercent());
}

static bool AddScale(std::vector<int>& scales, int scale)
{
    if (std::find(scales.begin(), scales.end(), scale) != scales.end())
        return false;
    scales.push_back(scale);
    return true;
}

static std::vector<int> GetConfiguredScales()
{
    std::vector<int> scales;
    if (g_generateScaleAuto)
    {
        // Auto mode: use only the detected DPI scale; manual flags are ignored
        AddScale(scales, CurrentAssetScale());
    }
    else
    {
        if (g_generateScale100) AddScale(scales, 100);
        if (g_generateScale125) AddScale(scales, 125);
        if (g_generateScale150) AddScale(scales, 150);
        if (g_generateScale200) AddScale(scales, 200);
        if (g_generateScale400) AddScale(scales, 400);
    }
    if (scales.empty()) AddScale(scales, 100);
    std::sort(scales.begin(), scales.end());
    return scales;
}

static int ScalePixels(int value, int scale)
{
    return MulDiv(value, scale, 100);
}

static std::wstring ScaleAssetPath(const std::wstring& path, int scale)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        dot = path.size();

    return path.substr(0, dot) + L".scale-" + IntToWString(scale) + path.substr(dot);
}

const wchar_t* FitModeToString(FitMode m)
{
    switch (m)
    {
    case FitMode::Fill:    return g_ui.fitFill.c_str();
    case FitMode::Fit:     return g_ui.fitFit.c_str();
    case FitMode::Stretch: return g_ui.fitStretch.c_str();
    case FitMode::Center:  return g_ui.fitCenter.c_str();
    case FitMode::Tile:    return g_ui.fitTile.c_str();
    case FitMode::Span:    return g_ui.fitSpan.c_str();
    default:               return L"?";
    }
}

INT_PTR ShowRenameDialog(HWND parent, std::wstring& path)
{
    size_t stringBytes =
        (g_ui.renameDialogTitle.size() + g_ui.okText.size() + g_ui.cancelText.size() + 64) * sizeof(wchar_t);
    std::vector<BYTE> tmpl(4096 + stringBytes);
    DLGTEMPLATE* dlg = (DLGTEMPLATE*)tmpl.data();

    dlg->style = WS_POPUP | WS_BORDER | WS_SYSMENU | DS_MODALFRAME | WS_CAPTION | DS_SETFONT;
    dlg->cdit = 3;
    dlg->x = 10; dlg->y = 10; dlg->cx = 200; dlg->cy = 60;

    WORD* p = (WORD*)(dlg + 1);

    *p++ = 0; // no menu
    *p++ = 0; // default class

    auto appendString = [](WORD*& dst, const std::wstring& text) {
        memcpy(dst, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        dst += text.size() + 1;
    };

    appendString(p, g_ui.renameDialogTitle);
    *p++ = 9; // font size
    appendString(p, L"Segoe UI");
    // ---- Edit box ----
    DLGITEMTEMPLATE* item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 5; item->y = 5; item->cx = 190; item->cy = 12;
    item->id = 1001;
    item->style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0081; // EDIT class
    *p++ = 0; // no text
    *p++ = 0; // no extra data

    // ---- OK button ----
    item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 40; item->y = 25; item->cx = 50; item->cy = 14;
    item->id = IDOK;
    item->style = WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080; // BUTTON
    appendString(p, g_ui.okText);
    *p++ = 0;

    // ---- Cancel button ----
    item = (DLGITEMTEMPLATE*)(((DWORD_PTR)p + 3) & ~3);
    item->x = 110; item->y = 25; item->cx = 50; item->cy = 14;
    item->id = IDCANCEL;
    item->style = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON;

    p = (WORD*)(item + 1);
    *p++ = 0xFFFF; *p++ = 0x0080;
    appendString(p, g_ui.cancelText);
    *p++ = 0;

    return DialogBoxIndirectParamW(
        GetModuleHandleW(nullptr),
        (DLGTEMPLATE*)tmpl.data(),
        parent,
        RenameDlgProc,
        (LPARAM)&path
    );
}

// -----------------------------------------------------------------------------
// Wallpaper
// -----------------------------------------------------------------------------

std::wstring GetWallpaper()
{
    HKEY h; if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h)) return L"";

    DWORD t=0, sz=0;
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, &t, nullptr, &sz) || t!=REG_BINARY)
    { RegCloseKey(h); return L""; }

    std::vector<BYTE> buf(sz);
    if (RegQueryValueExW(h, L"TranscodedImageCache", nullptr, nullptr, buf.data(), &sz))
    { RegCloseKey(h); return L""; }

    RegCloseKey(h);
    if (sz <= 24) return L"";
    const wchar_t* p = (wchar_t*)(buf.data()+24);
    size_t len = (sz-24)/sizeof(wchar_t);
    return std::wstring(p, wcsnlen(p, len));
}

FitMode GetWallpaperFit()
{
    HKEY h;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Control Panel\\Desktop", 0, KEY_READ, &h))
        return FitMode::Fill;

    wchar_t style[16] = {};
    wchar_t tile[8] = {};

    DWORD sz = sizeof(style);
    RegQueryValueExW(h, L"WallpaperStyle", nullptr, nullptr, (LPBYTE)style, &sz);

    sz = sizeof(tile);
    RegQueryValueExW(h, L"TileWallpaper", nullptr, nullptr, (LPBYTE)tile, &sz);

    RegCloseKey(h);

    int s = _wtoi(style);
    int t = _wtoi(tile);

    if (t == 1) return FitMode::Tile;

    switch (s)
    {
    case 10: return FitMode::Fill;
    case 6:  return FitMode::Fit;
    case 2:  return FitMode::Stretch;
    case 22: return FitMode::Span;
    default: return FitMode::Center;
    }
}

// -----------------------------------------------------------------------------
// GDI+ PNG save helpers
// -----------------------------------------------------------------------------
static CLSID g_png{};
bool FindPngEncoder()
{
    if (g_png.Data1) return true;

    UINT n=0, sz=0;
    if (GetImageEncodersSize(&n, &sz) != Ok || !sz) return false;
    auto* p = (ImageCodecInfo*)malloc(sz);
    if (!p) return false;

    if (GetImageEncoders(n, sz, p) == Ok)
        for (UINT i=0;i<n;i++)
            if (wcscmp(p[i].MimeType, L"image/png")==0)
                g_png = p[i].Clsid;

    free(p);
    return g_png.Data1 != 0;
}

static Bitmap* ResizeBitmapToMode(Bitmap* src, int w, int h, FitMode mode, Color background)
{
    if (!src) return nullptr;

    int sw = src->GetWidth();
    int sh = src->GetHeight();
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
        return nullptr;

    auto* out = new Bitmap(w, h, PixelFormat32bppARGB);
    Graphics g(out);

    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(background);

    double scaleX = (double)w / sw;
    double scaleY = (double)h / sh;

    int dw = w, dh = h;
    int dx = 0, dy = 0;

    switch (mode)
    {
    case FitMode::Fill:
    {
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Fit:
    {
        double s = std::min(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }

    case FitMode::Stretch:
    {
        dw = w;
        dh = h;
        dx = 0;
        dy = 0;
        break;
    }

    case FitMode::Center:
    {
        dw = sw;
        dh = sh;
        dx = (w - sw) / 2;
        dy = (h - sh) / 2;
        break;
    }

    case FitMode::Tile:
        // Handled by ResizeWithFit after building a virtual-desktop-sized tiled canvas.
        break;

    case FitMode::Span:
    {
        // fallback: treat as Fill
        double s = std::max(scaleX, scaleY);
        dw = int(sw * s + 0.5);
        dh = int(sh * s + 0.5);
        dx = (w - dw) / 2;
        dy = (h - dh) / 2;
        break;
    }
    }

    g.DrawImage(src, Rect(dx, dy, dw, dh), 0, 0, sw, sh, UnitPixel);
    return out;
}

static Bitmap* BuildTiledDesktopBitmap(Bitmap* src)
{
    if (!src) return nullptr;

    int sw = src->GetWidth();
    int sh = src->GetHeight();
    if (sw <= 0 || sh <= 0)
        return nullptr;

    int desktopW = GetSystemMetrics(SM_CXSCREEN);
    int desktopH = GetSystemMetrics(SM_CYSCREEN);
    if (desktopW <= 0 || desktopH <= 0)
        return nullptr;

    auto* out = new Bitmap(desktopW, desktopH, PixelFormat32bppARGB);
    Graphics g(out);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.Clear(Color(255, 0, 0, 0));

    for (int y = 0; y < desktopH; y += sh)
        for (int x = 0; x < desktopW; x += sw)
            g.DrawImage(src, x, y, sw, sh);

    return out;
}

Bitmap* ResizeWithFit(Bitmap* src, int w, int h, FitMode mode)
{
    if (mode != FitMode::Tile)
        return ResizeBitmapToMode(src, w, h, mode, Color(255, 0, 0, 0));

    Bitmap* desktop = BuildTiledDesktopBitmap(src);
    if (!desktop)
        return nullptr;

    Bitmap* out = ResizeBitmapToMode(desktop, w, h, FitMode::Fill, Color(255, 0, 0, 0));
    delete desktop;
    return out;
}

bool SavePNG(Bitmap* b, const wchar_t* f)
{ return b && FindPngEncoder() && b->Save(f,&g_png,nullptr)==Ok; }

// Embedded 30x30 RGBA PNG — the Desktop icon written to disabled asset slots.
static const BYTE g_desktopIconPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
    0x00, 0x00, 0x00, 0x1E, 0x00, 0x00, 0x00, 0x1E, 0x08, 0x06, 0x00, 0x00, 0x00, 0x3B, 0x30, 0xAE,
    0xA2, 0x00, 0x00, 0x00, 0x01, 0x73, 0x52, 0x47, 0x42, 0x00, 0xAE, 0xCE, 0x1C, 0xE9, 0x00, 0x00,
    0x00, 0x04, 0x67, 0x41, 0x4D, 0x41, 0x00, 0x00, 0xB1, 0x8F, 0x0B, 0xFC, 0x61, 0x05, 0x00, 0x00,
    0x00, 0x09, 0x70, 0x48, 0x59, 0x73, 0x00, 0x00, 0x0E, 0xC2, 0x00, 0x00, 0x0E, 0xC2, 0x01, 0x15,
    0x28, 0x4A, 0x80, 0x00, 0x00, 0x00, 0x62, 0x49, 0x44, 0x41, 0x54, 0x48, 0x4B, 0xED, 0xD0, 0xD1,
    0x0A, 0x80, 0x20, 0x10, 0x44, 0xD1, 0xFD, 0xFF, 0x9F, 0x2E, 0x88, 0x0B, 0x31, 0x61, 0x64, 0xC1,
    0xBA, 0x62, 0x73, 0x9E, 0xC4, 0x55, 0x2F, 0x18, 0xB6, 0xBE, 0xAD, 0x48, 0x7D, 0x98, 0x0F, 0x48,
    0x47, 0xCE, 0xE1, 0x3E, 0x5C, 0x79, 0xC4, 0x71, 0xC1, 0xC8, 0x61, 0xC5, 0x48, 0xDC, 0xED, 0xB7,
    0x1C, 0x8F, 0x5C, 0x30, 0x72, 0x58, 0x31, 0x12, 0x8C, 0x3E, 0xE3, 0x99, 0xF7, 0xE1, 0x16, 0x8E,
    0x77, 0xE1, 0xCA, 0xA4, 0xE1, 0x0C, 0xE4, 0xFE, 0x1E, 0x66, 0x99, 0x66, 0xBE, 0xF0, 0x08, 0xE4,
    0xCE, 0xF0, 0x68, 0x75, 0x61, 0x5B, 0x5C, 0xC4, 0x0E, 0xA0, 0xC4, 0x4A, 0xFC, 0x43, 0x87, 0x66,
    0xD5, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82
};

// Decodes g_desktopIconPng from memory into a GDI+ Bitmap. Caller owns the result.
static Bitmap* LoadEmbeddedDesktopIcon()
{
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, sizeof(g_desktopIconPng));
    if (!hMem) return nullptr;

    void* pMem = GlobalLock(hMem);
    if (!pMem) { GlobalFree(hMem); return nullptr; }
    memcpy(pMem, g_desktopIconPng, sizeof(g_desktopIconPng));
    GlobalUnlock(hMem);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(hMem, TRUE, &stream) != S_OK)
    {
        GlobalFree(hMem);
        return nullptr;
    }

    // CreateStreamOnHGlobal with fDeleteOnRelease=TRUE: stream owns hMem now.
    Bitmap* bmp = Bitmap::FromStream(stream);
    stream->Release();

    if (!bmp || bmp->GetLastStatus() != Ok)
    {
        delete bmp;
        return nullptr;
    }
    return bmp;
}

// -----------------------------------------------------------------------------
// PowerShell execution
// -----------------------------------------------------------------------------
static std::wstring Base64EncodeBytes(const BYTE* data, size_t len)
{
    static const wchar_t alphabet[] = L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::wstring out;
    out.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3)
    {
        DWORD block = ((DWORD)data[i]) << 16;
        bool have2 = i + 1 < len;
        bool have3 = i + 2 < len;
        if (have2) block |= ((DWORD)data[i + 1]) << 8;
        if (have3) block |= data[i + 2];

        out.push_back(alphabet[(block >> 18) & 0x3F]);
        out.push_back(alphabet[(block >> 12) & 0x3F]);
        out.push_back(have2 ? alphabet[(block >> 6) & 0x3F] : L'=');
        out.push_back(have3 ? alphabet[block & 0x3F] : L'=');
    }

    return out;
}

static std::wstring PowerShellEncodedCommand(const std::wstring& command)
{
    const BYTE* bytes = reinterpret_cast<const BYTE*>(command.c_str());
    return Base64EncodeBytes(bytes, command.size() * sizeof(wchar_t));
}

void PS_Clear()
{
    std::lock_guard<std::mutex> lk(g_psMutex);
    g_psErr=false; g_psOut.clear(); g_psMsg.clear(); g_psCode=0;
}

bool PS_Run(const std::wstring& cmd)
{
    LogText(g_ui.launchingPowerShellRegistration);
    Log(g_ui.powerShellCommand.c_str(), cmd.c_str());
    PS_Clear();

    SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
    HANDLE r=0,w=0;
    if (!CreatePipe(&r,&w,&sa,0))
    {
        LogWin32Failure(L"CreatePipe");
        return false;
    }
    SetHandleInformation(r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{}; si.cb=sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow=SW_HIDE; si.hStdOutput=w; si.hStdError=w;

    PROCESS_INFORMATION pi{};
    std::wstring cmdline = L"powershell.exe -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + PowerShellEncodedCommand(cmd);

    BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr,nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,nullptr, &si, &pi);

    CloseHandle(w);
    if (!ok)
    {
        LogWin32Failure(L"CreateProcessW");
        CloseHandle(r);
        return false;
    }

    char buf[2048]; DWORD n;
    std::wstring out;

    while (ReadFile(r,buf,2047,&n,nullptr) && n>0)
    {
        buf[n]=0;
        int need = MultiByteToWideChar(CP_UTF8,0,buf,n,nullptr,0);
        if (need>0){
            std::wstring tmp; tmp.resize(need);
            MultiByteToWideChar(CP_UTF8,0,buf,n,&tmp[0],need);
            out += tmp;
        }
    }
    CloseHandle(r);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD ec=0; GetExitCodeProcess(pi.hProcess,&ec);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    std::wstring psMsg;
    bool psErr = false;
    if (ec == 0) {
        psMsg = g_ui.powerShellCompleted;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = false;
            g_psMsg = psMsg;
        }
        LogText(g_ui.powerShellCompleted);
        return true;
    }

    psErr = true;

    if (out.find(L"0x80073CFF") != std::wstring::npos) {
        psMsg = g_ui.powerShellErrorSideloadDisabled;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        LogText(g_ui.powerShellErrorSideloadDisabled);
    } else {
        wchar_t msg[128];
        swprintf(msg, 128, g_ui.powerShellErrorCode.c_str(), ec);
        psMsg = msg;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        Log(g_ui.powerShellErrorCode.c_str(), ec);
    }

    LogText(g_ui.powerShellOutputFollows);
    Log(L"%ls", out.c_str());

    return false;
}

bool Appx_Register_COM(const std::wstring& manifestPath)
{
    try
    {
        LogText(g_ui.usingComRegistration);

        winrt::init_apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Foundation::Collections;

        PackageManager pm;

        std::wstring uriStr = MakeFileUri(manifestPath);
        if (uriStr.empty())
        {
            LogText(g_ui.invalidManifestPath);
            return false;
        }
        Uri uri{ winrt::hstring(uriStr) };

        IVector<Uri> deps = winrt::single_threaded_vector<Uri>();

        auto op = pm.RegisterPackageAsync(
            uri,
            deps,
            DeploymentOptions::ForceUpdateFromAnyVersion
        );

        op.get(); // wait

        if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            LogText(g_ui.comRegistrationSuccess);
            return true;
        }
        else
        {
            Log(g_ui.comRegistrationStatusFailed.c_str(), (int)op.Status());
            return false;
        }
    }
    catch (const winrt::hresult_error& e)
    {
        Log(g_ui.comRegistrationException.c_str(), e.code().value);
        Log(g_ui.comExceptionMessage.c_str(), e.message().c_str());
        return false;
    }
}

// -----------------------------------------------------------------------------
// Tile generation
// -----------------------------------------------------------------------------
struct Tile { const wchar_t* name; const wchar_t* file; int w,h; };
static const Tile g_tiles[] = {
    {L"StoreLogo",       L"Assets\\StoreLogo.png", 50,50},
    {L"MediumTile",      L"Assets\\MediumTile.png",150,150},
    {L"Square44x44Logo", L"Assets\\Square44x44Logo.png",44,44},
    {L"SmallTile",       L"Assets\\SmallTile.png",71,71},
    {L"WideTile",        L"Assets\\WideTile.png",310,150},
    {L"LargeTile",       L"Assets\\LargeTile.png",310,310},
};

bool Generate(const wchar_t* wp, const wchar_t* reason = nullptr)
{
    std::unique_lock<std::mutex> opLock(g_generationMutex, std::defer_lock);
    if (!opLock.try_lock())
    {
        LogText(g_ui.generationRequestBusy);
        if (g_notifyOnBusy)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationBusy, NIIF_WARNING);
        return false;
    }

    if (!wp || !*wp)
    {
        LogText(g_ui.emptyWallpaper);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.wallpaperNotFound, NIIF_WARNING);
        return false;
    }

    const wchar_t* why = (reason && *reason) ? reason : g_ui.unspecifiedReason.c_str();
    Log(g_ui.startingWallpaperGeneration.c_str(), why);
    Log(g_ui.wallpaperSource.c_str(), wp);

    if (g_notifyOnStart)
        NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationStarted, NIIF_INFO);

    Bitmap* src = new Bitmap(wp);
    if (src->GetLastStatus() != Ok)
    {
        LogText(g_ui.failedLoadWallpaper);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.failedLoadWallpaper, NIIF_ERROR);
        delete src;
        return false;
    }

    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();

    std::wstring exeDir = GetExeDir();
    std::wstring assetsDir = exeDir + L"\\Assets";
    CreateDirectoryW(assetsDir.c_str(), nullptr);

    bool assetsOk = true;
    std::vector<int> scales = GetConfiguredScales();
    {
        std::wstring scaleList;
        for (size_t i = 0; i < scales.size(); ++i)
        {
            if (i > 0) scaleList += L", ";
            scaleList += IntToWString(scales[i]) + L"%";
        }
        Log(g_ui.activeScales.c_str(), scaleList.c_str());
    }
    for (auto& t : g_tiles)
    {
        std::wstring outPath = exeDir + L"\\" + t.file;
        if (!IniReadI(L"Assets", t.name, 0))
        {
            if (!g_generateDesktopIconForDisabledEntries)
            {
                Log(g_ui.skippingAssetDisabled.c_str(), t.name);
                continue;
            }

            // Decode the embedded 30x30 icon and write it as-is to the base path,
            // then write the same 30x30 image to each scaled variant path.
            {
                Bitmap* icon = LoadEmbeddedDesktopIcon();
                if (!icon)
                {
                    assetsOk = false;
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                    continue;
                }

                bool ok = SavePNG(icon, outPath.c_str());
                if (ok)
                    Log(g_ui.generatedDesktopIconAsset.c_str(), outPath.c_str());
                else
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());

                for (int scale : scales)
                {
                    std::wstring scaledPath = ScaleAssetPath(outPath, scale);
                    bool scaledOk = SavePNG(icon, scaledPath.c_str());
                    if (scaledOk)
                        Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                    else
                        Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                    ok = ok && scaledOk;
                }

                delete icon;
                if (!ok) assetsOk = false;
            }
            continue;
        }

        auto* o = ResizeWithFit(src, t.w, t.h, mode);
        bool saved = false;
        if (o)
        {
            saved = SavePNG(o, outPath.c_str());
            if (saved)
                Log(g_ui.savedAsset.c_str(), outPath.c_str());
            else
                Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
            delete o;
        }

        for (int scale : scales)
        {
            int scaledW = ScalePixels(t.w, scale);
            int scaledH = ScalePixels(t.h, scale);
            auto* scaled = ResizeWithFit(src, scaledW, scaledH, mode);
            std::wstring scaledPath = ScaleAssetPath(outPath, scale);
            if (scaled)
            {
                bool scaledSaved = SavePNG(scaled, scaledPath.c_str());
                if (scaledSaved)
                    Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                else
                    Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                saved = scaledSaved && saved;
                delete scaled;
            }
            else
            {
                Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                saved = false;
            }
        }

        if (!saved)
            assetsOk = false;
    }
    delete src;

    LogText(g_ui.reRegisteringManifest);

    std::wstring manifestPath = exeDir + L"\\AppxManifest.xml";
    Log(g_ui.manifestPath.c_str(), manifestPath.c_str());

    bool registrationOk = false;

    if (!UsePowerShell())
    {
        registrationOk = Appx_Register_COM(manifestPath);

        if (!registrationOk)
        {
            LogText(g_ui.comRegistrationFailed);
            std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
            registrationOk = PS_Run(ps);
        }
    }
    else
    {
        std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
        registrationOk = PS_Run(ps);
    }

    bool ok = assetsOk && registrationOk;
    if (ok)
    {
        LogText(g_ui.assetGenerationFinished);
        if (g_notifyOnSuccess)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationSuccess, NIIF_INFO);
    }
    else
    {
        if (!assetsOk)
            LogText(g_ui.assetGenerationFailed);
        if (!registrationOk)
            LogText(g_ui.appRegistrationFailed);
        if (g_notifyOnFailure)
        {
            std::wstring failMsg;
            {
                std::lock_guard<std::mutex> lk(g_psMutex);
                failMsg = g_psMsg.empty() ? g_ui.notificationGenerationFailed : g_psMsg;
            }
            NotifyTrayBalloon(g_ui.notificationTitle, failMsg, NIIF_ERROR);
        }
    }

    return ok;
}

static void GenerationWorkerThread()
{
    for (;;)
    {
        std::wstring wallpaper;
        std::wstring reason;

        {
            std::unique_lock<std::mutex> lk(g_generationQueueMutex);
            g_generationQueueCv.wait(lk, [] { return g_generationWorkerStop || g_generationQueued; });

            if (!g_generationQueued && g_generationWorkerStop)
                break;

            wallpaper = std::move(g_generationQueuedWallpaper);
            reason = std::move(g_generationQueuedReason);
            g_generationQueued = false;
        }

        Generate(wallpaper.c_str(), reason.c_str());
    }
}

static void QueueGenerate(std::wstring wallpaper, std::wstring reason)
{
    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationWorkerStop)
            return;

        g_generationQueuedWallpaper = std::move(wallpaper);
        g_generationQueuedReason = std::move(reason);
        g_generationQueued = true;

        if (!g_generationWorkerStarted)
        {
            g_generationWorkerStarted = true;
            startWorker = true;
        }
    }

    if (startWorker)
        g_generationWorker = std::thread(GenerationWorkerThread);

    g_generationQueueCv.notify_one();
}

static void JoinQueuedGenerations()
{
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        g_generationWorkerStop = true;
    }

    g_generationQueueCv.notify_one();

    if (g_generationWorker.joinable())
        g_generationWorker.join();
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Poll thread
// -----------------------------------------------------------------------------
void PollThread()
{
    std::wstring last = GetWallpaper();
    FitMode lastFit = GetWallpaperFit();
    int lastDpiScale = CurrentAssetScale();
    auto lastGen = steady_clock::now() - seconds(5);

    while (g_running)
    {
        int poll = IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000);
        int confirm = IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000);
        int deb = IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000);

        std::wstring cur = GetWallpaper();
        FitMode curFit = GetWallpaperFit();
        int curDpiScale = CurrentAssetScale();

        bool wallpaperChanged = (cur != last);
        bool fitChanged = (!g_disableFitting && curFit != lastFit);
        bool dpiScaleChanged = (g_generateScaleAuto && curDpiScale != lastDpiScale);

        if ((g_listenWallpaper && wallpaperChanged) ||
            (g_listenFit && fitChanged) ||
            dpiScaleChanged)
        {
            std::this_thread::sleep_for(milliseconds(confirm));

            std::wstring cur2 = GetWallpaper();
            FitMode fit2 = GetWallpaperFit();
            int dpiScale2 = CurrentAssetScale();

            bool wallpaperChanged2 = (cur2 != last);
            bool fitChanged2 = (fit2 != lastFit);
            bool dpiScaleChanged2 = (g_generateScaleAuto && dpiScale2 != lastDpiScale);

            if ((g_listenWallpaper && wallpaperChanged2) ||
                (g_listenFit && fitChanged2) ||
                dpiScaleChanged2)
            {
                auto now = steady_clock::now();

                if (duration_cast<milliseconds>(now - lastGen).count() >= deb)
                {
                    const wchar_t* reason = g_ui.changeDetected.c_str();
                    if (dpiScaleChanged2 && !(g_listenWallpaper && wallpaperChanged2) && !(g_listenFit && fitChanged2))
                        reason = g_ui.dpiScaleChangeDetected.c_str();
                    else if (g_listenWallpaper && wallpaperChanged2 && g_listenFit && fitChanged2)
                        reason = g_ui.wallpaperAndFitChangeDetected.c_str();
                    else if (g_listenWallpaper && wallpaperChanged2)
                        reason = g_ui.wallpaperChangeDetected.c_str();
                    else if (g_listenFit && fitChanged2)
                        reason = g_ui.fitModeChangeDetected.c_str();

                    LogText(g_ui.changeConfirmed);

                    Generate(cur2.c_str(), reason);
                    last = cur2;
                    lastFit = fit2;
                    lastDpiScale = dpiScale2;
                    lastGen = steady_clock::now();
                }
            }
        }
        std::this_thread::sleep_for(milliseconds(poll));
    }
}

// -----------------------------------------------------------------------------
// Tray
// -----------------------------------------------------------------------------
enum {
    ID_EXIT=1001,

    // General
    ID_USE_PS,
    ID_TRAYICON,
    ID_CONSOLE,
    ID_LOG,
    ID_LOGFILE,
    ID_LOG_RENAME,
    ID_LOG_OPEN,
    ID_LOG_RESET,
    ID_GENERATE_NOW,
    ID_GENERATE_STARTUP,
    ID_HIDE_DISABLED,
    ID_GENERATE_DESKTOP_ICON_DISABLED,
    ID_SHOW_MENU_DROPDOWN,
    ID_NOTIF_ENABLED,
    ID_NOTIFY_START,
    ID_NOTIFY_SUCCESS,
    ID_NOTIFY_FAILURE,
    ID_NOTIFY_BUSY,
    ID_NOTIFY_ALREADY_RUNNING,
    ID_NOTIFY_SINGLE_INSTANCE_FAILURE,
    ID_SHOW_PSLOG,
    ID_LISTEN_WP,
    ID_LISTEN_FIT,
    ID_DISABLE_FIT,
    ID_SCALE_AUTO,
    ID_SCALE_100,
    ID_SCALE_125,
    ID_SCALE_150,
    ID_SCALE_200,
    ID_SCALE_400,

    // Assets
    ID_A1=2001, ID_A2, ID_A3, ID_A4, ID_A5, ID_A6
};

static bool EnsureTrayIcon(DWORD* err = nullptr)
{
    if (err) *err = ERROR_SUCCESS;

    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd)
    {
        g_tray = true;
        return true;
    }

    if (!g_hwnd)
    {
        if (err) *err = ERROR_INVALID_WINDOW_HANDLE;
        return false;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid);
    nid.hWnd=g_hwnd; nid.uID=1;
    nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
    nid.uCallbackMessage=WM_USER+1;
    HICON ic = LoadIconW(nullptr,IDI_APPLICATION);
    if (!ic)
    {
        if (err) *err = GetLastError();
        return false;
    }
    nid.hIcon=ic;
    CopyTruncated(nid.szTip, g_ui.trayTip);

    if (!Shell_NotifyIconW(NIM_ADD,&nid))
    {
        if (err) *err = GetLastError();
        return false;
    }

    g_nid = nid;
    g_tray = true;
    return true;
}

void TrayRemove()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd){
        Shell_NotifyIconW(NIM_DELETE,&g_nid);
        g_nid = {};
    }
}

static bool TrayIconPresent()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    return g_nid.hWnd != nullptr;
}

void ShowPSLog(HWND h)
{
    std::wstring psOut;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        psOut = g_psOut;
    }

    if (psOut.empty()){
        MessageBoxW(h, g_ui.noPowerShellOutput.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    const size_t CH=3000;
    for (size_t p=0;p<psOut.size();p+=CH){
        std::wstring s = psOut.substr(p,CH);
        MessageBoxW(h, s.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONERROR);
    }
}

INT_PTR CALLBACK RenameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    static std::wstring* pPath = nullptr;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        pPath = (std::wstring*)lParam;
        SetDlgItemTextW(hDlg, 1001, pPath->c_str()); // edit box
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            HWND edit = GetDlgItem(hDlg, 1001);
            int len = GetWindowTextLengthW(edit);
            std::vector<wchar_t> buf((size_t)len + 1);
            GetWindowTextW(edit, buf.data(), (int)buf.size());

            if (buf[0])
            {
                *pPath = buf.data();
                EndDialog(hDlg, IDOK);
            }
            else
            {
                MessageBoxW(hDlg, g_ui.pathCannotBeEmpty.c_str(), g_ui.errorTitle.c_str(), MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void Menu(HWND h)
{
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    bool showMenuAsDropdown = g_showMenuAsDropdown;

    auto addTo = [&](HMENU target, UINT id, const std::wstring& t, bool chk=false, bool en=true){
        if (g_hideDisabled && !en)
            return;
        AppendMenuW(target,
            MF_STRING |
            (chk ? MF_CHECKED : 0) |
            (!en ? MF_DISABLED : 0),
            id, t.c_str());
    };

    auto menuTitle = [](const std::wstring& text) {
        std::wstring title = TrimCopy(text);
        while (!title.empty() && (title.back() == L':' || iswspace(title.back())))
            title.pop_back();
        return title.empty() ? text : title;
    };

    auto beginSection = [&](const std::wstring& title) -> HMENU {
        if (showMenuAsDropdown)
            return CreatePopupMenu();
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, title.c_str());
        return m;
    };

    auto endSection = [&](HMENU target, const std::wstring& title) {
        if (showMenuAsDropdown)
        {
            std::wstring popupTitle = menuTitle(title);
            AppendMenuW(m, MF_POPUP, (UINT_PTR)target, popupTitle.c_str());
        }
        else
        {
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        }
    };

    addTo(m, ID_SHOW_MENU_DROPDOWN, g_ui.showMenuAsDropdown.c_str(), showMenuAsDropdown);
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);

    HMENU generalMenu = beginSection(g_ui.generalTitle);
    addTo(generalMenu, ID_LISTEN_WP, g_ui.listenWallpaper.c_str(), g_listenWallpaper);
    addTo(generalMenu, ID_TRAYICON, g_ui.trayIcon.c_str(), g_tray);
    addTo(generalMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
    addTo(generalMenu, ID_GENERATE_STARTUP, g_ui.generateOnStartup.c_str(), g_generateOnStartup);
    addTo(generalMenu, ID_HIDE_DISABLED, g_ui.hideDisabledEntries.c_str(), g_hideDisabled);
    addTo(generalMenu, ID_CONSOLE, g_ui.showConsole.c_str(), g_console);
    addTo(generalMenu, ID_GENERATE_NOW, g_ui.generateNow.c_str());
    endSection(generalMenu, g_ui.generalTitle);

    HMENU notificationsMenu = beginSection(g_ui.notificationsTitle);
    addTo(notificationsMenu, ID_NOTIF_ENABLED, g_ui.notificationsEnabled.c_str(), g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_START, g_ui.notifyOnStart.c_str(), g_notifyOnStart, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SUCCESS, g_ui.notifyOnSuccess.c_str(), g_notifyOnSuccess, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_FAILURE, g_ui.notifyOnFailure.c_str(), g_notifyOnFailure, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_BUSY, g_ui.notifyOnBusy.c_str(), g_notifyOnBusy, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_ALREADY_RUNNING, g_ui.notifyOnAlreadyRunning.c_str(), g_notifyOnAlreadyRunning, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SINGLE_INSTANCE_FAILURE, g_ui.notifyOnSingleInstanceFailure.c_str(), g_notifyOnSingleInstanceFailure, g_notificationsEnabled);
    endSection(notificationsMenu, g_ui.notificationsTitle);

    HMENU loggingMenu = beginSection(g_ui.loggingTitle);
    addTo(loggingMenu, ID_LOG, g_ui.loggingEnable.c_str(), g_logging);
    addTo(loggingMenu, ID_LOGFILE, g_ui.changePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RENAME, g_ui.renamePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_OPEN, g_ui.openInExplorer.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RESET, g_ui.resetDefault.c_str(), false, g_logging);
    addTo(loggingMenu, ID_SHOW_PSLOG, g_ui.showPowerShellLog.c_str());

    std::wstring pathLine = g_ui.logPathPrefix + GetLogPathCopy();
    HMENU hPath = CreatePopupMenu();
    AppendMenuW(hPath, MF_STRING | MF_DISABLED, 0, pathLine.c_str());
    AppendMenuW(loggingMenu, MF_POPUP, (UINT_PTR)hPath, g_ui.logPathTitle.c_str());

    std::wstring psLine = g_ui.psStatusPrefix;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        if (!g_psMsg.empty())
            psLine += g_psMsg;
        else if (g_psErr)
            psLine += g_ui.errorTitle.c_str();
        else
            psLine += g_ui.okText.c_str();
    }

    HMENU hPs = CreatePopupMenu();
    AppendMenuW(hPs, MF_STRING | MF_DISABLED, 0, psLine.c_str());
    AppendMenuW(loggingMenu, MF_POPUP, (UINT_PTR)hPs, g_ui.psStatusTitle.c_str());
    endSection(loggingMenu, g_ui.loggingTitle);

    HMENU fittingMenu = beginSection(g_ui.wallpaperFittingTitle);
    addTo(fittingMenu, ID_DISABLE_FIT, g_ui.disableFitting.c_str(), g_disableFitting);
    addTo(fittingMenu, ID_LISTEN_FIT, g_ui.listenFit.c_str(), g_listenFit, !g_disableFitting);
    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    std::wstring fitLine;
    if (g_disableFitting)
        fitLine = g_ui.fitModeForced;
    else {
        fitLine = g_ui.fitModePrefix;
        fitLine += FitModeToString(mode);
    }
    AppendMenuW(fittingMenu, MF_STRING | MF_DISABLED, 0, fitLine.c_str());
    endSection(fittingMenu, g_ui.wallpaperFittingTitle);

    HMENU assetsMenu = beginSection(g_ui.assetsTitle);
    addTo(assetsMenu, ID_GENERATE_DESKTOP_ICON_DISABLED, g_ui.generateDesktopIconForDisabledEntries.c_str(), g_generateDesktopIconForDisabledEntries);
    AppendMenuW(assetsMenu, MF_SEPARATOR, 0, nullptr);
    const wchar_t* keys[] = {
        L"StoreLogo",L"MediumTile",L"Square44x44Logo",
        L"SmallTile",L"WideTile",L"LargeTile"
    };
    for (int i=0;i<6;i++)
        addTo(assetsMenu, ID_A1+i, keys[i], IniReadI(L"Assets",keys[i],0)!=0);
    endSection(assetsMenu, g_ui.assetsTitle);

    HMENU scaleMenu = beginSection(g_ui.dpiScalesTitle);
    addTo(scaleMenu, ID_SCALE_AUTO, g_ui.generateScaleAuto.c_str(), g_generateScaleAuto);
    // Manual scale items are greyed out while Auto is active (they are ignored by GetConfiguredScales)
    bool manualScalesEnabled = !g_generateScaleAuto;
    addTo(scaleMenu, ID_SCALE_100, g_ui.generateScale100.c_str(), g_generateScale100, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_125, g_ui.generateScale125.c_str(), g_generateScale125, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_150, g_ui.generateScale150.c_str(), g_generateScale150, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_200, g_ui.generateScale200.c_str(), g_generateScale200, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_400, g_ui.generateScale400.c_str(), g_generateScale400, manualScalesEnabled);
    std::wstring currentDpiLine = FormatWide(g_ui.currentDpiScaleLabel.c_str(), CurrentDpiPercent(), CurrentAssetScale());
    AppendMenuW(scaleMenu, MF_STRING | MF_DISABLED, 0, currentDpiLine.c_str());
    endSection(scaleMenu, g_ui.dpiScalesTitle);

    HMENU advancedMenu = beginSection(g_ui.advancedTitle);
    std::wstring pollLine = FormatWide(g_ui.pollIntervalLabel.c_str(), IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000));
    std::wstring confirmLine = FormatWide(g_ui.confirmMsLabel.c_str(), IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000));
    std::wstring debounceLine = FormatWide(g_ui.debounceMinMsLabel.c_str(), IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000));
    std::wstring singleInstanceLine = FormatWide(g_ui.singleInstanceFailureActionLabel.c_str(), ErrorActionName(CurrentSingleInstanceFailureAction()));
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, pollLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, confirmLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, debounceLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, singleInstanceLine.c_str());
    endSection(advancedMenu, g_ui.advancedTitle);

    if (showMenuAsDropdown)
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    addTo(m, ID_EXIT, g_ui.exitText.c_str());

    SetForegroundWindow(h);
    UINT cmd = TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr);
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0");
        Log(g_ui.usePowerShellSummary.c_str(), v, v ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());
    }
    break;

    case ID_TRAYICON:
    {
        g_tray = !g_tray;

        if (!g_tray)
        {
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationTrayIconHidden, NIIF_WARNING, true);
            Sleep(750);
            TrayRemove();
        }
        else
        {
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
            {
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
                g_tray = false;
            }
        }

        Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
    }
    break;

    case ID_CONSOLE:
    {
        g_console = !g_console;

        if (g_console)
        {
            AllocConsole();
            FILE* f;
            freopen_s(&f,"CONOUT$","w",stdout);
            freopen_s(&f,"CONOUT$","w",stderr);
            freopen_s(&f,"CONIN$","r",stdin);
            ConfigureConsoleCP();

            std::lock_guard<std::mutex>lk(g_logMutex);
            for (auto& l:g_logBuf)
                fwprintf(stdout,L"%ls\n",l.c_str());
        }
        else
        {
            FreeConsole();
        }
        IniWrite(L"Settings", L"ShowConsole", g_console ? L"1" : L"0");
        Log(g_ui.consoleSummary.c_str(), StateOn(g_console));
    }
    break;

    case ID_LOG:
    {
        g_logging = !g_logging;
        IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0");
        Log(g_ui.loggingSummary.c_str(), StateOn(g_logging));
    }
    break;

    case ID_GENERATE_NOW:
    {
        std::wstring wp = GetWallpaper();

        if (wp.empty())
        {
            MessageBoxW(h,
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
        }
        else
        {
            LogText(g_ui.manualGenerationRequested);
            QueueGenerate(wp, g_ui.manualGenerateTriggered);
        }
    }
    break;

    case ID_GENERATE_STARTUP:
    {
        g_generateOnStartup = !g_generateOnStartup;
        IniWrite(L"Settings", L"GenerateOnStartup", g_generateOnStartup ? L"1" : L"0");
        Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    }
    break;

    case ID_HIDE_DISABLED:
    {
        g_hideDisabled = !g_hideDisabled;
        IniWrite(L"Settings", L"HideDisabledEntries", g_hideDisabled ? L"1" : L"0");
        Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    }
    break;

    case ID_GENERATE_DESKTOP_ICON_DISABLED:
    {
        g_generateDesktopIconForDisabledEntries = !g_generateDesktopIconForDisabledEntries;
        IniWrite(L"Settings", L"GenerateDesktopIconForDisabledEntries", g_generateDesktopIconForDisabledEntries ? L"1" : L"0");
        Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    }
    break;

    case ID_SCALE_AUTO:
    {
        g_generateScaleAuto = !g_generateScaleAuto;
        IniWrite(L"Settings", L"GenerateScaleAuto", g_generateScaleAuto ? L"1" : L"0");
        Log(g_ui.generateScaleAutoSummary.c_str(), StateEnabled(g_generateScaleAuto));
    }
    break;

    case ID_SCALE_100:
    case ID_SCALE_125:
    case ID_SCALE_150:
    case ID_SCALE_200:
    case ID_SCALE_400:
    {
        int scale = 100;
        std::atomic<bool>* setting = &g_generateScale100;
        const wchar_t* key = L"GenerateScale100";

        if (cmd == ID_SCALE_125) { scale = 125; setting = &g_generateScale125; key = L"GenerateScale125"; }
        else if (cmd == ID_SCALE_150) { scale = 150; setting = &g_generateScale150; key = L"GenerateScale150"; }
        else if (cmd == ID_SCALE_200) { scale = 200; setting = &g_generateScale200; key = L"GenerateScale200"; }
        else if (cmd == ID_SCALE_400) { scale = 400; setting = &g_generateScale400; key = L"GenerateScale400"; }

        bool value = !setting->load();
        *setting = value;
        IniWrite(L"Settings", key, value ? L"1" : L"0");
        Log(g_ui.generateScaleSummary.c_str(), scale, StateEnabled(value));
    }
    break;

    case ID_SHOW_MENU_DROPDOWN:
    {
        g_showMenuAsDropdown = !g_showMenuAsDropdown;
        IniWrite(L"Settings", L"ShowMenuAsDropdown", g_showMenuAsDropdown ? L"1" : L"0");
        Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    }
    break;

    case ID_NOTIF_ENABLED:
    {
        g_notificationsEnabled = !g_notificationsEnabled;
        IniWrite(L"Settings", L"NotificationsEnabled", g_notificationsEnabled ? L"1" : L"0");
        Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    }
    break;

    case ID_NOTIFY_START:
    {
        g_notifyOnStart = !g_notifyOnStart;
        IniWrite(L"Settings", L"NotifyOnStart", g_notifyOnStart ? L"1" : L"0");
        Log(g_ui.notifyOnStartState.c_str(), (int)g_notifyOnStart);
    }
    break;

    case ID_NOTIFY_SUCCESS:
    {
        g_notifyOnSuccess = !g_notifyOnSuccess;
        IniWrite(L"Settings", L"NotifyOnSuccess", g_notifyOnSuccess ? L"1" : L"0");
        Log(g_ui.notifyOnSuccessState.c_str(), (int)g_notifyOnSuccess);
    }
    break;

    case ID_NOTIFY_FAILURE:
    {
        g_notifyOnFailure = !g_notifyOnFailure;
        IniWrite(L"Settings", L"NotifyOnFailure", g_notifyOnFailure ? L"1" : L"0");
        Log(g_ui.notifyOnFailureState.c_str(), (int)g_notifyOnFailure);
    }
    break;

    case ID_NOTIFY_BUSY:
    {
        g_notifyOnBusy = !g_notifyOnBusy;
        IniWrite(L"Settings", L"NotifyOnBusy", g_notifyOnBusy ? L"1" : L"0");
        Log(g_ui.notifyOnBusyState.c_str(), (int)g_notifyOnBusy);
    }
    break;

    case ID_NOTIFY_ALREADY_RUNNING:
    {
        g_notifyOnAlreadyRunning = !g_notifyOnAlreadyRunning;
        IniWrite(L"Settings", L"NotifyOnAlreadyRunning", g_notifyOnAlreadyRunning ? L"1" : L"0");
        Log(g_ui.notifyOnAlreadyRunningState.c_str(), (int)g_notifyOnAlreadyRunning);
    }
    break;

    case ID_NOTIFY_SINGLE_INSTANCE_FAILURE:
    {
        g_notifyOnSingleInstanceFailure = !g_notifyOnSingleInstanceFailure;
        IniWrite(L"Settings", L"NotifyOnSingleInstanceFailure", g_notifyOnSingleInstanceFailure ? L"1" : L"0");
        Log(g_ui.notifyOnSingleInstanceFailureState.c_str(), (int)g_notifyOnSingleInstanceFailure);
    }
    break;

    case ID_SHOW_PSLOG:
        ShowPSLog(h);
        break;

    case ID_EXIT:
        g_running = false;
        TrayRemove();
        PostQuitMessage(0);
        break;

    case ID_LISTEN_WP:
    {
        g_listenWallpaper = !g_listenWallpaper;
        IniWrite(L"Settings", L"ListenWallpaper", g_listenWallpaper ? L"1" : L"0");
        Log(g_ui.listenWallpaperState.c_str(), (int)g_listenWallpaper);
    }
    break;

    case ID_LISTEN_FIT:
    {
        g_listenFit = !g_listenFit;
        IniWrite(L"Settings", L"ListenFit", g_listenFit ? L"1" : L"0");
        Log(g_ui.listenFitState.c_str(), (int)g_listenFit);
    }
    break;

    case ID_DISABLE_FIT:
    {
        g_disableFitting = !g_disableFitting;
        IniWrite(L"Settings", L"DisableFitting", g_disableFitting ? L"1" : L"0");
        if (g_disableFitting)
        {
            // Turn off listener to save CPU
            if (g_listenFit)
            {
                g_listenFit = false;
                IniWrite(L"Settings", L"ListenFit", L"0");
                LogText(g_ui.listenFitAutoDisabled);
            }
        }
        Log(g_ui.disableFittingState.c_str(), (int)g_disableFitting);
    }
    break;

    case ID_LOGFILE:
    {
        std::vector<wchar_t> file(32768);
        wcsncpy_s(file.data(), file.size(), GetLogPathCopy().c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = h;
        std::wstring filter = g_ui.logFileFilter;
        std::replace(filter.begin(), filter.end(), L'|', L'\0');
        filter.push_back(L'\0');
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = file.data();
        ofn.nMaxFile = (DWORD)file.size();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"log";
        if (GetSaveFileNameW(&ofn))
        {
            std::wstring newLogPath = file.data();
            SetLogPathCopy(newLogPath);
            IniWrite(L"Settings", L"LogPath", newLogPath.c_str());

            Log(g_ui.logPathChanged.c_str(), newLogPath.c_str());
            LogText(g_ui.futureLogEntriesNewPath);
        }
    }
    break;

    case ID_LOG_RENAME:
    {
        std::wstring newPath = GetLogPathCopy();
        if (ShowRenameDialog(h, newPath) == IDOK)
        {
            SetLogPathCopy(newPath);
            IniWrite(L"Settings", L"LogPath", newPath.c_str());
            Log(g_ui.logPathRenamed.c_str(), newPath.c_str());
            LogText(g_ui.futureLogEntriesRenamedPath);
        }
    }
    break;

    case ID_LOG_OPEN:
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
        {
            std::wstring param = L"/select,\"" + logPath + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOW);
        }
    }
    break;

    case ID_LOG_RESET:
    {
        std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/") + 1);
        base = base.substr(0, base.find_last_of(L'.'));
        std::wstring dir = g_exePath.substr(0, g_exePath.find_last_of(L"\\/"));
        std::wstring newLogPath = dir + L"\\" + base + L".log";
        SetLogPathCopy(newLogPath);
        IniWrite(L"Settings", L"LogPath", newLogPath.c_str());
        Log(g_ui.logPathReset.c_str(), newLogPath.c_str());
        LogText(g_ui.futureLogEntriesDefaultPath);
    }
    break;

    default:
        if (cmd>=ID_A1 && cmd<=ID_A6){
            int i = cmd-ID_A1;
            const wchar_t* k = keys[i];
            int nv = !IniReadI(L"Assets",k,0);
            IniWrite(L"Assets",k,nv?L"1":L"0");
            Log(g_ui.assetToggleState.c_str(), k, StateOn(nv));
        }
        break;
    }
}

static void HandleExistingInstanceRequest()
{
    DWORD err = 0;
    if (!EnsureTrayIcon(&err))
    {
        LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
        if (g_notificationsEnabled && g_notifyOnAlreadyRunning)
            MessageBoxW(nullptr, g_ui.notificationAlreadyRunning.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!g_notificationsEnabled || !g_notifyOnAlreadyRunning)
        return;

    bool trayPersistsAfterRestart = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    const std::wstring& message = trayPersistsAfterRestart
        ? g_ui.notificationAlreadyRunningTrayPersistent
        : g_ui.notificationAlreadyRunningTraySession;
    NotifyTrayBalloon(g_ui.notificationTitle, message, trayPersistsAfterRestart ? NIIF_INFO : NIIF_WARNING, true);
}

static int HandleSingleInstanceMutexFailure(DWORD err)
{
    ErrorAction action = CurrentSingleInstanceFailureAction();
    if (action == ErrorAction::Ignore)
        return 0;

    std::wstring errText = Win32ErrorString(err);
    const wchar_t* actionName = ErrorActionName(action);
    Log(g_ui.singleInstanceMutexFailure.c_str(), err, errText.c_str(), actionName);

    if (g_notificationsEnabled && g_notifyOnSingleInstanceFailure)
    {
        if (action == ErrorAction::Warn && TrayIconPresent())
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationSingleInstanceFailure, NIIF_WARNING, true);
        else
            MessageBoxW(g_hwnd, g_ui.notificationSingleInstanceFailure.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONWARNING);
    }

    if (action == ErrorAction::Exit)
        return 1;

    if (action == ErrorAction::Crash)
        RaiseFailFastException(nullptr, nullptr, 0);

    return 0;
}

// -----------------------------------------------------------------------------
// Window proc
// -----------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if (g_singleInstanceMessage && m == g_singleInstanceMessage)
    {
        HandleExistingInstanceRequest();
        return 0;
    }

    if (m==WM_USER+1){
        if (l==WM_RBUTTONUP || l==WM_CONTEXTMENU){
            Menu(h);
            return 0;
        }
    }
    else if (m==WM_DESTROY){
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
static HWND FindExistingInstanceWindow()
{
    for (int i = 0; i < 50; ++i)
    {
        HWND existing = FindWindowExW(HWND_MESSAGE, nullptr, WINDOW_CLASS_NAME, nullptr);
        if (existing)
            return existing;
        Sleep(100);
    }
    return nullptr;
}

static bool SignalExistingInstance()
{
    if (!g_singleInstanceMessage)
        return false;

    HWND existing = FindExistingInstanceWindow();
    if (!existing)
        return false;

    return PostMessageW(existing, g_singleInstanceMessage, 0, 0) != FALSE;
}

static std::wstring StartupString(const wchar_t* key, const wchar_t* fallback)
{
    std::wstring value = IniReadS(L"Strings", key, fallback);
    return value.empty() ? std::wstring(fallback) : value;
}

int WINAPI wWinMain(HINSTANCE hi,HINSTANCE, PWSTR, int)
{
    ConfigureDpiAwareness();

    g_exePath = GetModulePath();
    if (g_exePath.empty())
        return 1;

    std::wstring dir = GetExeDir();
    std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/")+1);
    base = base.substr(0, base.find_last_of(L'.'));
    g_iniPath = dir + L"\\" + base + L".ini";

    g_singleInstanceMessage = RegisterWindowMessageW(SINGLE_INSTANCE_MESSAGE_NAME);
    SetLastError(ERROR_SUCCESS);
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, SINGLE_INSTANCE_MUTEX_NAME);
    DWORD singleInstanceMutexLastError = GetLastError();
    DWORD singleInstanceMutexFailure = ERROR_SUCCESS;
    if (!g_singleInstanceMutex)
    {
        singleInstanceMutexFailure = singleInstanceMutexLastError;
    }
    else if (singleInstanceMutexLastError == ERROR_ALREADY_EXISTS)
    {
        if (!SignalExistingInstance())
        {
            if (IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0 &&
                IniReadI(L"Settings", L"NotifyOnAlreadyRunning", 1) != 0)
            {
                std::wstring title = StartupString(L"NotificationTitle", L"Desktop Tile Generator");
                std::wstring message = StartupString(L"NotificationAlreadyRunning", L"Desktop Tile Generator is already running.");
                MessageBoxW(nullptr,
                    message.c_str(),
                    title.c_str(),
                    MB_OK | MB_ICONINFORMATION);
            }
        }
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 0;
    }

    bool iniEncodingNormalized = NormalizeIniToUtf8BomIfNeeded();

    // defaults
    EnsureIniDefaults();
    EnsureIniStringDefaults();
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    g_logging = IniReadI(L"Settings",L"Logging",1)!=0;
    g_tray    = IniReadI(L"Settings",L"TrayIcon",1)!=0;
    std::wstring configuredLogPath = IniReadS(L"Settings", L"LogPath", L"");
    if (configuredLogPath.empty())
    {
        configuredLogPath = dir + L"\\" + base + L".log";
        IniWrite(L"Settings", L"LogPath", configuredLogPath.c_str());
    }
    SetLogPathCopy(configuredLogPath);
    g_console = IniReadI(L"Settings", L"ShowConsole", 0) != 0;
    g_generateOnStartup = IniReadI(L"Settings", L"GenerateOnStartup", 1) != 0;
    g_hideDisabled = IniReadI(L"Settings", L"HideDisabledEntries", 0) != 0;
    g_generateDesktopIconForDisabledEntries = IniReadI(L"Settings", L"GenerateDesktopIconForDisabledEntries", 1) != 0;
    g_generateScaleAuto = IniReadI(L"Settings", L"GenerateScaleAuto", 1) != 0;
    g_generateScale100 = IniReadI(L"Settings", L"GenerateScale100", 1) != 0;
    g_generateScale125 = IniReadI(L"Settings", L"GenerateScale125", 1) != 0;
    g_generateScale150 = IniReadI(L"Settings", L"GenerateScale150", 1) != 0;
    g_generateScale200 = IniReadI(L"Settings", L"GenerateScale200", 0) != 0;
    g_generateScale400 = IniReadI(L"Settings", L"GenerateScale400", 0) != 0;
    g_showMenuAsDropdown = IniReadI(L"Settings", L"ShowMenuAsDropdown", 1) != 0;
    g_notificationsEnabled = IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0;
    g_notifyOnStart = IniReadI(L"Settings", L"NotifyOnStart", 0) != 0;
    g_notifyOnSuccess = IniReadI(L"Settings", L"NotifyOnSuccess", 0) != 0;
    g_notifyOnFailure = IniReadI(L"Settings", L"NotifyOnFailure", 1) != 0;
    g_notifyOnBusy = IniReadI(L"Settings", L"NotifyOnBusy", 0) != 0;
    g_notifyOnAlreadyRunning = IniReadI(L"Settings", L"NotifyOnAlreadyRunning", 1) != 0;
    g_notifyOnSingleInstanceFailure = IniReadI(L"Settings", L"NotifyOnSingleInstanceFailure", 1) != 0;
    g_listenWallpaper = IniReadI(L"Settings", L"ListenWallpaper", 1) != 0;
    g_listenFit       = IniReadI(L"Settings", L"ListenFit", 1) != 0;
    g_disableFitting = IniReadI(L"Settings", L"DisableFitting", 0) != 0;

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr, g_ui.gdiPlusStartupFailedMessage.c_str(), g_ui.gdiPlusStartupFailedTitle.c_str(), 0); return 1; }

    LogText(g_ui.programStarting);
    Log(g_ui.exeLabel.c_str(), g_exePath.c_str());
    Log(g_ui.iniLabel.c_str(), g_iniPath.c_str());
    Log(g_ui.logFileLabel.c_str(), GetLogPathCopy().c_str());
    if (iniEncodingNormalized)
        LogText(g_ui.iniEncodingNormalized);
    Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    Log(g_ui.generateScaleAutoSummary.c_str(), StateEnabled(g_generateScaleAuto));
    Log(g_ui.generateScaleSummary.c_str(), 100, StateEnabled(g_generateScale100));
    Log(g_ui.generateScaleSummary.c_str(), 125, StateEnabled(g_generateScale125));
    Log(g_ui.generateScaleSummary.c_str(), 150, StateEnabled(g_generateScale150));
    Log(g_ui.generateScaleSummary.c_str(), 200, StateEnabled(g_generateScale200));
    Log(g_ui.generateScaleSummary.c_str(), 400, StateEnabled(g_generateScale400));
    Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
    Log(g_ui.powerShellRegistrationSummary.c_str(), UsePowerShell() ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());

    // Console setting
    if (g_console)
    {
        if (!AllocConsole())
            LogWin32Failure(L"AllocConsole");
        else
            LogText(g_ui.consoleAllocated);

        FILE* f;
        freopen_s(&f,"CONOUT$","w",stdout);
        freopen_s(&f,"CONOUT$","w",stderr);
        freopen_s(&f,"CONIN$","r",stdin);

        ConfigureConsoleCP();

        std::lock_guard<std::mutex> lk(g_logMutex);
        for (auto& l : g_logBuf)
            WriteConsoleLine(l);
    }

    // Window
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=WINDOW_CLASS_NAME;
    if (!RegisterClassW(&wc))
    {
        LogWin32Failure(L"RegisterClassW");
        GdiplusShutdown(tk);
        return 1;
    }
    g_hwnd = CreateWindowExW(0,WINDOW_CLASS_NAME,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,hi,nullptr);
    if (!g_hwnd)
    {
        LogWin32Failure(L"CreateWindowExW");
        GdiplusShutdown(tk);
        return 1;
    }

    // Tray
    if (g_tray){
        DWORD err = 0;
        if (!EnsureTrayIcon(&err))
        {
            LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
            g_tray = false;
        }
    }

    if (singleInstanceMutexFailure != ERROR_SUCCESS)
    {
        int exitCode = HandleSingleInstanceMutexFailure(singleInstanceMutexFailure);
        if (exitCode != 0)
        {
            TrayRemove();
            GdiplusShutdown(tk);
            return exitCode;
        }
    }

    if (g_generateOnStartup)
    {
        std::wstring wp = GetWallpaper();
        if (!wp.empty())
        {
            LogText(g_ui.startupGenerationEnabled);
            QueueGenerate(wp, g_ui.startupGenerationEnabled);
        }
        else
        {
            LogText(g_ui.startupGenerationSkippedNoWallpaper);
        }
    }

    std::thread th(PollThread);

    MSG msg;
    while (GetMessageW(&msg,nullptr,0,0))
    { TranslateMessage(&msg); DispatchMessageW(&msg); }

    g_running=false;
    if (th.joinable()) th.join();
    JoinQueuedGenerations();

    TrayRemove();
    GdiplusShutdown(tk);
    if (g_singleInstanceMutex)
    {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
    return 0;
}

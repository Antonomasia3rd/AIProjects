// compile command: cl /std:c++17 /EHsc /DUNICODE /D_UNICODE GenerateAssets.cpp /link gdiplus.lib gdi32.lib user32.lib shlwapi.lib shell32.lib ole32.lib comdlg32.lib advapi32.lib windowsapp.lib /SUBSYSTEM:WINDOWS
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
#include <chrono>
#include <climits>
#include <cerrno>
#include <cstdlib>
#include <algorithm>
#include <initializer_list>
#include <utility>
#include <cstdint>
#include <commdlg.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Management.Deployment.h>

INT_PTR CALLBACK RenameDlgProc(HWND, UINT, WPARAM, LPARAM);
static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType);

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
static constexpr const wchar_t* SINGLE_INSTANCE_MUTEX_BASE = L"Local\\DesktopTileGenerator.GenerateAssets";
static constexpr const wchar_t* SINGLE_INSTANCE_MESSAGE_BASE = L"DesktopTileGenerator.RestoreRunningInstance";
static std::wstring g_singleInstanceMutexName;
static std::wstring g_singleInstanceMessageName;
static std::wstring g_instanceWindowTitle;
static UINT g_singleInstanceMessage = 0;
static UINT g_taskbarCreatedMessage = 0;
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

static std::wstring MakeTempSiblingPath(const std::wstring& path, const wchar_t* tag)
{
    wchar_t suffix[96];
    swprintf(suffix, _countof(suffix), L".tmp.%lu.%llu.%s",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()),
        tag ? tag : L"file");
    return path + suffix;
}

static bool WriteWholeFileBytesDirect(const std::wstring& path, const std::vector<BYTE>& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    const BYTE* cursor = bytes.empty() ? nullptr : bytes.data();
    size_t remaining = bytes.size();
    bool ok = true;
    while (remaining > 0)
    {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1024 * 1024));
        DWORD wrote = 0;
        if (!WriteFile(h, cursor, chunk, &wrote, nullptr) || wrote != chunk)
        {
            ok = false;
            break;
        }
        cursor += wrote;
        remaining -= wrote;
    }

    if (ok)
        ok = FlushFileBuffers(h) != FALSE;
    CloseHandle(h);
    return ok;
}

static bool WriteWholeFileBytes(const std::wstring& path, const std::vector<BYTE>& bytes)
{
    std::wstring tempPath = MakeTempSiblingPath(path, L"text");
    DeleteFileW(tempPath.c_str());

    if (!WriteWholeFileBytesDirect(tempPath, bytes))
    {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        SetLastError(err);
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        SetLastError(err);
        return false;
    }

    return true;
}

static bool EnsureDirectoryExists(const std::wstring& path, DWORD* error = nullptr)
{
    if (error)
        *error = ERROR_SUCCESS;

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return true;

        if (error)
            *error = ERROR_ALREADY_EXISTS;
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    if (CreateDirectoryW(path.c_str(), nullptr))
        return true;

    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS)
    {
        attrs = GetFileAttributesW(path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
            return true;
    }

    if (error)
        *error = err == ERROR_SUCCESS ? ERROR_CANNOT_MAKE : err;
    SetLastError(error ? *error : err);
    return false;
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

    if (DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, MB_ERR_INVALID_CHARS))
        return true;

    // Some users edit INI files with the system ANSI code page. Decode that
    // instead of treating the file as unreadable and rebuilding it from empty.
    return DecodeCodePageText(CP_ACP, bytes.data(), bytes.size(), out, 0);
}

static std::wstring DecodeProcessOutput(const std::vector<BYTE>& bytes)
{
    std::wstring out;
    if (DecodeTextBytes(bytes, out))
        return out;

    if (DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, 0))
        return out;

    if (DecodeCodePageText(CP_ACP, bytes.data(), bytes.size(), out, 0))
        return out;

    return L"";
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
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    auto fail = [&](DWORD err) -> bool
    {
        CloseHandle(h);
        SetLastError(err == ERROR_SUCCESS ? ERROR_WRITE_FAULT : err);
        return false;
    };

    auto writeAll = [&](const std::vector<BYTE>& bytes) -> bool
    {
        const BYTE* cursor = bytes.empty() ? nullptr : bytes.data();
        size_t remaining = bytes.size();
        while (remaining > 0)
        {
            DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1024 * 1024));
            DWORD wrote = 0;
            if (!WriteFile(h, cursor, chunk, &wrote, nullptr) || wrote != chunk)
                return false;
            cursor += wrote;
            remaining -= wrote;
        }
        return true;
    };

    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz))
        return fail(GetLastError());
    if (sz.QuadPart == 0)
    {
        std::vector<BYTE> bom = EncodeUtf8Text(L"", true);
        if (bom.empty())
            return fail(ERROR_NO_UNICODE_TRANSLATION);
        if (!writeAll(bom))
            return fail(GetLastError());
    }
    else if (sz.QuadPart < 0)
    {
        return fail(ERROR_INVALID_DATA);
    }

    LARGE_INTEGER end{};
    if (!SetFilePointerEx(h, end, nullptr, FILE_END))
        return fail(GetLastError());

    std::vector<BYTE> bytes = EncodeUtf8Text(text, false);
    if (bytes.empty() && !text.empty())
        return fail(ERROR_NO_UNICODE_TRANSLATION);
    if (!writeAll(bytes))
        return fail(GetLastError());

    CloseHandle(h);
    return true;
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

static void TrimTrailingEmptyLines(std::vector<std::wstring>& lines)
{
    while (!lines.empty() && TrimCopy(lines.back()).empty())
        lines.pop_back();
}

static bool ReadIniFileAuto(std::wstring& text);
static bool WriteIniValueToText(std::wstring& text, const wchar_t* s, const wchar_t* k, const wchar_t* v);

static void StripLeadingBom(std::wstring& s)
{
    if (!s.empty() && s.front() == 0xFEFF)
        s.erase(s.begin());
}

static bool IsIniInlineCommentStart(const std::wstring& value, size_t pos, bool requireLeadingWhitespace)
{
    wchar_t ch = value[pos];
    if (ch != L';' && ch != L'#')
        return false;

    if (!requireLeadingWhitespace)
        return true;

    // Preserve values like URLs/fragments/paths that contain # or ; as data.
    return pos == 0 || iswspace(value[pos - 1]) != 0;
}

static std::wstring StripIniInlineComment(std::wstring value, bool requireLeadingWhitespace = true)
{
    bool inQuote = false;
    wchar_t quoteChar = 0;

    for (size_t i = 0; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        bool escaped = i > 0 && value[i - 1] == L'\\';

        if (!escaped && (ch == L'"' || ch == L'\''))
        {
            if (!inQuote)
            {
                inQuote = true;
                quoteChar = ch;
            }
            else if (quoteChar == ch)
            {
                inQuote = false;
                quoteChar = 0;
            }
            continue;
        }

        if (!inQuote && IsIniInlineCommentStart(value, i, requireLeadingWhitespace))
        {
            value.erase(i);
            break;
        }
    }

    while (!value.empty() && iswspace(value.back()) != 0)
        value.pop_back();
    return value;
}

static std::wstring UnquoteIniString(const std::wstring& text)
{
    std::wstring s = TrimCopy(text);
    if (s.size() < 2)
        return s;

    wchar_t quote = s.front();
    if ((quote != L'"' && quote != L'\'') || s.back() != quote)
        return s;

    std::wstring out;
    out.reserve(s.size() - 2);
    for (size_t i = 1; i + 1 < s.size(); ++i)
    {
        if (s[i] == L'\\' && i + 2 < s.size() && s[i + 1] == quote)
        {
            out.push_back(quote);
            ++i;
        }
        else
        {
            out.push_back(s[i]);
        }
    }
    return out;
}

static bool HasLaterQuote(const std::wstring& text, size_t start, wchar_t quote)
{
    for (size_t i = start; i < text.size(); ++i)
    {
        if (text[i] == quote)
            return true;
    }
    return false;
}

static std::wstring ParseIniName(std::wstring text)
{
    return UnquoteIniString(text);
}

static bool TryParseIniSectionHeader(std::wstring line, std::wstring& name)
{
    StripLeadingBom(line);
    std::wstring s = TrimCopy(line);
    if (s.empty() || s.front() == L';' || s.front() == L'#' || s.front() != L'[')
        return false;

    bool inQuote = false;
    wchar_t quoteChar = 0;
    for (size_t i = 1; i < s.size(); ++i)
    {
        wchar_t ch = s[i];
        bool escaped = i > 0 && s[i - 1] == L'\\';
        if (!escaped && (ch == L'"' || ch == L'\''))
        {
            if (!inQuote)
            {
                inQuote = true;
                quoteChar = ch;
            }
            else if (quoteChar == ch)
            {
                inQuote = false;
                quoteChar = 0;
            }
            continue;
        }

        if (!inQuote && ch == L']')
        {
            std::wstring rest = TrimCopy(s.substr(i + 1));
            if (!rest.empty() && rest.front() != L';' && rest.front() != L'#')
                return false;

            name = ParseIniName(s.substr(1, i - 1));
            return true;
        }
    }

    return false;
}

static std::wstring ParseIniValue(std::wstring text)
{
    std::wstring s = TrimCopy(text);
    if (s.empty())
        return s;

    wchar_t quote = s.front();
    if (quote != L'"' && quote != L'\'')
        return UnquoteIniString(StripIniInlineComment(std::move(text), false));

    std::wstring out;
    for (size_t i = 1; i < s.size(); ++i)
    {
        wchar_t ch = s[i];
        if (ch == L'\\' && i + 1 < s.size() && (s[i + 1] == quote || s[i + 1] == L'\\'))
        {
            // Treat \" as escaped only when another quote remains to close the
            // value. This keeps Windows paths ending in backslash parseable:
            // "C:\Temp\" ; comment
            if (s[i + 1] == quote && !HasLaterQuote(s, i + 2, quote))
            {
                out.push_back(ch);
                continue;
            }
            out.push_back(s[i + 1]);
            ++i;
            continue;
        }

        if (ch == quote)
            return out;

        out.push_back(ch);
    }

    return UnquoteIniString(StripIniInlineComment(std::move(text)));
}

static std::wstring QuoteIniString(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + 2);
    out.push_back(L'"');
    for (wchar_t ch : text)
    {
        if (ch == L'"' || ch == L'\\')
            out.push_back(L'\\');
        out.push_back(ch);
    }
    out.push_back(L'"');
    return out;
}

static std::wstring FormatIniAssignment(const std::wstring& key, const std::wstring& value)
{
    return QuoteIniString(key) + L" = " + QuoteIniString(value);
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

static std::mutex g_iniWriteMutex;
static std::mutex g_iniCacheMutex;
static bool g_iniCacheLoaded = false;
static FILETIME g_iniCacheWriteTime{};
static std::wstring g_iniCacheText;
static std::vector<IniSectionData> g_iniCacheDoc;
static std::atomic<int> g_iniCacheRefreshMs(250);
static steady_clock::time_point g_iniCacheLastCheck{};

static bool FileTimeEquals(const FILETIME& a, const FILETIME& b)
{
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

static bool GetFileWriteTime(const std::wstring& path, FILETIME& out)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    out = data.ftLastWriteTime;
    return true;
}

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

        std::wstring sectionHeaderName;
        if (TryParseIniSectionHeader(line, sectionHeaderName))
        {
            if (IniSectionData* existing = findSection(sectionHeaderName))
            {
                current = existing;
            }
            else
            {
                doc.push_back(IniSectionData{ sectionHeaderName, {} });
                current = &doc.back();
            }
            continue;
        }

        if (!current)
            continue;

        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = ParseIniName(line.substr(0, eq));
        std::wstring value = ParseIniValue(line.substr(eq + 1));

        if (IniEntry* existing = findKey(*current, key))
            existing->value = value;
        else
            current->entries.push_back(IniEntry{ key, value });
    }

    return doc;
}

static bool ReadIniValueFromDoc(const std::vector<IniSectionData>& doc, const wchar_t* s, const wchar_t* k, std::wstring& out)
{
    std::wstring sectionName = s ? s : L"";
    std::wstring keyName = k ? k : L"";

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

static void SetIniCacheUnlocked(const std::wstring& text)
{
    g_iniCacheText = text;
    g_iniCacheDoc = ParseIniDocument(text);
    g_iniCacheLoaded = true;
    g_iniCacheLastCheck = steady_clock::now();
    FILETIME ft{};
    if (GetFileWriteTime(g_iniPath, ft))
        g_iniCacheWriteTime = ft;
    else
        g_iniCacheWriteTime = {};
}

static bool ReadIniFileAuto(std::wstring& text)
{
    std::lock_guard<std::mutex> lk(g_iniCacheMutex);

    FILETIME ft{};
    bool haveTime = GetFileWriteTime(g_iniPath, ft);
    if (g_iniCacheLoaded && haveTime && FileTimeEquals(ft, g_iniCacheWriteTime))
    {
        text = g_iniCacheText;
        return true;
    }

    std::wstring fresh;
    if (!ReadTextFile(g_iniPath, fresh))
        return false;

    SetIniCacheUnlocked(fresh);
    text = g_iniCacheText;
    return true;
}

static bool IsMissingFileError(DWORD err)
{
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME;
}

static bool ReadIniFileForMutation(std::wstring& text)
{
    if (ReadIniFileAuto(text))
        return true;

    DWORD attrs = GetFileAttributesW(g_iniPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        if (IsMissingFileError(err))
        {
            text.clear();
            SetLastError(ERROR_SUCCESS);
            return true;
        }
        SetLastError(err);
        return false;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        SetLastError(ERROR_ACCESS_DENIED);
        return false;
    }

    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(g_iniPath, bytes))
        return false;

    if (bytes.empty())
    {
        text.clear();
        SetLastError(ERROR_SUCCESS);
        return true;
    }

    if (!DecodeTextBytes(bytes, text))
    {
        SetLastError(ERROR_INVALID_DATA);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(text);
    }
    return true;
}

static bool ReadIniValueCached(const wchar_t* s, const wchar_t* k, std::wstring& out)
{
    std::lock_guard<std::mutex> lk(g_iniCacheMutex);

    int refreshMs = g_iniCacheRefreshMs.load();
    if (refreshMs < 0) refreshMs = 0;
    if (refreshMs > 60000) refreshMs = 60000;

    auto now = steady_clock::now();
    bool cacheFreshEnough = g_iniCacheLoaded && refreshMs > 0 &&
        duration_cast<milliseconds>(now - g_iniCacheLastCheck).count() < refreshMs;

    if (!cacheFreshEnough)
    {
        FILETIME ft{};
        bool haveTime = GetFileWriteTime(g_iniPath, ft);
        g_iniCacheLastCheck = now;
        if (!g_iniCacheLoaded || !haveTime || !FileTimeEquals(ft, g_iniCacheWriteTime))
        {
            std::wstring fresh;
            if (!ReadTextFile(g_iniPath, fresh))
                return false;
            SetIniCacheUnlocked(fresh);
        }
    }

    return ReadIniValueFromDoc(g_iniCacheDoc, s, k, out);
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

        std::wstring sectionHeaderName;
        if (TryParseIniSectionHeader(lines[i], sectionHeaderName))
        {
            if (inTargetSection && insertPos == lines.size())
                insertPos = i;

            inTargetSection = IEquals(sectionHeaderName, sectionName);
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

        std::wstring key = ParseIniName(lines[i].substr(0, eq));
        if (IEquals(key, keyName))
        {
            lines[i] = FormatIniAssignment(keyName, value);
            updated = true;
        }
    }

    if (!updated)
    {
        std::wstring newEntry = FormatIniAssignment(keyName, value);
        if (sawTargetSection)
        {
            if (insertPos == lines.size())
            {
                // Strip any trailing blank lines that result from the file's
                // terminating \r\n being parsed as an empty element by
                // SplitLines. Without this, each sequential IniWrite call
                // inserts the new key *after* that empty element, producing
                // a blank line between every consecutive key written.
                TrimTrailingEmptyLines(lines);
                lines.push_back(newEntry);
            }
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

    TrimTrailingEmptyLines(lines);
    text = JoinLines(lines);
    if (!text.empty())
        text += L"\r\n";
    return true;
}

static bool WriteIniValue(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring text;
    if (!ReadIniFileForMutation(text))
        return false;

    if (!WriteIniValueToText(text, s, k, v))
        return false;

    if (!WriteUtf8BomTextFile(g_iniPath, text))
        return false;

    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(text);
    }
    return true;
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
static std::atomic<bool> g_wallpaperDetectionFallbackOnInvalid(true);
static std::atomic<bool> g_generateOnStartup(true);
static std::atomic<bool> g_generateOnStartupOnlyWhenChanged(true);
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
static std::atomic<bool> g_deleteDisabledAssets(false);
static std::atomic<bool> g_generateScaleAuto(true);
static std::atomic<bool> g_generateScale100(true);
static std::atomic<bool> g_generateScale125(true);
static std::atomic<bool> g_generateScale150(true);
static std::atomic<bool> g_generateScale200(false);
static std::atomic<bool> g_generateScale400(false);
static std::atomic<int> g_notificationTimeoutMs(4000);
static std::atomic<int> g_shutdownInitialNoticeMs(1000);
static std::atomic<int> g_shutdownRepeatNoticeMs(5000);
static std::atomic<int> g_trayIconHiddenDelayMs(750);
static std::atomic<int> g_logBufferLines(1024);
static std::atomic<int> g_pollInitialDebounceBypassMs(5000);
static std::mutex g_startupWarningMutex;
static std::vector<std::wstring> g_startupWarnings;
static std::mutex g_registrationUnresolvedMutex;
static bool g_registrationUnresolved = false;
static HANDLE g_registrationUnresolvedProcess = nullptr;
static DWORD g_registrationUnresolvedPid = 0;
static steady_clock::time_point g_registrationUnresolvedSince{};
static std::wstring g_registrationUnresolvedReason;
static std::mutex g_generationMutex;
static std::mutex g_generationQueueMutex;
static std::condition_variable g_generationQueueCv;
static std::thread g_generationWorker;
static std::atomic<bool> g_generationActive(false);
static std::atomic<bool> g_shutdownRequested(false);
static std::mutex g_generationStateMutex;
static std::wstring g_generationActiveReason;
static bool g_generationWorkerStarted = false;
static bool g_generationWorkerStop = false;
static bool g_generationQueued = false;
static std::wstring g_generationQueuedWallpaper;
static std::wstring g_generationQueuedReason;
static std::mutex g_pollWakeMutex;
static std::condition_variable g_pollWakeCv;
static constexpr UINT WM_APP_SHUTDOWN_READY = WM_APP + 1;
static constexpr UINT WM_APP_REQUEST_SHUTDOWN = WM_APP + 2;
static constexpr UINT_PTR SHUTDOWN_NOTICE_TIMER_ID = 1;
static std::atomic<HWND> g_shutdownWindow(nullptr);
static std::atomic<bool> g_consoleControlShutdownPending(false);

static void QueueStartupWarning(const std::wstring& warning)
{
    std::lock_guard<std::mutex> lk(g_startupWarningMutex);
    g_startupWarnings.push_back(warning);
}

static const IniDefault g_defaults[] = {
    // Settings
    {L"Settings", L"PollIntervalMs", L"2000"},
    {L"Settings", L"ConfirmMs", L"800"},
    {L"Settings", L"DebounceMinMs", L"1200"},
    {L"Settings", L"PollInitialDebounceBypassMs", L"5000"},
    {L"Settings", L"IniCacheRefreshMs", L"250"},
    {L"Settings", L"Logging", L"1"},
    {L"Settings", L"LogPath", L""},
    {L"Settings", L"LogBufferLines", L"1024"},
    {L"Settings", L"TrayIcon", L"1"},
    {L"Settings", L"TrayIconHiddenDelayMs", L"750"},
    {L"Settings", L"ShowConsole", L"0"},
    {L"Settings", L"GenerateOnStartup", L"1"},
    {L"Settings", L"GenerateOnStartupOnlyWhenChanged", L"1"},
    {L"Settings", L"ListenWallpaper", L"1"},
    {L"Settings", L"WallpaperDetectionMethod", L"TranscodedImageCache"},
    {L"Settings", L"WallpaperDetectionFallbackOnInvalid", L"1"},
    {L"Settings", L"ListenFit", L"1"},
    {L"Settings", L"DisableFitting", L"0"},
    {L"Settings", L"UsePowerShell", L"0"},
    {L"Settings", L"HideDisabledEntries", L"0"},
    {L"Settings", L"GenerateDesktopIconForDisabledEntries", L"1"},
    {L"Settings", L"DeleteDisabledAssets", L"0"},
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
    {L"Settings", L"NotificationTimeoutMs", L"4000"},
    {L"Settings", L"ShutdownInitialNoticeMs", L"1000"},
    {L"Settings", L"ShutdownRepeatNoticeMs", L"5000"},
    {L"Settings", L"SingleInstanceSignalRetries", L"50"},
    {L"Settings", L"SingleInstanceSignalDelayMs", L"100"},
    {L"Settings", L"SingleInstanceFailureAction", L"Warn"},
    {L"Settings", L"ComRegistrationTimeoutMs", L"120000"},
    {L"Settings", L"ComRegistrationCancelWaitMs", L"5000"},
    {L"Settings", L"PowerShellExe", L"powershell.exe"},
    {L"Settings", L"PowerShellPollMs", L"50"},
    {L"Settings", L"PowerShellTimeoutMs", L"120000"},
    {L"Settings", L"PowerShellTerminateWaitMs", L"5000"},
    {L"Settings", L"RegistrationUnresolvedBlockMs", L"300000"},
    {L"Settings", L"ManifestIdentityName", L""},
    {L"Settings", L"ManifestPublisher", L""},
    {L"Settings", L"ManifestPackageVersion", L"1.0.0.0"},
    {L"Settings", L"ManifestDisplayName", L"Desktop"},
    {L"Settings", L"ManifestPublisherDisplayName", L""},
    {L"Settings", L"ManifestDescription", L"Desktop"},
    {L"Settings", L"ManifestExecutable", L"rundll32.exe"},
    {L"Settings", L"ManifestApplicationId", L"App"},
    {L"Settings", L"ManifestMinVersion", L"10.0.15063.0"},
    {L"Settings", L"ManifestMaxVersionTested", L"10.0.15063.0"},
    {L"Settings", L"ManifestOverwriteExisting", L"0"},

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
    {L"TimingLog", L"[i] Timing: %s = %d ms"},
    {L"TimingPreLogInitialization", L"pre-log initialization"},
    {L"TimingIniDefaultStringLoading", L"INI/default/string loading"},
    {L"TimingGdiStartup", L"GDI+ startup"},
    {L"TimingAssetGeneration", L"asset generation"},
    {L"TimingAppRegistration", L"app registration"},
    {L"TimingGenerationTotal", L"generation total"},
    {L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s"},
    {L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)"},
    {L"WallpaperDetectionFallbackSummary", L"[i] Wallpaper detection fallback on invalid path: %s"},
    {L"TrayIconSummary", L"[i] Tray icon: %s"},
    {L"ConsoleSummary", L"[i] Console %s."},
    {L"LoggingSummary", L"[i] Logging %s."},
    {L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s"},
    {L"GenerateOnStartupOnlyWhenChangedSummary", L"[i] Startup generation only when changed: %s"},
    {L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s"},
    {L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s"},
    {L"DeleteDisabledAssetsSummary", L"[i] Auto-delete disabled asset files: %s"},
    {L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s"},
    {L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s"},
    {L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s"},
    {L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)"},
    {L"PowerShellEnabledMode", L"PowerShell enabled"},
    {L"ComPreferredMode", L"COM preferred with PowerShell fallback"},
    {L"ConsoleAllocated", L"[i] Console allocated."},
    {L"StartupGenerationEnabled", L"[i] Startup generation enabled."},
    {L"StartupGenerationSkippedUnchanged", L"[i] Startup generation skipped: wallpaper/config unchanged."},
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
    {L"LogWriteFailed", L"[!] Log file write failed (error %lu: %s): %s"},
    {L"IniWriteFailed", L"[!] Failed to write INI value [%s] %s (error %lu: %s)."},
    {L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting."},
    {L"SingleInstanceScopeSummary", L"[i] Single-instance scope: %s"},
    {L"GdiPlusOperationFailed", L"[!] GDI+ %s failed (status=%d)."},

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
    {L"PollInitialDebounceBypassMsLabel", L"Initial debounce bypass: %d ms"},
    {L"IniCacheRefreshMsLabel", L"INI cache refresh: %d ms"},
    {L"NotificationTimeoutMsLabel", L"Notification timeout: %d ms"},
    {L"ShutdownInitialNoticeMsLabel", L"Shutdown first notice: %d ms"},
    {L"ShutdownRepeatNoticeMsLabel", L"Shutdown repeat notice: %d ms"},
    {L"SingleInstanceSignalRetriesLabel", L"Single-instance signal retries: %d"},
    {L"SingleInstanceSignalDelayMsLabel", L"Single-instance signal delay: %d ms"},
    {L"ComRegistrationTimeoutMsLabel", L"COM registration timeout: %d ms"},
    {L"ComRegistrationCancelWaitMsLabel", L"COM cancel wait: %d ms"},
    {L"TrayIconHiddenDelayMsLabel", L"Tray hide warning delay: %d ms"},
    {L"LogBufferLinesLabel", L"Log buffer lines: %d"},
    {L"PowerShellExeLabel", L"PowerShell executable: %s"},
    {L"PowerShellPollMsLabel", L"PowerShell poll: %d ms"},
    {L"PowerShellTimeoutMsLabel", L"PowerShell timeout: %d ms"},
    {L"PowerShellTerminateWaitMsLabel", L"PowerShell terminate wait: %d ms"},
    {L"RegistrationUnresolvedBlockMsLabel", L"Unresolved registration block: %d ms"},
    {L"ManifestIdentityNameLabel", L"Manifest identity: %s"},
    {L"ManifestPublisherLabel", L"Manifest publisher: %s"},
    {L"ManifestPackageVersionLabel", L"Manifest version: %s"},
    {L"ManifestDisplayNameLabel", L"Manifest display name: %s"},
    {L"ManifestPublisherDisplayNameLabel", L"Manifest publisher display: %s"},
    {L"ManifestDescriptionLabel", L"Manifest description: %s"},
    {L"ManifestExecutableLabel", L"Manifest executable: %s"},
    {L"ManifestApplicationIdLabel", L"Manifest app id: %s"},
    {L"ManifestMinVersionLabel", L"Manifest min version: %s"},
    {L"ManifestMaxVersionTestedLabel", L"Manifest max tested: %s"},
    {L"ManifestSourceLabel", L"Manifest source: %s"},
    {L"ManifestSourceExisting", L"existing AppxManifest.xml"},
    {L"ManifestSourceGenerated", L"generated from INI if missing"},
    {L"ManifestOverwriteExistingLabel", L"Manifest overwrite existing: %s"},
    {L"WallpaperDetectionMethodLabel", L"Wallpaper detection method: %s"},
    {L"WallpaperDetectionFallback", L"Fallback when selected path is invalid"},
    {L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s"},
    {L"SingleInstanceActionIgnore", L"Ignore"},
    {L"SingleInstanceActionWarn", L"Warn"},
    {L"SingleInstanceActionExit", L"Exit"},
    {L"SingleInstanceActionCrash", L"Crash"},
    {L"UnknownError", L"Unknown error"},
    {L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||"},

    {L"GeneralTitle", L"General:"},
    {L"LoggingTitle", L"Logging:"},
    {L"WallpaperFittingTitle", L"Wallpaper Fitting:"},
    {L"MethodsTitle", L"Methods:"},
    {L"AssetsTitle", L"Assets:"},
    {L"DpiScalesTitle", L"DPI Scales:"},
    {L"ListenWallpaper", L"Listen Wallpaper"},
    {L"TrayIcon", L"Tray Icon (this session)"},
    {L"UsePowerShell", L"PowerShell only"},
    {L"GenerateOnStartup", L"Generate on Startup"},
    {L"GenerateOnStartupOnlyWhenChanged", L"Generate on Startup only when changed"},
    {L"HideDisabledEntries", L"Hide disabled entries"},
    {L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries"},
    {L"DeleteDisabledAssets", L"Auto-delete disabled asset files"},
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
    {L"ShutdownPendingTitle", L"Shutdown pending:"},
    {L"ShutdownPendingReason", L"Waiting for generation/registration: %s"},
    {L"CancelShutdown", L"Cancel shutdown"},
    {L"ForceShutdown", L"Force shutdown"},
    {L"ShutdownDelayedNotification", L"Exit is waiting for generation/registration to finish: %s"},
    {L"ShutdownRequested", L"[i] Shutdown requested."},
    {L"ShutdownCancelled", L"[i] Shutdown cancelled."},
    {L"ForceShutdownRequested", L"[!] Force shutdown requested."},
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
    {L"WallpaperImageLoaded", L"[i] Wallpaper image loaded (%d x %d)."},
    {L"RenderModeSummary", L"[i] Render fit mode: %s"},
    {L"AssetsDirectoryReady", L"[i] Assets directory ready: %s"},
    {L"FailedLoadWallpaper", L"Failed to load wallpaper image."},
    {L"SkippingAssetDisabled", L"Skipping %s (disabled in settings)."},
    {L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s"},
    {L"ActiveScales", L"[i] Active scales: %s"},
    {L"SavedAsset", L"Saved %s"},
    {L"SavedScaledAsset", L"Saved %s (scale %d%%)"},
    {L"FailedSaveAsset", L"Failed to save %s"},
    {L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)"},
    {L"SavePngInvalidInput", L"[!] SavePNG skipped for %s: invalid bitmap, path, or PNG encoder."},
    {L"SavePngGdiFailure", L"[!] SavePNG GDI+ save failed for %s (status=%d)."},
    {L"SavePngReplaceFailed", L"[!] SavePNG replace failed for %s (temp=%s)."},
    {L"DeletedDisabledAsset", L"[i] Deleted disabled asset file: %s"},
    {L"FailedDeleteDisabledAsset", L"[!] Failed to delete disabled asset file: %s"},
    {L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets."},
    {L"AppxManifestCreated", L"[i] Created AppxManifest.xml: %s"},
    {L"AppxManifestExisting", L"[i] Using existing AppxManifest.xml: %s"},
    {L"AppxManifestOverwritten", L"[i] Updated AppxManifest.xml from configured defaults: %s"},
    {L"AppxManifestCreateFailed", L"[!] Failed to create AppxManifest.xml: %s"},
    {L"ManifestPath", L"Manifest path: %s"},
    {L"UsingComRegistration", L"Using COM Appx registration..."},
    {L"ComRegistrationUri", L"[i] COM registration URI: %s"},
    {L"ComRegistrationTimeoutDetails", L"[i] COM registration timeout=%d ms, cancel wait=%d ms"},
    {L"InvalidManifestPath", L"Invalid manifest path."},
    {L"ComRegistrationSuccess", L"COM registration success."},
    {L"ComRegistrationTimedOut", L"COM registration timed out."},
    {L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration."},
    {L"ComRegistrationCancelStillRunning", L"COM registration is still running after cancel; PowerShell fallback skipped to avoid concurrent registration."},
    {L"ComRegistrationFallbackSkippedAfterTimeout", L"COM registration timeout is unresolved; PowerShell fallback skipped to avoid concurrent registration."},
    {L"ComRegistrationException", L"COM registration threw exception: 0x%08X"},
    {L"ComExceptionMessage", L"COM message: %s"},
    {L"LaunchingPowerShellRegistration", L"Launching PowerShell registration..."},
    {L"PowerShellCommand", L"Command: %s"},
    {L"PowerShellLaunchDetails", L"[i] PowerShell executable: %s (timeout=%d ms, poll=%d ms, terminate wait=%d ms)"},
    {L"PowerShellProcessStarted", L"[i] PowerShell process started (pid=%lu)."},
    {L"PowerShellTerminateRequested", L"[!] Terminating timed-out PowerShell process (pid=%lu)."},
    {L"PowerShellTerminateFailed", L"[!] Failed to terminate timed-out PowerShell process (pid=%lu)."},
    {L"PowerShellStillRunningAfterTimeout", L"[!] PowerShell process is still running after terminate wait (pid=%lu)."},
    {L"PowerShellTerminatedAfterTimeout", L"[i] PowerShell process terminated after timeout (pid=%lu)."},
    {L"PowerShellCompleted", L"PowerShell registration completed successfully."},
    {L"PowerShellTimedOut", L"PowerShell registration timed out."},
    {L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!"},
    {L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X."},
    {L"PowerShellOutputFollows", L"PowerShell output follows:"},
    {L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully."},
    {L"AssetGenerationFinished", L"Asset generation and registration finished successfully."},
    {L"AssetGenerationFailed", L"Asset generation did not complete successfully."},
    {L"AppRegistrationFailed", L"App registration did not complete successfully."},
    {L"SkippingRegistrationDueToAssetFailure", L"[!] Skipping Appx registration because asset generation failed."},
    {L"CreateAssetsDirectoryOperation", L"Create Assets directory"},
    {L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing or invalid: %s"},
    {L"NotificationMissingManifestAssets", L"Registration failed; one or more manifest assets are missing or invalid."},
    {L"RegistrationBlockedUnresolved", L"[!] Registration skipped because a previous registration is unresolved: %s"},
    {L"RegistrationUnresolvedMarked", L"[!] Registration marked unresolved: %s"},
    {L"RegistrationUnresolvedCleared", L"[i] Previous unresolved registration finished: %s"},
    {L"RegistrationUnresolvedExpired", L"[!] Previous unresolved registration block expired; allowing registration again: %s"},
    {L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce."},
    {L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected."},
    {L"WallpaperChangeDetected", L"Wallpaper change detected."},
    {L"WallpaperDetectionEmptyDuringPoll", L"[!] Wallpaper change ignored: wallpaper detection returned empty."},
    {L"WallpaperDetectionMethodFailed", L"[!] Wallpaper detection method %s did not return a wallpaper path."},
    {L"WallpaperDetectionMethodReturnedInvalid", L"[!] Wallpaper detection method %s returned an unusable path: %s"},
    {L"WallpaperDetectionFallbackSelected", L"[i] Wallpaper detection fallback selected %s: %s"},
    {L"WallpaperDetectionFallbackDisabled", L"[!] Wallpaper detection fallback disabled; using %s result as-is."},
    {L"FitModeChangeDetected", L"Fit mode change detected."},
    {L"DpiScaleChangeDetected", L"DPI scale change detected."},
    {L"SingleInstanceMutexFailure", L"[!] Single-instance mutex creation failed (error %lu: %s); action=%s."},
};

static void EnsureIniStringDefaults()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
    {
        QueueStartupWarning(L"[!] Failed to read INI while applying string defaults (error " + std::to_wstring(GetLastError()) + L").");
        return;
    }

    bool haveText = true;
    auto doc = ParseIniDocument(iniText);
    bool changed = false;

    for (const auto& d : g_stringDefaults)
    {
        std::wstring value;
        if (!haveText || !ReadIniValueFromDoc(doc, L"Strings", d.key, value))
        {
            if (!WriteIniValueToText(iniText, L"Strings", d.key, d.value))
            {
                QueueStartupWarning(L"[!] Failed to update INI string default: " + std::wstring(d.key ? d.key : L""));
                return;
            }
            changed = true;
            doc = ParseIniDocument(iniText);
            haveText = true;
        }
    }

    if (changed && !WriteUtf8BomTextFile(g_iniPath, iniText))
    {
        QueueStartupWarning(L"[!] Failed to write INI string defaults (error " + std::to_wstring(GetLastError()) + L").");
    }
    else if (changed)
    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(iniText);
    }
}

static std::wstring BuildInitialIniTemplate()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"; ============================================================");
    lines.push_back(L"; GenerateAssets - Configuration");
    lines.push_back(L"; ============================================================");
    lines.push_back(L"; Keys and values are quoted so ; and # can be used literally");
    lines.push_back(L"; inside strings. Keep required printf tokens such as %s/%d.");
    lines.push_back(L"; ============================================================");
    lines.push_back(L"");
    lines.push_back(L"[Settings]");
    lines.push_back(L"; General runtime behaviour.");
    for (const auto& d : g_defaults)
    {
        if (wcscmp(d.section, L"Settings") == 0)
            lines.push_back(FormatIniAssignment(d.key, d.value));
    }
    lines.push_back(L"");
    lines.push_back(L"; WallpaperDetectionMethod options:");
    lines.push_back(L"; TranscodedImageCache, SystemParametersInfo, DesktopWallpaperCOM,");
    lines.push_back(L"; TranscodedWallpaperFile, Auto.");
    lines.push_back(L"");
    lines.push_back(L"[Assets]");
    lines.push_back(L"; Toggle which AppX tile/logo assets are generated.");
    for (const auto& d : g_defaults)
    {
        if (wcscmp(d.section, L"Assets") == 0)
            lines.push_back(FormatIniAssignment(d.key, d.value));
    }
    lines.push_back(L"");
    lines.push_back(L"[Strings]");
    lines.push_back(L"; User-visible strings. Keep format tokens unchanged.");
    for (const auto& d : g_stringDefaults)
        lines.push_back(FormatIniAssignment(d.key, d.value));

    return JoinLines(lines) + L"\r\n";
}

static bool EnsureInitialIniTemplate()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::vector<BYTE> bytes;
    DWORD attrs = GetFileAttributesW(g_iniPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return false;
        }

        if (!ReadWholeFileBytes(g_iniPath, bytes))
            return false;
        if (!bytes.empty())
            return false;
    }
    else
    {
        DWORD err = GetLastError();
        if (!IsMissingFileError(err))
        {
            SetLastError(err);
            return false;
        }
    }

    std::wstring text = BuildInitialIniTemplate();
    if (!WriteUtf8BomTextFile(g_iniPath, text))
        return false;

    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(text);
    }
    return true;
}

static void UpgradeStringDefaultIfUnmodified(const wchar_t* key, const wchar_t* oldValue, const wchar_t* newValue)
{
    std::wstring current = IniReadS(L"Strings", key, L"");
    if (current == oldValue)
        IniWrite(L"Strings", key, newValue);
}

static void UpgradeRenamedStringDefaults()
{
    UpgradeStringDefaultIfUnmodified(L"UsePowerShell", L"Use PowerShell", L"PowerShell only");
    UpgradeStringDefaultIfUnmodified(L"TrayIcon", L"Tray Icon", L"Tray Icon (this session)");
    UpgradeStringDefaultIfUnmodified(L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing: %s", L"[!] Registration failed and manifest assets are missing or invalid: %s");
    UpgradeStringDefaultIfUnmodified(L"NotificationMissingManifestAssets", L"Registration failed because one or more manifest assets are missing.", L"Registration failed; one or more manifest assets are missing or invalid.");
    UpgradeStringDefaultIfUnmodified(L"NotificationMissingManifestAssets", L"Registration failed because one or more manifest assets are missing or invalid.", L"Registration failed; one or more manifest assets are missing or invalid.");
}

// Logging buffer
static std::mutex g_logMutex;
static std::vector<std::wstring> g_logBuf;
static std::atomic<bool> g_logWriteFailureReported(false);

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
    std::wstring methodsTitle;
    std::wstring assetsTitle;
    std::wstring dpiScalesTitle;

    std::wstring listenWallpaper;
    std::wstring trayIcon;
    std::wstring usePowerShell;
    std::wstring generateOnStartup;
    std::wstring generateOnStartupOnlyWhenChanged;
    std::wstring hideDisabledEntries;
    std::wstring generateDesktopIconForDisabledEntries;
    std::wstring deleteDisabledAssets;
    std::wstring wallpaperDetectionFallback;
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
    std::wstring shutdownPendingTitle;
    std::wstring shutdownPendingReason;
    std::wstring cancelShutdown;
    std::wstring forceShutdown;
    std::wstring shutdownDelayedNotification;
    std::wstring shutdownRequested;
    std::wstring shutdownCancelled;
    std::wstring forceShutdownRequested;

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
    std::wstring wallpaperImageLoaded;
    std::wstring renderModeSummary;
    std::wstring assetsDirectoryReady;
    std::wstring failedLoadWallpaper;
    std::wstring iniEncodingNormalized;
    std::wstring skippingAssetDisabled;
    std::wstring generatedDesktopIconAsset;
    std::wstring activeScales;
    std::wstring savedAsset;
    std::wstring savedScaledAsset;
    std::wstring failedSaveAsset;
    std::wstring failedSaveScaledAsset;
    std::wstring savePngInvalidInput;
    std::wstring savePngGdiFailure;
    std::wstring savePngReplaceFailed;
    std::wstring deletedDisabledAsset;
    std::wstring failedDeleteDisabledAsset;
    std::wstring reRegisteringManifest;
    std::wstring appxManifestCreated;
    std::wstring appxManifestExisting;
    std::wstring appxManifestOverwritten;
    std::wstring appxManifestCreateFailed;
    std::wstring manifestPath;
    std::wstring usingComRegistration;
    std::wstring comRegistrationUri;
    std::wstring comRegistrationTimeoutDetails;
    std::wstring invalidManifestPath;
    std::wstring comRegistrationSuccess;
    std::wstring comRegistrationTimedOut;
    std::wstring comRegistrationFailed;
    std::wstring comRegistrationCancelStillRunning;
    std::wstring comRegistrationFallbackSkippedAfterTimeout;
    std::wstring comRegistrationException;
    std::wstring comExceptionMessage;
    std::wstring launchingPowerShellRegistration;
    std::wstring powerShellCommand;
    std::wstring powerShellLaunchDetails;
    std::wstring powerShellProcessStarted;
    std::wstring powerShellTerminateRequested;
    std::wstring powerShellTerminateFailed;
    std::wstring powerShellStillRunningAfterTimeout;
    std::wstring powerShellTerminatedAfterTimeout;
    std::wstring powerShellCompleted;
    std::wstring powerShellTimedOut;
    std::wstring powerShellErrorSideloadDisabled;
    std::wstring powerShellErrorCode;
    std::wstring powerShellOutputFollows;
    std::wstring powerShellRegistrationFailed;
    std::wstring assetGenerationFinished;
    std::wstring assetGenerationFailed;
    std::wstring appRegistrationFailed;
    std::wstring skippingRegistrationDueToAssetFailure;
    std::wstring createAssetsDirectoryOperation;
    std::wstring missingManifestAssets;
    std::wstring notificationMissingManifestAssets;
    std::wstring registrationBlockedUnresolved;
    std::wstring registrationUnresolvedMarked;
    std::wstring registrationUnresolvedCleared;
    std::wstring registrationUnresolvedExpired;

    std::wstring changeConfirmed;
    std::wstring wallpaperAndFitChangeDetected;
    std::wstring wallpaperChangeDetected;
    std::wstring wallpaperDetectionEmptyDuringPoll;
    std::wstring wallpaperDetectionMethodFailed;
    std::wstring wallpaperDetectionMethodReturnedInvalid;
    std::wstring wallpaperDetectionFallbackSelected;
    std::wstring wallpaperDetectionFallbackDisabled;
    std::wstring fitModeChangeDetected;
    std::wstring dpiScaleChangeDetected;

    std::wstring enabledText;
    std::wstring disabledText;
    std::wstring onText;
    std::wstring offText;

    std::wstring trayTip;
    std::wstring exeLabel;
    std::wstring iniLabel;
    std::wstring logFileLabel;
    std::wstring timingLog;
    std::wstring timingPreLogInitialization;
    std::wstring timingIniDefaultStringLoading;
    std::wstring timingGdiStartup;
    std::wstring timingAssetGeneration;
    std::wstring timingAppRegistration;
    std::wstring timingGenerationTotal;
    std::wstring powerShellRegistrationSummary;
    std::wstring usePowerShellSummary;
    std::wstring wallpaperDetectionFallbackSummary;
    std::wstring trayIconSummary;
    std::wstring consoleSummary;
    std::wstring loggingSummary;
    std::wstring generateOnStartupSummary;
    std::wstring generateOnStartupOnlyWhenChangedSummary;
    std::wstring hideDisabledEntriesSummary;
    std::wstring generateDesktopIconForDisabledEntriesSummary;
    std::wstring deleteDisabledAssetsSummary;
    std::wstring generateScaleAutoSummary;
    std::wstring generateScaleSummary;
    std::wstring showMenuAsDropdownSummary;
    std::wstring notificationsSummary;
    std::wstring powerShellEnabledMode;
    std::wstring comPreferredMode;
    std::wstring consoleAllocated;
    std::wstring startupGenerationEnabled;
    std::wstring startupGenerationSkippedUnchanged;
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
    std::wstring gdiPlusOperationFailed;
    std::wstring win32FailureFmt;
    std::wstring logPathChanged;
    std::wstring logPathRenamed;
    std::wstring logPathReset;
    std::wstring logWriteFailed;
    std::wstring iniWriteFailed;
    std::wstring listenFitAutoDisabled;
    std::wstring singleInstanceScopeSummary;
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
    std::wstring pollInitialDebounceBypassMsLabel;
    std::wstring iniCacheRefreshMsLabel;
    std::wstring notificationTimeoutMsLabel;
    std::wstring shutdownInitialNoticeMsLabel;
    std::wstring shutdownRepeatNoticeMsLabel;
    std::wstring singleInstanceSignalRetriesLabel;
    std::wstring singleInstanceSignalDelayMsLabel;
    std::wstring comRegistrationTimeoutMsLabel;
    std::wstring comRegistrationCancelWaitMsLabel;
    std::wstring trayIconHiddenDelayMsLabel;
    std::wstring logBufferLinesLabel;
    std::wstring powerShellExeLabel;
    std::wstring powerShellPollMsLabel;
    std::wstring powerShellTimeoutMsLabel;
    std::wstring powerShellTerminateWaitMsLabel;
    std::wstring registrationUnresolvedBlockMsLabel;
    std::wstring manifestIdentityNameLabel;
    std::wstring manifestPublisherLabel;
    std::wstring manifestPackageVersionLabel;
    std::wstring manifestDisplayNameLabel;
    std::wstring manifestPublisherDisplayNameLabel;
    std::wstring manifestDescriptionLabel;
    std::wstring manifestExecutableLabel;
    std::wstring manifestApplicationIdLabel;
    std::wstring manifestMinVersionLabel;
    std::wstring manifestMaxVersionTestedLabel;
    std::wstring manifestSourceLabel;
    std::wstring manifestSourceExisting;
    std::wstring manifestSourceGenerated;
    std::wstring manifestOverwriteExistingLabel;
    std::wstring wallpaperDetectionMethodLabel;
    std::wstring singleInstanceFailureActionLabel;
    std::wstring singleInstanceActionIgnore;
    std::wstring singleInstanceActionWarn;
    std::wstring singleInstanceActionExit;
    std::wstring singleInstanceActionCrash;
    std::wstring unknownError;

    std::wstring logFileFilter;
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

std::wstring Win32ErrorString(DWORD err);
static std::wstring FormatWideV(const wchar_t* fmt, va_list ap);
std::wstring FormatWide(const wchar_t* fmt, ...);

void Log(const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::wstring msg = FormatWideV(fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[64];
    swprintf(ts, 64, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        static_cast<int>(st.wYear),
        static_cast<int>(st.wMonth),
        static_cast<int>(st.wDay),
        static_cast<int>(st.wHour),
        static_cast<int>(st.wMinute),
        static_cast<int>(st.wSecond));

    std::wstring line = ts + msg;

    std::lock_guard<std::mutex> lk(g_logMutex);
    int configuredMaxLogLines = g_logBufferLines.load();
    if (configuredMaxLogLines < 1) configuredMaxLogLines = 1;
    if (configuredMaxLogLines > 100000) configuredMaxLogLines = 100000;
    size_t maxLogLines = static_cast<size_t>(configuredMaxLogLines);
    auto bufferLine = [&](const std::wstring& bufferedLine)
    {
        while (g_logBuf.size() >= maxLogLines)
            g_logBuf.erase(g_logBuf.begin());
        g_logBuf.push_back(bufferedLine);
        if (g_console)
            WriteConsoleLine(bufferedLine);
    };

    bufferLine(line);

    if (g_logging)
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
        {
            if (AppendUtf8TextFile(logPath, line + L"\r\n"))
            {
                g_logWriteFailureReported = false;
            }
            else
            {
                DWORD err = GetLastError();
                if (err == ERROR_SUCCESS)
                    err = ERROR_WRITE_FAULT;
                if (!g_logWriteFailureReported.exchange(true))
                {
                    const wchar_t* failureFmt = g_ui.logWriteFailed.empty()
                        ? L"[!] Log file write failed (error %lu: %s): %s"
                        : g_ui.logWriteFailed.c_str();
                    std::wstring failureLine = ts + FormatWide(failureFmt, err, Win32ErrorString(err).c_str(), logPath.c_str());
                    bufferLine(failureLine);
                }
            }
        }
    }
}

void LogText(const std::wstring& text)
{
    Log(L"%ls", text.c_str());
}

static void FlushStartupWarnings()
{
    std::vector<std::wstring> warnings;
    {
        std::lock_guard<std::mutex> lk(g_startupWarningMutex);
        warnings.swap(g_startupWarnings);
    }

    for (const auto& warning : warnings)
        LogText(warning);
}

int IniReadI(const wchar_t* s, const wchar_t* k, int d)
{
    std::wstring value;
    if (!ReadIniValueCached(s, k, value))
        return d;

    std::wstring trimmed = TrimCopy(value);
    if (trimmed.empty())
        return d;

    errno = 0;
    wchar_t* end = nullptr;
    long parsed = wcstol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
        return d;

    while (end && *end && iswspace(*end) != 0)
        ++end;
    if (end && *end)
        return d;

    return static_cast<int>(parsed);
}

static int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

static int ElapsedMs(steady_clock::time_point start, steady_clock::time_point end)
{
    auto ms = duration_cast<milliseconds>(end - start).count();
    if (ms < 0) return 0;
    if (ms > INT_MAX) return INT_MAX;
    return static_cast<int>(ms);
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
    std::wstring out;
    if (ReadIniValueCached(s, k, out))
        return out;
    return d ? d : L"";
}

void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    if (WriteIniValue(s, k, v))
        return;

    DWORD err = GetLastError();
    if (err == ERROR_SUCCESS)
        err = ERROR_WRITE_FAULT;

    const wchar_t* fmt = g_ui.iniWriteFailed.empty()
        ? L"[!] Failed to write INI value [%s] %s (error %lu: %s)."
        : g_ui.iniWriteFailed.c_str();
    Log(fmt, s ? s : L"", k ? k : L"", err, Win32ErrorString(err).c_str());
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

    size_t uriCapSize = std::max<size_t>(260, p.size() * 3 + 32);
    if (uriCapSize <= MAXDWORD)
    {
        std::vector<wchar_t> uri(uriCapSize);
        DWORD uriLen = static_cast<DWORD>(uri.size());
        if (SUCCEEDED(UrlCreateFromPathW(p.c_str(), uri.data(), &uriLen, 0)) && uri[0])
            return std::wstring(uri.data());
    }

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

static bool IsAsciiAlpha(wchar_t ch)
{
    return (ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z');
}

static bool IsAsciiDigit(wchar_t ch)
{
    return ch >= L'0' && ch <= L'9';
}

static bool IsAsciiAlnum(wchar_t ch)
{
    return IsAsciiAlpha(ch) || IsAsciiDigit(ch);
}

static std::wstring ExeBaseName()
{
    std::wstring name = g_exePath;
    size_t slash = name.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        name.erase(0, slash + 1);

    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos)
        name.erase(dot);

    return name.empty() ? L"GenerateAssets" : name;
}

static std::wstring ManifestSetting(const wchar_t* key, const wchar_t* fallback)
{
    std::wstring value = TrimCopy(IniReadS(L"Settings", key, L""));
    if (!value.empty())
        return value;
    return fallback ? fallback : L"";
}

static std::wstring TrimNonAlnumEnds(std::wstring value)
{
    while (!value.empty() && !IsAsciiAlnum(value.front()))
        value.erase(value.begin());
    while (!value.empty() && !IsAsciiAlnum(value.back()))
        value.pop_back();
    return value;
}

static std::wstring SanitizeManifestIdentityName(const std::wstring& raw)
{
    std::wstring lower = ToLowerCopy(raw);
    std::wstring out;
    bool lastSeparator = false;
    for (wchar_t ch : lower)
    {
        if (IsAsciiAlnum(ch))
        {
            out.push_back(ch);
            lastSeparator = false;
        }
        else if (ch == L'.' || ch == L'-')
        {
            if (!out.empty() && !lastSeparator)
            {
                out.push_back(ch);
                lastSeparator = true;
            }
        }
        else if (!out.empty() && !lastSeparator)
        {
            out.push_back(L'.');
            lastSeparator = true;
        }
    }

    out = TrimNonAlnumEnds(out);
    if (out.empty())
        out = L"generateassets";

    if (out.find(L'.') == std::wstring::npos)
        out = L"dev.local." + out;

    if (out.size() > 50)
    {
        out.resize(50);
        out = TrimNonAlnumEnds(out);
    }

    return out.empty() ? L"dev.local.generateassets" : out;
}

static std::wstring SanitizeManifestToken(const std::wstring& raw, const wchar_t* fallback)
{
    std::wstring out;
    for (wchar_t ch : raw)
    {
        if (IsAsciiAlnum(ch) || ch == L'.' || ch == L'_' || ch == L'-')
            out.push_back(ch);
    }

    out = TrimNonAlnumEnds(out);
    if (out.empty())
        out = fallback ? fallback : L"App";
    if (!IsAsciiAlpha(out.front()))
        out = std::wstring(L"App") + out;
    return out;
}

static bool IsManifestVersion(const std::wstring& version)
{
    size_t pos = 0;
    int parts = 0;
    while (pos <= version.size())
    {
        size_t dot = version.find(L'.', pos);
        size_t end = dot == std::wstring::npos ? version.size() : dot;
        if (end == pos)
            return false;

        errno = 0;
        wchar_t* parseEnd = nullptr;
        std::wstring part = version.substr(pos, end - pos);
        unsigned long value = wcstoul(part.c_str(), &parseEnd, 10);
        if (parseEnd == part.c_str() || *parseEnd != L'\0' || errno == ERANGE || value > 65535)
            return false;

        ++parts;
        if (dot == std::wstring::npos)
            break;
        pos = dot + 1;
    }

    return parts == 4;
}

static std::wstring EffectiveManifestIdentityName()
{
    std::wstring configured = TrimCopy(IniReadS(L"Settings", L"ManifestIdentityName", L""));
    if (configured.empty())
        configured = L"dev.local." + ExeBaseName();
    return SanitizeManifestIdentityName(configured);
}

static std::wstring EffectiveManifestPublisher()
{
    std::wstring publisher = ManifestSetting(L"ManifestPublisher", L"CN=DesktopTileGenerator");
    if (ToLowerCopy(publisher).rfind(L"cn=", 0) != 0)
        publisher = L"CN=" + publisher;
    return publisher;
}

static std::wstring EffectiveManifestPackageVersion()
{
    std::wstring version = ManifestSetting(L"ManifestPackageVersion", L"1.0.0.0");
    return IsManifestVersion(version) ? version : L"1.0.0.0";
}

static std::wstring EffectiveManifestDisplayName()
{
    return ManifestSetting(L"ManifestDisplayName", L"Desktop");
}

static std::wstring EffectiveManifestPublisherDisplayName()
{
    return ManifestSetting(L"ManifestPublisherDisplayName", L"Desktop Tile Generator");
}

static std::wstring EffectiveManifestDescription()
{
    return ManifestSetting(L"ManifestDescription", L"Desktop");
}

static std::wstring EffectiveManifestExecutable()
{
    return ManifestSetting(L"ManifestExecutable", L"rundll32.exe");
}

static std::wstring EffectiveManifestApplicationId()
{
    return SanitizeManifestToken(ManifestSetting(L"ManifestApplicationId", L"App"), L"App");
}

static std::wstring EffectiveManifestMinVersion()
{
    std::wstring version = ManifestSetting(L"ManifestMinVersion", L"10.0.15063.0");
    return IsManifestVersion(version) ? version : L"10.0.15063.0";
}

static std::wstring EffectiveManifestMaxVersionTested()
{
    std::wstring version = ManifestSetting(L"ManifestMaxVersionTested", L"10.0.15063.0");
    return IsManifestVersion(version) ? version : L"10.0.15063.0";
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

static std::wstring XmlUnescape(std::wstring value)
{
    struct Replacement { const wchar_t* from; const wchar_t* to; };
    static const Replacement replacements[] = {
        { L"&quot;", L"\"" },
        { L"&apos;", L"'" },
        { L"&lt;", L"<" },
        { L"&gt;", L">" },
        { L"&amp;", L"&" },
    };

    for (const auto& r : replacements)
    {
        size_t pos = 0;
        while ((pos = value.find(r.from, pos)) != std::wstring::npos)
        {
            value.replace(pos, wcslen(r.from), r.to);
            pos += wcslen(r.to);
        }
    }
    return value;
}

static bool ExtractXmlAttributeFromTag(const std::wstring& xml, const wchar_t* tagStart, const wchar_t* attributeName, std::wstring& out)
{
    size_t tag = xml.find(tagStart);
    if (tag == std::wstring::npos)
        return false;

    size_t tagEnd = xml.find(L'>', tag);
    if (tagEnd == std::wstring::npos)
        return false;

    std::wstring needle = std::wstring(attributeName) + L"=\"";
    size_t pos = xml.find(needle, tag);
    if (pos == std::wstring::npos || pos > tagEnd)
        return false;

    pos += needle.size();
    size_t end = xml.find(L'"', pos);
    if (end == std::wstring::npos || end > tagEnd)
        return false;

    out = XmlUnescape(xml.substr(pos, end - pos));
    return true;
}

static bool ExtractXmlElementText(const std::wstring& xml, const wchar_t* elementName, std::wstring& out)
{
    std::wstring open = std::wstring(L"<") + elementName + L">";
    std::wstring close = std::wstring(L"</") + elementName + L">";

    size_t start = xml.find(open);
    if (start == std::wstring::npos)
        return false;

    start += open.size();
    size_t end = xml.find(close, start);
    if (end == std::wstring::npos)
        return false;

    out = XmlUnescape(xml.substr(start, end - start));
    return true;
}

static bool ManifestOverwriteExisting()
{
    return IniReadI(L"Settings", L"ManifestOverwriteExisting", 0) != 0;
}

struct ManifestDisplayInfo
{
    bool existing = false;
    std::wstring identityName;
    std::wstring publisher;
    std::wstring packageVersion;
    std::wstring displayName;
    std::wstring publisherDisplayName;
    std::wstring description;
    std::wstring executable;
    std::wstring applicationId;
    std::wstring minVersion;
    std::wstring maxVersionTested;
};

static ManifestDisplayInfo CurrentManifestDisplayInfo()
{
    ManifestDisplayInfo info;
    info.identityName = EffectiveManifestIdentityName();
    info.publisher = EffectiveManifestPublisher();
    info.packageVersion = EffectiveManifestPackageVersion();
    info.displayName = EffectiveManifestDisplayName();
    info.publisherDisplayName = EffectiveManifestPublisherDisplayName();
    info.description = EffectiveManifestDescription();
    info.executable = EffectiveManifestExecutable();
    info.applicationId = EffectiveManifestApplicationId();
    info.minVersion = EffectiveManifestMinVersion();
    info.maxVersionTested = EffectiveManifestMaxVersionTested();

    std::wstring manifestPath = GetExeDir() + L"\\AppxManifest.xml";
    DWORD attrs = GetFileAttributesW(manifestPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return info;

    std::wstring xml;
    if (!ReadTextFile(manifestPath, xml))
        return info;

    info.existing = true;
    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Name", info.identityName);
    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Publisher", info.publisher);
    ExtractXmlAttributeFromTag(xml, L"<Identity", L"Version", info.packageVersion);
    ExtractXmlElementText(xml, L"DisplayName", info.displayName);
    ExtractXmlElementText(xml, L"PublisherDisplayName", info.publisherDisplayName);
    ExtractXmlAttributeFromTag(xml, L"<Application ", L"Executable", info.executable);
    ExtractXmlAttributeFromTag(xml, L"<Application ", L"Id", info.applicationId);
    ExtractXmlAttributeFromTag(xml, L"<TargetDeviceFamily", L"MinVersion", info.minVersion);
    ExtractXmlAttributeFromTag(xml, L"<TargetDeviceFamily", L"MaxVersionTested", info.maxVersionTested);
    ExtractXmlAttributeFromTag(xml, L"<uap:VisualElements", L"Description", info.description);
    return info;
}

static std::wstring BuildDefaultAppxManifest()
{
    std::wstring identity = XmlEscape(EffectiveManifestIdentityName());
    std::wstring publisher = XmlEscape(EffectiveManifestPublisher());
    std::wstring version = XmlEscape(EffectiveManifestPackageVersion());
    std::wstring displayName = XmlEscape(EffectiveManifestDisplayName());
    std::wstring publisherDisplayName = XmlEscape(EffectiveManifestPublisherDisplayName());
    std::wstring description = XmlEscape(EffectiveManifestDescription());
    std::wstring executable = XmlEscape(EffectiveManifestExecutable());
    std::wstring appId = XmlEscape(EffectiveManifestApplicationId());
    std::wstring minVersion = XmlEscape(EffectiveManifestMinVersion());
    std::wstring maxVersionTested = XmlEscape(EffectiveManifestMaxVersionTested());

    std::wstring manifest;
    manifest += L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    manifest += L"<Package\r\n";
    manifest += L"  xmlns=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10\"\r\n";
    manifest += L"  xmlns:uap=\"http://schemas.microsoft.com/appx/manifest/uap/windows10\"\r\n";
    manifest += L"  xmlns:rescap=\"http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities\"\r\n";
    manifest += L"  IgnorableNamespaces=\"uap rescap\">\r\n\r\n";
    manifest += L"  <Identity\r\n";
    manifest += L"    Name=\"" + identity + L"\"\r\n";
    manifest += L"    Publisher=\"" + publisher + L"\"\r\n";
    manifest += L"    Version=\"" + version + L"\" />\r\n\r\n";
    manifest += L"  <Properties>\r\n";
    manifest += L"    <DisplayName>" + displayName + L"</DisplayName>\r\n";
    manifest += L"    <PublisherDisplayName>" + publisherDisplayName + L"</PublisherDisplayName>\r\n";
    manifest += L"    <Logo>Assets\\StoreLogo.png</Logo>\r\n";
    manifest += L"  </Properties>\r\n\r\n";
    manifest += L"  <Dependencies>\r\n";
    manifest += L"    <TargetDeviceFamily Name=\"Windows.Desktop\"\r\n";
    manifest += L"                        MinVersion=\"" + minVersion + L"\"\r\n";
    manifest += L"                        MaxVersionTested=\"" + maxVersionTested + L"\" />\r\n";
    manifest += L"  </Dependencies>\r\n\r\n";
    manifest += L"  <Resources>\r\n";
    manifest += L"    <Resource Language=\"en-us\" />\r\n";
    manifest += L"  </Resources>\r\n\r\n";
    manifest += L"  <Applications>\r\n";
    manifest += L"    <Application Id=\"" + appId + L"\"\r\n";
    manifest += L"                 Executable=\"" + executable + L"\"\r\n";
    manifest += L"                 EntryPoint=\"Windows.FullTrustApplication\">\r\n\r\n";
    manifest += L"      <uap:VisualElements\r\n";
    manifest += L"        DisplayName=\"" + displayName + L"\"\r\n";
    manifest += L"        Description=\"" + description + L"\"\r\n";
    manifest += L"        Square150x150Logo=\"Assets\\MediumTile.png\"\r\n";
    manifest += L"        Square44x44Logo=\"Assets\\Square44x44Logo.png\"\r\n";
    manifest += L"        BackgroundColor=\"transparent\">\r\n\r\n";
    manifest += L"        <uap:DefaultTile\r\n";
    manifest += L"          Square71x71Logo=\"Assets\\SmallTile.png\"\r\n";
    manifest += L"          Wide310x150Logo=\"Assets\\WideTile.png\"\r\n";
    manifest += L"          Square310x310Logo=\"Assets\\LargeTile.png\">\r\n\r\n";
    manifest += L"          <uap:ShowNameOnTiles>\r\n";
    manifest += L"            <uap:ShowOn Tile=\"square150x150Logo\" />\r\n";
    manifest += L"            <uap:ShowOn Tile=\"wide310x150Logo\" />\r\n";
    manifest += L"            <uap:ShowOn Tile=\"square310x310Logo\" />\r\n";
    manifest += L"          </uap:ShowNameOnTiles>\r\n\r\n";
    manifest += L"        </uap:DefaultTile>\r\n\r\n";
    manifest += L"      </uap:VisualElements>\r\n";
    manifest += L"    </Application>\r\n";
    manifest += L"  </Applications>\r\n\r\n";
    manifest += L"  <Capabilities>\r\n";
    manifest += L"    <rescap:Capability Name=\"runFullTrust\" />\r\n";
    manifest += L"  </Capabilities>\r\n\r\n";
    manifest += L"</Package>\r\n";
    return manifest;
}

static bool EnsureAppxManifest(std::wstring& manifestPath, bool& created, DWORD& error)
{
    manifestPath = GetExeDir() + L"\\AppxManifest.xml";
    created = false;
    error = ERROR_SUCCESS;

    DWORD attrs = GetFileAttributesW(manifestPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0)
        {
            if (ManifestOverwriteExisting())
            {
                if (WriteUtf8BomTextFile(manifestPath, BuildDefaultAppxManifest()))
                {
                    Log(g_ui.appxManifestOverwritten.c_str(), manifestPath.c_str());
                    return true;
                }

                error = GetLastError();
                if (error == ERROR_SUCCESS)
                    error = ERROR_WRITE_FAULT;
                return false;
            }
            Log(g_ui.appxManifestExisting.c_str(), manifestPath.c_str());
            return true;
        }
        error = ERROR_ALREADY_EXISTS;
        SetLastError(error);
        return false;
    }

    if (WriteUtf8BomTextFile(manifestPath, BuildDefaultAppxManifest()))
    {
        created = true;
        return true;
    }

    error = GetLastError();
    if (error == ERROR_SUCCESS)
        error = ERROR_WRITE_FAULT;
    return false;
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

static std::wstring FormatWideV(const wchar_t* fmt, va_list ap)
{
    if (!fmt)
        return L"";

    va_list measure;
    va_copy(measure, ap);
    int needed = _vscwprintf(fmt, measure);
    va_end(measure);

    if (needed < 0)
        return L"";

    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, L'\0');
    _vsnwprintf_s(buf.data(), buf.size(), _TRUNCATE, fmt, ap);
    std::wstring result(buf.data());
    return result;
}

std::wstring FormatWide(const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::wstring result = FormatWideV(fmt, ap);
    va_end(ap);
    return result;
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
    nid.uTimeout = static_cast<UINT>(ClampInt(g_notificationTimeoutMs.load(), 1000, 60000));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid))
        LogWin32Failure(L"Shell_NotifyIconW(NIM_MODIFY)");
}

static bool NormalizeIniToUtf8BomIfNeeded()
{
    return NormalizeTextFileToUtf8Bom(g_iniPath);
}

void EnsureIniDefaults()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
    {
        QueueStartupWarning(L"[!] Failed to read INI while applying setting defaults (error " + std::to_wstring(GetLastError()) + L").");
        return;
    }

    bool haveText = true;
    auto doc = ParseIniDocument(iniText);
    bool changed = false;

    for (const auto& d : g_defaults)
    {
        std::wstring value;
        if (!haveText || !ReadIniValueFromDoc(doc, d.section, d.key, value))
        {
            if (!WriteIniValueToText(iniText, d.section, d.key, d.value))
            {
                QueueStartupWarning(L"[!] Failed to update INI setting default: [" + std::wstring(d.section ? d.section : L"") + L"] " + std::wstring(d.key ? d.key : L""));
                return;
            }
            changed = true;
            doc = ParseIniDocument(iniText);
            haveText = true;
        }
    }

    if (changed && !WriteUtf8BomTextFile(g_iniPath, iniText))
    {
        QueueStartupWarning(L"[!] Failed to write INI setting defaults (error " + std::to_wstring(GetLastError()) + L").");
    }
    else if (changed)
    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(iniText);
    }
}


void LoadUiStrings()
{
    g_ui.generalTitle = IniReadS(L"Strings", L"GeneralTitle", L"General:");
    g_ui.loggingTitle = IniReadS(L"Strings", L"LoggingTitle", L"Logging:");
    g_ui.wallpaperFittingTitle = IniReadS(L"Strings", L"WallpaperFittingTitle", L"Wallpaper Fitting:");
    g_ui.methodsTitle = IniReadS(L"Strings", L"MethodsTitle", L"Methods:");
    g_ui.assetsTitle = IniReadS(L"Strings", L"AssetsTitle", L"Assets:");
    g_ui.dpiScalesTitle = IniReadS(L"Strings", L"DpiScalesTitle", L"DPI Scales:");

    g_ui.listenWallpaper = IniReadS(L"Strings", L"ListenWallpaper", L"Listen Wallpaper");
    g_ui.trayIcon = IniReadS(L"Strings", L"TrayIcon", L"Tray Icon (this session)");
    g_ui.usePowerShell = IniReadS(L"Strings", L"UsePowerShell", L"PowerShell only");
    g_ui.generateOnStartup = IniReadS(L"Strings", L"GenerateOnStartup", L"Generate on Startup");
    g_ui.generateOnStartupOnlyWhenChanged = IniReadS(L"Strings", L"GenerateOnStartupOnlyWhenChanged", L"Generate on Startup only when changed");
    g_ui.hideDisabledEntries = IniReadS(L"Strings", L"HideDisabledEntries", L"Hide disabled entries");
    g_ui.generateDesktopIconForDisabledEntries = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries");
    g_ui.deleteDisabledAssets = IniReadS(L"Strings", L"DeleteDisabledAssets", L"Auto-delete disabled asset files");
    g_ui.wallpaperDetectionFallback = IniReadS(L"Strings", L"WallpaperDetectionFallback", L"Fallback when selected path is invalid");
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
    g_ui.shutdownPendingTitle = IniReadS(L"Strings", L"ShutdownPendingTitle", L"Shutdown pending:");
    g_ui.shutdownPendingReason = IniReadS(L"Strings", L"ShutdownPendingReason", L"Waiting for generation/registration: %s");
    g_ui.cancelShutdown = IniReadS(L"Strings", L"CancelShutdown", L"Cancel shutdown");
    g_ui.forceShutdown = IniReadS(L"Strings", L"ForceShutdown", L"Force shutdown");
    g_ui.shutdownDelayedNotification = IniReadS(L"Strings", L"ShutdownDelayedNotification", L"Exit is waiting for generation/registration to finish: %s");
    g_ui.shutdownRequested = IniReadS(L"Strings", L"ShutdownRequested", L"[i] Shutdown requested.");
    g_ui.shutdownCancelled = IniReadS(L"Strings", L"ShutdownCancelled", L"[i] Shutdown cancelled.");
    g_ui.forceShutdownRequested = IniReadS(L"Strings", L"ForceShutdownRequested", L"[!] Force shutdown requested.");

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
    g_ui.wallpaperImageLoaded = IniReadS(L"Strings", L"WallpaperImageLoaded", L"[i] Wallpaper image loaded (%d x %d).");
    g_ui.renderModeSummary = IniReadS(L"Strings", L"RenderModeSummary", L"[i] Render fit mode: %s");
    g_ui.assetsDirectoryReady = IniReadS(L"Strings", L"AssetsDirectoryReady", L"[i] Assets directory ready: %s");
    g_ui.failedLoadWallpaper = IniReadS(L"Strings", L"FailedLoadWallpaper", L"Failed to load wallpaper image.");
    g_ui.iniEncodingNormalized = IniReadS(L"Strings", L"IniEncodingNormalized", L"[i] INI file normalized to UTF-8 with BOM.");
    g_ui.skippingAssetDisabled = IniReadS(L"Strings", L"SkippingAssetDisabled", L"Skipping %s (disabled in settings).");
    g_ui.generatedDesktopIconAsset = IniReadS(L"Strings", L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s");
    g_ui.activeScales = IniReadS(L"Strings", L"ActiveScales", L"[i] Active scales: %s");
    g_ui.savedAsset = IniReadS(L"Strings", L"SavedAsset", L"Saved %s");
    g_ui.savedScaledAsset = IniReadS(L"Strings", L"SavedScaledAsset", L"Saved %s (scale %d%%)");
    g_ui.failedSaveAsset = IniReadS(L"Strings", L"FailedSaveAsset", L"Failed to save %s");
    g_ui.failedSaveScaledAsset = IniReadS(L"Strings", L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)");
    g_ui.savePngInvalidInput = IniReadS(L"Strings", L"SavePngInvalidInput", L"[!] SavePNG skipped for %s: invalid bitmap, path, or PNG encoder.");
    g_ui.savePngGdiFailure = IniReadS(L"Strings", L"SavePngGdiFailure", L"[!] SavePNG GDI+ save failed for %s (status=%d).");
    g_ui.savePngReplaceFailed = IniReadS(L"Strings", L"SavePngReplaceFailed", L"[!] SavePNG replace failed for %s (temp=%s).");
    g_ui.deletedDisabledAsset = IniReadS(L"Strings", L"DeletedDisabledAsset", L"[i] Deleted disabled asset file: %s");
    g_ui.failedDeleteDisabledAsset = IniReadS(L"Strings", L"FailedDeleteDisabledAsset", L"[!] Failed to delete disabled asset file: %s");

    g_ui.reRegisteringManifest = IniReadS(L"Strings", L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets.");
    g_ui.appxManifestCreated = IniReadS(L"Strings", L"AppxManifestCreated", L"[i] Created AppxManifest.xml: %s");
    g_ui.appxManifestExisting = IniReadS(L"Strings", L"AppxManifestExisting", L"[i] Using existing AppxManifest.xml: %s");
    g_ui.appxManifestOverwritten = IniReadS(L"Strings", L"AppxManifestOverwritten", L"[i] Updated AppxManifest.xml from configured defaults: %s");
    g_ui.appxManifestCreateFailed = IniReadS(L"Strings", L"AppxManifestCreateFailed", L"[!] Failed to create AppxManifest.xml: %s");
    g_ui.manifestPath = IniReadS(L"Strings", L"ManifestPath", L"Manifest path: %s");
    g_ui.usingComRegistration = IniReadS(L"Strings", L"UsingComRegistration", L"Using COM Appx registration...");
    g_ui.comRegistrationUri = IniReadS(L"Strings", L"ComRegistrationUri", L"[i] COM registration URI: %s");
    g_ui.comRegistrationTimeoutDetails = IniReadS(L"Strings", L"ComRegistrationTimeoutDetails", L"[i] COM registration timeout=%d ms, cancel wait=%d ms");
    g_ui.invalidManifestPath = IniReadS(L"Strings", L"InvalidManifestPath", L"Invalid manifest path.");
    g_ui.comRegistrationSuccess = IniReadS(L"Strings", L"ComRegistrationSuccess", L"COM registration success.");
    g_ui.comRegistrationTimedOut = IniReadS(L"Strings", L"ComRegistrationTimedOut", L"COM registration timed out.");
    g_ui.comRegistrationFailed = IniReadS(L"Strings", L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration.");
    g_ui.comRegistrationCancelStillRunning = IniReadS(L"Strings", L"ComRegistrationCancelStillRunning", L"COM registration is still running after cancel; PowerShell fallback skipped to avoid concurrent registration.");
    g_ui.comRegistrationFallbackSkippedAfterTimeout = IniReadS(L"Strings", L"ComRegistrationFallbackSkippedAfterTimeout", L"COM registration timeout is unresolved; PowerShell fallback skipped to avoid concurrent registration.");
    g_ui.comRegistrationException = IniReadS(L"Strings", L"ComRegistrationException", L"COM registration threw exception: 0x%08X");
    g_ui.comExceptionMessage = IniReadS(L"Strings", L"ComExceptionMessage", L"COM message: %s");
    g_ui.launchingPowerShellRegistration = IniReadS(L"Strings", L"LaunchingPowerShellRegistration", L"Launching PowerShell registration...");
    g_ui.powerShellCommand = IniReadS(L"Strings", L"PowerShellCommand", L"Command: %s");
    g_ui.powerShellLaunchDetails = IniReadS(L"Strings", L"PowerShellLaunchDetails", L"[i] PowerShell executable: %s (timeout=%d ms, poll=%d ms, terminate wait=%d ms)");
    g_ui.powerShellProcessStarted = IniReadS(L"Strings", L"PowerShellProcessStarted", L"[i] PowerShell process started (pid=%lu).");
    g_ui.powerShellTerminateRequested = IniReadS(L"Strings", L"PowerShellTerminateRequested", L"[!] Terminating timed-out PowerShell process (pid=%lu).");
    g_ui.powerShellTerminateFailed = IniReadS(L"Strings", L"PowerShellTerminateFailed", L"[!] Failed to terminate timed-out PowerShell process (pid=%lu).");
    g_ui.powerShellStillRunningAfterTimeout = IniReadS(L"Strings", L"PowerShellStillRunningAfterTimeout", L"[!] PowerShell process is still running after terminate wait (pid=%lu).");
    g_ui.powerShellTerminatedAfterTimeout = IniReadS(L"Strings", L"PowerShellTerminatedAfterTimeout", L"[i] PowerShell process terminated after timeout (pid=%lu).");
    g_ui.powerShellCompleted = IniReadS(L"Strings", L"PowerShellCompleted", L"PowerShell registration completed successfully.");
    g_ui.powerShellTimedOut = IniReadS(L"Strings", L"PowerShellTimedOut", L"PowerShell registration timed out.");
    g_ui.powerShellErrorSideloadDisabled = IniReadS(L"Strings", L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!");
    g_ui.powerShellErrorCode = IniReadS(L"Strings", L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X.");
    g_ui.powerShellOutputFollows = IniReadS(L"Strings", L"PowerShellOutputFollows", L"PowerShell output follows:");
    g_ui.powerShellRegistrationFailed = IniReadS(L"Strings", L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully.");
    g_ui.assetGenerationFinished = IniReadS(L"Strings", L"AssetGenerationFinished", L"Asset generation and registration finished successfully.");
    g_ui.assetGenerationFailed = IniReadS(L"Strings", L"AssetGenerationFailed", L"Asset generation did not complete successfully.");
    g_ui.appRegistrationFailed = IniReadS(L"Strings", L"AppRegistrationFailed", L"App registration did not complete successfully.");
    g_ui.skippingRegistrationDueToAssetFailure = IniReadS(L"Strings", L"SkippingRegistrationDueToAssetFailure", L"[!] Skipping Appx registration because asset generation failed.");
    g_ui.createAssetsDirectoryOperation = IniReadS(L"Strings", L"CreateAssetsDirectoryOperation", L"Create Assets directory");
    g_ui.missingManifestAssets = IniReadS(L"Strings", L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing or invalid: %s");
    g_ui.notificationMissingManifestAssets = IniReadS(L"Strings", L"NotificationMissingManifestAssets", L"Registration failed; one or more manifest assets are missing or invalid.");
    g_ui.registrationBlockedUnresolved = IniReadS(L"Strings", L"RegistrationBlockedUnresolved", L"[!] Registration skipped because a previous registration is unresolved: %s");
    g_ui.registrationUnresolvedMarked = IniReadS(L"Strings", L"RegistrationUnresolvedMarked", L"[!] Registration marked unresolved: %s");
    g_ui.registrationUnresolvedCleared = IniReadS(L"Strings", L"RegistrationUnresolvedCleared", L"[i] Previous unresolved registration finished: %s");
    g_ui.registrationUnresolvedExpired = IniReadS(L"Strings", L"RegistrationUnresolvedExpired", L"[!] Previous unresolved registration block expired; allowing registration again: %s");

    g_ui.changeConfirmed = IniReadS(L"Strings", L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce.");
    g_ui.wallpaperAndFitChangeDetected = IniReadS(L"Strings", L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected.");
    g_ui.wallpaperChangeDetected = IniReadS(L"Strings", L"WallpaperChangeDetected", L"Wallpaper change detected.");
    g_ui.wallpaperDetectionEmptyDuringPoll = IniReadS(L"Strings", L"WallpaperDetectionEmptyDuringPoll", L"[!] Wallpaper change ignored: wallpaper detection returned empty.");
    g_ui.wallpaperDetectionMethodFailed = IniReadS(L"Strings", L"WallpaperDetectionMethodFailed", L"[!] Wallpaper detection method %s did not return a wallpaper path.");
    g_ui.wallpaperDetectionMethodReturnedInvalid = IniReadS(L"Strings", L"WallpaperDetectionMethodReturnedInvalid", L"[!] Wallpaper detection method %s returned an unusable path: %s");
    g_ui.wallpaperDetectionFallbackSelected = IniReadS(L"Strings", L"WallpaperDetectionFallbackSelected", L"[i] Wallpaper detection fallback selected %s: %s");
    g_ui.wallpaperDetectionFallbackDisabled = IniReadS(L"Strings", L"WallpaperDetectionFallbackDisabled", L"[!] Wallpaper detection fallback disabled; using %s result as-is.");
    g_ui.fitModeChangeDetected = IniReadS(L"Strings", L"FitModeChangeDetected", L"Fit mode change detected.");
    g_ui.dpiScaleChangeDetected = IniReadS(L"Strings", L"DpiScaleChangeDetected", L"DPI scale change detected.");
    g_ui.enabledText = IniReadS(L"Strings", L"EnabledText", L"enabled");
    g_ui.disabledText = IniReadS(L"Strings", L"DisabledText", L"disabled");
    g_ui.onText = IniReadS(L"Strings", L"OnText", L"on");
    g_ui.offText = IniReadS(L"Strings", L"OffText", L"off");

    g_ui.trayTip = IniReadS(L"Strings", L"TrayTip", L"Desktop Tile Generator");
    g_ui.exeLabel = IniReadS(L"Strings", L"ExeLabel", L"[i] EXE: %s");
    g_ui.iniLabel = IniReadS(L"Strings", L"IniLabel", L"[i] INI: %s");
    g_ui.logFileLabel = IniReadS(L"Strings", L"LogFileLabel", L"[i] Log file: %s");
    g_ui.timingLog = IniReadS(L"Strings", L"TimingLog", L"[i] Timing: %s = %d ms");
    g_ui.timingPreLogInitialization = IniReadS(L"Strings", L"TimingPreLogInitialization", L"pre-log initialization");
    g_ui.timingIniDefaultStringLoading = IniReadS(L"Strings", L"TimingIniDefaultStringLoading", L"INI/default/string loading");
    g_ui.timingGdiStartup = IniReadS(L"Strings", L"TimingGdiStartup", L"GDI+ startup");
    g_ui.timingAssetGeneration = IniReadS(L"Strings", L"TimingAssetGeneration", L"asset generation");
    g_ui.timingAppRegistration = IniReadS(L"Strings", L"TimingAppRegistration", L"app registration");
    g_ui.timingGenerationTotal = IniReadS(L"Strings", L"TimingGenerationTotal", L"generation total");
    g_ui.powerShellRegistrationSummary = IniReadS(L"Strings", L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s");
    g_ui.usePowerShellSummary = IniReadS(L"Strings", L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)");
    g_ui.wallpaperDetectionFallbackSummary = IniReadS(L"Strings", L"WallpaperDetectionFallbackSummary", L"[i] Wallpaper detection fallback on invalid path: %s");
    g_ui.trayIconSummary = IniReadS(L"Strings", L"TrayIconSummary", L"[i] Tray icon: %s");
    g_ui.consoleSummary = IniReadS(L"Strings", L"ConsoleSummary", L"[i] Console %s.");
    g_ui.loggingSummary = IniReadS(L"Strings", L"LoggingSummary", L"[i] Logging %s.");
    g_ui.generateOnStartupSummary = IniReadS(L"Strings", L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s");
    g_ui.generateOnStartupOnlyWhenChangedSummary = IniReadS(L"Strings", L"GenerateOnStartupOnlyWhenChangedSummary", L"[i] Startup generation only when changed: %s");
    g_ui.hideDisabledEntriesSummary = IniReadS(L"Strings", L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s");
    g_ui.generateDesktopIconForDisabledEntriesSummary = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s");
    g_ui.deleteDisabledAssetsSummary = IniReadS(L"Strings", L"DeleteDisabledAssetsSummary", L"[i] Auto-delete disabled asset files: %s");
    g_ui.generateScaleAutoSummary = IniReadS(L"Strings", L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s");
    g_ui.generateScaleSummary = IniReadS(L"Strings", L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s");
    g_ui.showMenuAsDropdownSummary = IniReadS(L"Strings", L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s");
    g_ui.notificationsSummary = IniReadS(L"Strings", L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)");
    g_ui.powerShellEnabledMode = IniReadS(L"Strings", L"PowerShellEnabledMode", L"PowerShell enabled");
    g_ui.comPreferredMode = IniReadS(L"Strings", L"ComPreferredMode", L"COM preferred with PowerShell fallback");
    g_ui.consoleAllocated = IniReadS(L"Strings", L"ConsoleAllocated", L"[i] Console allocated.");
    g_ui.startupGenerationEnabled = IniReadS(L"Strings", L"StartupGenerationEnabled", L"[i] Startup generation enabled.");
    g_ui.startupGenerationSkippedUnchanged = IniReadS(L"Strings", L"StartupGenerationSkippedUnchanged", L"[i] Startup generation skipped: wallpaper/config unchanged.");
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
    g_ui.gdiPlusOperationFailed = IniReadS(L"Strings", L"GdiPlusOperationFailed", L"[!] GDI+ %s failed (status=%d).");
    g_ui.win32FailureFmt = IniReadS(L"Strings", L"Win32FailureFmt", L"[!] %s failed (error %lu: %s)");
    g_ui.logPathChanged = IniReadS(L"Strings", L"LogPathChanged", L"[i] Log path changed to: %s");
    g_ui.logPathRenamed = IniReadS(L"Strings", L"LogPathRenamed", L"[i] Log path renamed to: %s");
    g_ui.logPathReset = IniReadS(L"Strings", L"LogPathReset", L"[i] Log path reset to default: %s");
    g_ui.logWriteFailed = IniReadS(L"Strings", L"LogWriteFailed", L"[!] Log file write failed (error %lu: %s): %s");
    g_ui.iniWriteFailed = IniReadS(L"Strings", L"IniWriteFailed", L"[!] Failed to write INI value [%s] %s (error %lu: %s).");
    g_ui.listenFitAutoDisabled = IniReadS(L"Strings", L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting.");
    g_ui.singleInstanceScopeSummary = IniReadS(L"Strings", L"SingleInstanceScopeSummary", L"[i] Single-instance scope: %s");
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
    g_ui.pollInitialDebounceBypassMsLabel = IniReadS(L"Strings", L"PollInitialDebounceBypassMsLabel", L"Initial debounce bypass: %d ms");
    g_ui.iniCacheRefreshMsLabel = IniReadS(L"Strings", L"IniCacheRefreshMsLabel", L"INI cache refresh: %d ms");
    g_ui.notificationTimeoutMsLabel = IniReadS(L"Strings", L"NotificationTimeoutMsLabel", L"Notification timeout: %d ms");
    g_ui.shutdownInitialNoticeMsLabel = IniReadS(L"Strings", L"ShutdownInitialNoticeMsLabel", L"Shutdown first notice: %d ms");
    g_ui.shutdownRepeatNoticeMsLabel = IniReadS(L"Strings", L"ShutdownRepeatNoticeMsLabel", L"Shutdown repeat notice: %d ms");
    g_ui.singleInstanceSignalRetriesLabel = IniReadS(L"Strings", L"SingleInstanceSignalRetriesLabel", L"Single-instance signal retries: %d");
    g_ui.singleInstanceSignalDelayMsLabel = IniReadS(L"Strings", L"SingleInstanceSignalDelayMsLabel", L"Single-instance signal delay: %d ms");
    g_ui.comRegistrationTimeoutMsLabel = IniReadS(L"Strings", L"ComRegistrationTimeoutMsLabel", L"COM registration timeout: %d ms");
    g_ui.comRegistrationCancelWaitMsLabel = IniReadS(L"Strings", L"ComRegistrationCancelWaitMsLabel", L"COM cancel wait: %d ms");
    g_ui.trayIconHiddenDelayMsLabel = IniReadS(L"Strings", L"TrayIconHiddenDelayMsLabel", L"Tray hide warning delay: %d ms");
    g_ui.logBufferLinesLabel = IniReadS(L"Strings", L"LogBufferLinesLabel", L"Log buffer lines: %d");
    g_ui.powerShellExeLabel = IniReadS(L"Strings", L"PowerShellExeLabel", L"PowerShell executable: %s");
    g_ui.powerShellPollMsLabel = IniReadS(L"Strings", L"PowerShellPollMsLabel", L"PowerShell poll: %d ms");
    g_ui.powerShellTimeoutMsLabel = IniReadS(L"Strings", L"PowerShellTimeoutMsLabel", L"PowerShell timeout: %d ms");
    g_ui.powerShellTerminateWaitMsLabel = IniReadS(L"Strings", L"PowerShellTerminateWaitMsLabel", L"PowerShell terminate wait: %d ms");
    g_ui.registrationUnresolvedBlockMsLabel = IniReadS(L"Strings", L"RegistrationUnresolvedBlockMsLabel", L"Unresolved registration block: %d ms");
    g_ui.manifestIdentityNameLabel = IniReadS(L"Strings", L"ManifestIdentityNameLabel", L"Manifest identity: %s");
    g_ui.manifestPublisherLabel = IniReadS(L"Strings", L"ManifestPublisherLabel", L"Manifest publisher: %s");
    g_ui.manifestPackageVersionLabel = IniReadS(L"Strings", L"ManifestPackageVersionLabel", L"Manifest version: %s");
    g_ui.manifestDisplayNameLabel = IniReadS(L"Strings", L"ManifestDisplayNameLabel", L"Manifest display name: %s");
    g_ui.manifestPublisherDisplayNameLabel = IniReadS(L"Strings", L"ManifestPublisherDisplayNameLabel", L"Manifest publisher display: %s");
    g_ui.manifestDescriptionLabel = IniReadS(L"Strings", L"ManifestDescriptionLabel", L"Manifest description: %s");
    g_ui.manifestExecutableLabel = IniReadS(L"Strings", L"ManifestExecutableLabel", L"Manifest executable: %s");
    g_ui.manifestApplicationIdLabel = IniReadS(L"Strings", L"ManifestApplicationIdLabel", L"Manifest app id: %s");
    g_ui.manifestMinVersionLabel = IniReadS(L"Strings", L"ManifestMinVersionLabel", L"Manifest min version: %s");
    g_ui.manifestMaxVersionTestedLabel = IniReadS(L"Strings", L"ManifestMaxVersionTestedLabel", L"Manifest max tested: %s");
    g_ui.manifestSourceLabel = IniReadS(L"Strings", L"ManifestSourceLabel", L"Manifest source: %s");
    g_ui.manifestSourceExisting = IniReadS(L"Strings", L"ManifestSourceExisting", L"existing AppxManifest.xml");
    g_ui.manifestSourceGenerated = IniReadS(L"Strings", L"ManifestSourceGenerated", L"generated from INI if missing");
    g_ui.manifestOverwriteExistingLabel = IniReadS(L"Strings", L"ManifestOverwriteExistingLabel", L"Manifest overwrite existing: %s");
    g_ui.wallpaperDetectionMethodLabel = IniReadS(L"Strings", L"WallpaperDetectionMethodLabel", L"Wallpaper detection method: %s");
    g_ui.singleInstanceFailureActionLabel = IniReadS(L"Strings", L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s");
    g_ui.singleInstanceActionIgnore = IniReadS(L"Strings", L"SingleInstanceActionIgnore", L"Ignore");
    g_ui.singleInstanceActionWarn = IniReadS(L"Strings", L"SingleInstanceActionWarn", L"Warn");
    g_ui.singleInstanceActionExit = IniReadS(L"Strings", L"SingleInstanceActionExit", L"Exit");
    g_ui.singleInstanceActionCrash = IniReadS(L"Strings", L"SingleInstanceActionCrash", L"Crash");
    g_ui.unknownError = IniReadS(L"Strings", L"UnknownError", L"Unknown error");

    g_ui.logFileFilter = IniReadS(L"Strings", L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||");
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
    {
        QueueStartupWarning(L"[!] Invalid format tokens in [Strings] " + std::wstring(key ? key : L"") + L"; using built-in default for this session.");
        text = FindStringDefault(key, L"");
    }
}

static void ValidateFormatStrings()
{
    RequireFormat(g_ui.exeLabel, L"ExeLabel", { L"%s" });
    RequireFormat(g_ui.iniLabel, L"IniLabel", { L"%s" });
    RequireFormat(g_ui.logFileLabel, L"LogFileLabel", { L"%s" });
    RequireFormat(g_ui.timingLog, L"TimingLog", { L"%s", L"%d" });
    RequireFormat(g_ui.powerShellRegistrationSummary, L"PowerShellRegistrationSummary", { L"%s" });
    RequireFormat(g_ui.usePowerShellSummary, L"UsePowerShellSummary", { L"%d", L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackSummary, L"WallpaperDetectionFallbackSummary", { L"%s" });
    RequireFormat(g_ui.trayIconSummary, L"TrayIconSummary", { L"%s" });
    RequireFormat(g_ui.consoleSummary, L"ConsoleSummary", { L"%s" });
    RequireFormat(g_ui.loggingSummary, L"LoggingSummary", { L"%s" });
    RequireFormat(g_ui.generateOnStartupSummary, L"GenerateOnStartupSummary", { L"%s" });
    RequireFormat(g_ui.generateOnStartupOnlyWhenChangedSummary, L"GenerateOnStartupOnlyWhenChangedSummary", { L"%s" });
    RequireFormat(g_ui.hideDisabledEntriesSummary, L"HideDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.generateDesktopIconForDisabledEntriesSummary, L"GenerateDesktopIconForDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.deleteDisabledAssetsSummary, L"DeleteDisabledAssetsSummary", { L"%s" });
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
    RequireFormat(g_ui.gdiPlusOperationFailed, L"GdiPlusOperationFailed", { L"%s", L"%d" });
    RequireFormat(g_ui.win32FailureFmt, L"Win32FailureFmt", { L"%s", L"%lu", L"%s" });
    RequireFormat(g_ui.logPathChanged, L"LogPathChanged", { L"%s" });
    RequireFormat(g_ui.logPathRenamed, L"LogPathRenamed", { L"%s" });
    RequireFormat(g_ui.logPathReset, L"LogPathReset", { L"%s" });
    RequireFormat(g_ui.logWriteFailed, L"LogWriteFailed", { L"%lu", L"%s", L"%s" });
    RequireFormat(g_ui.iniWriteFailed, L"IniWriteFailed", { L"%s", L"%s", L"%lu", L"%s" });
    RequireFormat(g_ui.singleInstanceScopeSummary, L"SingleInstanceScopeSummary", { L"%s" });
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
    RequireFormat(g_ui.pollInitialDebounceBypassMsLabel, L"PollInitialDebounceBypassMsLabel", { L"%d" });
    RequireFormat(g_ui.iniCacheRefreshMsLabel, L"IniCacheRefreshMsLabel", { L"%d" });
    RequireFormat(g_ui.notificationTimeoutMsLabel, L"NotificationTimeoutMsLabel", { L"%d" });
    RequireFormat(g_ui.shutdownInitialNoticeMsLabel, L"ShutdownInitialNoticeMsLabel", { L"%d" });
    RequireFormat(g_ui.shutdownRepeatNoticeMsLabel, L"ShutdownRepeatNoticeMsLabel", { L"%d" });
    RequireFormat(g_ui.singleInstanceSignalRetriesLabel, L"SingleInstanceSignalRetriesLabel", { L"%d" });
    RequireFormat(g_ui.singleInstanceSignalDelayMsLabel, L"SingleInstanceSignalDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.comRegistrationTimeoutMsLabel, L"ComRegistrationTimeoutMsLabel", { L"%d" });
    RequireFormat(g_ui.comRegistrationCancelWaitMsLabel, L"ComRegistrationCancelWaitMsLabel", { L"%d" });
    RequireFormat(g_ui.trayIconHiddenDelayMsLabel, L"TrayIconHiddenDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.logBufferLinesLabel, L"LogBufferLinesLabel", { L"%d" });
    RequireFormat(g_ui.powerShellExeLabel, L"PowerShellExeLabel", { L"%s" });
    RequireFormat(g_ui.powerShellPollMsLabel, L"PowerShellPollMsLabel", { L"%d" });
    RequireFormat(g_ui.powerShellTimeoutMsLabel, L"PowerShellTimeoutMsLabel", { L"%d" });
    RequireFormat(g_ui.powerShellTerminateWaitMsLabel, L"PowerShellTerminateWaitMsLabel", { L"%d" });
    RequireFormat(g_ui.registrationUnresolvedBlockMsLabel, L"RegistrationUnresolvedBlockMsLabel", { L"%d" });
    RequireFormat(g_ui.manifestIdentityNameLabel, L"ManifestIdentityNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestPublisherLabel, L"ManifestPublisherLabel", { L"%s" });
    RequireFormat(g_ui.manifestPackageVersionLabel, L"ManifestPackageVersionLabel", { L"%s" });
    RequireFormat(g_ui.manifestDisplayNameLabel, L"ManifestDisplayNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestPublisherDisplayNameLabel, L"ManifestPublisherDisplayNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestDescriptionLabel, L"ManifestDescriptionLabel", { L"%s" });
    RequireFormat(g_ui.manifestExecutableLabel, L"ManifestExecutableLabel", { L"%s" });
    RequireFormat(g_ui.manifestApplicationIdLabel, L"ManifestApplicationIdLabel", { L"%s" });
    RequireFormat(g_ui.manifestMinVersionLabel, L"ManifestMinVersionLabel", { L"%s" });
    RequireFormat(g_ui.manifestMaxVersionTestedLabel, L"ManifestMaxVersionTestedLabel", { L"%s" });
    RequireFormat(g_ui.manifestSourceLabel, L"ManifestSourceLabel", { L"%s" });
    RequireFormat(g_ui.manifestOverwriteExistingLabel, L"ManifestOverwriteExistingLabel", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionMethodLabel, L"WallpaperDetectionMethodLabel", { L"%s" });
    RequireFormat(g_ui.singleInstanceFailureActionLabel, L"SingleInstanceFailureActionLabel", { L"%s" });
    RequireFormat(g_ui.shutdownPendingReason, L"ShutdownPendingReason", { L"%s" });
    RequireFormat(g_ui.shutdownDelayedNotification, L"ShutdownDelayedNotification", { L"%s" });

    RequireFormat(g_ui.startingWallpaperGeneration, L"StartingWallpaperGeneration", { L"%s" });
    RequireFormat(g_ui.wallpaperSource, L"WallpaperSource", { L"%s" });
    RequireFormat(g_ui.wallpaperImageLoaded, L"WallpaperImageLoaded", { L"%d", L"%d" });
    RequireFormat(g_ui.renderModeSummary, L"RenderModeSummary", { L"%s" });
    RequireFormat(g_ui.assetsDirectoryReady, L"AssetsDirectoryReady", { L"%s" });
    RequireFormat(g_ui.skippingAssetDisabled, L"SkippingAssetDisabled", { L"%s" });
    RequireFormat(g_ui.generatedDesktopIconAsset, L"GeneratedDesktopIconAsset", { L"%s" });
    RequireFormat(g_ui.activeScales, L"ActiveScales", { L"%s" });
    RequireFormat(g_ui.savedAsset, L"SavedAsset", { L"%s" });
    RequireFormat(g_ui.savedScaledAsset, L"SavedScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.failedSaveAsset, L"FailedSaveAsset", { L"%s" });
    RequireFormat(g_ui.failedSaveScaledAsset, L"FailedSaveScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.savePngInvalidInput, L"SavePngInvalidInput", { L"%s" });
    RequireFormat(g_ui.savePngGdiFailure, L"SavePngGdiFailure", { L"%s", L"%d" });
    RequireFormat(g_ui.savePngReplaceFailed, L"SavePngReplaceFailed", { L"%s", L"%s" });
    RequireFormat(g_ui.deletedDisabledAsset, L"DeletedDisabledAsset", { L"%s" });
    RequireFormat(g_ui.failedDeleteDisabledAsset, L"FailedDeleteDisabledAsset", { L"%s" });
    RequireFormat(g_ui.appxManifestCreated, L"AppxManifestCreated", { L"%s" });
    RequireFormat(g_ui.appxManifestExisting, L"AppxManifestExisting", { L"%s" });
    RequireFormat(g_ui.appxManifestOverwritten, L"AppxManifestOverwritten", { L"%s" });
    RequireFormat(g_ui.appxManifestCreateFailed, L"AppxManifestCreateFailed", { L"%s" });
    RequireFormat(g_ui.manifestPath, L"ManifestPath", { L"%s" });
    RequireFormat(g_ui.comRegistrationUri, L"ComRegistrationUri", { L"%s" });
    RequireFormat(g_ui.comRegistrationTimeoutDetails, L"ComRegistrationTimeoutDetails", { L"%d", L"%d" });
    RequireFormat(g_ui.comRegistrationException, L"ComRegistrationException", { L"%08X" });
    RequireFormat(g_ui.comExceptionMessage, L"ComExceptionMessage", { L"%s" });
    RequireFormat(g_ui.powerShellCommand, L"PowerShellCommand", { L"%s" });
    RequireFormat(g_ui.powerShellLaunchDetails, L"PowerShellLaunchDetails", { L"%s", L"%d", L"%d", L"%d" });
    RequireFormat(g_ui.powerShellProcessStarted, L"PowerShellProcessStarted", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminateRequested, L"PowerShellTerminateRequested", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminateFailed, L"PowerShellTerminateFailed", { L"%lu" });
    RequireFormat(g_ui.powerShellStillRunningAfterTimeout, L"PowerShellStillRunningAfterTimeout", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminatedAfterTimeout, L"PowerShellTerminatedAfterTimeout", { L"%lu" });
    RequireFormat(g_ui.powerShellErrorCode, L"PowerShellErrorCode", { L"%08X" });
    RequireFormat(g_ui.missingManifestAssets, L"MissingManifestAssets", { L"%s" });
    RequireFormat(g_ui.registrationBlockedUnresolved, L"RegistrationBlockedUnresolved", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedMarked, L"RegistrationUnresolvedMarked", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedCleared, L"RegistrationUnresolvedCleared", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedExpired, L"RegistrationUnresolvedExpired", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionMethodFailed, L"WallpaperDetectionMethodFailed", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionMethodReturnedInvalid, L"WallpaperDetectionMethodReturnedInvalid", { L"%s", L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackSelected, L"WallpaperDetectionFallbackSelected", { L"%s", L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackDisabled, L"WallpaperDetectionFallbackDisabled", { L"%s" });
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
    return IniReadI(L"Settings", L"UsePowerShell", 0) != 0;
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

static constexpr int SUPPORTED_ASSET_SCALES[] = { 100, 125, 150, 200, 400 };

static int SnapToSupportedScale(int percent)
{
    for (int scale : SUPPORTED_ASSET_SCALES)
    {
        if (percent <= scale)
            return scale;
    }
    return 400;
}

static HMONITOR GetPrimaryMonitorHandle()
{
    struct MonitorSearch
    {
        HMONITOR primary = nullptr;
    } search;

    EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR monitor, HDC, LPRECT, LPARAM param) -> BOOL
        {
            auto* search = reinterpret_cast<MonitorSearch*>(param);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(monitor, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY))
            {
                search->primary = monitor;
                return FALSE;
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));

    if (search.primary)
        return search.primary;
    return MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
}

static bool GetPrimaryMonitorRect(RECT& rect)
{
    HMONITOR monitor = GetPrimaryMonitorHandle();
    if (!monitor)
        return false;

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi))
        return false;

    rect = mi.rcMonitor;
    return true;
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
            HMONITOR monitor = GetPrimaryMonitorHandle();
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

static bool DeleteFileIfExistsForDisabledAsset(const std::wstring& path)
{
    if (!PathFileExistsW(path.c_str()))
        return true;

    if (DeleteFileW(path.c_str()))
    {
        Log(g_ui.deletedDisabledAsset.c_str(), path.c_str());
        return true;
    }

    Log(g_ui.failedDeleteDisabledAsset.c_str(), path.c_str());
    LogWin32Failure(L"DeleteFileW");
    return false;
}

static bool DeleteDisabledAssetFiles(const std::wstring& basePath)
{
    bool ok = DeleteFileIfExistsForDisabledAsset(basePath);
    for (int scale : SUPPORTED_ASSET_SCALES)
        ok = DeleteFileIfExistsForDisabledAsset(ScaleAssetPath(basePath, scale)) && ok;
    return ok;
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

enum class WallpaperDetectionMethod
{
    TranscodedImageCache,
    SystemParametersInfo,
    DesktopWallpaperCOM,
    TranscodedWallpaperFile,
    Auto
};

static const wchar_t* WallpaperDetectionMethodName(WallpaperDetectionMethod method)
{
    switch (method)
    {
    case WallpaperDetectionMethod::SystemParametersInfo: return L"SystemParametersInfo";
    case WallpaperDetectionMethod::DesktopWallpaperCOM: return L"DesktopWallpaperCOM";
    case WallpaperDetectionMethod::TranscodedWallpaperFile: return L"TranscodedWallpaperFile";
    case WallpaperDetectionMethod::Auto: return L"Auto";
    case WallpaperDetectionMethod::TranscodedImageCache:
    default:
        return L"TranscodedImageCache";
    }
}

static WallpaperDetectionMethod ParseWallpaperDetectionMethod(const std::wstring& value)
{
    std::wstring v = ToLowerCopy(TrimCopy(value));
    if (v == L"auto")
        return WallpaperDetectionMethod::Auto;
    if (v == L"systemparametersinfo" || v == L"spi" || v == L"spi_getdeskwallpaper")
        return WallpaperDetectionMethod::SystemParametersInfo;
    if (v == L"desktopwallpapercom" || v == L"desktopwallpaper" || v == L"idesktopwallpaper" || v == L"com")
        return WallpaperDetectionMethod::DesktopWallpaperCOM;
    if (v == L"transcodedwallpaperfile" || v == L"transcodedwallpaper" || v == L"themefile" || v == L"cachedfile")
        return WallpaperDetectionMethod::TranscodedWallpaperFile;
    return WallpaperDetectionMethod::TranscodedImageCache;
}

static WallpaperDetectionMethod CurrentWallpaperDetectionMethod()
{
    return ParseWallpaperDetectionMethod(
        IniReadS(L"Settings", L"WallpaperDetectionMethod", L"TranscodedImageCache"));
}

static std::wstring GetWallpaperFromTranscodedImageCache()
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
    size_t byteLen = sz - 24;
    size_t len = byteLen / sizeof(wchar_t);
    if (len == 0)
        return L"";

    std::wstring value(len, L'\0');
    memcpy(value.data(), buf.data() + 24, len * sizeof(wchar_t));
    value.resize(wcsnlen(value.c_str(), value.size()));
    return value;
}

static std::wstring GetWallpaperFromSystemParametersInfo()
{
    std::vector<wchar_t> buf(32768);
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, (UINT)buf.size(), buf.data(), 0))
        return L"";
    return buf[0] ? std::wstring(buf.data()) : L"";
}

static std::wstring GetWallpaperFromDesktopWallpaperCOM()
{
    HRESULT initHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool uninitialize = SUCCEEDED(initHr);
    if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
        return L"";

    IDesktopWallpaper* desktopWallpaper = nullptr;
    HRESULT createHr = CoCreateInstance(
        CLSID_DesktopWallpaper,
        nullptr,
        CLSCTX_ALL,
        IID_PPV_ARGS(&desktopWallpaper));

    std::wstring result;
    if (SUCCEEDED(createHr) && desktopWallpaper)
    {
        LPWSTR wallpaper = nullptr;
        HRESULT getHr = desktopWallpaper->GetWallpaper(nullptr, &wallpaper);
        if (SUCCEEDED(getHr) && wallpaper && *wallpaper)
            result = wallpaper;
        CoTaskMemFree(wallpaper);
        desktopWallpaper->Release();
    }

    if (uninitialize)
        CoUninitialize();
    return result;
}

static std::wstring GetWallpaperFromTranscodedWallpaperFile()
{
    wchar_t appData[MAX_PATH] = {};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appData)))
        return L"";

    std::wstring path = std::wstring(appData) + L"\\Microsoft\\Windows\\Themes\\TranscodedWallpaper";
    return PathFileExistsW(path.c_str()) ? path : L"";
}

static bool IsUsableWallpaperPath(const std::wstring& path)
{
    if (path.empty())
        return false;

    DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring GetWallpaperByMethod(WallpaperDetectionMethod method)
{
    switch (method)
    {
    case WallpaperDetectionMethod::SystemParametersInfo:
        return GetWallpaperFromSystemParametersInfo();
    case WallpaperDetectionMethod::DesktopWallpaperCOM:
        return GetWallpaperFromDesktopWallpaperCOM();
    case WallpaperDetectionMethod::TranscodedWallpaperFile:
        return GetWallpaperFromTranscodedWallpaperFile();
    case WallpaperDetectionMethod::TranscodedImageCache:
    default:
        return GetWallpaperFromTranscodedImageCache();
    }
}

static std::wstring TryWallpaperMethod(WallpaperDetectionMethod method, bool logFailure)
{
    std::wstring wallpaper = GetWallpaperByMethod(method);
    if (IsUsableWallpaperPath(wallpaper))
        return wallpaper;

    if (logFailure)
    {
        const wchar_t* name = WallpaperDetectionMethodName(method);
        if (wallpaper.empty())
            Log(g_ui.wallpaperDetectionMethodFailed.c_str(), name);
        else
            Log(g_ui.wallpaperDetectionMethodReturnedInvalid.c_str(), name, wallpaper.c_str());
    }
    return L"";
}

static std::wstring FindWallpaperByFallback(WallpaperDetectionMethod skip)
{
    const WallpaperDetectionMethod order[] = {
        WallpaperDetectionMethod::TranscodedImageCache,
        WallpaperDetectionMethod::DesktopWallpaperCOM,
        WallpaperDetectionMethod::SystemParametersInfo,
        WallpaperDetectionMethod::TranscodedWallpaperFile
    };

    for (WallpaperDetectionMethod candidate : order)
    {
        if (candidate == skip)
            continue;

        std::wstring wallpaper = TryWallpaperMethod(candidate, true);
        if (!wallpaper.empty())
        {
            Log(g_ui.wallpaperDetectionFallbackSelected.c_str(), WallpaperDetectionMethodName(candidate), wallpaper.c_str());
            return wallpaper;
        }
    }

    return L"";
}

std::wstring GetWallpaper()
{
    WallpaperDetectionMethod method = CurrentWallpaperDetectionMethod();
    switch (method)
    {
    case WallpaperDetectionMethod::Auto:
    {
        std::wstring wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::TranscodedImageCache, true);
        if (!wallpaper.empty()) return wallpaper;
        wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::DesktopWallpaperCOM, true);
        if (!wallpaper.empty()) return wallpaper;
        wallpaper = TryWallpaperMethod(WallpaperDetectionMethod::SystemParametersInfo, true);
        if (!wallpaper.empty()) return wallpaper;
        return TryWallpaperMethod(WallpaperDetectionMethod::TranscodedWallpaperFile, true);
    }
    case WallpaperDetectionMethod::TranscodedImageCache:
    case WallpaperDetectionMethod::SystemParametersInfo:
    case WallpaperDetectionMethod::DesktopWallpaperCOM:
    case WallpaperDetectionMethod::TranscodedWallpaperFile:
    default:
    {
        std::wstring wallpaper = GetWallpaperByMethod(method);
        if (IsUsableWallpaperPath(wallpaper))
            return wallpaper;

        const wchar_t* methodName = WallpaperDetectionMethodName(method);
        if (wallpaper.empty())
            Log(g_ui.wallpaperDetectionMethodFailed.c_str(), methodName);
        else
            Log(g_ui.wallpaperDetectionMethodReturnedInvalid.c_str(), methodName, wallpaper.c_str());

        if (!g_wallpaperDetectionFallbackOnInvalid)
        {
            Log(g_ui.wallpaperDetectionFallbackDisabled.c_str(), methodName);
            return wallpaper;
        }

        return FindWallpaperByFallback(method);
    }
    }
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

static bool GdiOk(Status status, const wchar_t* operation)
{
    if (status == Ok)
        return true;

    Log(g_ui.gdiPlusOperationFailed.c_str(), operation ? operation : L"operation", static_cast<int>(status));
    return false;
}

static bool BitmapOk(Bitmap* bitmap, const wchar_t* operation)
{
    return bitmap && GdiOk(bitmap->GetLastStatus(), operation);
}

static bool GraphicsOk(Graphics& graphics, const wchar_t* operation)
{
    return GdiOk(graphics.GetLastStatus(), operation);
}

static Bitmap* ResizeBitmapToMode(Bitmap* src, int w, int h, FitMode mode, Color background)
{
    if (!src) return nullptr;

    int sw = src->GetWidth();
    int sh = src->GetHeight();
    if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
        return nullptr;

    auto* out = new Bitmap(w, h, PixelFormat32bppARGB);
    if (!BitmapOk(out, L"create resized bitmap"))
    {
        delete out;
        return nullptr;
    }

    Graphics g(out);
    if (!GraphicsOk(g, L"create resize graphics"))
    {
        delete out;
        return nullptr;
    }

    if (!GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set resize interpolation") ||
        !GdiOk(g.Clear(background), L"clear resized bitmap"))
    {
        delete out;
        return nullptr;
    }

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

    if (!GdiOk(g.DrawImage(src, Rect(dx, dy, dw, dh), 0, 0, sw, sh, UnitPixel), L"draw resized bitmap"))
    {
        delete out;
        return nullptr;
    }
    return out;
}

static bool GetVirtualDesktopRect(RECT& rect)
{
    int left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int width = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int height = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if (width <= 0 || height <= 0)
    {
        left = 0;
        top = 0;
        width = GetSystemMetrics(SM_CXSCREEN);
        height = GetSystemMetrics(SM_CYSCREEN);
    }
    if (width <= 0 || height <= 0)
        return false;

    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + height;
    return true;
}

static int FirstTileStart(int targetStart, int tileOrigin, int tileSize)
{
    int delta = targetStart - tileOrigin;
    int rem = delta % tileSize;
    if (rem < 0)
        rem += tileSize;
    return targetStart - rem;
}

static Bitmap* BuildTiledPrimaryBitmap(Bitmap* src)
{
    if (!src) return nullptr;

    int sw = src->GetWidth();
    int sh = src->GetHeight();
    if (sw <= 0 || sh <= 0)
        return nullptr;

    RECT primaryRect{};
    if (!GetPrimaryMonitorRect(primaryRect))
    {
        primaryRect.left = 0;
        primaryRect.top = 0;
        primaryRect.right = GetSystemMetrics(SM_CXSCREEN);
        primaryRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT virtualRect{};
    if (!GetVirtualDesktopRect(virtualRect))
        virtualRect = primaryRect;

    int primaryW = primaryRect.right - primaryRect.left;
    int primaryH = primaryRect.bottom - primaryRect.top;
    if (primaryW <= 0 || primaryH <= 0)
        return nullptr;

    auto* out = new Bitmap(primaryW, primaryH, PixelFormat32bppARGB);
    if (!BitmapOk(out, L"create tiled bitmap"))
    {
        delete out;
        return nullptr;
    }

    Graphics g(out);
    if (!GraphicsOk(g, L"create tiled graphics") ||
        !GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set tiled interpolation") ||
        !GdiOk(g.Clear(Color(255, 0, 0, 0)), L"clear tiled bitmap"))
    {
        delete out;
        return nullptr;
    }

    TextureBrush brush(src, WrapModeTile);
    Matrix transform;
    transform.Translate(
        static_cast<REAL>(virtualRect.left - primaryRect.left),
        static_cast<REAL>(virtualRect.top - primaryRect.top));
    if (brush.GetLastStatus() == Ok &&
        brush.SetTransform(&transform) == Ok &&
        g.FillRectangle(&brush, 0, 0, primaryW, primaryH) == Ok)
    {
        return out;
    }

    int firstX = FirstTileStart(primaryRect.left, virtualRect.left, sw);
    int firstY = FirstTileStart(primaryRect.top, virtualRect.top, sh);
    for (int y = firstY; y < primaryRect.bottom; y += sh)
    {
        for (int x = firstX; x < primaryRect.right; x += sw)
        {
            if (!GdiOk(g.DrawImage(src, x - primaryRect.left, y - primaryRect.top, sw, sh), L"draw tiled bitmap"))
            {
                delete out;
                return nullptr;
            }
        }
    }

    return out;
}

static Bitmap* BuildSpannedPrimaryBitmap(Bitmap* src)
{
    if (!src) return nullptr;

    RECT primaryRect{};
    if (!GetPrimaryMonitorRect(primaryRect))
    {
        primaryRect.left = 0;
        primaryRect.top = 0;
        primaryRect.right = GetSystemMetrics(SM_CXSCREEN);
        primaryRect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    RECT virtualRect{};
    if (!GetVirtualDesktopRect(virtualRect))
        virtualRect = primaryRect;

    int primaryW = primaryRect.right - primaryRect.left;
    int primaryH = primaryRect.bottom - primaryRect.top;
    int virtualW = virtualRect.right - virtualRect.left;
    int virtualH = virtualRect.bottom - virtualRect.top;
    if (primaryW <= 0 || primaryH <= 0 || virtualW <= 0 || virtualH <= 0)
        return nullptr;

    Bitmap* virtualWallpaper = ResizeBitmapToMode(src, virtualW, virtualH, FitMode::Fill, Color(255, 0, 0, 0));
    if (!virtualWallpaper)
        return nullptr;

    if (primaryW == virtualW && primaryH == virtualH &&
        primaryRect.left == virtualRect.left && primaryRect.top == virtualRect.top)
    {
        return virtualWallpaper;
    }

    auto* out = new Bitmap(primaryW, primaryH, PixelFormat32bppARGB);
    if (!BitmapOk(out, L"create spanned bitmap"))
    {
        delete virtualWallpaper;
        delete out;
        return nullptr;
    }

    Graphics g(out);
    if (!GraphicsOk(g, L"create spanned graphics") ||
        !GdiOk(g.SetInterpolationMode(InterpolationModeHighQualityBicubic), L"set spanned interpolation") ||
        !GdiOk(g.Clear(Color(255, 0, 0, 0)), L"clear spanned bitmap") ||
        !GdiOk(g.DrawImage(
        virtualWallpaper,
        Rect(0, 0, primaryW, primaryH),
        primaryRect.left - virtualRect.left,
        primaryRect.top - virtualRect.top,
        primaryW,
        primaryH,
        UnitPixel), L"draw spanned bitmap"))
    {
        delete virtualWallpaper;
        delete out;
        return nullptr;
    }
    delete virtualWallpaper;

    return out;
}

Bitmap* ResizeWithFit(Bitmap* src, int w, int h, FitMode mode)
{
    if (mode != FitMode::Tile && mode != FitMode::Span)
        return ResizeBitmapToMode(src, w, h, mode, Color(255, 0, 0, 0));

    Bitmap* primaryWallpaper = (mode == FitMode::Tile)
        ? BuildTiledPrimaryBitmap(src)
        : BuildSpannedPrimaryBitmap(src);
    if (!primaryWallpaper)
        return nullptr;

    Bitmap* out = ResizeBitmapToMode(primaryWallpaper, w, h, FitMode::Fill, Color(255, 0, 0, 0));
    delete primaryWallpaper;
    return out;
}

bool SavePNG(Bitmap* b, const wchar_t* f)
{
    const wchar_t* pathForLog = (f && *f) ? f : L"(empty path)";
    if (!b || !f || !*f || !FindPngEncoder())
    {
        Log(g_ui.savePngInvalidInput.c_str(), pathForLog);
        return false;
    }

    std::wstring path(f);
    std::wstring tempPath = MakeTempSiblingPath(path, L"png");
    DeleteFileW(tempPath.c_str());

    Status saveStatus = b->Save(tempPath.c_str(), &g_png, nullptr);
    if (saveStatus != Ok)
    {
        DeleteFileW(tempPath.c_str());
        Log(g_ui.savePngGdiFailure.c_str(), path.c_str(), static_cast<int>(saveStatus));
        return false;
    }

    if (!MoveFileExW(tempPath.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD err = GetLastError();
        DeleteFileW(tempPath.c_str());
        Log(g_ui.savePngReplaceFailed.c_str(), path.c_str(), tempPath.c_str());
        LogWin32Failure(L"MoveFileExW", err);
        return false;
    }

    return true;
}

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

    // Clone before releasing the stream; GDI+ bitmaps can retain stream-backed state.
    Bitmap* loaded = Bitmap::FromStream(stream);
    Bitmap* bmp = nullptr;
    if (loaded && loaded->GetLastStatus() == Ok)
    {
        Rect bounds(0, 0, loaded->GetWidth(), loaded->GetHeight());
        bmp = loaded->Clone(bounds, PixelFormat32bppARGB);
    }
    delete loaded;
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
static void MarkRegistrationUnresolved(const std::wstring& reason, HANDLE processHandle, DWORD processId);

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

static std::wstring PowerShellUtf8Preamble()
{
    return L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; "
        L"$OutputEncoding = [System.Text.Encoding]::UTF8; ";
}

static std::wstring QuoteCommandLineArg(const std::wstring& arg)
{
    std::wstring out = L"\"";
    size_t slashCount = 0;
    for (wchar_t ch : arg)
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
        }
        else
        {
            out.append(slashCount, L'\\');
            out.push_back(ch);
        }
        slashCount = 0;
    }
    out.append(slashCount * 2, L'\\');
    out.push_back(L'"');
    return out;
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
    std::wstring powerShellExe = IniReadS(L"Settings", L"PowerShellExe", L"powershell.exe");
    if (TrimCopy(powerShellExe).empty())
        powerShellExe = L"powershell.exe";
    const int timeoutMs = IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000);
    const int pollMs = IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000);
    const int terminateWaitMs = IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000);
    Log(g_ui.powerShellLaunchDetails.c_str(), powerShellExe.c_str(), timeoutMs, pollMs, terminateWaitMs);

    std::wstring cmdline = QuoteCommandLineArg(powerShellExe) + L" -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + PowerShellEncodedCommand(PowerShellUtf8Preamble() + cmd);

    BOOL ok = CreateProcessW(nullptr, &cmdline[0], nullptr,nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr,nullptr, &si, &pi);

    CloseHandle(w);
    if (!ok)
    {
        LogWin32Failure(L"CreateProcessW");
        CloseHandle(r);
        return false;
    }
    Log(g_ui.powerShellProcessStarted.c_str(), pi.dwProcessId);

    char buf[2048];
    std::vector<BYTE> outBytes;

    auto appendOutputBytes = [&](const char* data, DWORD bytes)
    {
        outBytes.insert(outBytes.end(), reinterpret_cast<const BYTE*>(data), reinterpret_cast<const BYTE*>(data) + bytes);
    };

    auto drainPipe = [&]()
    {
        for (;;)
        {
            DWORD available = 0;
            if (!PeekNamedPipe(r, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                break;

            DWORD toRead = std::min<DWORD>(available, sizeof(buf));
            DWORD n = 0;
            if (!ReadFile(r, buf, toRead, &n, nullptr) || n == 0)
                break;

            appendOutputBytes(buf, n);
        }
    };

    const auto psStart = steady_clock::now();
    bool timedOut = false;
    bool timeoutExitConfirmed = false;

    for (;;)
    {
        drainPipe();

        DWORD waitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(pollMs));
        if (waitResult == WAIT_OBJECT_0)
        {
            drainPipe();
            break;
        }
        if (waitResult == WAIT_FAILED)
        {
            LogWin32Failure(L"WaitForSingleObject");
            break;
        }
        if (timeoutMs > 0 && ElapsedMs(psStart, steady_clock::now()) >= timeoutMs)
        {
            timedOut = true;
            Log(g_ui.powerShellTerminateRequested.c_str(), pi.dwProcessId);
            if (!TerminateProcess(pi.hProcess, WAIT_TIMEOUT))
            {
                Log(g_ui.powerShellTerminateFailed.c_str(), pi.dwProcessId);
                LogWin32Failure(L"TerminateProcess");
            }
            if (terminateWaitMs > 0)
            {
                DWORD terminateWaitResult = WaitForSingleObject(pi.hProcess, static_cast<DWORD>(terminateWaitMs));
                if (terminateWaitResult == WAIT_OBJECT_0)
                {
                    timeoutExitConfirmed = true;
                    Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
                }
                else if (terminateWaitResult == WAIT_TIMEOUT)
                    Log(g_ui.powerShellStillRunningAfterTimeout.c_str(), pi.dwProcessId);
                else if (terminateWaitResult == WAIT_FAILED)
                    LogWin32Failure(L"WaitForSingleObject(PowerShell terminate)");
            }
            else if (WaitForSingleObject(pi.hProcess, 0) == WAIT_OBJECT_0)
            {
                timeoutExitConfirmed = true;
                Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
            }
            drainPipe();
            break;
        }
    }

    CloseHandle(r);
    DWORD ec=0; GetExitCodeProcess(pi.hProcess,&ec);
    if (timedOut)
        ec = WAIT_TIMEOUT;

    if (timedOut && !timeoutExitConfirmed)
    {
        DWORD finalWait = WaitForSingleObject(pi.hProcess, 0);
        if (finalWait == WAIT_OBJECT_0)
        {
            timeoutExitConfirmed = true;
            Log(g_ui.powerShellTerminatedAfterTimeout.c_str(), pi.dwProcessId);
        }
        else
        {
            MarkRegistrationUnresolved(g_ui.powerShellTimedOut, pi.hProcess, pi.dwProcessId);
            pi.hProcess = nullptr;
        }
    }

    if (pi.hProcess)
        CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wstring out = DecodeProcessOutput(outBytes);
    std::wstring psMsg;
    bool psErr = false;
    if (ec == 0 && !timedOut) {
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

    if (timedOut) {
        psMsg = g_ui.powerShellTimedOut;
        {
            std::lock_guard<std::mutex> lk(g_psMutex);
            g_psOut = out;
            g_psCode = ec;
            g_psErr = psErr;
            g_psMsg = psMsg;
        }
        LogText(g_ui.powerShellTimedOut);
    } else if (out.find(L"0x80073CFF") != std::wstring::npos) {
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

enum class AppxRegistrationResult
{
    Success,
    Failed,
    TimeoutUncertain
};

struct WinrtApartmentGuard
{
    bool initialized = false;

    explicit WinrtApartmentGuard(winrt::apartment_type type)
    {
        winrt::init_apartment(type);
        initialized = true;
    }

    ~WinrtApartmentGuard()
    {
        if (initialized)
            winrt::uninit_apartment();
    }

    WinrtApartmentGuard(const WinrtApartmentGuard&) = delete;
    WinrtApartmentGuard& operator=(const WinrtApartmentGuard&) = delete;
};

AppxRegistrationResult Appx_Register_COM(const std::wstring& manifestPath)
{
    try
    {
        LogText(g_ui.usingComRegistration);

        WinrtApartmentGuard apartment(winrt::apartment_type::single_threaded);

        using namespace winrt::Windows::Management::Deployment;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Foundation::Collections;

        PackageManager pm;

        std::wstring uriStr = MakeFileUri(manifestPath);
        if (uriStr.empty())
        {
            LogText(g_ui.invalidManifestPath);
            return AppxRegistrationResult::Failed;
        }
        Log(g_ui.comRegistrationUri.c_str(), uriStr.c_str());
        Uri uri{ winrt::hstring(uriStr) };

        IVector<Uri> deps = winrt::single_threaded_vector<Uri>();

        auto op = pm.RegisterPackageAsync(
            uri,
            deps,
            DeploymentOptions::ForceUpdateFromAnyVersion
        );

        int timeoutMs = IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000);
        int cancelWaitMs = IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000);
        Log(g_ui.comRegistrationTimeoutDetails.c_str(), timeoutMs, cancelWaitMs);
        if (timeoutMs > 0)
        {
            auto status = op.wait_for(std::chrono::milliseconds(timeoutMs));
            if (status == winrt::Windows::Foundation::AsyncStatus::Started)
            {
                op.Cancel();
                LogText(g_ui.comRegistrationTimedOut);

                if (cancelWaitMs <= 0)
                    return AppxRegistrationResult::TimeoutUncertain;

                status = op.wait_for(std::chrono::milliseconds(cancelWaitMs));
                if (status == winrt::Windows::Foundation::AsyncStatus::Started)
                {
                    LogText(g_ui.comRegistrationCancelStillRunning);
                    return AppxRegistrationResult::TimeoutUncertain;
                }
            }
            if (status == winrt::Windows::Foundation::AsyncStatus::Canceled)
            {
                Log(g_ui.comRegistrationStatusFailed.c_str(), (int)status);
                return AppxRegistrationResult::Failed;
            }
        }

        op.get(); // wait/throw if Windows deployment reports an error

        if (op.Status() == winrt::Windows::Foundation::AsyncStatus::Completed)
        {
            LogText(g_ui.comRegistrationSuccess);
            return AppxRegistrationResult::Success;
        }
        else
        {
            Log(g_ui.comRegistrationStatusFailed.c_str(), (int)op.Status());
            return AppxRegistrationResult::Failed;
        }
    }
    catch (const winrt::hresult_error& e)
    {
        Log(g_ui.comRegistrationException.c_str(), e.code().value);
        Log(g_ui.comExceptionMessage.c_str(), e.message().c_str());
        return AppxRegistrationResult::Failed;
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

static bool IsValidGeneratedPng(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (size.QuadPart == 0)
        return false;

    Bitmap probe(path.c_str());
    return probe.GetLastStatus() == Ok && probe.GetWidth() > 0 && probe.GetHeight() > 0;
}

static std::wstring FindMissingManifestAssets(const std::wstring& exeDir)
{
    std::wstring missing;
    for (const auto& t : g_tiles)
    {
        std::wstring path = exeDir + L"\\" + t.file;
        if (!IsValidGeneratedPng(path))
        {
            if (!missing.empty())
                missing += L", ";
            missing += t.file;
        }
    }
    return missing;
}

static void ClearRegistrationUnresolvedLocked()
{
    if (g_registrationUnresolvedProcess)
    {
        CloseHandle(g_registrationUnresolvedProcess);
        g_registrationUnresolvedProcess = nullptr;
    }
    g_registrationUnresolved = false;
    g_registrationUnresolvedPid = 0;
    g_registrationUnresolvedReason.clear();
    g_registrationUnresolvedSince = steady_clock::time_point{};
}

static void MarkRegistrationUnresolved(const std::wstring& reason, HANDLE processHandle, DWORD processId)
{
    HANDLE oldHandle = nullptr;
    std::wstring storedReason = reason.empty() ? L"unknown registration state" : reason;
    {
        std::lock_guard<std::mutex> lk(g_registrationUnresolvedMutex);
        oldHandle = g_registrationUnresolvedProcess;
        g_registrationUnresolved = true;
        g_registrationUnresolvedProcess = processHandle;
        g_registrationUnresolvedPid = processId;
        g_registrationUnresolvedSince = steady_clock::now();
        g_registrationUnresolvedReason = storedReason;
    }

    if (oldHandle && oldHandle != processHandle)
        CloseHandle(oldHandle);

    Log(g_ui.registrationUnresolvedMarked.c_str(), storedReason.c_str());
}

static bool RegistrationBlockedByUnresolved(std::wstring& reason)
{
    std::wstring clearedReason;
    std::wstring expiredReason;
    bool blocked = false;

    {
        std::lock_guard<std::mutex> lk(g_registrationUnresolvedMutex);
        if (!g_registrationUnresolved)
            return false;

        bool stillUnresolved = true;
        if (g_registrationUnresolvedProcess)
        {
            DWORD waitResult = WaitForSingleObject(g_registrationUnresolvedProcess, 0);
            if (waitResult == WAIT_OBJECT_0)
            {
                clearedReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
            else if (waitResult == WAIT_FAILED)
            {
                clearedReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
        }

        if (stillUnresolved)
        {
            int blockMs = IniReadClampedI(L"Settings", L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000);
            if (blockMs <= 0 || ElapsedMs(g_registrationUnresolvedSince, steady_clock::now()) >= blockMs)
            {
                expiredReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
        }

        if (stillUnresolved)
        {
            reason = g_registrationUnresolvedReason;
            if (g_registrationUnresolvedPid)
                reason += L" (pid=" + std::to_wstring(g_registrationUnresolvedPid) + L")";
            blocked = true;
        }
    }

    if (!clearedReason.empty())
        Log(g_ui.registrationUnresolvedCleared.c_str(), clearedReason.c_str());
    if (!expiredReason.empty())
        Log(g_ui.registrationUnresolvedExpired.c_str(), expiredReason.c_str());

    return blocked;
}

static bool StartupGeneratedAssetsPresent(const std::wstring& exeDir)
{
    std::vector<int> scales = GetConfiguredScales();
    for (const auto& t : g_tiles)
    {
        std::wstring basePath = exeDir + L"\\" + t.file;

        bool generatedByCurrentConfig =
            IniReadI(L"Assets", t.name, 0) != 0 ||
            g_generateDesktopIconForDisabledEntries;
        if (!generatedByCurrentConfig)
            continue;

        if (!IsValidGeneratedPng(basePath))
            return false;

        for (int scale : scales)
        {
            std::wstring scaledPath = ScaleAssetPath(basePath, scale);
            if (!IsValidGeneratedPng(scaledPath))
                return false;
        }
    }
    return true;
}

static void HashWide(uint64_t& hash, const std::wstring& text)
{
    static constexpr uint64_t FNV_PRIME = 1099511628211ull;
    const BYTE* bytes = reinterpret_cast<const BYTE*>(text.data());
    size_t len = text.size() * sizeof(wchar_t);
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    hash ^= 0xff;
    hash *= FNV_PRIME;
}

static std::wstring Hex64(uint64_t value)
{
    wchar_t buf[32];
    swprintf(buf, _countof(buf), L"%016llX", static_cast<unsigned long long>(value));
    return buf;
}

static std::wstring FileFingerprint(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return path + L"|missing";

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    ULARGE_INTEGER time{};
    time.LowPart = data.ftLastWriteTime.dwLowDateTime;
    time.HighPart = data.ftLastWriteTime.dwHighDateTime;

    return path + L"|size=" + Hex64(size.QuadPart) + L"|mtime=" + Hex64(time.QuadPart);
}

static void AppendRectFingerprint(std::wstring& out, const wchar_t* name, const RECT& rect)
{
    out += name;
    out += L"=" + IntToWString(rect.left) + L"," + IntToWString(rect.top) + L"," +
        IntToWString(rect.right) + L"," + IntToWString(rect.bottom) + L";";
}

static std::wstring BuildGenerationStateKey(const std::wstring& wallpaperPath)
{
    std::wstring state;
    state += L"v=2;";
    state += L"wallpaper=" + FileFingerprint(wallpaperPath) + L";";
    state += L"exe=" + FileFingerprint(g_exePath) + L";";
    state += L"manifest=" + FileFingerprint(GetExeDir() + L"\\AppxManifest.xml") + L";";
    state += L"method=" + std::wstring(WallpaperDetectionMethodName(CurrentWallpaperDetectionMethod())) + L";";
    state += L"methodFallback=" + IntToWString(g_wallpaperDetectionFallbackOnInvalid ? 1 : 0) + L";";
    state += L"usePowerShell=" + IntToWString(UsePowerShell() ? 1 : 0) + L";";
    state += L"disableFitting=" + IntToWString(g_disableFitting ? 1 : 0) + L";";
    state += L"fit=" + IntToWString(static_cast<int>(g_disableFitting ? FitMode::Fill : GetWallpaperFit())) + L";";
    state += L"desktopIconForDisabled=" + IntToWString(g_generateDesktopIconForDisabledEntries ? 1 : 0) + L";";
    state += L"deleteDisabledAssets=" + IntToWString(g_deleteDisabledAssets ? 1 : 0) + L";";
    state += L"scaleAuto=" + IntToWString(g_generateScaleAuto ? 1 : 0) + L";";

    std::vector<int> scales = GetConfiguredScales();
    state += L"scales=";
    for (int scale : scales)
        state += IntToWString(scale) + L",";
    state += L";";

    RECT primaryRect{};
    if (GetPrimaryMonitorRect(primaryRect))
        AppendRectFingerprint(state, L"primary", primaryRect);
    RECT virtualRect{};
    if (GetVirtualDesktopRect(virtualRect))
        AppendRectFingerprint(state, L"virtual", virtualRect);

    for (const auto& t : g_tiles)
        state += std::wstring(t.name) + L"=" + IntToWString(IniReadI(L"Assets", t.name, 0) ? 1 : 0) + L";";

    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, state);
    return Hex64(hash);
}

static bool StartupGenerationCanSkip(const std::wstring& wallpaperPath)
{
    if (!g_generateOnStartupOnlyWhenChanged)
        return false;

    std::wstring last = IniReadS(L"State", L"LastSuccessfulGenerationKey", L"");
    if (last.empty() || !IEquals(last, BuildGenerationStateKey(wallpaperPath)))
        return false;

    return StartupGeneratedAssetsPresent(GetExeDir());
}

static void StoreSuccessfulGenerationState(const std::wstring& wallpaperPath)
{
    std::wstring key = BuildGenerationStateKey(wallpaperPath);
    if (!key.empty())
        IniWrite(L"State", L"LastSuccessfulGenerationKey", key.c_str());
}

bool Generate(const wchar_t* wp, const wchar_t* reason = nullptr)
{
    auto generationStart = steady_clock::now();
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
    Log(g_ui.wallpaperImageLoaded.c_str(), static_cast<int>(src->GetWidth()), static_cast<int>(src->GetHeight()));

    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    Bitmap* preparedWallpaper = nullptr;
    Bitmap* renderSource = src;
    FitMode renderMode = mode;
    if (mode == FitMode::Tile || mode == FitMode::Span)
    {
        preparedWallpaper = (mode == FitMode::Tile)
            ? BuildTiledPrimaryBitmap(src)
            : BuildSpannedPrimaryBitmap(src);
        if (preparedWallpaper)
        {
            renderSource = preparedWallpaper;
            renderMode = FitMode::Fill;
        }
        else
        {
            renderMode = FitMode::Fill;
        }
    }
    Log(g_ui.renderModeSummary.c_str(), FitModeToString(renderMode));

    std::wstring exeDir = GetExeDir();
    std::wstring assetsDir = exeDir + L"\\Assets";
    DWORD assetsDirError = ERROR_SUCCESS;
    bool assetsDirReady = EnsureDirectoryExists(assetsDir, &assetsDirError);
    if (!assetsDirReady)
        LogWin32Failure(g_ui.createAssetsDirectoryOperation.c_str(), assetsDirError);
    else
        Log(g_ui.assetsDirectoryReady.c_str(), assetsDir.c_str());

    bool assetsOk = assetsDirReady;
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
    if (assetsDirReady)
    {
        for (auto& t : g_tiles)
        {
            std::wstring outPath = exeDir + L"\\" + t.file;
            if (!IniReadI(L"Assets", t.name, 0))
            {
                if (!g_generateDesktopIconForDisabledEntries)
                {
                    Log(g_ui.skippingAssetDisabled.c_str(), t.name);
                    if (g_deleteDisabledAssets && !DeleteDisabledAssetFiles(outPath))
                        assetsOk = false;
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

            auto* o = ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0));
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
                auto* scaled = ResizeBitmapToMode(renderSource, scaledW, scaledH, renderMode, Color(255, 0, 0, 0));
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
    }
    delete preparedWallpaper;
    delete src;

    auto assetsDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(), g_ui.timingAssetGeneration.c_str(), ElapsedMs(generationStart, assetsDone));

    if (!assetsOk)
    {
        auto generationDone = steady_clock::now();
        LogText(g_ui.skippingRegistrationDueToAssetFailure);
        Log(g_ui.timingLog.c_str(), g_ui.timingAppRegistration.c_str(), 0);
        Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, generationDone));
        LogText(g_ui.assetGenerationFailed);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
        return false;
    }

    LogText(g_ui.reRegisteringManifest);

    std::wstring manifestPath;
    bool manifestCreated = false;
    DWORD manifestError = ERROR_SUCCESS;
    bool manifestReady = EnsureAppxManifest(manifestPath, manifestCreated, manifestError);
    if (manifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), manifestPath.c_str());
    if (!manifestReady)
    {
        Log(g_ui.appxManifestCreateFailed.c_str(), manifestPath.c_str());
        LogWin32Failure(L"Create AppxManifest.xml", manifestError);
    }
    else
    {
        Log(g_ui.manifestPath.c_str(), manifestPath.c_str());
    }

    bool registrationOk = false;
    std::wstring registrationFailureMessage;
    auto setPowerShellFailureMessage = [&]()
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        registrationFailureMessage = g_psMsg.empty() ? g_ui.powerShellRegistrationFailed : g_psMsg;
    };

    if (!manifestReady)
    {
        registrationOk = false;
        registrationFailureMessage = FormatWide(g_ui.appxManifestCreateFailed.c_str(), manifestPath.c_str());
    }
    else
    {
        std::wstring unresolvedReason;
        if (RegistrationBlockedByUnresolved(unresolvedReason))
        {
            Log(g_ui.registrationBlockedUnresolved.c_str(), unresolvedReason.c_str());
            registrationOk = false;
            registrationFailureMessage = unresolvedReason;
        }
        else if (!UsePowerShell())
        {
            AppxRegistrationResult comResult = Appx_Register_COM(manifestPath);
            registrationOk = comResult == AppxRegistrationResult::Success;

            if (comResult == AppxRegistrationResult::Failed)
            {
                LogText(g_ui.comRegistrationFailed);
                std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
                registrationOk = PS_Run(ps);
                if (!registrationOk)
                    setPowerShellFailureMessage();
            }
            else if (comResult == AppxRegistrationResult::TimeoutUncertain)
            {
                LogText(g_ui.comRegistrationFallbackSkippedAfterTimeout);
                MarkRegistrationUnresolved(g_ui.comRegistrationFallbackSkippedAfterTimeout, nullptr, 0);
                registrationFailureMessage = g_ui.comRegistrationFallbackSkippedAfterTimeout;
            }
        }
        else
        {
            std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
            registrationOk = PS_Run(ps);
            if (!registrationOk)
                setPowerShellFailureMessage();
        }
    }

    bool ok = assetsOk && registrationOk;
    auto registrationDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(), g_ui.timingAppRegistration.c_str(), ElapsedMs(assetsDone, registrationDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, registrationDone));
    std::wstring missingManifestAssets;
    if (!registrationOk)
    {
        missingManifestAssets = FindMissingManifestAssets(exeDir);
        if (!missingManifestAssets.empty())
            Log(g_ui.missingManifestAssets.c_str(), missingManifestAssets.c_str());
    }

    if (ok)
    {
        StoreSuccessfulGenerationState(wp);
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
            std::wstring failMsg = registrationFailureMessage.empty()
                ? g_ui.notificationGenerationFailed
                : registrationFailureMessage;
            if (!missingManifestAssets.empty() &&
                (registrationFailureMessage.empty() ||
                    failMsg == g_ui.notificationGenerationFailed ||
                    failMsg == g_ui.appRegistrationFailed ||
                    failMsg == g_ui.powerShellRegistrationFailed))
            {
                failMsg = g_ui.notificationMissingManifestAssets;
            }
            NotifyTrayBalloon(g_ui.notificationTitle, failMsg, NIIF_ERROR);
        }
    }

    return ok;
}

void TrayRemove();

static void WakePollThread()
{
    g_pollWakeCv.notify_all();
}

static void PollSleepFor(milliseconds duration)
{
    std::unique_lock<std::mutex> lk(g_pollWakeMutex);
    g_pollWakeCv.wait_for(lk, duration);
}

static std::wstring CurrentGenerationReason()
{
    if (g_generationActive)
    {
        std::lock_guard<std::mutex> lk(g_generationStateMutex);
        if (!g_generationActiveReason.empty())
            return g_generationActiveReason;
    }

    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationQueued && !g_generationQueuedReason.empty())
            return g_generationQueuedReason;
    }

    std::lock_guard<std::mutex> lk(g_generationStateMutex);
    return g_generationActiveReason.empty() ? g_ui.unspecifiedReason : g_generationActiveReason;
}

static bool GenerationWorkPending()
{
    std::lock_guard<std::mutex> lk(g_generationQueueMutex);
    return g_generationQueued || g_generationActive;
}

static void NotifyShutdownDelayed()
{
    std::wstring reason = CurrentGenerationReason();
    NotifyTrayBalloon(
        g_ui.notificationTitle,
        FormatWide(g_ui.shutdownDelayedNotification.c_str(), reason.c_str()),
        NIIF_WARNING,
        true);
}

static void MaybeCompleteRequestedShutdown()
{
    if (g_shutdownRequested && !GenerationWorkPending() && g_hwnd)
    {
        if (!PostMessageW(g_hwnd, WM_APP_SHUTDOWN_READY, 0, 0))
            LogWin32Failure(L"PostMessageW(WM_APP_SHUTDOWN_READY)");
    }
}

static void MarkGenerationActive(bool active, const std::wstring& reason = L"")
{
    {
        std::lock_guard<std::mutex> lk(g_generationStateMutex);
        g_generationActiveReason = active ? reason : L"";
    }
    g_generationActive = active;
}

static void RequestGracefulShutdown(HWND h)
{
    if (g_shutdownRequested)
        return;

    g_shutdownRequested = true;
    WakePollThread();
    LogText(g_ui.shutdownRequested);

    if (GenerationWorkPending())
    {
        NotifyShutdownDelayed();
        if (h)
            SetTimer(h, SHUTDOWN_NOTICE_TIMER_ID, static_cast<UINT>(ClampInt(g_shutdownInitialNoticeMs.load(), 250, 60000)), nullptr);
        return;
    }

    PostQuitMessage(0);
}

static void CancelRequestedShutdown(HWND h)
{
    if (!g_shutdownRequested)
        return;

    g_shutdownRequested = false;
    WakePollThread();
    if (h)
        KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
    LogText(g_ui.shutdownCancelled);
}

static void ForceShutdownNow()
{
    LogText(g_ui.forceShutdownRequested);
    TrayRemove();
    ExitProcess(0);
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
            MarkGenerationActive(true, reason);
            g_generationQueued = false;
        }

        Generate(wallpaper.c_str(), reason.c_str());
        MarkGenerationActive(false);
        MaybeCompleteRequestedShutdown();
    }
}

static void QueueGenerate(std::wstring wallpaper, std::wstring reason)
{
    if (g_shutdownRequested)
        return;

    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationWorkerStop || g_shutdownRequested)
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
    auto lastGen = steady_clock::now() - milliseconds(ClampInt(g_pollInitialDebounceBypassMs.load(), 0, 60000));
    bool emptyWallpaperAlreadyReported = false;

    while (g_running)
    {
        int poll = IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000);
        int confirm = IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000);
        int deb = IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000);

        if (g_shutdownRequested)
        {
            PollSleepFor(milliseconds(poll));
            continue;
        }

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
            PollSleepFor(milliseconds(confirm));
            if (!g_running || g_shutdownRequested)
                continue;

            std::wstring cur2 = GetWallpaper();
            FitMode fit2 = GetWallpaperFit();
            int dpiScale2 = CurrentAssetScale();

            bool wallpaperChanged2 = (cur2 != last);
            bool fitChanged2 = (!g_disableFitting && fit2 != lastFit);
            bool dpiScaleChanged2 = (g_generateScaleAuto && dpiScale2 != lastDpiScale);

            if ((g_listenWallpaper && wallpaperChanged2) ||
                (g_listenFit && fitChanged2) ||
                dpiScaleChanged2)
            {
                auto now = steady_clock::now();

                if (duration_cast<milliseconds>(now - lastGen).count() >= deb)
                {
                    if (cur2.empty())
                    {
                        if (!emptyWallpaperAlreadyReported)
                            LogText(g_ui.wallpaperDetectionEmptyDuringPoll);
                        emptyWallpaperAlreadyReported = true;
                        lastGen = now;
                        continue;
                    }

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

                    QueueGenerate(cur2, reason);
                    emptyWallpaperAlreadyReported = false;
                    last = cur2;
                    lastFit = fit2;
                    lastDpiScale = dpiScale2;
                    lastGen = steady_clock::now();
                }
            }
        }
        PollSleepFor(milliseconds(poll));
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
    ID_CANCEL_SHUTDOWN,
    ID_FORCE_SHUTDOWN,
    ID_GENERATE_STARTUP,
    ID_GENERATE_STARTUP_CHANGED_ONLY,
    ID_HIDE_DISABLED,
    ID_GENERATE_DESKTOP_ICON_DISABLED,
    ID_DELETE_DISABLED_ASSETS,
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
    ID_WP_METHOD_TRANSCODED_CACHE,
    ID_WP_METHOD_SYSTEM_PARAMETERS,
    ID_WP_METHOD_DESKTOP_WALLPAPER_COM,
    ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE,
    ID_WP_METHOD_AUTO,
    ID_WP_METHOD_FALLBACK,
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

static void ForgetTrayIconHandle()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    g_nid = {};
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

    if (g_shutdownRequested)
    {
        if (!GenerationWorkPending())
        {
            DestroyMenu(m);
            PostQuitMessage(0);
            return;
        }

        std::wstring reason = FormatWide(g_ui.shutdownPendingReason.c_str(), CurrentGenerationReason().c_str());
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, g_ui.shutdownPendingTitle.c_str());
        AppendMenuW(m, MF_STRING | MF_DISABLED, 0, reason.c_str());
        AppendMenuW(m, MF_STRING, ID_CANCEL_SHUTDOWN, g_ui.cancelShutdown.c_str());
        AppendMenuW(m, MF_STRING, ID_FORCE_SHUTDOWN, g_ui.forceShutdown.c_str());

        SetForegroundWindow(h);
        UINT cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr);
        DestroyMenu(m);

        if (cmd == ID_CANCEL_SHUTDOWN)
            CancelRequestedShutdown(h);
        else if (cmd == ID_FORCE_SHUTDOWN)
            ForceShutdownNow();
        return;
    }

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
    addTo(generalMenu, ID_GENERATE_STARTUP_CHANGED_ONLY, g_ui.generateOnStartupOnlyWhenChanged.c_str(), g_generateOnStartupOnlyWhenChanged, g_generateOnStartup);
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

    HMENU methodsMenu = beginSection(g_ui.methodsTitle);
    WallpaperDetectionMethod wallpaperMethod = CurrentWallpaperDetectionMethod();
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_CACHE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedImageCache), wallpaperMethod == WallpaperDetectionMethod::TranscodedImageCache);
    addTo(methodsMenu, ID_WP_METHOD_SYSTEM_PARAMETERS, WallpaperDetectionMethodName(WallpaperDetectionMethod::SystemParametersInfo), wallpaperMethod == WallpaperDetectionMethod::SystemParametersInfo);
    addTo(methodsMenu, ID_WP_METHOD_DESKTOP_WALLPAPER_COM, WallpaperDetectionMethodName(WallpaperDetectionMethod::DesktopWallpaperCOM), wallpaperMethod == WallpaperDetectionMethod::DesktopWallpaperCOM);
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedWallpaperFile), wallpaperMethod == WallpaperDetectionMethod::TranscodedWallpaperFile);
    addTo(methodsMenu, ID_WP_METHOD_AUTO, WallpaperDetectionMethodName(WallpaperDetectionMethod::Auto), wallpaperMethod == WallpaperDetectionMethod::Auto);
    addTo(methodsMenu, ID_WP_METHOD_FALLBACK, g_ui.wallpaperDetectionFallback.c_str(), g_wallpaperDetectionFallbackOnInvalid);
    std::wstring wallpaperDetectionLine = FormatWide(g_ui.wallpaperDetectionMethodLabel.c_str(), WallpaperDetectionMethodName(wallpaperMethod));
    AppendMenuW(methodsMenu, MF_STRING | MF_DISABLED, 0, wallpaperDetectionLine.c_str());
    endSection(methodsMenu, g_ui.methodsTitle);

    HMENU assetsMenu = beginSection(g_ui.assetsTitle);
    addTo(assetsMenu, ID_GENERATE_DESKTOP_ICON_DISABLED, g_ui.generateDesktopIconForDisabledEntries.c_str(), g_generateDesktopIconForDisabledEntries);
    addTo(assetsMenu, ID_DELETE_DISABLED_ASSETS, g_ui.deleteDisabledAssets.c_str(), g_deleteDisabledAssets, !g_generateDesktopIconForDisabledEntries);
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
    std::wstring pollInitialBypassLine = FormatWide(g_ui.pollInitialDebounceBypassMsLabel.c_str(), IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000));
    std::wstring iniCacheRefreshLine = FormatWide(g_ui.iniCacheRefreshMsLabel.c_str(), IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000));
    std::wstring notificationTimeoutLine = FormatWide(g_ui.notificationTimeoutMsLabel.c_str(), IniReadClampedI(L"Settings", L"NotificationTimeoutMs", 4000, 1000, 60000));
    std::wstring shutdownInitialLine = FormatWide(g_ui.shutdownInitialNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownInitialNoticeMs", 1000, 250, 60000));
    std::wstring shutdownRepeatLine = FormatWide(g_ui.shutdownRepeatNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownRepeatNoticeMs", 5000, 250, 60000));
    std::wstring signalRetriesLine = FormatWide(g_ui.singleInstanceSignalRetriesLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200));
    std::wstring signalDelayLine = FormatWide(g_ui.singleInstanceSignalDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000));
    std::wstring comRegistrationTimeoutLine = FormatWide(g_ui.comRegistrationTimeoutMsLabel.c_str(), IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000));
    std::wstring comRegistrationCancelWaitLine = FormatWide(g_ui.comRegistrationCancelWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000));
    std::wstring trayHideDelayLine = FormatWide(g_ui.trayIconHiddenDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000));
    std::wstring logBufferLine = FormatWide(g_ui.logBufferLinesLabel.c_str(), IniReadClampedI(L"Settings", L"LogBufferLines", 1024, 1, 100000));
    std::wstring powerShellExeLine = FormatWide(g_ui.powerShellExeLabel.c_str(), IniReadS(L"Settings", L"PowerShellExe", L"powershell.exe").c_str());
    std::wstring powerShellPollLine = FormatWide(g_ui.powerShellPollMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000));
    std::wstring powerShellTimeoutLine = FormatWide(g_ui.powerShellTimeoutMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000));
    std::wstring powerShellTerminateWaitLine = FormatWide(g_ui.powerShellTerminateWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000));
    std::wstring registrationUnresolvedBlockLine = FormatWide(g_ui.registrationUnresolvedBlockMsLabel.c_str(), IniReadClampedI(L"Settings", L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000));
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring manifestSourceLine = FormatWide(g_ui.manifestSourceLabel.c_str(), manifestInfo.existing ? g_ui.manifestSourceExisting.c_str() : g_ui.manifestSourceGenerated.c_str());
    std::wstring manifestOverwriteLine = FormatWide(g_ui.manifestOverwriteExistingLabel.c_str(), StateEnabled(ManifestOverwriteExisting()));
    std::wstring manifestIdentityLine = FormatWide(g_ui.manifestIdentityNameLabel.c_str(), manifestInfo.identityName.c_str());
    std::wstring manifestPublisherLine = FormatWide(g_ui.manifestPublisherLabel.c_str(), manifestInfo.publisher.c_str());
    std::wstring manifestPackageVersionLine = FormatWide(g_ui.manifestPackageVersionLabel.c_str(), manifestInfo.packageVersion.c_str());
    std::wstring manifestDisplayNameLine = FormatWide(g_ui.manifestDisplayNameLabel.c_str(), manifestInfo.displayName.c_str());
    std::wstring manifestPublisherDisplayNameLine = FormatWide(g_ui.manifestPublisherDisplayNameLabel.c_str(), manifestInfo.publisherDisplayName.c_str());
    std::wstring manifestDescriptionLine = FormatWide(g_ui.manifestDescriptionLabel.c_str(), manifestInfo.description.c_str());
    std::wstring manifestExecutableLine = FormatWide(g_ui.manifestExecutableLabel.c_str(), manifestInfo.executable.c_str());
    std::wstring manifestApplicationIdLine = FormatWide(g_ui.manifestApplicationIdLabel.c_str(), manifestInfo.applicationId.c_str());
    std::wstring manifestMinVersionLine = FormatWide(g_ui.manifestMinVersionLabel.c_str(), manifestInfo.minVersion.c_str());
    std::wstring manifestMaxVersionTestedLine = FormatWide(g_ui.manifestMaxVersionTestedLabel.c_str(), manifestInfo.maxVersionTested.c_str());
    std::wstring singleInstanceLine = FormatWide(g_ui.singleInstanceFailureActionLabel.c_str(), ErrorActionName(CurrentSingleInstanceFailureAction()));
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, pollLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, confirmLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, debounceLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, pollInitialBypassLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, iniCacheRefreshLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, notificationTimeoutLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, shutdownInitialLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, shutdownRepeatLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, signalRetriesLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, signalDelayLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, comRegistrationTimeoutLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, comRegistrationCancelWaitLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, trayHideDelayLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, logBufferLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellExeLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellPollLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellTimeoutLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellTerminateWaitLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, registrationUnresolvedBlockLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestSourceLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestOverwriteLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestIdentityLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestPackageVersionLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestDisplayNameLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherDisplayNameLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestDescriptionLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestExecutableLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestApplicationIdLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestMinVersionLine.c_str());
    AppendMenuW(advancedMenu, MF_STRING | MF_DISABLED, 0, manifestMaxVersionTestedLine.c_str());
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
            Sleep(static_cast<DWORD>(ClampInt(g_trayIconHiddenDelayMs.load(), 0, 10000)));
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
            if (!AllocConsole())
                LogWin32Failure(L"AllocConsole");
            else if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
                LogWin32Failure(L"SetConsoleCtrlHandler");

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

    case ID_GENERATE_STARTUP_CHANGED_ONLY:
    {
        if (!g_generateOnStartup)
            break;
        g_generateOnStartupOnlyWhenChanged = !g_generateOnStartupOnlyWhenChanged;
        IniWrite(L"Settings", L"GenerateOnStartupOnlyWhenChanged", g_generateOnStartupOnlyWhenChanged ? L"1" : L"0");
        Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
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

    case ID_DELETE_DISABLED_ASSETS:
    {
        if (g_generateDesktopIconForDisabledEntries)
            break;
        g_deleteDisabledAssets = !g_deleteDisabledAssets;
        IniWrite(L"Settings", L"DeleteDisabledAssets", g_deleteDisabledAssets ? L"1" : L"0");
        Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
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
        RequestGracefulShutdown(h);
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

    case ID_WP_METHOD_TRANSCODED_CACHE:
    case ID_WP_METHOD_SYSTEM_PARAMETERS:
    case ID_WP_METHOD_DESKTOP_WALLPAPER_COM:
    case ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE:
    case ID_WP_METHOD_AUTO:
    {
        WallpaperDetectionMethod method = WallpaperDetectionMethod::TranscodedImageCache;
        if (cmd == ID_WP_METHOD_SYSTEM_PARAMETERS)
            method = WallpaperDetectionMethod::SystemParametersInfo;
        else if (cmd == ID_WP_METHOD_DESKTOP_WALLPAPER_COM)
            method = WallpaperDetectionMethod::DesktopWallpaperCOM;
        else if (cmd == ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE)
            method = WallpaperDetectionMethod::TranscodedWallpaperFile;
        else if (cmd == ID_WP_METHOD_AUTO)
            method = WallpaperDetectionMethod::Auto;

        const wchar_t* methodName = WallpaperDetectionMethodName(method);
        IniWrite(L"Settings", L"WallpaperDetectionMethod", methodName);
        Log(g_ui.wallpaperDetectionMethodLabel.c_str(), methodName);
    }
    break;

    case ID_WP_METHOD_FALLBACK:
    {
        g_wallpaperDetectionFallbackOnInvalid = !g_wallpaperDetectionFallbackOnInvalid;
        IniWrite(L"Settings", L"WallpaperDetectionFallbackOnInvalid", g_wallpaperDetectionFallbackOnInvalid ? L"1" : L"0");
        Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
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

    if (g_taskbarCreatedMessage && m == g_taskbarCreatedMessage)
    {
        if (g_tray)
        {
            ForgetTrayIconHandle();
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
        }
        return 0;
    }

    if (m == WM_APP_SHUTDOWN_READY)
    {
        if (g_shutdownRequested)
            PostQuitMessage(0);
        return 0;
    }

    if (m == WM_APP_REQUEST_SHUTDOWN)
    {
        g_consoleControlShutdownPending = false;
        RequestGracefulShutdown(h);
        return 0;
    }

    if (m == WM_TIMER && w == SHUTDOWN_NOTICE_TIMER_ID)
    {
        if (!g_shutdownRequested)
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            return 0;
        }

        if (!GenerationWorkPending())
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            PostQuitMessage(0);
            return 0;
        }

        NotifyShutdownDelayed();
        SetTimer(h, SHUTDOWN_NOTICE_TIMER_ID, static_cast<UINT>(ClampInt(g_shutdownRepeatNoticeMs.load(), 250, 60000)), nullptr);
        return 0;
    }

    if (m == WM_QUERYENDSESSION)
    {
        RequestGracefulShutdown(h);
        return TRUE;
    }

    if (m == WM_ENDSESSION)
    {
        if (w)
            RequestGracefulShutdown(h);
        else
            CancelRequestedShutdown(h);
        return 0;
    }

    if (m==WM_USER+1){
        if (l==WM_RBUTTONUP || l==WM_CONTEXTMENU){
            Menu(h);
            return 0;
        }
    }
    else if (m==WM_CLOSE){
        RequestGracefulShutdown(h);
        return 0;
    }
    else if (m==WM_DESTROY){
        g_shutdownWindow = nullptr;
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
static void ConfigureSingleInstanceIdentity(const std::wstring& exeDir)
{
    std::wstring scope = ToLowerCopy(exeDir);
    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, scope);
    std::wstring suffix = Hex64(hash);

    g_singleInstanceMutexName = std::wstring(SINGLE_INSTANCE_MUTEX_BASE) + L"." + suffix;
    g_singleInstanceMessageName = std::wstring(SINGLE_INSTANCE_MESSAGE_BASE) + L"." + suffix;
    g_instanceWindowTitle = std::wstring(WINDOW_CLASS_NAME) + L"." + suffix;
}

static HWND FindExistingInstanceWindow()
{
    int retries = IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200);
    int delayMs = IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000);
    for (int i = 0; i <= retries; ++i)
    {
        const wchar_t* title = g_instanceWindowTitle.empty() ? nullptr : g_instanceWindowTitle.c_str();
        HWND existing = FindWindowW(WINDOW_CLASS_NAME, title);
        if (!existing)
            existing = FindWindowExW(HWND_MESSAGE, nullptr, WINDOW_CLASS_NAME, title);
        if (existing)
            return existing;
        if (i < retries && delayMs > 0)
            Sleep(static_cast<DWORD>(delayMs));
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

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType)
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    {
        g_consoleControlShutdownPending = true;
        HWND h = g_shutdownWindow.load();
        if (h)
            PostMessageW(h, WM_APP_REQUEST_SHUTDOWN, ctrlType, 0);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hi, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    auto appStart = steady_clock::now();
    ConfigureDpiAwareness();

    g_exePath = GetModulePath();
    if (g_exePath.empty())
        return 1;

    std::wstring dir = GetExeDir();
    std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/")+1);
    base = base.substr(0, base.find_last_of(L'.'));
    g_iniPath = dir + L"\\" + base + L".ini";
    ConfigureSingleInstanceIdentity(dir);

    bool consoleCtrlHandlerInstalled = SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE) != FALSE;
    DWORD consoleCtrlHandlerError = consoleCtrlHandlerInstalled ? ERROR_SUCCESS : GetLastError();

    g_singleInstanceMessage = RegisterWindowMessageW(g_singleInstanceMessageName.c_str());
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    SetLastError(ERROR_SUCCESS);
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, g_singleInstanceMutexName.c_str());
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

    auto iniStart = steady_clock::now();
    bool initialIniCreated = EnsureInitialIniTemplate();
    bool iniEncodingNormalized = initialIniCreated ? false : NormalizeIniToUtf8BomIfNeeded();

    // defaults
    EnsureIniDefaults();
    EnsureIniStringDefaults();
    UpgradeRenamedStringDefaults();
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    auto iniDone = steady_clock::now();
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
    g_generateOnStartupOnlyWhenChanged = IniReadI(L"Settings", L"GenerateOnStartupOnlyWhenChanged", 1) != 0;
    g_hideDisabled = IniReadI(L"Settings", L"HideDisabledEntries", 0) != 0;
    g_generateDesktopIconForDisabledEntries = IniReadI(L"Settings", L"GenerateDesktopIconForDisabledEntries", 1) != 0;
    g_deleteDisabledAssets = IniReadI(L"Settings", L"DeleteDisabledAssets", 0) != 0;
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
    g_wallpaperDetectionFallbackOnInvalid = IniReadI(L"Settings", L"WallpaperDetectionFallbackOnInvalid", 1) != 0;
    g_notificationTimeoutMs = IniReadClampedI(L"Settings", L"NotificationTimeoutMs", 4000, 1000, 60000);
    g_shutdownInitialNoticeMs = IniReadClampedI(L"Settings", L"ShutdownInitialNoticeMs", 1000, 250, 60000);
    g_shutdownRepeatNoticeMs = IniReadClampedI(L"Settings", L"ShutdownRepeatNoticeMs", 5000, 250, 60000);
    g_trayIconHiddenDelayMs = IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000);
    g_logBufferLines = IniReadClampedI(L"Settings", L"LogBufferLines", 1024, 1, 100000);
    g_pollInitialDebounceBypassMs = IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000);
    g_iniCacheRefreshMs = IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000);

    std::wstring startupManifestPath;
    bool startupManifestCreated = false;
    DWORD startupManifestError = ERROR_SUCCESS;
    bool startupManifestReady = EnsureAppxManifest(startupManifestPath, startupManifestCreated, startupManifestError);

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr, g_ui.gdiPlusStartupFailedMessage.c_str(), g_ui.gdiPlusStartupFailedTitle.c_str(), 0); return 1; }
    auto gdiDone = steady_clock::now();

    LogText(g_ui.programStarting);
    Log(g_ui.timingLog.c_str(), g_ui.timingPreLogInitialization.c_str(), ElapsedMs(appStart, gdiDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingIniDefaultStringLoading.c_str(), ElapsedMs(iniStart, iniDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGdiStartup.c_str(), ElapsedMs(iniDone, gdiDone));
    Log(g_ui.exeLabel.c_str(), g_exePath.c_str());
    Log(g_ui.iniLabel.c_str(), g_iniPath.c_str());
    Log(g_ui.logFileLabel.c_str(), GetLogPathCopy().c_str());
    FlushStartupWarnings();
    Log(g_ui.singleInstanceScopeSummary.c_str(), g_singleInstanceMutexName.c_str());
    if (iniEncodingNormalized)
        LogText(g_ui.iniEncodingNormalized);
    if (startupManifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), startupManifestPath.c_str());
    else if (!startupManifestReady)
    {
        Log(g_ui.appxManifestCreateFailed.c_str(), startupManifestPath.c_str());
        LogWin32Failure(L"Create AppxManifest.xml", startupManifestError);
    }
    Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
    Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
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
    Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    if (!consoleCtrlHandlerInstalled)
        LogWin32Failure(L"SetConsoleCtrlHandler", consoleCtrlHandlerError);

    // Console setting
    if (g_console)
    {
        if (!AllocConsole())
            LogWin32Failure(L"AllocConsole");
        else
        {
            if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
                LogWin32Failure(L"SetConsoleCtrlHandler");
            LogText(g_ui.consoleAllocated);
        }

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
    g_hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, g_instanceWindowTitle.c_str(), 0, 0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd)
    {
        LogWin32Failure(L"CreateWindowExW");
        GdiplusShutdown(tk);
        return 1;
    }
    g_shutdownWindow = g_hwnd;

    // Tray
    if (g_tray){
        DWORD err = 0;
        if (!EnsureTrayIcon(&err))
        {
            LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
            g_tray = false;
        }
    }

    if (g_consoleControlShutdownPending.exchange(false))
        RequestGracefulShutdown(g_hwnd);

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

    if (!g_shutdownRequested && g_generateOnStartup)
    {
        std::wstring wp = GetWallpaper();
        if (!wp.empty())
        {
            if (StartupGenerationCanSkip(wp))
            {
                LogText(g_ui.startupGenerationSkippedUnchanged);
            }
            else
            {
                LogText(g_ui.startupGenerationEnabled);
                QueueGenerate(wp, g_ui.startupGenerationEnabled);
            }
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
    WakePollThread();
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

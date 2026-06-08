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
    return aip::ReadWholeFileBytes(path, bytes);
}

static std::wstring MakeTempSiblingPath(const std::wstring& path, const wchar_t* tag)
{
    static std::atomic<unsigned long> counter(0);
    wchar_t suffix[128];
    swprintf(suffix, _countof(suffix), L".tmp.%lu.%llu.%lu.%s",
        GetCurrentProcessId(),
        static_cast<unsigned long long>(GetTickCount64()),
        counter.fetch_add(1, std::memory_order_relaxed),
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
    DWORD savedErr = ERROR_SUCCESS;
    while (remaining > 0)
    {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1024 * 1024));
        DWORD wrote = 0;
        if (!WriteFile(h, cursor, chunk, &wrote, nullptr))
        {
            savedErr = GetLastError();
            ok = false;
            break;
        }
        if (wrote != chunk)
        {
            savedErr = ERROR_WRITE_FAULT;
            ok = false;
            break;
        }
        cursor += wrote;
        remaining -= wrote;
    }

    if (ok)
    {
        if (!FlushFileBuffers(h))
        {
            savedErr = GetLastError();
            ok = false;
        }
    }

    if (!CloseHandle(h) && ok)
    {
        DWORD err = GetLastError();
        SetLastError(err);
        return false;
    }

    if (!ok)
        SetLastError(savedErr == ERROR_SUCCESS ? ERROR_WRITE_FAULT : savedErr);
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

static std::wstring ParentDirectoryOfPath(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return L"";
    if (slash == 0)
        return path.substr(0, 1);
    return path.substr(0, slash);
}

static bool EnsureDirectoryTreeExists(const std::wstring& path, DWORD* error = nullptr)
{
    if (path.empty())
    {
        if (error)
            *error = ERROR_SUCCESS;
        return true;
    }

    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            if (error)
                *error = ERROR_SUCCESS;
            return true;
        }

        if (error)
            *error = ERROR_ALREADY_EXISTS;
        SetLastError(ERROR_ALREADY_EXISTS);
        return false;
    }

    std::wstring parent = ParentDirectoryOfPath(path);
    if (!parent.empty() && parent != path && !EnsureDirectoryTreeExists(parent, error))
        return false;

    return EnsureDirectoryExists(path, error);
}

static bool EnsureParentDirectoryForFile(const std::wstring& path, DWORD* error = nullptr)
{
    return EnsureDirectoryTreeExists(ParentDirectoryOfPath(path), error);
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

    out.resize(static_cast<size_t>(need));
    return MultiByteToWideChar(codePage, flags, (LPCCH)data, (int)size, &out[0], need) > 0;
}

static bool DecodeTextBytes(const std::vector<BYTE>& bytes, std::wstring& out)
{
    return aip::DecodeTextBytes(bytes, out);
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
    std::vector<BYTE> bytes = EncodeUtf8Text(text, true);
    if (bytes.empty() && !text.empty())
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
        return false;
    }
    return WriteWholeFileBytes(path, bytes);
}

static bool AppendUtf8TextFile(const std::wstring& path, const std::wstring& text)
{
    DWORD lockWaitMs = static_cast<DWORD>(ClampInt(g_logAppendLockWaitMs.load(), 0, 60000));
    return aip::AppendUtf8TextToFile(path, text, true, lockWaitMs);
}

enum class TextNormalizeResult
{
    NoChange,
    Changed,
    Failed
};

static TextNormalizeResult NormalizeTextFileToUtf8Bom(const std::wstring& path)
{
    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(path, bytes))
        return TextNormalizeResult::Failed;
    if (bytes.empty() || HasUtf8Bom(bytes))
        return TextNormalizeResult::NoChange;

    std::wstring text;
    if (!DecodeTextBytes(bytes, text))
    {
        SetLastError(ERROR_INVALID_DATA);
        return TextNormalizeResult::Failed;
    }

    return WriteUtf8BomTextFile(path, text)
        ? TextNormalizeResult::Changed
        : TextNormalizeResult::Failed;
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
#pragma warning(suppress: 4191)
        auto setContext = (SetProcessDpiAwarenessContextFn)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
            return;
    }

    SetProcessDPIAware();
}

static std::wstring GetModulePath()
{
    return aip::GetCurrentExecutablePath();
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

static void StripLeadingBom(std::wstring& s);

static int HexDigitValue(wchar_t ch)
{
    if (ch >= L'0' && ch <= L'9')
        return (int)(ch - L'0');
    if (ch >= L'a' && ch <= L'f')
        return 10 + (int)(ch - L'a');
    if (ch >= L'A' && ch <= L'F')
        return 10 + (int)(ch - L'A');
    return -1;
}

static bool StartsWithAt(const std::wstring& text, size_t pos, const wchar_t* needle)
{
    size_t len = wcslen(needle);
    return pos <= text.size() && len <= text.size() - pos && text.compare(pos, len, needle) == 0;
}

static std::wstring DecodeXmlEntities(std::wstring value)
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

    std::wstring out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == L'&' && i + 2 < value.size() && value[i + 1] == L'#')
        {
            size_t pos = i + 2;
            int radix = 10;
            if (pos < value.size() && (value[pos] == L'x' || value[pos] == L'X'))
            {
                radix = 16;
                ++pos;
            }

            unsigned int code = 0;
            bool any = false;
            while (pos < value.size() && value[pos] != L';')
            {
                int digit = radix == 16 ? HexDigitValue(value[pos])
                    : (value[pos] >= L'0' && value[pos] <= L'9' ? (int)(value[pos] - L'0') : -1);
                if (digit < 0 || digit >= radix)
                    break;
                code = code * (unsigned int)radix + (unsigned int)digit;
                any = true;
                ++pos;
            }

            if (any && pos < value.size() && value[pos] == L';' && code <= 0xFFFF)
            {
                out.push_back((wchar_t)code);
                i = pos;
                continue;
            }
        }

        out.push_back(value[i]);
    }
    return out;
}

static std::wstring DecodePowerShellClixmlValue(const std::wstring& value)
{
    std::wstring xml = DecodeXmlEntities(value);
    std::wstring out;
    out.reserve(xml.size());

    for (size_t i = 0; i < xml.size(); ++i)
    {
        if (i + 6 < xml.size() && xml[i] == L'_' && xml[i + 1] == L'x' && xml[i + 6] == L'_')
        {
            int d0 = HexDigitValue(xml[i + 2]);
            int d1 = HexDigitValue(xml[i + 3]);
            int d2 = HexDigitValue(xml[i + 4]);
            int d3 = HexDigitValue(xml[i + 5]);
            if (d0 >= 0 && d1 >= 0 && d2 >= 0 && d3 >= 0)
            {
                wchar_t ch = (wchar_t)((d0 << 12) | (d1 << 8) | (d2 << 4) | d3);
                out.push_back(ch);
                i += 6;
                continue;
            }
        }

        out.push_back(xml[i]);
    }
    return out;
}

static std::wstring NormalizePowerShellOutputText(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    int blankRun = 0;
    for (std::wstring line : SplitLines(text))
    {
        while (!line.empty() && (line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();

        if (TrimCopy(line).empty())
        {
            if (++blankRun <= 1)
                lines.push_back(L"");
            continue;
        }

        blankRun = 0;
        lines.push_back(line);
    }

    TrimTrailingEmptyLines(lines);
    return JoinLines(lines);
}

static bool LooksLikePowerShellClixml(std::wstring text)
{
    StripLeadingBom(text);
    size_t pos = 0;
    while (pos < text.size() && iswspace(text[pos]) != 0)
        ++pos;

    return StartsWithAt(text, pos, L"#< CLIXML")
        || (text.find(L"<Objs") != std::wstring::npos && text.find(L"<S") != std::wstring::npos);
}

static bool ExtractPowerShellStringNode(const std::wstring& text, size_t& pos, std::wstring& tag, std::wstring& value)
{
    for (;;)
    {
        size_t open = text.find(L"<S", pos);
        if (open == std::wstring::npos)
            return false;
        if (open + 2 >= text.size() || text[open + 2] == L'>' || iswspace(text[open + 2]) != 0)
        {
            size_t tagEnd = text.find(L'>', open + 2);
            if (tagEnd == std::wstring::npos)
                return false;
            size_t close = text.find(L"</S", tagEnd + 1);
            if (close == std::wstring::npos)
                return false;
            size_t closeEnd = text.find(L'>', close + 3);
            if (closeEnd == std::wstring::npos)
                return false;

            tag = text.substr(open, tagEnd - open + 1);
            value = text.substr(tagEnd + 1, close - tagEnd - 1);
            pos = closeEnd + 1;
            return true;
        }
        pos = open + 2;
    }
}

static void AddPowerShellRecord(std::vector<std::wstring>& records, const std::wstring& value)
{
    std::wstring normalized = NormalizePowerShellOutputText(value);
    if (TrimCopy(normalized).empty())
        return;
    if (!records.empty() && records.back() == normalized)
        return;
    records.push_back(normalized);
}

static void AppendPowerShellRecords(std::vector<std::wstring>& lines, const std::vector<std::wstring>& records)
{
    for (const std::wstring& record : records)
    {
        std::vector<std::wstring> recordLines = SplitLines(record);
        lines.insert(lines.end(), recordLines.begin(), recordLines.end());
    }
}

static std::wstring FormatPowerShellClixml(const std::wstring& text)
{
    std::vector<std::wstring> errors;
    std::vector<std::wstring> other;
    std::vector<std::wstring> progress;

    size_t pos = 0;
    std::wstring tag;
    std::wstring rawValue;
    while (ExtractPowerShellStringNode(text, pos, tag, rawValue))
    {
        std::wstring decoded = DecodePowerShellClixmlValue(rawValue);
        std::wstring loweredTag = ToLowerCopy(tag);
        if (loweredTag.find(L"s=\"error\"") != std::wstring::npos || loweredTag.find(L"s='error'") != std::wstring::npos)
            AddPowerShellRecord(errors, decoded);
        else if (loweredTag.find(L"s=\"progress\"") != std::wstring::npos || loweredTag.find(L"s='progress'") != std::wstring::npos)
            AddPowerShellRecord(progress, decoded);
        else
            AddPowerShellRecord(other, decoded);
    }

    std::vector<std::wstring> lines;
    if (!errors.empty())
    {
        AppendPowerShellRecords(lines, errors);
        if (!other.empty())
        {
            lines.push_back(L"");
            lines.push_back(L"Additional output:");
            AppendPowerShellRecords(lines, other);
        }
    }
    else if (!other.empty())
    {
        AppendPowerShellRecords(lines, other);
    }
    else
    {
        AppendPowerShellRecords(lines, progress);
    }

    if (!lines.empty())
        return NormalizePowerShellOutputText(JoinLines(lines));

    return NormalizePowerShellOutputText(DecodePowerShellClixmlValue(text));
}

static std::wstring FormatPowerShellOutput(const std::wstring& text)
{
    if (LooksLikePowerShellClixml(text))
        return FormatPowerShellClixml(text);
    return NormalizePowerShellOutputText(text);
}

static bool WriteIniValueToText(std::wstring& text, const wchar_t* s, const wchar_t* k, const wchar_t* v);

static void StripLeadingBom(std::wstring& s)
{
    if (!s.empty() && s.front() == 0xFEFF)
        s.erase(s.begin());
}

using IniEntry = aip::IniEntry;
using IniSectionData = aip::IniSectionData;
using aip::FormatIniAssignment;
using aip::ParseIniName;
using aip::ReadIniValueFromDoc;
using aip::TryParseIniSectionHeader;

static std::mutex g_iniWriteMutex;
static std::mutex g_iniCacheMutex;
static bool g_iniCacheLoaded = false;
static FILETIME g_iniCacheWriteTime{};
static std::wstring g_iniCacheText;
static std::vector<IniSectionData> g_iniCacheDoc;
static std::atomic<int> g_iniCacheRefreshMs(250);
static steady_clock::time_point g_iniCacheLastCheck{};

class DesktopStubIniMutationGuard
{
public:
    DesktopStubIniMutationGuard()
        : processLock_(g_iniWriteMutex),
          sharedLock_(g_iniPath)
    {
        if (!sharedLock_.IsLocked())
            SetLastError(sharedLock_.Error());
    }

    bool IsLocked() const
    {
        return sharedLock_.IsLocked();
    }

private:
    std::unique_lock<std::mutex> processLock_;
    aip::IniWriteMutexGuard sharedLock_;
};

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
    return aip::ParseIniDocument(text);
}

static bool ReadIniSectionFromDoc(const std::vector<IniSectionData>& doc, const wchar_t* s, std::vector<IniEntry>& out)
{
    std::wstring sectionName = s ? s : L"";
    for (const auto& sec : doc)
    {
        if (IEquals(sec.name, sectionName))
        {
            out = sec.entries;
            return true;
        }
    }
    out.clear();
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

static bool IsMissingFileError(DWORD err)
{
    return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME;
}

static bool ReadIniFileForMutation(std::wstring& text)
{
    if (!aip::IniConfigStore(g_iniPath).ReadFreshText(text))
        return false;

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

static bool ReadIniSectionCached(const wchar_t* s, std::vector<IniEntry>& out)
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

    return ReadIniSectionFromDoc(g_iniCacheDoc, s, out);
}

static bool WriteIniValueToText(std::wstring& text, const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    return aip::WriteIniValueToText(text, s, k, v ? v : L"");
}

static bool WriteIniValue(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    DesktopStubIniMutationGuard writeLock;
    if (!writeLock.IsLocked())
        return false;

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

static constexpr const wchar_t* POWERSHELL_ERROR_MESSAGES_SECTION = L"PowerShellErrorMessages";


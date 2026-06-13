#ifndef AIP_DEPENDENCIES_CONFIG_INI_INC
#define AIP_DEPENDENCIES_CONFIG_INI_INC

namespace aip
{
inline constexpr size_t kMaxIniFileBytes = 16u * 1024u * 1024u;

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

inline bool IniEquals(const std::wstring& left, const std::wstring& right)
{
    return _wcsicmp(left.c_str(), right.c_str()) == 0;
}

class IniWriteMutexGuard
{
public:
    explicit IniWriteMutexGuard(const std::wstring& path, DWORD waitMs = INFINITE)
    {
        std::wstring name = L"Local\\AIP.ConfigIni." + StableHashHex64(MakeAbsolutePath(path));
        handle_ = CreateMutexW(nullptr, FALSE, name.c_str());
        if (handle_ == nullptr)
        {
            error_ = GetLastError();
            if (error_ == ERROR_ACCESS_DENIED)
            {
                handle_ = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name.c_str());
                if (handle_ == nullptr)
                {
                    error_ = GetLastError();
                    return;
                }
            }
            else
            {
                return;
            }
        }

        DWORD wait = WaitForSingleObject(handle_, waitMs);
        if (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED)
        {
            acquired_ = true;
            error_ = ERROR_SUCCESS;
            return;
        }

        error_ = wait == WAIT_FAILED ? GetLastError() : ERROR_SEM_TIMEOUT;
        CloseHandle(handle_);
        handle_ = nullptr;
    }

    ~IniWriteMutexGuard()
    {
        if (handle_ != nullptr)
        {
            if (acquired_)
            {
                ReleaseMutex(handle_);
            }
            CloseHandle(handle_);
        }
    }

    IniWriteMutexGuard(const IniWriteMutexGuard&) = delete;
    IniWriteMutexGuard& operator=(const IniWriteMutexGuard&) = delete;

    bool IsLocked() const
    {
        return acquired_;
    }

    DWORD Error() const
    {
        return error_ == ERROR_SUCCESS ? ERROR_ACCESS_DENIED : error_;
    }

private:
    HANDLE handle_ = nullptr;
    bool acquired_ = false;
    DWORD error_ = ERROR_SUCCESS;
};

inline bool ReadWholeFileBytes(
    const std::wstring& path,
    std::vector<BYTE>& bytes,
    size_t maxBytes = static_cast<size_t>(INT_MAX))
{
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    auto fail = [&](DWORD err) -> bool
    {
        CloseHandle(file);
        SetLastError(err == ERROR_SUCCESS ? ERROR_READ_FAULT : err);
        bytes.clear();
        return false;
    };

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size))
    {
        return fail(GetLastError());
    }
    if (size.QuadPart < 0)
    {
        return fail(ERROR_INVALID_DATA);
    }
    if (size.QuadPart > INT_MAX ||
        static_cast<unsigned long long>(size.QuadPart) >
            static_cast<unsigned long long>(maxBytes))
    {
        return fail(ERROR_FILE_TOO_LARGE);
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    BYTE* cursor = bytes.empty() ? nullptr : bytes.data();
    size_t remaining = bytes.size();
    while (remaining > 0)
    {
        DWORD chunk = static_cast<DWORD>(std::min<size_t>(remaining, 1024 * 1024));
        DWORD read = 0;
        if (!ReadFile(file, cursor, chunk, &read, nullptr))
        {
            return fail(GetLastError());
        }
        if (read == 0)
        {
            return fail(ERROR_HANDLE_EOF);
        }
        cursor += read;
        remaining -= read;
    }

    if (!CloseHandle(file))
    {
        DWORD err = GetLastError();
        bytes.clear();
        SetLastError(err);
        return false;
    }
    return true;
}

inline bool DecodeCodePageText(UINT codePage, const BYTE* data, size_t size, std::wstring& out, DWORD flags = 0)
{
    if (size > INT_MAX)
    {
        return false;
    }
    if (size == 0)
    {
        out.clear();
        return true;
    }

    int need = MultiByteToWideChar(codePage, flags, reinterpret_cast<LPCCH>(data), static_cast<int>(size), nullptr, 0);
    if (need <= 0)
    {
        return false;
    }

    out.resize(static_cast<size_t>(need));
    int written = MultiByteToWideChar(codePage, flags, reinterpret_cast<LPCCH>(data), static_cast<int>(size), out.data(), need);
    if (written != need)
    {
        out.clear();
        return false;
    }
    return true;
}

inline bool DecodeTextBytes(const std::vector<BYTE>& bytes, std::wstring& out)
{
    if (bytes.empty())
    {
        out.clear();
        return true;
    }

    if (bytes.size() >= 3 && bytes[0] == 0xEF && bytes[1] == 0xBB && bytes[2] == 0xBF)
    {
        return DecodeCodePageText(CP_UTF8, bytes.data() + 3, bytes.size() - 3, out, MB_ERR_INVALID_CHARS);
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE)
    {
        if ((bytes.size() - 2) % 2 != 0)
        {
            return false;
        }
        size_t n = (bytes.size() - 2) / 2;
        out.resize(n);
        if (n != 0)
        {
            memcpy(out.data(), bytes.data() + 2, n * sizeof(wchar_t));
        }
        return true;
    }

    if (bytes.size() >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF)
    {
        if ((bytes.size() - 2) % 2 != 0)
        {
            return false;
        }
        size_t n = (bytes.size() - 2) / 2;
        out.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            BYTE hi = bytes[2 + i * 2 + 0];
            BYTE lo = bytes[2 + i * 2 + 1];
            out[i] = static_cast<wchar_t>((hi << 8) | lo);
        }
        return true;
    }

    if (DecodeCodePageText(CP_UTF8, bytes.data(), bytes.size(), out, MB_ERR_INVALID_CHARS))
    {
        return true;
    }

    // Match DesktopStub's tolerance for INI files edited with the system ANSI
    // code page instead of UTF-8.
    return DecodeCodePageText(CP_ACP, bytes.data(), bytes.size(), out, 0);
}

inline bool ReadTextFileUtf8BomAware(const std::wstring& path, std::wstring& text)
{
    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(path, bytes, kMaxIniFileBytes))
    {
        return false;
    }
    return DecodeTextBytes(bytes, text);
}

inline bool WriteTextFileUtf8Bom(const std::wstring& path, const std::wstring& text)
{
    EnsureDirectory(GetDirectoryName(path));

    static volatile LONG tempCounter = 0;
    wchar_t suffix[128] = {};
    swprintf_s(suffix,
        L".tmp.%lu.%lu.%llu.%ld.ini",
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        static_cast<unsigned long long>(GetTickCount64()),
        InterlockedIncrement(&tempCounter));
    std::wstring temp = path + suffix;

    DeleteFileW(temp.c_str());
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    static const unsigned char bom[] = { 0xef, 0xbb, 0xbf };
    bool ok = WriteAllBytes(file, bom, static_cast<DWORD>(sizeof(bom)));
    std::string utf8;
    if (ok && !TryWideToUtf8(text, utf8))
    {
        DWORD utf8Error = GetLastError();
        SetLastError(utf8Error == ERROR_SUCCESS ? ERROR_NO_UNICODE_TRANSLATION : utf8Error);
        ok = false;
    }
    if (ok && !utf8.empty())
    {
        ok = WriteAllBytes(file, utf8.data(), static_cast<DWORD>(utf8.size()));
    }
    if (ok && !FlushFileBuffers(file))
    {
        ok = false;
    }
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(file) && ok)
    {
        err = GetLastError();
        ok = false;
    }

    if (!ok)
    {
        DeleteFileW(temp.c_str());
        SetLastError(err == ERROR_SUCCESS ? ERROR_WRITE_FAULT : err);
        return false;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
    {
        err = GetLastError();
        DeleteFileW(temp.c_str());
        SetLastError(err);
        return false;
    }
    return true;
}

inline std::vector<std::wstring> SplitIniLines(const std::wstring& text)
{
    std::vector<std::wstring> lines;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos)
        {
            lines.push_back(text.substr(pos));
            break;
        }
        lines.push_back(text.substr(pos, end - pos));
        pos = end + 1;
        if (pos < text.size() && text[end] == L'\r' && text[pos] == L'\n')
        {
            ++pos;
        }
        if (pos == text.size())
        {
            lines.push_back(L"");
            break;
        }
    }
    return lines;
}

inline void TrimTrailingIniBlankLines(std::vector<std::wstring>& lines)
{
    while (!lines.empty() && Trim(lines.back()).empty())
    {
        lines.pop_back();
    }
}

inline std::wstring JoinIniLines(const std::vector<std::wstring>& lines)
{
    std::wstring text;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i != 0)
        {
            text += L"\r\n";
        }
        text += lines[i];
    }
    return text;
}

inline void StripLeadingIniBom(std::wstring& text)
{
    if (!text.empty() && text.front() == 0xFEFF)
    {
        text.erase(text.begin());
    }
}

inline bool IsBackslashEscaped(const std::wstring& text, size_t pos)
{
    if (pos == 0)
    {
        return false;
    }
    size_t count = 0;
    for (size_t i = pos; i > 0 && text[i - 1] == L'\\'; --i)
    {
        ++count;
    }
    return (count % 2) != 0;
}

inline bool IsIniInlineCommentStart(const std::wstring& value, size_t pos, bool requireLeadingWhitespace)
{
    wchar_t ch = value[pos];
    if (ch != L';' && ch != L'#')
    {
        return false;
    }
    if (!requireLeadingWhitespace)
    {
        return true;
    }
    return pos == 0 || iswspace(value[pos - 1]) != 0;
}

inline std::wstring StripIniInlineComment(std::wstring value, bool requireLeadingWhitespace = true)
{
    bool inQuote = false;
    wchar_t quoteChar = 0;
    for (size_t i = 0; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        bool escaped = IsBackslashEscaped(value, i);
        if (!escaped && (ch == L'\"' || ch == L'\''))
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
    {
        value.pop_back();
    }
    return value;
}

inline std::wstring UnquoteIniString(const std::wstring& text)
{
    std::wstring s = Trim(text);
    if (s.size() < 2)
    {
        return s;
    }

    wchar_t quote = s.front();
    if ((quote != L'\"' && quote != L'\'') || s.back() != quote)
    {
        return s;
    }

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

inline bool HasLaterQuote(const std::wstring& text, size_t start, wchar_t quote)
{
    for (size_t i = start; i < text.size(); ++i)
    {
        if (text[i] == quote && !IsBackslashEscaped(text, i))
        {
            return true;
        }
    }
    return false;
}

inline std::wstring ParseIniName(std::wstring text)
{
    return UnquoteIniString(std::move(text));
}

inline bool TryParseIniSectionHeader(std::wstring line, std::wstring& name)
{
    StripLeadingIniBom(line);
    std::wstring s = Trim(line);
    if (s.empty() || s.front() == L';' || s.front() == L'#' || s.front() != L'[')
    {
        return false;
    }

    bool inQuote = false;
    wchar_t quoteChar = 0;
    for (size_t i = 1; i < s.size(); ++i)
    {
        wchar_t ch = s[i];
        bool escaped = IsBackslashEscaped(s, i);
        if (!escaped && (ch == L'\"' || ch == L'\''))
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
            std::wstring rest = Trim(s.substr(i + 1));
            if (!rest.empty() && rest.front() != L';' && rest.front() != L'#')
            {
                return false;
            }
            name = ParseIniName(s.substr(1, i - 1));
            return true;
        }
    }
    return false;
}

inline std::wstring ParseIniValue(std::wstring text)
{
    std::wstring s = Trim(text);
    if (s.empty())
    {
        return s;
    }

    wchar_t quote = s.front();
    if (quote != L'\"' && quote != L'\'')
    {
        return UnquoteIniString(StripIniInlineComment(std::move(text), false));
    }

    std::wstring out;
    for (size_t i = 1; i < s.size(); ++i)
    {
        wchar_t ch = s[i];
        if (ch == L'\\' && i + 1 < s.size() && (s[i + 1] == quote || s[i + 1] == L'\\'))
        {
            // Match DesktopStub's INI behavior: quoted Windows paths ending in
            // backslash remain parseable, while actual escaped quotes/backslashes
            // are decoded.
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
        {
            return out;
        }
        out.push_back(ch);
    }

    return UnquoteIniString(StripIniInlineComment(std::move(text)));
}

inline std::wstring QuoteIniString(const std::wstring& text)
{
    std::wstring out;
    out.reserve(text.size() + 2);
    out.push_back(L'\"');
    for (wchar_t ch : text)
    {
        if (ch == L'\"' || ch == L'\\')
        {
            out.push_back(L'\\');
        }
        out.push_back(ch);
    }
    out.push_back(L'\"');
    return out;
}

inline std::wstring FormatIniAssignment(const std::wstring& key, const std::wstring& value)
{
    return QuoteIniString(key) + L" = " + QuoteIniString(value);
}

inline std::vector<IniSectionData> ParseIniDocument(const std::wstring& text)
{
    std::vector<IniSectionData> doc;
    IniSectionData* current = nullptr;

    auto findSection = [&](const std::wstring& name) -> IniSectionData*
    {
        for (auto& sec : doc)
        {
            if (IniEquals(sec.name, name))
            {
                return &sec;
            }
        }
        return nullptr;
    };

    auto findKey = [](IniSectionData& sec, const std::wstring& key) -> IniEntry*
    {
        for (auto& entry : sec.entries)
        {
            if (IniEquals(entry.key, key))
            {
                return &entry;
            }
        }
        return nullptr;
    };

    auto lines = SplitIniLines(text);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring line = lines[i];
        if (i == 0)
        {
            StripLeadingIniBom(line);
        }

        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#')
        {
            continue;
        }

        std::wstring sectionName;
        if (TryParseIniSectionHeader(line, sectionName))
        {
            if (IniSectionData* existing = findSection(sectionName))
            {
                current = existing;
            }
            else
            {
                doc.push_back(IniSectionData{ sectionName, {} });
                current = &doc.back();
            }
            continue;
        }

        if (!current)
        {
            continue;
        }

        size_t equals = line.find(L'=');
        if (equals == std::wstring::npos)
        {
            continue;
        }

        std::wstring key = ParseIniName(line.substr(0, equals));
        std::wstring value = ParseIniValue(line.substr(equals + 1));

        if (IniEntry* existing = findKey(*current, key))
        {
            existing->value = value;
        }
        else
        {
            current->entries.push_back(IniEntry{ key, value });
        }
    }
    return doc;
}

inline bool ReadIniValueFromDoc(const std::vector<IniSectionData>& doc, const wchar_t* section, const wchar_t* key, std::wstring& out)
{
    std::wstring sectionName = section ? section : L"";
    std::wstring keyName = key ? key : L"";
    for (const auto& sec : doc)
    {
        if (!IniEquals(sec.name, sectionName))
        {
            continue;
        }
        for (const auto& entry : sec.entries)
        {
            if (IniEquals(entry.key, keyName))
            {
                out = entry.value;
                return true;
            }
        }
    }
    return false;
}

inline bool LoadIniText(const std::wstring& path, std::wstring& text)
{
    if (ReadTextFileUtf8BomAware(path, text))
    {
        return true;
    }
    DWORD readError = GetLastError();
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES)
    {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND || err == ERROR_INVALID_NAME)
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
    SetLastError(readError == ERROR_SUCCESS ? ERROR_READ_FAULT : readError);
    return false;
}

inline bool LoadIniDocument(const std::wstring& path, std::vector<IniSectionData>& doc)
{
    std::wstring text;
    if (!LoadIniText(path, text))
    {
        doc.clear();
        return false;
    }
    doc = ParseIniDocument(text);
    return true;
}

inline bool IniHasKeyInDoc(const std::vector<IniSectionData>& doc, const wchar_t* section, const wchar_t* key)
{
    std::wstring ignored;
    return ReadIniValueFromDoc(doc, section, key, ignored);
}

inline std::wstring IniReadRaw(const std::wstring& path, const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    std::vector<IniSectionData> doc;
    if (!LoadIniDocument(path, doc))
    {
        return fallback ? fallback : L"";
    }
    std::wstring value;
    if (!ReadIniValueFromDoc(doc, section, key, value))
    {
        return fallback ? fallback : L"";
    }
    return value;
}

inline bool IniHasKey(const std::wstring& path, const wchar_t* section, const wchar_t* key)
{
    std::vector<IniSectionData> doc;
    return LoadIniDocument(path, doc) && IniHasKeyInDoc(doc, section, key);
}

inline bool WriteIniValueToText(std::wstring& text, const wchar_t* section, const wchar_t* key, const std::wstring& value, const std::wstring& header = L"")
{
    std::wstring sectionName = section ? section : L"";
    std::wstring keyName = key ? key : L"";

    if (text.empty() && !header.empty())
    {
        text = header;
    }

    std::vector<std::wstring> lines = SplitIniLines(text);
    if (!lines.empty())
    {
        StripLeadingIniBom(lines[0]);
    }

    bool inTargetSection = false;
    bool sawTargetSection = false;
    bool updated = false;
    size_t insertPos = lines.size();

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring trimmed = Trim(lines[i]);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#')
        {
            continue;
        }

        std::wstring sectionHeaderName;
        if (TryParseIniSectionHeader(lines[i], sectionHeaderName))
        {
            if (inTargetSection && insertPos == lines.size())
            {
                insertPos = i;
            }
            inTargetSection = IniEquals(sectionHeaderName, sectionName);
            if (inTargetSection && !sawTargetSection)
            {
                sawTargetSection = true;
                insertPos = lines.size();
            }
            continue;
        }

        if (!inTargetSection)
        {
            continue;
        }

        size_t equals = lines[i].find(L'=');
        if (equals == std::wstring::npos)
        {
            continue;
        }

        std::wstring existingKey = ParseIniName(lines[i].substr(0, equals));
        if (IniEquals(existingKey, keyName))
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
                TrimTrailingIniBlankLines(lines);
                lines.push_back(newEntry);
            }
            else
            {
                lines.insert(lines.begin() + static_cast<std::vector<std::wstring>::difference_type>(insertPos), newEntry);
            }
        }
        else
        {
            TrimTrailingIniBlankLines(lines);
            if (!lines.empty() && !lines.back().empty())
            {
                lines.push_back(L"");
            }
            lines.push_back(L"[" + sectionName + L"]");
            lines.push_back(newEntry);
        }
    }

    TrimTrailingIniBlankLines(lines);
    text = JoinIniLines(lines);
    if (!text.empty())
    {
        text += L"\r\n";
    }
    return true;
}

inline bool RemoveIniValueFromText(
    std::wstring& text,
    const wchar_t* section,
    const wchar_t* key,
    bool* removed = nullptr)
{
    std::wstring sectionName = section ? section : L"";
    std::wstring keyName = key ? key : L"";
    std::vector<std::wstring> lines = SplitIniLines(text);
    if (!lines.empty())
    {
        StripLeadingIniBom(lines[0]);
    }

    bool inTargetSection = false;
    bool didRemove = false;
    std::vector<std::wstring> kept;
    kept.reserve(lines.size());
    for (const std::wstring& line : lines)
    {
        std::wstring trimmed = Trim(line);
        std::wstring sectionHeaderName;
        if (!trimmed.empty() &&
            trimmed.front() != L';' &&
            trimmed.front() != L'#' &&
            TryParseIniSectionHeader(line, sectionHeaderName))
        {
            inTargetSection = IniEquals(sectionHeaderName, sectionName);
            kept.push_back(line);
            continue;
        }

        bool removeLine = false;
        if (inTargetSection &&
            !trimmed.empty() &&
            trimmed.front() != L';' &&
            trimmed.front() != L'#')
        {
            size_t equals = line.find(L'=');
            if (equals != std::wstring::npos)
            {
                removeLine = IniEquals(ParseIniName(line.substr(0, equals)), keyName);
            }
        }

        if (removeLine)
        {
            didRemove = true;
        }
        else
        {
            kept.push_back(line);
        }
    }

    TrimTrailingIniBlankLines(kept);
    text = JoinIniLines(kept);
    if (!text.empty())
    {
        text += L"\r\n";
    }
    if (removed)
    {
        *removed = didRemove;
    }
    return true;
}

inline bool RemoveIniSectionFromText(
    std::wstring& text,
    const wchar_t* section,
    bool* removed = nullptr)
{
    std::wstring sectionName = section ? section : L"";
    std::vector<std::wstring> lines = SplitIniLines(text);
    if (!lines.empty())
    {
        StripLeadingIniBom(lines[0]);
    }

    bool inTargetSection = false;
    bool didRemove = false;
    std::vector<std::wstring> kept;
    kept.reserve(lines.size());
    for (const std::wstring& line : lines)
    {
        std::wstring trimmed = Trim(line);
        std::wstring sectionHeaderName;
        if (!trimmed.empty() &&
            trimmed.front() != L';' &&
            trimmed.front() != L'#' &&
            TryParseIniSectionHeader(line, sectionHeaderName))
        {
            inTargetSection = IniEquals(sectionHeaderName, sectionName);
            if (inTargetSection)
            {
                didRemove = true;
                continue;
            }
        }

        if (!inTargetSection)
        {
            kept.push_back(line);
        }
    }

    TrimTrailingIniBlankLines(kept);
    text = JoinIniLines(kept);
    if (!text.empty())
    {
        text += L"\r\n";
    }
    if (removed)
    {
        *removed = didRemove;
    }
    return true;
}

inline bool IniWriteRaw(
    const std::wstring& path,
    const wchar_t* section,
    const wchar_t* key,
    const std::wstring& value,
    const std::wstring& header = L"",
    DWORD writeMutexWaitMs = INFINITE)
{
    IniWriteMutexGuard guard(path, writeMutexWaitMs);
    if (!guard.IsLocked())
    {
        SetLastError(guard.Error());
        return false;
    }

    std::wstring text;
    if (!LoadIniText(path, text))
    {
        return false;
    }
    if (!WriteIniValueToText(text, section, key, value, header))
    {
        if (GetLastError() == ERROR_SUCCESS)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
        }
        return false;
    }
    return WriteTextFileUtf8Bom(path, text);
}

struct IniDefaultValue
{
    const wchar_t* section;
    const wchar_t* key;
    const wchar_t* value;
};

inline bool IniEnsureDefaultsRaw(
    const std::wstring& path,
    const IniDefaultValue* defaults,
    size_t defaultCount,
    const std::wstring& header = L"",
    DWORD writeMutexWaitMs = INFINITE)
{
    IniWriteMutexGuard guard(path, writeMutexWaitMs);
    if (!guard.IsLocked())
    {
        SetLastError(guard.Error());
        return false;
    }

    std::wstring text;
    if (!LoadIniText(path, text))
    {
        return false;
    }

    bool changed = false;
    std::vector<IniSectionData> doc = ParseIniDocument(text);
    for (size_t i = 0; i < defaultCount; ++i)
    {
        const IniDefaultValue& entry = defaults[i];
        if (IniHasKeyInDoc(doc, entry.section, entry.key))
        {
            continue;
        }
        if (!WriteIniValueToText(text, entry.section, entry.key, entry.value ? entry.value : L"", header))
        {
            if (GetLastError() == ERROR_SUCCESS)
            {
                SetLastError(ERROR_INVALID_PARAMETER);
            }
            return false;
        }
        doc = ParseIniDocument(text);
        changed = true;
    }

    if (!changed && !text.empty())
    {
        return true;
    }
    if (text.empty() && !header.empty())
    {
        text = header;
    }
    return WriteTextFileUtf8Bom(path, text);
}

class IniConfigStore
{
public:
    explicit IniConfigStore(std::wstring path, std::wstring header = L"", DWORD writeMutexWaitMs = INFINITE)
        : path_(std::move(path)),
          header_(std::move(header)),
          writeMutexWaitMs_(writeMutexWaitMs)
    {
    }

    const std::wstring& Path() const
    {
        return path_;
    }

    std::wstring ReadRaw(const wchar_t* section, const wchar_t* key, const wchar_t* fallback) const
    {
        return IniReadRaw(path_, section, key, fallback);
    }

    bool WriteRaw(const wchar_t* section, const wchar_t* key, const std::wstring& value) const
    {
        return IniWriteRaw(path_, section, key, value, header_, writeMutexWaitMs_);
    }

    bool EnsureDefaults(const IniDefaultValue* defaults, size_t defaultCount) const
    {
        return IniEnsureDefaultsRaw(path_, defaults, defaultCount, header_, writeMutexWaitMs_);
    }

    bool ReadFreshText(std::wstring& text) const
    {
        return LoadIniText(path_, text);
    }

    template <typename Mutator>
    bool MutateFresh(Mutator&& mutator) const
    {
        IniWriteMutexGuard guard(path_, writeMutexWaitMs_);
        if (!guard.IsLocked())
        {
            SetLastError(guard.Error());
            return false;
        }

        std::wstring text;
        if (!LoadIniText(path_, text))
        {
            return false;
        }
        if (text.empty() && !header_.empty())
        {
            text = header_;
        }
        SetLastError(ERROR_SUCCESS);
        if (!std::forward<Mutator>(mutator)(text))
        {
            if (GetLastError() == ERROR_SUCCESS)
            {
                SetLastError(ERROR_INVALID_DATA);
            }
            return false;
        }
        return WriteTextFileUtf8Bom(path_, text);
    }

    bool WriteFreshText(const std::wstring& text) const
    {
        IniWriteMutexGuard guard(path_, writeMutexWaitMs_);
        if (!guard.IsLocked())
        {
            SetLastError(guard.Error());
            return false;
        }
        return WriteTextFileUtf8Bom(path_, text);
    }

private:
    std::wstring path_;
    std::wstring header_;
    DWORD writeMutexWaitMs_ = INFINITE;
};
}

#endif

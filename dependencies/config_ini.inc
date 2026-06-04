#ifndef AIP_DEPENDENCIES_CONFIG_INI_INC
#define AIP_DEPENDENCIES_CONFIG_INI_INC

namespace aip
{
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

inline bool ReadWholeFileBytes(const std::wstring& path, std::vector<BYTE>& bytes)
{
    bytes.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 32ll * 1024ll * 1024ll)
    {
        CloseHandle(file);
        SetLastError(ERROR_FILE_TOO_LARGE);
        return false;
    }

    bytes.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE;
    DWORD err = ok ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (!ok)
    {
        SetLastError(err);
        return false;
    }
    bytes.resize(read);
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
    return MultiByteToWideChar(codePage, flags, reinterpret_cast<LPCCH>(data), static_cast<int>(size), out.data(), need) > 0;
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
    if (!ReadWholeFileBytes(path, bytes))
    {
        return false;
    }
    return DecodeTextBytes(bytes, text);
}

inline bool WriteTextFileUtf8Bom(const std::wstring& path, const std::wstring& text)
{
    EnsureDirectory(GetDirectoryName(path));
    std::wstring temp = path + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    static const unsigned char bom[] = { 0xef, 0xbb, 0xbf };
    DWORD written = 0;
    bool ok = WriteFile(file, bom, static_cast<DWORD>(sizeof(bom)), &written, nullptr) != FALSE;
    std::string utf8 = WideToUtf8(text);
    if (ok && !utf8.empty())
    {
        ok = WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE;
    }
    CloseHandle(file);

    if (!ok)
    {
        DeleteFileW(temp.c_str());
        return false;
    }

    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(temp.c_str());
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
        if (key.empty())
        {
            continue;
        }

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
    if (sectionName.empty() || keyName.empty())
    {
        return false;
    }

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

inline bool IniWriteRaw(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& value, const std::wstring& header = L"")
{
    std::wstring text;
    if (!LoadIniText(path, text))
    {
        return false;
    }
    if (!WriteIniValueToText(text, section, key, value, header))
    {
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

inline bool IniEnsureDefaultsRaw(const std::wstring& path, const IniDefaultValue* defaults, size_t defaultCount, const std::wstring& header = L"")
{
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
}

#endif

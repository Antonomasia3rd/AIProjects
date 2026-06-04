#ifndef AIP_DEPENDENCIES_CONFIG_INI_INC
#define AIP_DEPENDENCIES_CONFIG_INI_INC

namespace aip
{
struct IniDocument
{
    std::vector<std::wstring> sectionOrder;
    std::map<std::wstring, std::vector<std::wstring>, std::less<>> keyOrder;
    std::map<std::wstring, std::map<std::wstring, std::wstring, std::less<>>, std::less<>> values;
};

inline std::wstring NormalizeIniSectionName(const std::wstring& value)
{
    return ToLower(Trim(value));
}

inline std::wstring NormalizeIniKeyName(const std::wstring& value)
{
    return ToLower(Trim(value));
}

inline bool ReadTextFileUtf8BomAware(const std::wstring& path, std::wstring& text)
{
    text.clear();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > 32ll * 1024ll * 1024ll)
    {
        CloseHandle(file);
        return false;
    }

    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    bool ok = bytes.empty() || ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) != FALSE;
    CloseHandle(file);
    if (!ok)
    {
        return false;
    }
    bytes.resize(read);

    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xef &&
        static_cast<unsigned char>(bytes[1]) == 0xbb &&
        static_cast<unsigned char>(bytes[2]) == 0xbf)
    {
        bytes.erase(0, 3);
    }

    text = Utf8ToWide(bytes);
    return true;
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

inline void IniRememberSection(IniDocument& doc, const std::wstring& section)
{
    if (std::find(doc.sectionOrder.begin(), doc.sectionOrder.end(), section) == doc.sectionOrder.end())
    {
        doc.sectionOrder.push_back(section);
    }
}

inline void IniRememberKey(IniDocument& doc, const std::wstring& section, const std::wstring& key)
{
    IniRememberSection(doc, section);
    auto& keys = doc.keyOrder[section];
    if (std::find(keys.begin(), keys.end(), key) == keys.end())
    {
        keys.push_back(key);
    }
}

inline IniDocument ParseIniText(const std::wstring& text)
{
    IniDocument doc;
    std::wstring section;
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t end = text.find_first_of(L"\r\n", pos);
        std::wstring line = end == std::wstring::npos ? text.substr(pos) : text.substr(pos, end - pos);
        if (end == std::wstring::npos)
        {
            pos = text.size() + 1;
        }
        else
        {
            pos = end + 1;
            if (pos < text.size() && text[end] == L'\r' && text[pos] == L'\n')
            {
                ++pos;
            }
        }

        std::wstring trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#')
        {
            continue;
        }
        if (trimmed.front() == L'[' && trimmed.back() == L']')
        {
            section = NormalizeIniSectionName(trimmed.substr(1, trimmed.size() - 2));
            IniRememberSection(doc, section);
            continue;
        }

        size_t equals = line.find(L'=');
        if (equals == std::wstring::npos || section.empty())
        {
            continue;
        }

        std::wstring key = NormalizeIniKeyName(line.substr(0, equals));
        std::wstring value = line.substr(equals + 1);
        while (!value.empty() && (value.back() == L'\r' || value.back() == L'\n'))
        {
            value.pop_back();
        }
        if (!key.empty())
        {
            IniRememberKey(doc, section, key);
            doc.values[section][key] = value;
        }
    }
    return doc;
}

inline std::wstring SerializeIniDocument(const IniDocument& doc, const std::wstring& header = L"")
{
    std::wstring text = header;
    if (!text.empty() && text.find_last_of(L"\r\n") != text.size() - 1)
    {
        text += L"\r\n";
    }

    for (const auto& section : doc.sectionOrder)
    {
        auto sectionIt = doc.values.find(section);
        if (sectionIt == doc.values.end() || sectionIt->second.empty())
        {
            continue;
        }
        if (!text.empty() && text.substr(text.size() >= 2 ? text.size() - 2 : 0) != L"\r\n")
        {
            text += L"\r\n";
        }
        text += L"[" + section + L"]\r\n";
        auto keysIt = doc.keyOrder.find(section);
        if (keysIt != doc.keyOrder.end())
        {
            for (const auto& key : keysIt->second)
            {
                auto valueIt = sectionIt->second.find(key);
                if (valueIt != sectionIt->second.end())
                {
                    text += key + L"=" + valueIt->second + L"\r\n";
                }
            }
        }
        text += L"\r\n";
    }
    return text;
}

inline bool LoadIniDocument(const std::wstring& path, IniDocument& doc)
{
    std::wstring text;
    if (!ReadTextFileUtf8BomAware(path, text))
    {
        doc = IniDocument();
        return false;
    }
    doc = ParseIniText(text);
    return true;
}

inline std::wstring IniReadRaw(const std::wstring& path, const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    IniDocument doc;
    LoadIniDocument(path, doc);
    std::wstring normalizedSection = NormalizeIniSectionName(section ? section : L"");
    std::wstring normalizedKey = NormalizeIniKeyName(key ? key : L"");
    auto sectionIt = doc.values.find(normalizedSection);
    if (sectionIt == doc.values.end())
    {
        return fallback ? fallback : L"";
    }
    auto keyIt = sectionIt->second.find(normalizedKey);
    if (keyIt == sectionIt->second.end())
    {
        return fallback ? fallback : L"";
    }
    return keyIt->second;
}

inline bool IniHasKey(const std::wstring& path, const wchar_t* section, const wchar_t* key)
{
    IniDocument doc;
    LoadIniDocument(path, doc);
    std::wstring normalizedSection = NormalizeIniSectionName(section ? section : L"");
    std::wstring normalizedKey = NormalizeIniKeyName(key ? key : L"");
    auto sectionIt = doc.values.find(normalizedSection);
    return sectionIt != doc.values.end() && sectionIt->second.find(normalizedKey) != sectionIt->second.end();
}

inline bool IniWriteRaw(const std::wstring& path, const wchar_t* section, const wchar_t* key, const std::wstring& value, const std::wstring& header = L"")
{
    IniDocument doc;
    LoadIniDocument(path, doc);
    std::wstring normalizedSection = NormalizeIniSectionName(section ? section : L"");
    std::wstring normalizedKey = NormalizeIniKeyName(key ? key : L"");
    if (normalizedSection.empty() || normalizedKey.empty())
    {
        return false;
    }

    IniRememberKey(doc, normalizedSection, normalizedKey);
    doc.values[normalizedSection][normalizedKey] = value;
    return WriteTextFileUtf8Bom(path, SerializeIniDocument(doc, header));
}
}

#endif

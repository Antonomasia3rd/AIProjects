#ifndef AIP_DEPENDENCIES_CORE_INC
#define AIP_DEPENDENCIES_CORE_INC

namespace aip
{
inline std::wstring Trim(std::wstring value)
{
    size_t first = 0;
    while (first < value.size() && iswspace(value[first]))
    {
        ++first;
    }

    size_t last = value.size();
    while (last > first && iswspace(value[last - 1]))
    {
        --last;
    }
    return value.substr(first, last - first);
}

inline std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

inline bool EqualsI(const std::wstring& left, const wchar_t* right)
{
    return _wcsicmp(left.c_str(), right ? right : L"") == 0;
}

inline bool StartsWithI(const std::wstring& value, const wchar_t* prefix)
{
    size_t len = wcslen(prefix);
    return value.size() >= len && _wcsnicmp(value.c_str(), prefix, len) == 0;
}

inline int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

inline std::wstring GetLastErrorText(DWORD error)
{
    if (error == 0)
    {
        return L"";
    }

    wchar_t* buffer = nullptr;
    DWORD count = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        error,
        0,
        reinterpret_cast<LPWSTR>(&buffer),
        0,
        nullptr);

    std::wstring text;
    if (count != 0 && buffer != nullptr)
    {
        text.assign(buffer, count);
        while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ' || text.back() == L'\t'))
        {
            text.pop_back();
        }
        LocalFree(buffer);
    }
    else
    {
        wchar_t fallback[64] = {};
        swprintf_s(fallback, L"Win32 error %lu", error);
        text = fallback;
    }

    return text;
}

inline std::wstring GetDirectoryName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
    {
        return L".";
    }
    if (slash == 2 && path.size() > 2 && path[1] == L':')
    {
        return path.substr(0, 3);
    }
    return path.substr(0, slash);
}

inline std::wstring GetFileBaseName(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t start = slash == std::wstring::npos ? 0 : slash + 1;
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < start)
    {
        dot = path.size();
    }
    return path.substr(start, dot - start);
}

inline std::wstring PathJoin(const std::wstring& left, const std::wstring& right)
{
    if (left.empty())
    {
        return right;
    }
    if (right.empty())
    {
        return left;
    }

    std::wstring result = left;
    if (result.back() != L'\\' && result.back() != L'/')
    {
        result.push_back(L'\\');
    }
    result += right;
    return result;
}

inline void EnsureDirectory(const std::wstring& path)
{
    if (path.empty() || path == L".")
    {
        return;
    }

    std::wstring current;
    current.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        wchar_t ch = path[i];
        current.push_back(ch);
        if ((ch == L'\\' || ch == L'/') && current.size() > 3)
        {
            CreateDirectoryW(current.c_str(), nullptr);
        }
    }
    CreateDirectoryW(path.c_str(), nullptr);
}

inline bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

inline std::wstring MakeAbsolutePath(const std::wstring& path)
{
    if (path.empty())
    {
        return path;
    }

    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        return path;
    }

    std::vector<wchar_t> buffer(needed + 1);
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size())
    {
        return path;
    }
    return buffer.data();
}

inline std::string WideToUtf8(const std::wstring& value)
{
    if (value.empty())
    {
        return std::string();
    }

    int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0)
    {
        return std::string();
    }

    std::string result(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

inline std::wstring Utf8ToWide(const std::string& value)
{
    if (value.empty())
    {
        return std::wstring();
    }

    int length = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0)
    {
        return std::wstring();
    }

    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

inline std::wstring ReadEnvironmentString(const std::wstring& name)
{
    if (Trim(name).empty())
    {
        return L"";
    }

    DWORD needed = GetEnvironmentVariableW(name.c_str(), nullptr, 0);
    if (needed == 0)
    {
        return L"";
    }

    std::vector<wchar_t> buffer(needed + 1);
    DWORD written = GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (written == 0 || written >= buffer.size())
    {
        return L"";
    }
    return std::wstring(buffer.data(), written);
}

inline std::wstring BytesToHex(const std::vector<BYTE>& bytes)
{
    static constexpr wchar_t digits[] = L"0123456789abcdef";
    std::wstring hex;
    hex.reserve(bytes.size() * 2);
    for (BYTE byte : bytes)
    {
        hex.push_back(digits[(byte >> 4) & 0x0f]);
        hex.push_back(digits[byte & 0x0f]);
    }
    return hex;
}

inline bool HexToBytes(const std::wstring& hex, std::vector<BYTE>& bytes)
{
    auto hexValue = [](wchar_t ch) -> int {
        if (ch >= L'0' && ch <= L'9') return ch - L'0';
        if (ch >= L'a' && ch <= L'f') return ch - L'a' + 10;
        if (ch >= L'A' && ch <= L'F') return ch - L'A' + 10;
        return -1;
    };

    bytes.clear();
    if ((hex.size() % 2) != 0)
    {
        return false;
    }

    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2)
    {
        int hi = hexValue(hex[i]);
        int lo = hexValue(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            bytes.clear();
            return false;
        }
        bytes.push_back(static_cast<BYTE>((hi << 4) | lo));
    }
    return true;
}

inline std::string JsonEscape(const std::wstring& value)
{
    std::string utf8 = WideToUtf8(value);
    std::string out;
    out.reserve(utf8.size() + 2);
    out.push_back('"');
    for (unsigned char ch : utf8)
    {
        switch (ch)
        {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (ch < 0x20)
            {
                char escaped[8] = {};
                sprintf_s(escaped, "\\u%04x", ch);
                out += escaped;
            }
            else
            {
                out.push_back(static_cast<char>(ch));
            }
            break;
        }
    }
    out.push_back('"');
    return out;
}

inline void JsonFieldRaw(std::string& object, bool& first, const char* key, const std::string& rawValue)
{
    if (!first)
    {
        object.push_back(',');
    }
    first = false;
    object.push_back('"');
    object += key;
    object += "\":";
    object += rawValue;
}

inline void JsonFieldString(std::string& object, bool& first, const char* key, const std::wstring& value)
{
    JsonFieldRaw(object, first, key, JsonEscape(value));
}

inline void JsonFieldNumber(std::string& object, bool& first, const char* key, long long value)
{
    JsonFieldRaw(object, first, key, std::to_string(value));
}

inline void JsonFieldBool(std::string& object, bool& first, const char* key, bool value)
{
    JsonFieldRaw(object, first, key, value ? "true" : "false");
}

inline std::wstring ExtractJsonStringValue(const std::string& json, const char* key)
{
    std::string needle = std::string("\"") + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
    {
        return L"";
    }
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
    {
        return L"";
    }
    ++pos;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
    {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '"')
    {
        return L"";
    }
    ++pos;

    std::string out;
    bool escaped = false;
    for (; pos < json.size(); ++pos)
    {
        char ch = json[pos];
        if (escaped)
        {
            switch (ch)
            {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(ch); break;
            }
            escaped = false;
        }
        else if (ch == '\\')
        {
            escaped = true;
        }
        else if (ch == '"')
        {
            break;
        }
        else
        {
            out.push_back(ch);
        }
    }
    return Utf8ToWide(out);
}
}

#endif

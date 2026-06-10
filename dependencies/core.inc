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

inline bool TryMakeAbsolutePath(const std::wstring& path, std::wstring& absolutePath, std::wstring* error = nullptr)
{
    absolutePath.clear();
    if (path.empty())
    {
        if (error)
        {
            *error = L"path is empty";
        }
        SetLastError(ERROR_INVALID_PARAMETER);
        return false;
    }

    DWORD needed = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (needed == 0)
    {
        DWORD err = GetLastError();
        if (error)
        {
            *error = GetLastErrorText(err == ERROR_SUCCESS ? ERROR_INVALID_NAME : err);
        }
        SetLastError(err == ERROR_SUCCESS ? ERROR_INVALID_NAME : err);
        return false;
    }

    std::vector<wchar_t> buffer(needed + 1);
    DWORD written = GetFullPathNameW(path.c_str(), static_cast<DWORD>(buffer.size()), buffer.data(), nullptr);
    if (written == 0 || written >= buffer.size())
    {
        DWORD err = GetLastError();
        if (error)
        {
            *error = GetLastErrorText(err == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : err);
        }
        SetLastError(err == ERROR_SUCCESS ? ERROR_INSUFFICIENT_BUFFER : err);
        return false;
    }

    absolutePath.assign(buffer.data(), written);
    return true;
}

inline std::wstring MakeAbsolutePath(const std::wstring& path)
{
    std::wstring absolutePath;
    return TryMakeAbsolutePath(path, absolutePath, nullptr) ? absolutePath : path;
}

inline bool WriteAllBytes(HANDLE file, const void* data, DWORD size)
{
    const BYTE* cursor = static_cast<const BYTE*>(data);
    DWORD remaining = size;
    while (remaining > 0)
    {
        DWORD written = 0;
        if (!WriteFile(file, cursor, remaining, &written, nullptr) || written == 0)
        {
            return false;
        }
        cursor += written;
        remaining -= written;
    }
    return true;
}

inline bool TryWideToUtf8(const std::wstring& value, std::string& result)
{
    result.clear();
    if (value.empty())
    {
        SetLastError(ERROR_SUCCESS);
        return true;
    }

    if (value.size() > static_cast<size_t>(INT_MAX))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    int inputLength = static_cast<int>(value.size());
    int length = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        inputLength,
        nullptr,
        0,
        nullptr,
        nullptr);
    if (length <= 0)
    {
        DWORD error = GetLastError();
        SetLastError(error == ERROR_SUCCESS ? ERROR_NO_UNICODE_TRANSLATION : error);
        return false;
    }

    result.assign(static_cast<size_t>(length), '\0');
    int written = WideCharToMultiByte(
        CP_UTF8,
        WC_ERR_INVALID_CHARS,
        value.c_str(),
        inputLength,
        result.data(),
        length,
        nullptr,
        nullptr);
    if (written != length)
    {
        DWORD error = GetLastError();
        result.clear();
        SetLastError(error == ERROR_SUCCESS ? ERROR_NO_UNICODE_TRANSLATION : error);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    return true;
}

// Convenience wrapper for diagnostics/optional text. Use TryWideToUtf8 when
// empty output and conversion failure must be distinguished.
inline std::string WideToUtf8(const std::wstring& value)
{
    std::string result;
    return TryWideToUtf8(value, result) ? result : std::string();
}

inline std::string WideToUtf8Lossy(const std::wstring& value)
{
    std::string result;
    if (TryWideToUtf8(value, result))
    {
        return result;
    }
    if (value.empty() || value.size() > static_cast<size_t>(INT_MAX))
    {
        return std::string();
    }

    std::wstring sanitized;
    sanitized.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        wchar_t ch = value[i];
        if (ch >= 0xD800 && ch <= 0xDBFF)
        {
            if (i + 1 < value.size() && value[i + 1] >= 0xDC00 && value[i + 1] <= 0xDFFF)
            {
                sanitized.push_back(ch);
                sanitized.push_back(value[i + 1]);
                ++i;
            }
            else
            {
                sanitized.push_back(0xFFFD);
            }
            continue;
        }
        if (ch >= 0xDC00 && ch <= 0xDFFF)
        {
            sanitized.push_back(0xFFFD);
            continue;
        }
        sanitized.push_back(ch);
    }

    return TryWideToUtf8(sanitized, result) ? result : std::string();
}

inline bool TryUtf8ToWide(const std::string& value, std::wstring& result)
{
    result.clear();
    if (value.empty())
    {
        SetLastError(ERROR_SUCCESS);
        return true;
    }

    if (value.size() > static_cast<size_t>(INT_MAX))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return false;
    }

    int inputLength = static_cast<int>(value.size());
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength, nullptr, 0);
    if (length <= 0)
    {
        DWORD error = GetLastError();
        SetLastError(error == ERROR_SUCCESS ? ERROR_NO_UNICODE_TRANSLATION : error);
        return false;
    }

    result.assign(static_cast<size_t>(length), L'\0');
    int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength, result.data(), length);
    if (written != length)
    {
        DWORD error = GetLastError();
        result.clear();
        SetLastError(error == ERROR_SUCCESS ? ERROR_NO_UNICODE_TRANSLATION : error);
        return false;
    }

    SetLastError(ERROR_SUCCESS);
    return true;
}

// Convenience wrapper for diagnostics/optional text. Use TryUtf8ToWide when
// empty output and conversion failure must be distinguished.
inline std::wstring Utf8ToWide(const std::string& value)
{
    std::wstring result;
    return TryUtf8ToWide(value, result) ? result : std::wstring();
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
    std::string utf8 = WideToUtf8Lossy(value);
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

inline bool JsonIsHexDigit(char ch)
{
    return (ch >= '0' && ch <= '9') ||
        (ch >= 'a' && ch <= 'f') ||
        (ch >= 'A' && ch <= 'F');
}

inline int JsonHexValue(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

inline bool JsonReadHex4(const std::string& json, size_t pos, unsigned int& value)
{
    if (pos + 4 > json.size())
    {
        return false;
    }

    unsigned int result = 0;
    for (size_t i = 0; i < 4; ++i)
    {
        int digit = JsonHexValue(json[pos + i]);
        if (digit < 0)
        {
            return false;
        }
        result = (result << 4) | static_cast<unsigned int>(digit);
    }
    value = result;
    return true;
}

inline void JsonAppendUtf8CodePoint(std::string& out, unsigned int codePoint)
{
    if (codePoint <= 0x7f)
    {
        out.push_back(static_cast<char>(codePoint));
    }
    else if (codePoint <= 0x7ff)
    {
        out.push_back(static_cast<char>(0xc0 | ((codePoint >> 6) & 0x1f)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else if (codePoint <= 0xffff)
    {
        out.push_back(static_cast<char>(0xe0 | ((codePoint >> 12) & 0x0f)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
    else if (codePoint <= 0x10ffff)
    {
        out.push_back(static_cast<char>(0xf0 | ((codePoint >> 18) & 0x07)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 12) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | ((codePoint >> 6) & 0x3f)));
        out.push_back(static_cast<char>(0x80 | (codePoint & 0x3f)));
    }
}

inline bool JsonEscapeEnd(const std::string& json, size_t backslash, size_t& nextIndex)
{
    if (backslash + 1 >= json.size())
    {
        return false;
    }

    char esc = json[backslash + 1];
    switch (esc)
    {
    case '"':
    case '\\':
    case '/':
    case 'b':
    case 'f':
    case 'n':
    case 'r':
    case 't':
        nextIndex = backslash + 2;
        return true;
    case 'u':
        if (backslash + 5 >= json.size())
        {
            return false;
        }
        for (size_t i = 2; i < 6; ++i)
        {
            if (!JsonIsHexDigit(json[backslash + i]))
            {
                return false;
            }
        }
        nextIndex = backslash + 6;
        return true;
    default:
        return false;
    }
}

inline size_t JsonStringEnd(const std::string& json, size_t quote)
{
    if (quote >= json.size() || json[quote] != '"')
    {
        return std::string::npos;
    }

    for (size_t i = quote + 1; i < json.size(); ++i)
    {
        char ch = json[i];
        if (ch == '\\')
        {
            size_t next = 0;
            if (!JsonEscapeEnd(json, i, next))
            {
                return std::string::npos;
            }
            i = next - 1;
        }
        else if (static_cast<unsigned char>(ch) < 0x20)
        {
            return std::string::npos;
        }
        else if (ch == '"')
        {
            return i + 1;
        }
    }
    return std::string::npos;
}


inline bool JsonIsWhitespace(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

inline bool JsonIsDigit(char ch)
{
    return ch >= '0' && ch <= '9';
}

inline bool JsonIsNonZeroDigit(char ch)
{
    return ch >= '1' && ch <= '9';
}

inline bool JsonNumberTokenEnd(const std::string& json, size_t start, size_t end, size_t& tokenEnd)
{
    tokenEnd = start;
    if (tokenEnd < end && json[tokenEnd] == '-')
    {
        ++tokenEnd;
    }

    if (tokenEnd >= end)
    {
        return false;
    }

    if (json[tokenEnd] == '0')
    {
        ++tokenEnd;
    }
    else if (JsonIsNonZeroDigit(json[tokenEnd]))
    {
        while (tokenEnd < end && JsonIsDigit(json[tokenEnd]))
        {
            ++tokenEnd;
        }
    }
    else
    {
        return false;
    }

    if (tokenEnd < end && json[tokenEnd] == '.')
    {
        ++tokenEnd;
        if (tokenEnd >= end || !JsonIsDigit(json[tokenEnd]))
        {
            return false;
        }
        while (tokenEnd < end && JsonIsDigit(json[tokenEnd]))
        {
            ++tokenEnd;
        }
    }

    if (tokenEnd < end && (json[tokenEnd] == 'e' || json[tokenEnd] == 'E'))
    {
        ++tokenEnd;
        if (tokenEnd < end && (json[tokenEnd] == '+' || json[tokenEnd] == '-'))
        {
            ++tokenEnd;
        }
        if (tokenEnd >= end || !JsonIsDigit(json[tokenEnd]))
        {
            return false;
        }
        while (tokenEnd < end && JsonIsDigit(json[tokenEnd]))
        {
            ++tokenEnd;
        }
    }

    return true;
}

inline bool JsonPrimitiveTokenIsValid(const std::string& json, size_t tokenStart, size_t tokenEnd)
{
    if (tokenStart == tokenEnd)
    {
        return false;
    }

    size_t tokenLength = tokenEnd - tokenStart;
    if ((tokenLength == 4 && json.compare(tokenStart, tokenLength, "true") == 0) ||
        (tokenLength == 4 && json.compare(tokenStart, tokenLength, "null") == 0) ||
        (tokenLength == 5 && json.compare(tokenStart, tokenLength, "false") == 0))
    {
        return true;
    }

    size_t numberEnd = tokenStart;
    return JsonNumberTokenEnd(json, tokenStart, tokenEnd, numberEnd) && numberEnd == tokenEnd;
}

inline size_t JsonPrimitiveValueEnd(const std::string& json, size_t start)
{
    size_t rawEnd = start;
    while (rawEnd < json.size() && json[rawEnd] != ',' && json[rawEnd] != '}' && json[rawEnd] != ']')
    {
        ++rawEnd;
    }

    size_t tokenStart = start;
    while (tokenStart < rawEnd && JsonIsWhitespace(json[tokenStart]))
    {
        ++tokenStart;
    }

    size_t tokenEnd = rawEnd;
    while (tokenEnd > tokenStart && JsonIsWhitespace(json[tokenEnd - 1]))
    {
        --tokenEnd;
    }

    return JsonPrimitiveTokenIsValid(json, tokenStart, tokenEnd) ? rawEnd : std::string::npos;
}

inline size_t JsonValueEnd(const std::string& json, size_t start)
{
    if (start >= json.size())
    {
        return std::string::npos;
    }

    if (json[start] == '"')
    {
        return JsonStringEnd(json, start);
    }

    if (json[start] == '{' || json[start] == '[')
    {
        std::vector<char> stack;
        stack.reserve(8);
        bool inString = false;
        for (size_t i = start; i < json.size(); ++i)
        {
            char ch = json[i];
            if (inString)
            {
                if (ch == '\\')
                {
                    size_t next = 0;
                    if (!JsonEscapeEnd(json, i, next))
                    {
                        return std::string::npos;
                    }
                    i = next - 1;
                }
                else if (static_cast<unsigned char>(ch) < 0x20)
                {
                    return std::string::npos;
                }
                else if (ch == '"')
                {
                    inString = false;
                }
                continue;
            }

            if (ch == '"')
            {
                inString = true;
            }
            else if (ch == '{')
            {
                stack.push_back('}');
            }
            else if (ch == '[')
            {
                stack.push_back(']');
            }
            else if (ch == '}' || ch == ']')
            {
                if (stack.empty() || stack.back() != ch)
                {
                    return std::string::npos;
                }
                stack.pop_back();
                if (stack.empty())
                {
                    return i + 1;
                }
            }
        }
        return std::string::npos;
    }

    return JsonPrimitiveValueEnd(json, start);
}

inline bool DecodeJsonStringUtf8Range(const std::string& json, size_t valueStart, size_t valueEnd, std::string& value)
{
    value.clear();
    if (valueStart >= json.size() ||
        valueEnd <= valueStart + 1 ||
        json[valueStart] != '"' ||
        json[valueEnd - 1] != '"')
    {
        return false;
    }

    for (size_t pos = valueStart + 1; pos + 1 < valueEnd; ++pos)
    {
        char ch = json[pos];
        if (ch == '\\')
        {
            if (pos + 1 >= valueEnd - 1)
            {
                return false;
            }

            char esc = json[++pos];
            switch (esc)
            {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u':
            {
                unsigned int codeUnit = 0;
                if (!JsonReadHex4(json, pos + 1, codeUnit))
                {
                    return false;
                }
                pos += 4;

                unsigned int codePoint = codeUnit;
                if (codeUnit >= 0xd800 && codeUnit <= 0xdbff)
                {
                    if (pos + 6 >= valueEnd || json[pos + 1] != '\\' || json[pos + 2] != 'u')
                    {
                        return false;
                    }
                    unsigned int low = 0;
                    if (!JsonReadHex4(json, pos + 3, low) || low < 0xdc00 || low > 0xdfff)
                    {
                        return false;
                    }
                    pos += 6;
                    codePoint = 0x10000 + (((codeUnit - 0xd800) << 10) | (low - 0xdc00));
                }
                else if (codeUnit >= 0xdc00 && codeUnit <= 0xdfff)
                {
                    return false;
                }
                JsonAppendUtf8CodePoint(value, codePoint);
                break;
            }
            default:
                return false;
            }
        }
        else
        {
            unsigned char uch = static_cast<unsigned char>(ch);
            if (uch < 0x20)
            {
                return false;
            }
            value.push_back(ch);
        }
    }
    return true;
}

// Lightweight top-level object field scanner. It validates skipped values enough
// to avoid walking through malformed JSON and returns the first matching key.
inline bool FindJsonFieldValue(const std::string& json, const char* key, size_t& keyPos, size_t& valueStart, size_t& valueEnd)
{
    keyPos = std::string::npos;
    valueStart = std::string::npos;
    valueEnd = std::string::npos;

    if (key == nullptr || *key == '\0')
    {
        return false;
    }

    const size_t keyLength = strlen(key);
    size_t pos = 0;
    while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
    {
        ++pos;
    }
    if (pos >= json.size() || json[pos] != '{')
    {
        return false;
    }
    ++pos;

    bool firstMember = true;
    while (pos < json.size())
    {
        while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
        {
            ++pos;
        }
        if (pos >= json.size() || json[pos] == '}')
        {
            return false;
        }
        if (!firstMember)
        {
            if (json[pos] != ',')
            {
                return false;
            }
            ++pos;
            while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
            {
                ++pos;
            }
            if (pos >= json.size() || json[pos] == '}')
            {
                return false;
            }
        }
        firstMember = false;
        if (json[pos] != '"')
        {
            return false;
        }
        size_t stringEnd = JsonStringEnd(json, pos);
        if (stringEnd == std::string::npos)
        {
            return false;
        }

        size_t colon = stringEnd;
        while (colon < json.size() && isspace(static_cast<unsigned char>(json[colon])))
        {
            ++colon;
        }
        if (colon >= json.size() || json[colon] != ':')
        {
            return false;
        }

        size_t currentValueStart = colon + 1;
        while (currentValueStart < json.size() && isspace(static_cast<unsigned char>(json[currentValueStart])))
        {
            ++currentValueStart;
        }

        size_t currentValueEnd = JsonValueEnd(json, currentValueStart);
        if (currentValueEnd == std::string::npos || currentValueEnd < currentValueStart)
        {
            return false;
        }

        std::string decodedKey;
        if (!DecodeJsonStringUtf8Range(json, pos, stringEnd, decodedKey))
        {
            return false;
        }
        if (decodedKey.size() == keyLength && decodedKey == key)
        {
            keyPos = pos;
            valueStart = currentValueStart;
            valueEnd = currentValueEnd;
            return true;
        }

        pos = currentValueEnd;
        while (pos < json.size() && isspace(static_cast<unsigned char>(json[pos])))
        {
            ++pos;
        }
        if (pos < json.size() && json[pos] != ',' && json[pos] != '}')
        {
            return false;
        }
    }
    return false;
}

inline bool DecodeJsonStringRange(const std::string& json, size_t valueStart, size_t valueEnd, std::wstring& value)
{
    std::string utf8;
    if (!DecodeJsonStringUtf8Range(json, valueStart, valueEnd, utf8))
    {
        value.clear();
        return false;
    }
    return TryUtf8ToWide(utf8, value);
}

inline bool TryExtractJsonStringValue(const std::string& json, const char* key, std::wstring& value)
{
    value.clear();
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(json, key, keyPos, valueStart, valueEnd))
    {
        return false;
    }

    return DecodeJsonStringRange(json, valueStart, valueEnd, value);
}

inline std::wstring ExtractJsonStringValue(const std::string& json, const char* key)
{
    std::wstring out;
    return TryExtractJsonStringValue(json, key, out) ? out : std::wstring();
}
}

#endif

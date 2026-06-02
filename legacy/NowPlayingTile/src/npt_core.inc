std::wstring MediaSnapshot::FormatClockTime()
{
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%02u:%02u:%02u", st.wHour, st.wMinute, st.wSecond);
    return buffer;
}

static std::wstring GetLastErrorText(DWORD error)
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

static void EnsureDirectory(const std::wstring& path)
{
    if (path.empty())
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

static bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring PathJoin(const std::wstring& left, const std::wstring& right)
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

static std::wstring GetDirectoryName(const std::wstring& path)
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

static std::wstring GetFileBaseName(const std::wstring& path)
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

static void InitPaths()
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, buffer, ARRAYSIZE(buffer));
    if (length == 0 || length >= ARRAYSIZE(buffer))
    {
        g_exePath = APP_NAME;
        g_exeDir = L".";
        g_exeBaseName = APP_NAME;
    }
    else
    {
        g_exePath = buffer;
        g_exeDir = GetDirectoryName(g_exePath);
        g_exeBaseName = GetFileBaseName(g_exePath);
        if (g_exeBaseName.empty())
        {
            g_exeBaseName = APP_NAME;
        }
    }

    g_iniPath = PathJoin(g_exeDir, g_exeBaseName + L".ini");
    g_logPath = PathJoin(g_exeDir, g_exeBaseName + L".log");
}

static void Log(const wchar_t* fmt, ...)
{
    if (g_logPath.empty() || fmt == nullptr)
    {
        return;
    }

    wchar_t message[4096] = {};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, fmt, args);
    va_end(args);

    SYSTEMTIME st = {};
    GetLocalTime(&st);

    wchar_t prefix[64] = {};
    swprintf_s(prefix,
        L"%04u-%02u-%02u %02u:%02u:%02u  ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond);

    std::wstring line = prefix;
    line += message;
    line += L"\r\n";

    EnsureDirectory(GetDirectoryName(g_logPath));
    HANDLE file = CreateFileW(g_logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    int utf8Length = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (utf8Length > 1)
    {
        std::vector<char> bytes(static_cast<size_t>(utf8Length));
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), -1, bytes.data(), utf8Length, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(file, bytes.data(), static_cast<DWORD>(utf8Length - 1), &written, nullptr);
    }

    CloseHandle(file);
}

static std::wstring XmlEscape(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size() + 16);
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'\"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

static std::wstring FilePathToUri(const std::wstring& path)
{
    wchar_t uri[4096] = {};
    DWORD length = ARRAYSIZE(uri);
    HRESULT hr = UrlCreateFromPathW(path.c_str(), uri, &length, 0);
    if (SUCCEEDED(hr))
    {
        return uri;
    }

    std::wstring fallback = L"file:///";
    for (wchar_t ch : path)
    {
        fallback.push_back(ch == L'\\' ? L'/' : ch);
    }
    return fallback;
}

static bool EqualsIgnoreCase(const std::wstring& a, const wchar_t* b)
{
    return _wcsicmp(a.c_str(), b) == 0;
}

static std::wstring Trim(const std::wstring& value)
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

static std::wstring FirstNonEmpty(std::initializer_list<std::wstring> values)
{
    for (const auto& value : values)
    {
        if (!Trim(value).empty())
        {
            return value;
        }
    }
    return L"";
}

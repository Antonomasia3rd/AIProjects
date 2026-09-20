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

static std::wstring BuildTemporarySiblingPath(const std::wstring& path)
{
    static std::atomic<unsigned long> counter{ 0 };
    return path +
        L".tmp." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(GetCurrentThreadId()) +
        L"." + std::to_wstring(GetTickCount64()) +
        L"." + std::to_wstring(++counter);
}

static bool CommitTemporaryFile(
    const std::wstring& temporaryPath,
    const std::wstring& destinationPath)
{
    if (MoveFileExW(
            temporaryPath.c_str(),
            destinationPath.c_str(),
            MOVEFILE_REPLACE_EXISTING |
                MOVEFILE_COPY_ALLOWED |
                MOVEFILE_WRITE_THROUGH) != FALSE)
    {
        return true;
    }

    DWORD error = GetLastError();
    DeleteFileW(temporaryPath.c_str());
    SetLastError(error);
    return false;
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

static void InitPaths()
{
    aip::SidecarPaths paths = aip::BuildCurrentProcessSidecarPaths(APP_NAME);
    g_exePath = paths.exePath;
    g_exeDir = paths.exeDir;
    g_exeBaseName = paths.exeBaseName;
    g_iniPath = paths.configPath;
    g_logPath = paths.defaultLogPath;
    g_instanceIdentity = aip::BuildPathScopedInstanceIdentity(
        L"NowPlayingTile.App",
        L"NowPlayingTile.RestoreRunningInstance",
        WINDOW_CLASS_NAME,
        APP_NAME,
        g_iniPath);

    aip::Utf8LoggerOptions loggerOptions;
    loggerOptions.filePath = g_logPath;
    loggerOptions.lockWaitMs = 5000;
    g_logger.Configure(loggerOptions);
}

static bool QueryProcessImagePath(DWORD processId, std::wstring* path)
{
    path->clear();
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr)
    {
        return false;
    }

    std::vector<wchar_t> buffer(512);
    bool success = false;
    for (;;)
    {
        DWORD length = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(process, 0, buffer.data(), &length) != FALSE)
        {
            if (length != 0)
            {
                path->assign(buffer.data(), length);
                success = true;
            }
            break;
        }

        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 32768)
        {
            break;
        }
        buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)));
    }

    CloseHandle(process);
    return success;
}

static HWND FindWindowForCurrentExecutable(const wchar_t* windowClass, int retries, int delayMs)
{
    retries = (std::max)(retries, 0);
    delayMs = (std::max)(delayMs, 0);
    for (int attempt = 0; attempt <= retries; ++attempt)
    {
        HWND candidate = nullptr;
        while ((candidate = FindWindowExW(nullptr, candidate, windowClass, nullptr)) != nullptr)
        {
            DWORD processId = 0;
            GetWindowThreadProcessId(candidate, &processId);
            std::wstring candidatePath;
            if (processId != 0 &&
                QueryProcessImagePath(processId, &candidatePath) &&
                _wcsicmp(candidatePath.c_str(), g_exePath.c_str()) == 0)
            {
                return candidate;
            }
        }

        if (attempt < retries && delayMs > 0)
        {
            Sleep(static_cast<DWORD>(delayMs));
        }
    }
    return nullptr;
}

static void Log(const wchar_t* fmt, ...)
{
    if (fmt == nullptr)
    {
        return;
    }

    wchar_t message[4096] = {};
    va_list args;
    va_start(args, fmt);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, fmt, args);
    va_end(args);
    g_logger.Write(L"info", message);
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

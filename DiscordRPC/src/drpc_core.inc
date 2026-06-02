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

static std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(towlower(ch));
    });
    return value;
}

static bool EqualsI(const std::wstring& left, const wchar_t* right)
{
    return _wcsicmp(left.c_str(), right ? right : L"") == 0;
}

static bool StartsWithI(const std::wstring& value, const wchar_t* prefix)
{
    size_t len = wcslen(prefix);
    return value.size() >= len && _wcsnicmp(value.c_str(), prefix, len) == 0;
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

static void EnsureDirectory(const std::wstring& path)
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

static bool FileExists(const std::wstring& path)
{
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static std::wstring MakeAbsolutePath(const std::wstring& path)
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

static void InitPaths()
{
    wchar_t buffer[4096] = {};
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
    g_defaultLogPath = PathJoin(g_exeDir, g_exeBaseName + L".log");
    g_effectiveLogPath = g_defaultLogPath;
}

static std::string WideToUtf8(const std::wstring& value)
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

static std::wstring Utf8ToWide(const std::string& value)
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

static std::wstring DecodeEscapes(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i)
    {
        if (value[i] == L'\\' && i + 1 < value.size())
        {
            wchar_t next = value[++i];
            if (next == L'n')
            {
                out.push_back(L'\n');
            }
            else if (next == L'r')
            {
                out.push_back(L'\r');
            }
            else if (next == L't')
            {
                out.push_back(L'\t');
            }
            else
            {
                out.push_back(next);
            }
        }
        else
        {
            out.push_back(value[i]);
        }
    }
    return out;
}

static std::wstring EncodeEscapes(const std::wstring& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value)
    {
        if (ch == L'\n')
        {
            out += L"\\n";
        }
        else if (ch == L'\r')
        {
            out += L"\\r";
        }
        else if (ch == L'\t')
        {
            out += L"\\t";
        }
        else if (ch == L'\\')
        {
            out += L"\\\\";
        }
        else
        {
            out.push_back(ch);
        }
    }
    return out;
}

static std::wstring IniReadRaw(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    std::vector<wchar_t> buffer(8192);
    DWORD count = GetPrivateProfileStringW(section, key, fallback ? fallback : L"", buffer.data(), static_cast<DWORD>(buffer.size()), g_iniPath.c_str());
    if (count + 1 >= buffer.size())
    {
        buffer.resize(65536);
        count = GetPrivateProfileStringW(section, key, fallback ? fallback : L"", buffer.data(), static_cast<DWORD>(buffer.size()), g_iniPath.c_str());
    }
    return std::wstring(buffer.data(), count);
}

static std::wstring IniReadS(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    return DecodeEscapes(IniReadRaw(section, key, fallback));
}

static int IniReadI(const wchar_t* section, const wchar_t* key, int fallback)
{
    std::wstring raw = Trim(IniReadRaw(section, key, L""));
    if (raw.empty())
    {
        return fallback;
    }

    wchar_t* end = nullptr;
    long value = wcstol(raw.c_str(), &end, 10);
    if (end == raw.c_str() || *end != L'\0')
    {
        return fallback;
    }
    return static_cast<int>(value);
}

static bool IniReadB(const wchar_t* section, const wchar_t* key, bool fallback)
{
    std::wstring raw = ToLower(Trim(IniReadRaw(section, key, L"")));
    if (raw.empty())
    {
        return fallback;
    }
    return raw == L"1" || raw == L"true" || raw == L"yes" || raw == L"on";
}

static bool IniHasKey(const wchar_t* section, const wchar_t* key)
{
    constexpr const wchar_t* sentinel = L"\x1e__discordrpc_missing__";
    return IniReadRaw(section, key, sentinel) != sentinel;
}

static bool IniWriteRaw(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    EnsureDirectory(GetDirectoryName(g_iniPath));
    return WritePrivateProfileStringW(section, key, value.c_str(), g_iniPath.c_str()) != FALSE;
}

static bool IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return IniWriteRaw(section, key, EncodeEscapes(value));
}

static std::wstring GetUiString(const wchar_t* key, const std::wstring& fallback)
{
    return IniReadS(L"strings", key, fallback.c_str());
}

static void PrintLine(const std::wstring& line, bool error = false)
{
    FILE* stream = error ? stderr : stdout;
    std::string utf8 = WideToUtf8(line + L"\n");
    fwrite(utf8.data(), 1, utf8.size(), stream);
    fflush(stream);
}

static void AddRecentLogLine(const std::wstring& line)
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    g_recentLog.push_back(line);
    while (g_recentLog.size() > 250)
    {
        g_recentLog.pop_front();
    }
}

static void WriteLogLine(const wchar_t* level, const std::wstring& message)
{
    if (!g_loggingEnabled.load())
    {
        return;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t prefix[96] = {};
    swprintf_s(prefix,
        L"%04u-%02u-%02u %02u:%02u:%02u [%s] ",
        st.wYear,
        st.wMonth,
        st.wDay,
        st.wHour,
        st.wMinute,
        st.wSecond,
        level);

    std::wstring line = std::wstring(prefix) + message;
    AddRecentLogLine(line);

    if (!g_fileLoggingEnabled.load() || g_effectiveLogPath.empty())
    {
        return;
    }

    std::lock_guard<std::mutex> lock(g_logMutex);
    EnsureDirectory(GetDirectoryName(g_effectiveLogPath));
    HANDLE file = CreateFileW(g_effectiveLogPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return;
    }

    std::string utf8 = WideToUtf8(line + L"\r\n");
    DWORD written = 0;
    WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    CloseHandle(file);
}

static void LogInfo(const std::wstring& message)
{
    WriteLogLine(L"info", message);
}

static void LogError(const std::wstring& message)
{
    WriteLogLine(L"error", message);
}

static void LogDebug(const std::wstring& message)
{
    if (g_verboseLogging.load())
    {
        WriteLogLine(L"debug", message);
    }
}

static std::wstring RecentLogText()
{
    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring text;
    for (const auto& line : g_recentLog)
    {
        text += line;
        text += L"\r\n";
    }
    if (text.empty())
    {
        text = GetUiString(L"no_recent_log", L"No log entries yet.");
    }
    return text;
}

static bool OwnsConsole()
{
    DWORD processes[4] = {};
    DWORD count = GetConsoleProcessList(processes, ARRAYSIZE(processes));
    return count <= 1;
}

static void SetConsoleVisible(bool visible, bool safeOnly)
{
    HWND console = GetConsoleWindow();
    if (console == nullptr)
    {
        return;
    }
    if (safeOnly && !visible && !OwnsConsole())
    {
        return;
    }
    ShowWindow(console, visible ? SW_SHOW : SW_HIDE);
}

static std::string JsonEscape(const std::wstring& value)
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

static void JsonFieldRaw(std::string& object, bool& first, const char* key, const std::string& rawValue)
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

static void JsonFieldString(std::string& object, bool& first, const char* key, const std::wstring& value)
{
    JsonFieldRaw(object, first, key, JsonEscape(value));
}

static void JsonFieldNumber(std::string& object, bool& first, const char* key, long long value)
{
    JsonFieldRaw(object, first, key, std::to_string(value));
}

static void JsonFieldBool(std::string& object, bool& first, const char* key, bool value)
{
    JsonFieldRaw(object, first, key, value ? "true" : "false");
}

static std::wstring ExtractJsonStringValue(const std::string& json, const char* key)
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

static std::wstring NormalizeTransportMode(const std::wstring& raw)
{
    std::wstring mode = ToLower(Trim(raw));
    if (mode == L"ipc" || mode == L"auto" || mode == L"gateway")
    {
        return mode;
    }
    return L"ipc";
}

static std::wstring NormalizeStatus(const std::wstring& raw)
{
    std::wstring status = ToLower(Trim(raw));
    if (status == L"online" || status == L"idle" || status == L"dnd" || status == L"invisible")
    {
        return status;
    }
    return L"online";
}

static bool IsTokenEnvironmentReference(const std::wstring& token)
{
    return StartsWithI(Trim(token), L"env:");
}

static std::wstring ResolveDiscordToken()
{
    std::wstring tokenEnv = Trim(IniReadS(L"general", L"token_env", L""));
    if (!tokenEnv.empty())
    {
        std::wstring envToken = Trim(aip::ReadEnvironmentString(tokenEnv));
        if (!envToken.empty())
        {
            return envToken;
        }
        LogError(L"Configured Discord token environment variable is empty or missing: " + tokenEnv);
    }

    std::wstring protectedToken = Trim(IniReadS(L"general", L"token_protected", L""));
    if (!protectedToken.empty())
    {
        try
        {
            return Trim(aip::UnprotectSecretForCurrentUser(protectedToken));
        }
        catch (const std::exception& ex)
        {
            LogError(L"Could not decrypt [general] token_protected with DPAPI for this user: " + Utf8ToWide(ex.what()));
        }
    }

    std::wstring token = Trim(IniReadS(L"general", L"token", L""));
    if (IsTokenEnvironmentReference(token))
    {
        std::wstring inlineEnv = Trim(token.substr(4));
        std::wstring envToken = Trim(aip::ReadEnvironmentString(inlineEnv));
        if (!envToken.empty())
        {
            return envToken;
        }
        LogError(L"Configured Discord token environment variable is empty or missing: " + inlineEnv);
        return L"";
    }

    return token;
}

static bool ProtectDiscordTokenInConfig()
{
    std::wstring token = Trim(IniReadS(L"general", L"token", L""));
    if (token.empty() || IsTokenEnvironmentReference(token))
    {
        return true;
    }

    std::wstring protectedToken = Trim(IniReadS(L"general", L"token_protected", L""));
    if (protectedToken.empty())
    {
        try
        {
            IniWriteRaw(L"general", L"token_protected", aip::ProtectSecretForCurrentUser(token));
            LogInfo(L"Migrating [general] token to DPAPI-protected [general] token_protected.");
        }
        catch (const std::exception& ex)
        {
            LogError(L"Failed to protect Discord token in config: " + Utf8ToWide(ex.what()));
            return false;
        }
    }
    else
    {
        LogInfo(L"Clearing legacy plaintext [general] token because token_protected is already configured.");
    }

    IniWriteRaw(L"general", L"token", L"");
    return true;
}

static int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(maxValue, value));
}

static IpcOptions LoadIpcOptions()
{
    IpcOptions options;
    options.connectTimeoutMs = ClampInt(IniReadI(L"ipc", L"connect_timeout_ms", 250), 50, 30000);
    options.responseTimeoutMs = ClampInt(IniReadI(L"ipc", L"response_timeout_ms", 5000), 250, 120000);
    return options;
}

static std::wstring NonEmptyConfig(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    std::wstring value = Trim(IniReadS(section, key, fallback));
    return value.empty() ? std::wstring(fallback) : value;
}

static GatewayOptions LoadGatewayOptions()
{
    GatewayOptions options;
    options.systemLocale = NonEmptyConfig(L"gateway", L"system_locale", L"en-US");
    options.browserUserAgent = NonEmptyConfig(
        L"gateway",
        L"browser_user_agent",
        L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) discord/1.0.9181 Chrome/128.0.6613.186 Electron/32.2.7 Safari/537.36");
    options.browserVersion = NonEmptyConfig(L"gateway", L"browser_version", L"32.2.7");
    options.osVersion = NonEmptyConfig(L"gateway", L"os_version", L"10.0.26100");
    options.releaseChannel = NonEmptyConfig(L"gateway", L"release_channel", L"stable");
    options.clientBuildNumber = ClampInt(IniReadI(L"gateway", L"client_build_number", 390000), 1, INT_MAX);
    options.capabilities = ClampInt(IniReadI(L"gateway", L"capabilities", 65), 0, INT_MAX);
    options.connectTimeoutMs = ClampInt(IniReadI(L"gateway", L"connect_timeout_ms", 10000), 1000, 120000);
    options.helloTimeoutMs = ClampInt(IniReadI(L"gateway", L"hello_timeout_ms", 10000), 1000, 120000);
    options.readyTimeoutMs = ClampInt(IniReadI(L"gateway", L"ready_timeout_ms", 30000), 1000, 180000);
    options.sendTimeoutMs = ClampInt(IniReadI(L"gateway", L"send_timeout_ms", 10000), 1000, 120000);
    options.closeTimeoutMs = ClampInt(IniReadI(L"gateway", L"close_timeout_ms", 2000), 0, 30000);
    options.assetFetchTimeoutMs = ClampInt(IniReadI(L"gateway", L"asset_fetch_timeout_ms", 10000), 1000, 120000);
    options.assetFetchUserAgent = NonEmptyConfig(L"gateway", L"asset_fetch_user_agent", L"DiscordBot (https://github.com, 1)");
    return options;
}

static void ConfigureRuntimeFromConfig(const AppOptions& options)
{
    g_loggingEnabled = IniReadB(L"app", L"logging_enabled", true);
    g_fileLoggingEnabled = IniReadB(L"app", L"file_logging_enabled", true);
    g_verboseLogging = options.verbose || IniReadB(L"app", L"verbose_logging", false);
    g_notificationsEnabled = IniReadB(L"app", L"notifications_enabled", true);

    std::wstring configuredLog = Trim(IniReadS(L"app", L"log_path", L""));
    if (configuredLog.empty())
    {
        g_effectiveLogPath = g_defaultLogPath;
    }
    else if (PathIsRelativeW(configuredLog.c_str()))
    {
        g_effectiveLogPath = PathJoin(g_exeDir, configuredLog);
    }
    else
    {
        g_effectiveLogPath = configuredLog;
    }

    if (options.forceShowConsole)
    {
        SetConsoleVisible(true, false);
    }
    else if (options.forceHideConsole)
    {
        SetConsoleVisible(false, false);
    }
    else
    {
        SetConsoleVisible(IniReadB(L"app", L"show_console", false), true);
    }
}

static std::wstring StableHash(const std::wstring& value)
{
    uint64_t hash = 1469598103934665603ull;
    for (wchar_t ch : value)
    {
        hash ^= static_cast<uint64_t>(towlower(ch));
        hash *= 1099511628211ull;
    }

    wchar_t buffer[32] = {};
    swprintf_s(buffer, L"%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

static bool AcquireSingleInstance()
{
    if (!IniReadB(L"app", L"single_instance", true))
    {
        return true;
    }

    std::wstring identity = MakeAbsolutePath(g_iniPath) + L"|" + g_exeBaseName;
    std::wstring name = L"Local\\DiscordRPC." + StableHash(identity);
    g_singleInstanceMessageName = L"DiscordRPC.Stop." + StableHash(identity);
    g_singleInstanceMessage = RegisterWindowMessageW(g_singleInstanceMessageName.c_str());

    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (g_singleInstanceMutex == nullptr)
    {
        LogError(L"Could not create single-instance mutex: " + GetLastErrorText(GetLastError()));
        return true;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        LogError(L"DiscordRPC is already running for this config.");
        return false;
    }
    return true;
}

static void ReleaseSingleInstance()
{
    if (g_singleInstanceMutex != nullptr)
    {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
}

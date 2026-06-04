static std::wstring Trim(const std::wstring& value)
{
    return aip::Trim(value);
}

static std::wstring ToLower(std::wstring value)
{
    return aip::ToLower(std::move(value));
}

static bool EqualsI(const std::wstring& left, const wchar_t* right)
{
    return aip::EqualsI(left, right);
}

static bool StartsWithI(const std::wstring& value, const wchar_t* prefix)
{
    return aip::StartsWithI(value, prefix);
}

static std::wstring GetLastErrorText(DWORD error)
{
    return aip::GetLastErrorText(error);
}

static std::wstring GetDirectoryName(const std::wstring& path)
{
    return aip::GetDirectoryName(path);
}

static std::wstring GetFileBaseName(const std::wstring& path)
{
    return aip::GetFileBaseName(path);
}

static std::wstring PathJoin(const std::wstring& left, const std::wstring& right)
{
    return aip::PathJoin(left, right);
}

static void EnsureDirectory(const std::wstring& path)
{
    aip::EnsureDirectory(path);
}

static bool FileExists(const std::wstring& path)
{
    return aip::FileExists(path);
}

static std::string WideToUtf8(const std::wstring& value)
{
    return aip::WideToUtf8(value);
}

static std::wstring Utf8ToWide(const std::string& value)
{
    return aip::Utf8ToWide(value);
}

static std::wstring MakeAbsolutePath(const std::wstring& path)
{
    return aip::MakeAbsolutePath(path);
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
            else if (next == L'\\')
            {
                out.push_back(L'\\');
            }
            else
            {
                out.push_back(L'\\');
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
        else
        {
            out.push_back(ch);
        }
    }
    return out;
}

static std::wstring IniReadRaw(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    return aip::IniReadRaw(g_iniPath, section, key, fallback);
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
    bool parsed = false;
    return aip::ParseBoolValue(raw, parsed) ? parsed : fallback;
}

static bool IniHasKey(const wchar_t* section, const wchar_t* key)
{
    return aip::IniHasKey(g_iniPath, section, key);
}

static std::wstring DefaultConfigHeader()
{
    return
        L"; DiscordRPC - Configuration\r\n"
        L"; Generated values are safe defaults for the C++ Discord IPC/Gateway build.\r\n"
        L"; User-facing strings live in [strings]. Use \\n for line breaks.\r\n\r\n";
}

static bool IniWriteRaw(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return aip::IniWriteRaw(g_iniPath, section, key, value, DefaultConfigHeader());
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

static std::wstring FormatConfigWriteFailure(const std::wstring& action)
{
    DWORD error = GetLastError();
    std::wstring message = action + L" failed for " + MakeAbsolutePath(g_iniPath);
    if (error != ERROR_SUCCESS)
    {
        message += L" (error " + std::to_wstring(error) + L": " + GetLastErrorText(error) + L")";
    }
    else
    {
        message += L" (unknown error)";
    }
    return message;
}

static void ReportConfigWriteFailure(const std::wstring& action)
{
    std::wstring message = FormatConfigWriteFailure(action);
    LogError(message);
    PrintLine(L"ERROR: " + message, true);
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

static size_t JsonStringEnd(const std::string& json, size_t quote)
{
    if (quote >= json.size() || json[quote] != '"')
    {
        return std::string::npos;
    }

    bool escaped = false;
    for (size_t i = quote + 1; i < json.size(); ++i)
    {
        char ch = json[i];
        if (escaped)
        {
            escaped = false;
        }
        else if (ch == '\\')
        {
            escaped = true;
        }
        else if (ch == '"')
        {
            return i + 1;
        }
    }
    return std::string::npos;
}

static size_t JsonValueEnd(const std::string& json, size_t start)
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
        bool escaped = false;
        for (size_t i = start; i < json.size(); ++i)
        {
            char ch = json[i];
            if (inString)
            {
                if (escaped)
                {
                    escaped = false;
                }
                else if (ch == '\\')
                {
                    escaped = true;
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

    size_t end = start;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ']')
    {
        ++end;
    }
    return end;
}

static bool FindJsonFieldValue(const std::string& json, const char* key, size_t& keyPos, size_t& valueStart, size_t& valueEnd)
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
        if (json[pos] == ',')
        {
            ++pos;
            continue;
        }
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

        if (stringEnd == pos + keyLength + 2 && json.compare(pos + 1, keyLength, key) == 0)
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
        if (pos < json.size() && json[pos] == ',')
        {
            ++pos;
        }
        else if (pos < json.size() && json[pos] != '}')
        {
            return false;
        }
    }
    return false;
}

static void RemoveJsonObjectMember(std::string& objectJson, size_t keyPos, size_t valueEnd)
{
    if (keyPos >= objectJson.size() || valueEnd > objectJson.size() || valueEnd < keyPos)
    {
        return;
    }

    size_t removeStart = keyPos;
    while (removeStart > 0 && isspace(static_cast<unsigned char>(objectJson[removeStart - 1])))
    {
        --removeStart;
    }

    size_t removeEnd = valueEnd;
    while (removeEnd < objectJson.size() && isspace(static_cast<unsigned char>(objectJson[removeEnd])))
    {
        ++removeEnd;
    }

    if (removeEnd < objectJson.size() && objectJson[removeEnd] == ',')
    {
        ++removeEnd;
        while (removeEnd < objectJson.size() && isspace(static_cast<unsigned char>(objectJson[removeEnd])))
        {
            ++removeEnd;
        }
    }
    else if (removeStart > 0 && objectJson[removeStart - 1] == ',')
    {
        --removeStart;
        while (removeStart > 0 && isspace(static_cast<unsigned char>(objectJson[removeStart - 1])))
        {
            --removeStart;
        }
    }

    objectJson.erase(removeStart, removeEnd - removeStart);
}

static size_t FindJsonObjectStart(const std::string& json, size_t search)
{
    size_t pos = search;
    while (pos < json.size())
    {
        if (json[pos] == '"')
        {
            size_t end = JsonStringEnd(json, pos);
            if (end == std::string::npos)
            {
                return std::string::npos;
            }
            pos = end;
            continue;
        }
        if (json[pos] == '{')
        {
            return pos;
        }
        ++pos;
    }
    return std::string::npos;
}

static std::wstring ExtractJsonStringValue(const std::string& json, const char* key)
{
    size_t keyPos = 0;
    size_t valueStart = 0;
    size_t valueEnd = 0;
    if (!FindJsonFieldValue(json, key, keyPos, valueStart, valueEnd) ||
        valueStart >= json.size() ||
        valueEnd <= valueStart + 1 ||
        json[valueStart] != '"' ||
        json[valueEnd - 1] != '"')
    {
        return L"";
    }

    std::string out;
    bool escaped = false;
    for (size_t pos = valueStart + 1; pos + 1 < valueEnd; ++pos)
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

static bool ProtectDiscordTokenInConfig(bool replaceProtectedToken = false)
{
    std::wstring token = Trim(IniReadS(L"general", L"token", L""));
    if (token.empty() || IsTokenEnvironmentReference(token))
    {
        return true;
    }

    std::wstring protectedToken = Trim(IniReadS(L"general", L"token_protected", L""));
    if (protectedToken.empty() || replaceProtectedToken)
    {
        try
        {
            if (!IniWriteRaw(L"general", L"token_protected", aip::ProtectSecretForCurrentUser(token)))
            {
                ReportConfigWriteFailure(L"Writing DPAPI-protected Discord token");
                return false;
            }
            LogInfo(protectedToken.empty()
                ? L"Migrating [general] token to DPAPI-protected [general] token_protected."
                : L"Replacing DPAPI-protected [general] token_protected from an explicit plaintext token update.");
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

    if (!IniWriteRaw(L"general", L"token", L""))
    {
        ReportConfigWriteFailure(L"Clearing legacy plaintext Discord token");
        return false;
    }
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

static void ApplyRuntimeLoggingOverridesFromCommandLine(const AppOptions& options)
{
    for (const auto& setting : options.settings)
    {
        if (!EqualsI(setting.section, L"app"))
        {
            continue;
        }

        bool parsed = false;
        if (EqualsI(setting.key, L"logging_enabled") && aip::ParseBoolValue(setting.value, parsed))
        {
            g_loggingEnabled = parsed;
        }
        else if (EqualsI(setting.key, L"file_logging_enabled") && aip::ParseBoolValue(setting.value, parsed))
        {
            g_fileLoggingEnabled = parsed;
        }
        else if (EqualsI(setting.key, L"verbose_logging") && aip::ParseBoolValue(setting.value, parsed))
        {
            g_verboseLogging = options.verbose || parsed;
        }
    }
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

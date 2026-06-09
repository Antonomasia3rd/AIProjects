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

static void ConfigureLoggerFromRuntimeState();

static void ApplySidecarPaths(const aip::SidecarPaths& paths)
{
    g_exePath = paths.exePath.empty() ? APP_NAME : paths.exePath;
    g_exeDir = paths.exeDir.empty() ? L"." : paths.exeDir;
    g_exeBaseName = paths.exeBaseName.empty() ? APP_NAME : paths.exeBaseName;
    g_iniPath = paths.configPath;
    g_defaultLogPath = paths.defaultLogPath;
    g_effectiveLogPath = g_defaultLogPath;
    ConfigureLoggerFromRuntimeState();
}

static void InitPaths()
{
    ApplySidecarPaths(aip::BuildCurrentProcessSidecarPaths(APP_NAME));
}

static void ApplyConfiguredIniPath(const std::wstring& configPath)
{
    aip::SidecarPaths paths;
    std::wstring error;
    if (aip::TryBuildSidecarPathsFromExecutable(g_exePath, APP_NAME, configPath, paths, &error))
    {
        ApplySidecarPaths(paths);
        return;
    }

    // ParseCommandLine should already reject invalid --ini values. Keep this
    // fallback side-effect-free and deterministic if a future caller bypasses it.
    ApplySidecarPaths(aip::BuildSidecarPathsFromExecutable(g_exePath, APP_NAME));
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
    return aip::IniConfigStore(g_iniPath).ReadRaw(section, key, fallback);
}

static std::wstring IniReadS(const wchar_t* section, const wchar_t* key, const wchar_t* fallback)
{
    return aip::DecodeBackslashEscapes(IniReadRaw(section, key, fallback));
}

static int IniReadI(const wchar_t* section, const wchar_t* key, int fallback)
{
    std::wstring raw = Trim(IniReadRaw(section, key, L""));
    if (raw.empty())
    {
        return fallback;
    }

    int parsed = 0;
    return aip::ParseIntValue(raw, parsed) ? parsed : fallback;
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

static std::wstring DefaultConfigHeader()
{
    return
        L"; DiscordRPC - Configuration\r\n"
        L"; Generated values are safe defaults for the C++ Discord IPC/Gateway build.\r\n"
        L"; User-facing strings live in [strings]. Use \\n for line breaks.\r\n\r\n";
}

static bool IniWriteRawWithWait(const wchar_t* section, const wchar_t* key, const std::wstring& value, DWORD writeMutexWaitMs)
{
    return aip::IniConfigStore(g_iniPath, DefaultConfigHeader(), writeMutexWaitMs).WriteRaw(section, key, value);
}

static bool IniWriteRaw(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return IniWriteRawWithWait(section, key, value, g_iniWriteLockWaitMs.load());
}

static bool IniWriteWithWait(const wchar_t* section, const wchar_t* key, const std::wstring& value, DWORD writeMutexWaitMs)
{
    return IniWriteRawWithWait(section, key, EncodeEscapes(value), writeMutexWaitMs);
}

static bool IniWrite(const wchar_t* section, const wchar_t* key, const std::wstring& value)
{
    return IniWriteWithWait(section, key, value, g_iniWriteLockWaitMs.load());
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

static void ConfigureLoggerFromRuntimeState()
{
    aip::Utf8LoggerOptions options;
    options.enabled = g_loggingEnabled.load();
    options.fileEnabled = g_fileLoggingEnabled.load();
    options.filePath = g_effectiveLogPath;
    options.maxRecentLines = 250;
    options.lockWaitMs = g_logAppendLockWaitMs.load();
    g_logger.Configure(options);
}

static void WriteLogLine(const wchar_t* level, const std::wstring& message)
{
    g_logger.Write(level, message);
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
    std::wstring text;
    for (const auto& line : g_logger.RecentLines())
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

using aip::ExtractJsonStringValue;
using aip::TryExtractJsonStringValue;
using aip::FindJsonFieldValue;
using aip::DecodeJsonStringRange;
using aip::JsonEscape;
using aip::JsonFieldBool;
using aip::JsonFieldNumber;
using aip::JsonFieldRaw;
using aip::JsonFieldString;
using aip::JsonStringEnd;
using aip::JsonValueEnd;

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

static std::wstring ResolveDiscordToken()
{
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

    return Trim(IniReadS(L"general", L"token", L""));
}

static bool ProtectDiscordTokenInConfig(bool replaceProtectedToken = false)
{
    std::wstring token = Trim(IniReadS(L"general", L"token", L""));
    if (token.empty())
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
        else if (EqualsI(setting.key, L"log_append_lock_wait_ms"))
        {
            int lockWaitMs = 0;
            if (aip::ParseIntValue(setting.value, lockWaitMs))
            {
                g_logAppendLockWaitMs = static_cast<DWORD>(ClampInt(lockWaitMs, 0, 60000));
            }
        }
        else if (EqualsI(setting.key, L"ini_write_lock_wait_ms"))
        {
            int lockWaitMs = 0;
            if (aip::ParseIntValue(setting.value, lockWaitMs))
            {
                g_iniWriteLockWaitMs = static_cast<DWORD>(ClampInt(lockWaitMs, 0, 60000));
            }
        }
    }
    ConfigureLoggerFromRuntimeState();
}

static void ConfigureRuntimeFromConfig(const AppOptions& options)
{
    g_loggingEnabled = IniReadB(L"app", L"logging_enabled", true);
    g_fileLoggingEnabled = IniReadB(L"app", L"file_logging_enabled", true);
    g_verboseLogging = options.verbose || IniReadB(L"app", L"verbose_logging", false);
    g_notificationsEnabled = IniReadB(L"app", L"notifications_enabled", true);
    g_logAppendLockWaitMs = static_cast<DWORD>(ClampInt(IniReadI(L"app", L"log_append_lock_wait_ms", 5000), 0, 60000));
    g_iniWriteLockWaitMs = static_cast<DWORD>(ClampInt(IniReadI(L"app", L"ini_write_lock_wait_ms", 5000), 0, 60000));

    std::wstring configuredLog = Trim(IniReadS(L"app", L"log_path", L""));
    std::wstring effectiveLogPath;
    if (configuredLog.empty())
    {
        effectiveLogPath = g_defaultLogPath;
    }
    else if (PathIsRelativeW(configuredLog.c_str()))
    {
        effectiveLogPath = PathJoin(g_exeDir, configuredLog);
    }
    else
    {
        effectiveLogPath = configuredLog;
    }
    g_effectiveLogPath = std::move(effectiveLogPath);
    ConfigureLoggerFromRuntimeState();

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

static void ConfigureSingleInstanceIdentity()
{
    // Match DesktopStub's baseline rule: a resident instance is scoped by the
    // effective INI path. The default INI already changes when the executable
    // is renamed, so adding g_exeBaseName here would break explicit --ini
    // sharing between renamed copies.
    std::wstring scope = MakeAbsolutePath(g_iniPath);
    aip::InstanceIdentity identity = aip::BuildPathScopedInstanceIdentity(
        APP_NAME,
        L"DiscordRPC.Stop",
        WINDOW_CLASS_NAME,
        L"",
        scope);
    g_singleInstanceMutexName = std::move(identity.mutexName);
    g_singleInstanceMessageName = std::move(identity.messageName);
    g_instanceWindowTitle = std::move(identity.windowTitle);
    g_singleInstanceMessage = RegisterWindowMessageW(g_singleInstanceMessageName.c_str());
}

static bool AcquireSingleInstance()
{
    ConfigureSingleInstanceIdentity();

    if (!IniReadB(L"app", L"single_instance", true))
    {
        return true;
    }

    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, g_singleInstanceMutexName.c_str());
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

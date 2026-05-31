static std::wstring ExceptionTextToWide(const std::exception& ex)
{
    const char* text = ex.what();
    if (text && *text)
    {
        size_t len = 0;
        while (text[len])
            ++len;

        const BYTE* begin = reinterpret_cast<const BYTE*>(text);
        std::vector<BYTE> bytes(begin, begin + len);
        std::wstring decoded = DecodeProcessOutput(bytes);
        if (!decoded.empty())
            return decoded;
    }

    return g_ui.unknownException.empty() ? L"unknown exception" : g_ui.unknownException;
}

static std::wstring UnknownExceptionText()
{
    return g_ui.unknownException.empty() ? L"unknown exception" : g_ui.unknownException;
}
// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static void WriteConsoleLine(const std::wstring& line)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
    {
        std::wstring s = line;
        s += L"\r\n";
        DWORD written = 0;
        if (WriteConsoleW(h, s.c_str(), (DWORD)s.size(), &written, nullptr))
            return;
    }

    fwprintf(stdout, L"%ls\n", line.c_str());
    fflush(stdout);
}

static std::wstring GetLogPathCopy()
{
    std::lock_guard<std::mutex> lk(g_pathMutex);
    return g_logPath;
}

static void SetLogPathCopy(const std::wstring& path)
{
    std::lock_guard<std::mutex> lk(g_pathMutex);
    g_logPath = path;
}

std::wstring Win32ErrorString(DWORD err);
static std::wstring FormatWideV(const wchar_t* fmt, va_list ap);
std::wstring FormatWide(const wchar_t* fmt, ...);

void Log(const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::wstring msg = FormatWideV(fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    wchar_t ts[64];
    swprintf(ts, 64, L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        static_cast<int>(st.wYear),
        static_cast<int>(st.wMonth),
        static_cast<int>(st.wDay),
        static_cast<int>(st.wHour),
        static_cast<int>(st.wMinute),
        static_cast<int>(st.wSecond));

    std::wstring line = ts + msg;

    std::lock_guard<std::mutex> lk(g_logMutex);
    int configuredMaxLogLines = g_logBufferLines.load();
    if (configuredMaxLogLines < 1) configuredMaxLogLines = 1;
    if (configuredMaxLogLines > 100000) configuredMaxLogLines = 100000;
    size_t maxLogLines = static_cast<size_t>(configuredMaxLogLines);
    auto bufferLine = [&](const std::wstring& bufferedLine)
    {
        while (g_logBuf.size() >= maxLogLines)
            g_logBuf.erase(g_logBuf.begin());
        g_logBuf.push_back(bufferedLine);
        if (g_console)
            WriteConsoleLine(bufferedLine);
    };

    bufferLine(line);

    if (g_logging)
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
        {
            if (AppendUtf8TextFile(logPath, line + L"\r\n"))
            {
                g_logWriteFailureReported = false;
            }
            else
            {
                DWORD err = GetLastError();
                if (err == ERROR_SUCCESS)
                    err = ERROR_WRITE_FAULT;
                if (!g_logWriteFailureReported.exchange(true))
                {
                    const wchar_t* failureFmt = g_ui.logWriteFailed.empty()
                        ? L"[!] Log file write failed (error %lu: %s): %s"
                        : g_ui.logWriteFailed.c_str();
                    std::wstring failureLine = ts + FormatWide(failureFmt, err, Win32ErrorString(err).c_str(), logPath.c_str());
                    bufferLine(failureLine);
                }
            }
        }
    }
}

void LogText(const std::wstring& text)
{
    Log(L"%ls", text.c_str());
}

static void BindConsoleStreams()
{
    FILE* f = nullptr;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
    ConfigureConsoleCP();
}

static void ReplayLogBufferToConsole()
{
    std::lock_guard<std::mutex> lk(g_logMutex);
    for (const auto& line : g_logBuf)
        WriteConsoleLine(line);
}

static void LogWin32FailureText(const wchar_t* what, DWORD err)
{
    Log(g_ui.win32FailureFmt.c_str(), what, err, Win32ErrorString(err).c_str());
}

void LogWin32Failure(const wchar_t* what, DWORD err = GetLastError())
{
    LogWin32FailureText(what, err);
}

static void TrimIdleWorkingSetIfConfigured()
{
    if (g_compactCrtHeapOnIdle.load())
    {
        errno = 0;
        int heapStatus = _heapmin();
        if (heapStatus != 0 && errno != 0)
            Log(L"[w] _heapmin failed while compacting the idle CRT heap, errno=%d.", errno);
    }

    if (!g_trimWorkingSetOnIdle.load())
        return;

    // This trims the resident working set that Task Manager shows as "Memory".
    // It does not discard program state; pages are faulted back only if needed.
    SetLastError(ERROR_SUCCESS);
    if (!SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1)))
    {
        DWORD err = GetLastError();
        if (err != ERROR_SUCCESS)
            LogWin32FailureText(g_ui.trimWorkingSetOperation.c_str(), err);
    }
}

static void ApplyConsoleSetting(bool enable, bool writeIni, bool logState)
{
    std::lock_guard<std::mutex> applyLock(g_consoleApplyMutex);
    bool attached = GetConsoleWindow() != nullptr;

    if (enable)
    {
        if (g_consoleAttached && attached)
        {
            g_console = true;
        }
        else
        {
            if (!attached)
            {
                if (!AllocConsole())
                {
                    DWORD err = GetLastError();
                    g_console = false;
                    g_consoleAttached = false;
                    LogWin32FailureText(L"AllocConsole", err);
                    if (writeIni)
                        IniWrite(L"Settings", L"ShowConsole", L"0");
                    return;
                }
                attached = true;
            }

            if (!SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE))
                LogWin32FailureText(L"SetConsoleCtrlHandler", GetLastError());
            BindConsoleStreams();
            g_consoleAttached = true;
            g_console = true;
            ReplayLogBufferToConsole();
            LogText(g_ui.consoleAllocated);
        }
    }
    else
    {
        if (attached || g_consoleAttached)
            FreeConsole();
        g_consoleAttached = false;
        g_console = false;
    }

    if (writeIni)
        IniWrite(L"Settings", L"ShowConsole", g_console ? L"1" : L"0");
    if (logState)
        Log(g_ui.consoleSummary.c_str(), StateOn(g_console));
}

static void FlushStartupWarnings()
{
    std::vector<std::wstring> warnings;
    {
        std::lock_guard<std::mutex> lk(g_startupWarningMutex);
        warnings.swap(g_startupWarnings);
    }

    for (const auto& warning : warnings)
        LogText(warning);
}

int IniReadI(const wchar_t* s, const wchar_t* k, int d)
{
    std::wstring value;
    if (!ReadIniValueCached(s, k, value))
        return d;

    std::wstring trimmed = TrimCopy(value);
    if (trimmed.empty())
        return d;

    errno = 0;
    wchar_t* end = nullptr;
    long parsed = wcstol(trimmed.c_str(), &end, 10);
    if (end == trimmed.c_str() || errno == ERANGE || parsed < INT_MIN || parsed > INT_MAX)
        return d;

    while (end && *end && iswspace(*end) != 0)
        ++end;
    if (end && *end)
        return d;

    return static_cast<int>(parsed);
}

static int ClampInt(int value, int minValue, int maxValue)
{
    return std::max(minValue, std::min(value, maxValue));
}

static int ElapsedMs(steady_clock::time_point start, steady_clock::time_point end)
{
    auto ms = duration_cast<milliseconds>(end - start).count();
    if (ms < 0) return 0;
    if (ms > INT_MAX) return INT_MAX;
    return static_cast<int>(ms);
}

static int IniReadClampedI(const wchar_t* s, const wchar_t* k, int d, int minValue, int maxValue)
{
    return ClampInt(IniReadI(s, k, d), minValue, maxValue);
}

static std::wstring EffectiveConfiguredLogPath();

static void LoadRuntimeSettingsFromIni(bool includeTrayIcon)
{
    g_logging = IniReadI(L"Settings", L"Logging", 1) != 0;
    if (includeTrayIcon)
        g_tray = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    SetLogPathCopy(EffectiveConfiguredLogPath());

    ApplyConsoleSetting(IniReadI(L"Settings", L"ShowConsole", 0) != 0, false, false);
    g_generateOnStartup = IniReadI(L"Settings", L"GenerateOnStartup", 1) != 0;
    g_generateOnStartupOnlyWhenChanged = IniReadI(L"Settings", L"GenerateOnStartupOnlyWhenChanged", 1) != 0;
    g_liveTileUpdateMode = static_cast<int>(ParseLiveTileUpdateModeValue(
        IniReadS(L"Settings", L"ExperimentalLiveTileUpdate", L"Auto")));
    g_liveTileRelaunchOnSwitch = IniReadI(L"Settings", L"LiveTileRelaunchOnSwitch", 1) != 0;
    g_clearLiveTileOnShutdown = IniReadI(L"Settings", L"ClearLiveTileOnShutdown", 1) != 0;
    g_manifestCompatibilityTarget = static_cast<int>(ParseManifestCompatibilityTargetValue(
        IniReadS(L"Settings", L"AppxManifestTarget", L"Windows10")));
    g_hideDisabled = IniReadI(L"Settings", L"HideDisabledEntries", 0) != 0;
    g_generateDesktopIconForDisabledEntries = IniReadI(L"Settings", L"GenerateDesktopIconForDisabledEntries", 1) != 0;
    g_deleteDisabledAssets = IniReadI(L"Settings", L"DeleteDisabledAssets", 0) != 0;
    g_generateScaleAuto = IniReadI(L"Settings", L"GenerateScaleAuto", 1) != 0;
    g_generateScale100 = IniReadI(L"Settings", L"GenerateScale100", 1) != 0;
    g_generateScale125 = IniReadI(L"Settings", L"GenerateScale125", 1) != 0;
    g_generateScale150 = IniReadI(L"Settings", L"GenerateScale150", 1) != 0;
    g_generateScale200 = IniReadI(L"Settings", L"GenerateScale200", 0) != 0;
    g_generateScale400 = IniReadI(L"Settings", L"GenerateScale400", 0) != 0;
    g_showMenuAsDropdown = IniReadI(L"Settings", L"ShowMenuAsDropdown", 1) != 0;
    g_notificationsEnabled = IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0;
    g_notifyOnStart = IniReadI(L"Settings", L"NotifyOnStart", 0) != 0;
    g_notifyOnSuccess = IniReadI(L"Settings", L"NotifyOnSuccess", 0) != 0;
    g_notifyOnFailure = IniReadI(L"Settings", L"NotifyOnFailure", 1) != 0;
    g_notifyOnBusy = IniReadI(L"Settings", L"NotifyOnBusy", 0) != 0;
    g_notifyOnAlreadyRunning = IniReadI(L"Settings", L"NotifyOnAlreadyRunning", 0) != 0;
    g_notifyOnSingleInstanceFailure = IniReadI(L"Settings", L"NotifyOnSingleInstanceFailure", 1) != 0;
    g_listenWallpaper = IniReadI(L"Settings", L"ListenWallpaper", 1) != 0;
    g_listenFit = IniReadI(L"Settings", L"ListenFit", 1) != 0;
    g_disableFitting = IniReadI(L"Settings", L"DisableFitting", 0) != 0;
    g_wallpaperDetectionFallbackOnInvalid = IniReadI(L"Settings", L"WallpaperDetectionFallbackOnInvalid", 1) != 0;
    g_notificationTimeoutMs = IniReadClampedI(L"Settings", L"NotificationTimeoutMs", 4000, 1000, 60000);
    g_shutdownInitialNoticeMs = IniReadClampedI(L"Settings", L"ShutdownInitialNoticeMs", 1000, 250, 60000);
    g_shutdownRepeatNoticeMs = IniReadClampedI(L"Settings", L"ShutdownRepeatNoticeMs", 5000, 250, 60000);
    g_trayIconHiddenDelayMs = IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000);
    g_logBufferLines = IniReadClampedI(L"Settings", L"LogBufferLines", 1024, 1, 100000);
    g_logAppendLockWaitMs = IniReadClampedI(L"Settings", L"LogAppendLockWaitMs", 1000, 0, 60000);
    g_trimWorkingSetOnIdle = IniReadI(L"Settings", L"TrimWorkingSetOnIdle", 1) != 0;
    g_compactCrtHeapOnIdle = IniReadI(L"Settings", L"CompactCrtHeapOnIdle", 1) != 0;
    g_keepGdiPlusLoaded = IniReadI(L"Settings", L"KeepGdiPlusLoaded", 0) != 0;
    g_pollInitialDebounceBypassMs = IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000);
    g_wallpaperDetectionLogRepeatMs = IniReadClampedI(L"Settings", L"WallpaperDetectionLogRepeatMs", 60000, 0, 3600000);
    g_powerShellOutputMessageBoxChars = IniReadClampedI(L"Settings", L"PowerShellOutputMessageBoxChars", 6000, 0, 60000);
    g_iniCacheRefreshMs = IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000);
}

static void ReloadRuntimeSettingsFromIni()
{
    // TrayIcon is session-scoped after startup; do not resurrect or hide it
    // because the INI changed while this instance is already running.
    LoadRuntimeSettingsFromIni(false);
}

static std::wstring IntToWString(int value)
{
    wchar_t buf[32];
    swprintf(buf, _countof(buf), L"%d", value);
    return buf;
}

static std::wstring TimeoutDisplayText(int timeoutMs)
{
    if (timeoutMs <= 0)
        return g_ui.timeoutInfiniteText.empty() ? L"infinite" : g_ui.timeoutInfiniteText;
    return IntToWString(timeoutMs) + L" ms";
}

std::wstring IniReadS(const wchar_t* s, const wchar_t* k, const wchar_t* d)
{
    std::wstring out;
    if (ReadIniValueCached(s, k, out))
        return out;
    return d ? d : L"";
}

void IniWrite(const wchar_t* s, const wchar_t* k, const wchar_t* v)
{
    if (WriteIniValue(s, k, v))
        return;

    DWORD err = GetLastError();
    if (err == ERROR_SUCCESS)
        err = ERROR_WRITE_FAULT;

    const wchar_t* fmt = g_ui.iniWriteFailed.empty()
        ? L"[!] Failed to write INI value [%s] %s (error %lu: %s)."
        : g_ui.iniWriteFailed.c_str();
    Log(fmt, s ? s : L"", k ? k : L"", err, Win32ErrorString(err).c_str());
}


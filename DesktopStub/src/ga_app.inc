// -----------------------------------------------------------------------------
// Window proc
// -----------------------------------------------------------------------------
static bool ConsumeAppxActivationRequestFile();

static void ApplyTrayIconSettingFromIni()
{
    bool desiredTray = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    bool trayPresent = TrayIconPresent();

    if (desiredTray)
    {
        if (!trayPresent)
        {
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
            {
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
                g_tray = false;
                return;
            }
        }
        g_tray = true;
    }
    else
    {
        if (trayPresent)
            TrayRemove();
        g_tray = false;
    }

    Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
}

static void ApplySettingsChangedByCommandLine(bool applyTrayIcon)
{
    LiveTileUpdateMode liveTileBefore = ConfiguredLiveTileUpdateMode();
    ManifestCompatibilityTarget manifestTargetBefore = ConfiguredManifestCompatibilityTarget();
    ReloadRuntimeSettingsFromIni();
    if (applyTrayIcon)
        ApplyTrayIconSettingFromIni();

    if (liveTileBefore != ConfiguredLiveTileUpdateMode())
        QueueLiveTileModeReregistration();
    if (manifestTargetBefore != ConfiguredManifestCompatibilityTarget())
        Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());

    WakePollThread();
}

static std::wstring ConsumeCommandLineWallpaperRequest()
{
    std::wstring wallpaper = IniReadS(L"State", L"CommandLineWallpaperRequest", L"");
    if (!wallpaper.empty())
        IniWrite(L"State", L"CommandLineWallpaperRequest", L"");
    return wallpaper;
}

static void QueueCommandLineGenerationRequest()
{
    std::wstring wp = ConsumeCommandLineWallpaperRequest();
    if (wp.empty())
        wp = GetWallpaper();

    if (wp.empty())
    {
        LogText(g_ui.startupGenerationSkippedNoWallpaper);
        return;
    }

    QueueGenerate(wp, g_ui.commandLineGenerationRequestReason);
}

static void HandleSingleInstanceRequest(WPARAM request)
{
    bool applyTrayIcon = (request & SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON) != 0;
    bool regenerateManifest = (request & SINGLE_INSTANCE_FLAG_REGENERATE_MANIFEST) != 0;

    switch (request & SINGLE_INSTANCE_REQUEST_MASK)
    {
    case SINGLE_INSTANCE_REQUEST_EXIT:
        RequestGracefulShutdown(g_hwnd);
        return;

    case SINGLE_INSTANCE_REQUEST_RELOAD_SETTINGS:
        ApplySettingsChangedByCommandLine(applyTrayIcon);
        if (regenerateManifest)
            RegenerateAppxManifestFromConfigAndLog();
        return;

    case SINGLE_INSTANCE_REQUEST_GENERATE:
        ApplySettingsChangedByCommandLine(applyTrayIcon);
        if (regenerateManifest && !RegenerateAppxManifestFromConfigAndLog())
            return;
        QueueCommandLineGenerationRequest();
        return;

    case SINGLE_INSTANCE_REQUEST_REGENERATE_MANIFEST:
        ApplySettingsChangedByCommandLine(applyTrayIcon);
        RegenerateAppxManifestFromConfigAndLog();
        return;

    case SINGLE_INSTANCE_REQUEST_SHOW_MESSAGEBOX:
        HandleExistingInstanceMessageBoxRequest();
        return;

    case SINGLE_INSTANCE_REQUEST_SHOW:
    default:
        HandleExistingInstanceRequest();
        return;
    }
}

static void MaybeClearLiveTileOnShutdown()
{
    if (!g_clearLiveTileOnShutdown.load())
        return;

    // When switching from unpackaged/static mode to packaged Live Tile mode,
    // do not clear immediately before the packaged instance starts and updates.
    if (g_relaunchAfterExit.load() == 1)
        return;

    ManifestCompatibilityTarget target = ConfiguredManifestCompatibilityTarget();
    if (!CurrentProcessHasPackageIdentity() && target == ManifestCompatibilityTarget::Windows10)
    {
        if (ConfiguredLiveTileUpdateMode() == LiveTileUpdateMode::LiveTile ||
            EffectiveLiveTileUpdateMode() == LiveTileUpdateMode::LiveTile)
        {
            Log(g_ui.liveTileClearOnShutdownSkipped.c_str(), L"current process has no package identity");
        }
        return;
    }

    LogText(g_ui.liveTileClearOnShutdown);
    std::wstring failure;
    if (Appx_Clear_Or_Request_LiveTile(failure))
        LogText(g_ui.liveTileClearOnShutdownSuccess);
    else
        Log(g_ui.liveTileClearOnShutdownSkipped.c_str(), failure.empty() ? g_ui.unknownError.c_str() : failure.c_str());
}

static std::wstring ModeSwitchRelaunchArguments(bool forceGenerate, bool waitForThisProcessToExit)
{
    std::wstring args = L"--launched-from-mode-switch";
    if (waitForThisProcessToExit)
        args += L" --ga-wait-for-pid " + std::to_wstring(GetCurrentProcessId());
    if (forceGenerate)
        args += L" --generate";
    if (g_commandLine.customIniPath)
        args += L" --ini " + QuoteCommandLineArg(g_iniPath);
    return args;
}

static bool StartUnpackagedRelaunchViaCmdBroker(const std::wstring& arguments, DWORD* errorOut)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    wchar_t systemDir[MAX_PATH]{};
    UINT n = GetSystemDirectoryW(systemDir, _countof(systemDir));
    if (n == 0 || n >= _countof(systemDir))
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    std::wstring cmdPath = std::wstring(systemDir) + L"\\cmd.exe";
    std::wstring commandLine = QuoteCommandLineArg(cmdPath) + L" /d /c start \"\" " +
        QuoteCommandLineArg(g_exePath);
    if (!arguments.empty())
        commandLine += L" " + arguments;

    std::wstring workingDir = GetExeDir();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(),
        &si,
        &pi);

    if (!ok)
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool StartUnpackagedRelaunchDirect(const std::wstring& arguments)
{
    std::wstring workingDir = GetExeDir();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = g_exePath.c_str();
    sei.lpParameters = arguments.empty() ? nullptr : arguments.c_str();
    sei.lpDirectory = workingDir.empty() ? nullptr : workingDir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        LogWin32Failure(L"ShellExecuteExW(unpackaged relaunch direct fallback)");
        return false;
    }

    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    return true;
}

static bool StartUnpackagedRelaunchAfterExit()
{
    std::wstring arguments = ModeSwitchRelaunchArguments(false, true);

    // Use an unpackaged system broker instead of ShellExecute'ing the registered
    // EXE directly from the packaged process. Direct relaunch can preserve the
    // package identity, which makes the following manifest registration fail
    // with "the app is still running". The new instance also waits for this PID
    // to exit before touching the manifest.
    DWORD brokerError = ERROR_SUCCESS;
    if (StartUnpackagedRelaunchViaCmdBroker(arguments, &brokerError))
    {
        LogText(L"[i] Relaunched unpackaged desktop instance after Live Tile mode switch.");
        return true;
    }

    LogWin32Failure(L"CreateProcessW(cmd.exe unpackaged relaunch broker)", brokerError);
    if (StartUnpackagedRelaunchDirect(arguments))
    {
        LogText(L"[i] Relaunched desktop instance after Live Tile mode switch using direct fallback.");
        return true;
    }

    return false;
}

static bool StartPackagedRelaunchAfterExit()
{
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring failure;
    std::wstring arguments = ModeSwitchRelaunchArguments(true, false);
    if (ActivateRegisteredPackageApplication(manifestInfo, arguments.c_str(), failure))
    {
        LogText(L"[i] Relaunched packaged app instance after Live Tile mode switch.");
        return true;
    }

    Log(g_ui.liveTileClearOnShutdownSkipped.c_str(), failure.empty() ? L"packaged relaunch failed" : failure.c_str());
    return false;
}

static void ExecutePendingRelaunchAfterExit()
{
    int relaunch = g_relaunchAfterExit.exchange(0);
    if (relaunch == 1)
    {
        if (LiveTileModeReregistrationPending())
        {
            LogText(L"[!] Packaged relaunch skipped because Live Tile mode re-registration is still pending.");
            return;
        }
        StartPackagedRelaunchAfterExit();
    }
    else if (relaunch == 2)
        StartUnpackagedRelaunchAfterExit();
}

LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if (g_singleInstanceMessage && m == g_singleInstanceMessage)
    {
        HandleSingleInstanceRequest(w);
        return 0;
    }

    if (g_taskbarCreatedMessage && m == g_taskbarCreatedMessage)
    {
        if (g_tray)
        {
            ForgetTrayIconHandle();
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
        }
        return 0;
    }

    if (m == WM_APP_SHUTDOWN_READY)
    {
        if (g_shutdownRequested)
            PostQuitMessage(0);
        return 0;
    }

    if (m == WM_APP_REQUEST_SHUTDOWN)
    {
        g_consoleControlShutdownPending = false;
        RequestGracefulShutdown(h);
        return 0;
    }

    if (m == WM_TIMER && w == SHUTDOWN_NOTICE_TIMER_ID)
    {
        if (!g_shutdownRequested)
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            return 0;
        }

        if (!GenerationWorkPending())
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            PostQuitMessage(0);
            return 0;
        }

        NotifyShutdownDelayed();
        SetShutdownNoticeTimer(h, g_shutdownRepeatNoticeMs.load(), L"SetTimer(SHUTDOWN_NOTICE_TIMER_ID repeat)");
        return 0;
    }

    if (m == WM_TIMER && w == APPX_ACTIVATION_REQUEST_TIMER_ID)
    {
        ConsumeAppxActivationRequestFile();
        return 0;
    }

    if (m == WM_QUERYENDSESSION)
    {
        RequestGracefulShutdown(h);
        return TRUE;
    }

    if (m == WM_ENDSESSION)
    {
        if (w)
            RequestGracefulShutdown(h);
        else
            CancelRequestedShutdown(h);
        return 0;
    }

    if (m==WM_USER+1){
        if (l==WM_RBUTTONUP || l==WM_CONTEXTMENU){
            Menu(h);
            return 0;
        }
    }
    else if (m==WM_CLOSE){
        RequestGracefulShutdown(h);
        return 0;
    }
    else if (m==WM_DESTROY){
        g_shutdownWindow = nullptr;
        TrayRemove();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h,m,w,l);
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
static void ConfigureSingleInstanceIdentity(const std::wstring& exeDir, const std::wstring& iniPath, bool customIniPath)
{
    std::wstring scope = ToLowerCopy(customIniPath ? iniPath : exeDir);
    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, scope);
    std::wstring suffix = Hex64(hash);

    g_singleInstanceMutexName = std::wstring(SINGLE_INSTANCE_MUTEX_BASE) + L"." + suffix;
    g_singleInstanceMessageName = std::wstring(SINGLE_INSTANCE_MESSAGE_BASE) + L"." + suffix;
    g_instanceWindowTitle = std::wstring(WINDOW_CLASS_NAME) + L"." + suffix;
}

static HWND FindExistingInstanceWindow()
{
    int retries = IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200);
    int delayMs = IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000);
    for (int i = 0; i <= retries; ++i)
    {
        const wchar_t* title = g_instanceWindowTitle.empty() ? nullptr : g_instanceWindowTitle.c_str();
        HWND existing = FindWindowW(WINDOW_CLASS_NAME, title);
        if (!existing)
            existing = FindWindowExW(HWND_MESSAGE, nullptr, WINDOW_CLASS_NAME, title);
        if (existing)
            return existing;
        if (i < retries && delayMs > 0)
            Sleep(static_cast<DWORD>(delayMs));
    }
    return nullptr;
}

static bool SignalExistingInstance(WPARAM request = SINGLE_INSTANCE_REQUEST_SHOW)
{
    if (!g_singleInstanceMessage)
        return false;

    HWND existing = FindExistingInstanceWindow();
    if (!existing)
        return false;

    return PostMessageW(existing, g_singleInstanceMessage, request, 0) != FALSE;
}

static std::wstring AppxActivationTraceLogPath()
{
    return GetExeDir() + L"\\GenerateAssets.appxactivation.log";
}

static void AppxActivationTrace(const std::wstring& message)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t ts[64];
    swprintf(ts, _countof(ts), L"[%04d-%02d-%02d %02d:%02d:%02d] ",
        static_cast<int>(st.wYear),
        static_cast<int>(st.wMonth),
        static_cast<int>(st.wDay),
        static_cast<int>(st.wHour),
        static_cast<int>(st.wMinute),
        static_cast<int>(st.wSecond));
    AppendUtf8TextFile(AppxActivationTraceLogPath(),
        std::wstring(ts) + L"pid=" + std::to_wstring(GetCurrentProcessId()) + L" " + message + L"\r\n");
}

static std::wstring AppxActivationRequestPath()
{
    return GetExeDir() + L"\\GenerateAssets.appxactivation.request";
}

static bool WriteAppxActivationRequest(const wchar_t* action)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::wstring text = L"action=";
    text += action ? action : L"show-messagebox";
    text += L"\r\npid=" + std::to_wstring(GetCurrentProcessId());
    text += L"\r\ntime=";
    text += std::to_wstring(st.wYear) + L"-" + std::to_wstring(st.wMonth) + L"-" + std::to_wstring(st.wDay) +
        L" " + std::to_wstring(st.wHour) + L":" + std::to_wstring(st.wMinute) + L":" + std::to_wstring(st.wSecond);
    text += L"\r\n";

    bool ok = WriteUtf8BomTextFile(AppxActivationRequestPath(), text);
    if (!ok)
    {
        DWORD err = GetLastError();
        if (err == ERROR_SUCCESS)
            err = ERROR_WRITE_FAULT;
        AppxActivationTrace(L"failed to write activation request file, error=" + std::to_wstring(err));
    }
    else
    {
        AppxActivationTrace(L"wrote activation request file: " + AppxActivationRequestPath());
    }
    return ok;
}

static bool ConsumeAppxActivationRequestFile()
{
    std::wstring path = AppxActivationRequestPath();
    DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return false;

    std::wstring text;
    if (!ReadTextFile(path, text))
        return false;

    DeleteFileW(path.c_str());
    Log(L"[i] AppX activation request received: %s", TrimCopy(text).c_str());
    HandleExistingInstanceMessageBoxRequest();
    return true;
}

static std::wstring FileNameOfPath(std::wstring path)
{
    size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        path.erase(0, slash + 1);
    return path;
}

static bool SamePathI(const std::wstring& a, const std::wstring& b)
{
    return IEquals(a, b);
}

static bool QueryProcessImagePath(DWORD pid, std::wstring& path)
{
    path.clear();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return false;

    wchar_t buf[MAX_PATH * 4]{};
    DWORD len = _countof(buf);
    BOOL ok = QueryFullProcessImageNameW(h, 0, buf, &len);
    CloseHandle(h);
    if (!ok || len == 0)
        return false;

    path.assign(buf, len);
    return true;
}

static bool IsNormalHostProcessRunningForAppxActivation(const std::wstring& hostPath)
{
    DWORD self = GetCurrentProcessId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
    {
        DWORD err = GetLastError();
        AppxActivationTrace(L"CreateToolhelp32Snapshot failed, error=" + std::to_wstring(err));
        return false;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    std::wstring hostFile = FileNameOfPath(hostPath);

    for (BOOL ok = Process32FirstW(snap, &pe); ok; ok = Process32NextW(snap, &pe))
    {
        if (pe.th32ProcessID == self)
            continue;
        if (!IEquals(pe.szExeFile, hostFile))
            continue;

        std::wstring imagePath;
        if (QueryProcessImagePath(pe.th32ProcessID, imagePath))
        {
            if (!SamePathI(imagePath, hostPath))
                continue;
        }

        AppxActivationTrace(L"normal host process detected, pid=" + std::to_wstring(pe.th32ProcessID));
        found = true;
        break;
    }

    CloseHandle(snap);
    if (!found)
        AppxActivationTrace(L"normal host process not detected");
    return found;
}

static std::wstring AppxActivationHostExecutablePath()
{
    std::wstring dir = GetExeDir();
    if (dir.empty())
        return g_exePath;

    // If this process was started through the Win8/Win8.1 manifest, it should be
    // the copied activation-stub executable. Launch the normal host executable
    // name so the new process is not the registered immersive executable again.
    std::wstring base = g_exePath;
    size_t slash = base.find_last_of(L"\\/");
    if (slash != std::wstring::npos)
        base.erase(0, slash + 1);

    if (IEquals(base, ManifestAppxActivationStubExecutableName()))
        return dir + L"\\" + ManifestHostExecutableName();

    return g_exePath;
}

static bool ShellExecuteNormalInstanceAfterAppxActivation(const std::wstring& hostPath, DWORD* errorOut)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    std::wstring workingDir = GetExeDir();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = hostPath.c_str();
    sei.lpDirectory = workingDir.empty() ? nullptr : workingDir.c_str();
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei))
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    if (sei.hProcess)
        CloseHandle(sei.hProcess);
    return true;
}

static bool CreateProcessNormalInstanceAfterAppxActivation(const std::wstring& hostPath, DWORD* errorOut)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    std::wstring commandLine = QuoteCommandLineArg(hostPath) + L" --launched-from-appx-stub";
    std::wstring workingDir = GetExeDir();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(),
        &si,
        &pi);

    if (!ok)
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    DWORD waitResult = WaitForInputIdle(pi.hProcess, 3000);
    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE)
        AppxActivationTrace(L"CreateProcess child exited early, exitCode=" + std::to_wstring(exitCode));
    else
        AppxActivationTrace(L"CreateProcess child is still running, WaitForInputIdle=" + std::to_wstring(waitResult));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

static bool CmdStartNormalInstanceAfterAppxActivation(const std::wstring& hostPath, DWORD* errorOut)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    wchar_t systemDir[MAX_PATH]{};
    UINT n = GetSystemDirectoryW(systemDir, _countof(systemDir));
    if (n == 0 || n >= _countof(systemDir))
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    std::wstring cmdPath = std::wstring(systemDir) + L"\\cmd.exe";
    std::wstring commandLine = QuoteCommandLineArg(cmdPath) + L" /d /c start \"\" " +
        QuoteCommandLineArg(hostPath) + L" --launched-from-appx-stub";
    std::wstring workingDir = GetExeDir();

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCommandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDir.empty() ? nullptr : workingDir.c_str(),
        &si,
        &pi);

    if (!ok)
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    WaitForSingleObject(pi.hProcess, 5000);
    DWORD exitCode = STILL_ACTIVE;
    if (GetExitCodeProcess(pi.hProcess, &exitCode))
        AppxActivationTrace(L"cmd /c start broker exitCode=" + std::to_wstring(exitCode));

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4505) // obsolete AppX launch-forwarding fallback kept for diagnostics/rollback
#endif
static bool LaunchNormalInstanceAfterAppxActivation(DWORD* errorOut = nullptr)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    std::wstring hostPath = AppxActivationHostExecutablePath();
    AppxActivationTrace(L"activation host path: " + hostPath);

    if (hostPath.empty())
    {
        if (errorOut)
            *errorOut = ERROR_INVALID_PARAMETER;
        AppxActivationTrace(L"launch failed: empty host path");
        return false;
    }

    DWORD attrs = GetFileAttributesW(hostPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY))
    {
        DWORD err = GetLastError();
        if (err == ERROR_SUCCESS)
            err = ERROR_FILE_NOT_FOUND;
        if (errorOut)
            *errorOut = err;
        AppxActivationTrace(L"launch failed: host executable missing, error=" + std::to_wstring(err));
        return false;
    }

    if (IsNormalHostProcessRunningForAppxActivation(hostPath))
    {
        WriteAppxActivationRequest(L"show-messagebox");
        return true;
    }

    DWORD shellError = ERROR_SUCCESS;
    if (ShellExecuteNormalInstanceAfterAppxActivation(hostPath, &shellError))
    {
        AppxActivationTrace(L"ShellExecuteEx launch succeeded");
        return true;
    }

    AppxActivationTrace(L"ShellExecuteEx launch failed, error=" + std::to_wstring(shellError));

    DWORD cmdError = ERROR_SUCCESS;
    if (CmdStartNormalInstanceAfterAppxActivation(hostPath, &cmdError))
    {
        AppxActivationTrace(L"cmd /c start broker launch attempted");
        return true;
    }

    AppxActivationTrace(L"cmd /c start broker failed, error=" + std::to_wstring(cmdError));

    DWORD processError = ERROR_SUCCESS;
    if (CreateProcessNormalInstanceAfterAppxActivation(hostPath, &processError))
    {
        AppxActivationTrace(L"CreateProcess launch succeeded");
        return true;
    }

    AppxActivationTrace(L"CreateProcess launch failed, error=" + std::to_wstring(processError));
    if (errorOut)
        *errorOut = processError != ERROR_SUCCESS ? processError : (cmdError != ERROR_SUCCESS ? cmdError : shellError);
    return false;
}
#ifdef _MSC_VER
#pragma warning(pop)
#endif


static HANDLE g_liveTileOopServerDoneEvent = nullptr;

static std::wstring HStringToWideForTrace(HSTRING value)
{
    UINT32 len = 0;
    const wchar_t* raw = WindowsGetStringRawBuffer(value, &len);
    return raw ? std::wstring(raw, raw + len) : std::wstring();
}

struct LiveTileOopInspectable final : ::IInspectable
{
    volatile ULONG refCount = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(::IInspectable))
        {
            *object = static_cast<::IInspectable*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(reinterpret_cast<volatile LONG*>(&refCount)));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG value = static_cast<ULONG>(InterlockedDecrement(reinterpret_cast<volatile LONG*>(&refCount)));
        if (!value)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) override
    {
        if (!iidCount || !iids)
            return E_POINTER;
        *iidCount = 1;
        *iids = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID)));
        if (!*iids)
            return E_OUTOFMEMORY;
        (*iids)[0] = __uuidof(::IInspectable);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) override
    {
        if (!className)
            return E_POINTER;
        std::wstring id = ManifestLiveTileOopRuntimeClassId();
        return WindowsCreateString(id.c_str(), static_cast<UINT32>(id.size()), className);
    }

    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trustLevel) override
    {
        if (!trustLevel)
            return E_POINTER;
        *trustLevel = BaseTrust;
        return S_OK;
    }
};

static void ConfigureAppxStubHostIniForActivation()
{
    std::wstring exeName = FileNameOfPath(g_exePath);
    if (!IEquals(exeName, ManifestAppxActivationStubExecutableName()))
        return;

    g_iniPath = GetExeDir() + L"\\" + ManifestHostExecutableName();
    size_t dot = g_iniPath.find_last_of(L'.');
    if (dot != std::wstring::npos)
        g_iniPath = g_iniPath.substr(0, dot);
    g_iniPath += L".ini";
}

struct LiveTileOopActivationFactory final : IActivationFactory
{
    volatile ULONG refCount = 1;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (!object)
            return E_POINTER;
        *object = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(::IInspectable) || riid == __uuidof(IActivationFactory))
        {
            *object = static_cast<IActivationFactory*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(reinterpret_cast<volatile LONG*>(&refCount)));
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG value = static_cast<ULONG>(InterlockedDecrement(reinterpret_cast<volatile LONG*>(&refCount)));
        if (!value)
            delete this;
        return value;
    }

    HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) override
    {
        if (!iidCount || !iids)
            return E_POINTER;
        *iidCount = 1;
        *iids = static_cast<IID*>(CoTaskMemAlloc(sizeof(IID)));
        if (!*iids)
            return E_OUTOFMEMORY;
        (*iids)[0] = __uuidof(IActivationFactory);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* className) override
    {
        if (!className)
            return E_POINTER;
        std::wstring id = ManifestLiveTileOopRuntimeClassId();
        return WindowsCreateString(id.c_str(), static_cast<UINT32>(id.size()), className);
    }

    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* trustLevel) override
    {
        if (!trustLevel)
            return E_POINTER;
        *trustLevel = BaseTrust;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ActivateInstance(::IInspectable** instance) override
    {
        if (!instance)
            return E_POINTER;
        *instance = nullptr;

        AppxActivationTrace(L"Live Tile OOP server ActivateInstance called");

        LoadRuntimeSettingsFromIni(false);

        std::wstring packageFullName;
        if (CurrentProcessPackageFullName(packageFullName))
            AppxActivationTrace(L"Live Tile OOP server package identity: " + packageFullName);
        else
            AppxActivationTrace(L"Live Tile OOP server has no package identity; TileUpdateManager may fail");

        std::wstring failureMessage;
        if (Appx_Update_LiveTileFromPendingXmlForActivationStub(failureMessage))
        {
            DeleteFileW(PendingLiveTileXmlPath().c_str());
            AppxActivationTrace(L"Live Tile update completed under package identity");
            *instance = new (std::nothrow) LiveTileOopInspectable();
            if (g_liveTileOopServerDoneEvent)
                SetEvent(g_liveTileOopServerDoneEvent);
            return *instance ? S_OK : E_OUTOFMEMORY;
        }

        AppxActivationTrace(L"Live Tile update failed under package identity: " + failureMessage);
        if (g_liveTileOopServerDoneEvent)
            SetEvent(g_liveTileOopServerDoneEvent);
        return E_FAIL;
    }
};

static HRESULT STDMETHODCALLTYPE LiveTileOopGetActivationFactory(HSTRING activatableClassId, IActivationFactory** factory)
{
    if (!factory)
        return E_POINTER;
    *factory = nullptr;

    std::wstring requested = HStringToWideForTrace(activatableClassId);
    AppxActivationTrace(L"Live Tile OOP server factory requested: " + requested);

    if (!IEquals(requested, ManifestLiveTileOopRuntimeClassId()))
        return CLASS_E_CLASSNOTAVAILABLE;

    *factory = new (std::nothrow) LiveTileOopActivationFactory();
    return *factory ? S_OK : E_OUTOFMEMORY;
}

static bool IsLiveTileOopServerActivation()
{
    if (!g_commandLine.appxActivationServer)
        return false;

    std::wstring name = g_commandLine.appxActivationServerName;
    return name.find(ManifestLiveTileOopServerName()) != std::wstring::npos;
}

static int RunLiveTileOopServerAndExit()
{
    AppxActivationTrace(L"received WinRT OOP server activation argument: " + g_commandLine.appxActivationServerName);

    ConfigureAppxStubHostIniForActivation();
    if (!g_iniPath.empty())
        AppxActivationTrace(L"using host INI for Live Tile OOP server: " + g_iniPath);

    HRESULT hr = RoInitialize(RO_INIT_SINGLETHREADED);
    bool shouldUninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
    {
        AppxActivationTrace(L"RoInitialize failed for Live Tile OOP server, hresult=0x" +
            Hex64(static_cast<uint32_t>(hr)));
        return 0;
    }

    g_liveTileOopServerDoneEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_liveTileOopServerDoneEvent)
        AppxActivationTrace(L"CreateEvent failed for Live Tile OOP server, error=" + std::to_wstring(GetLastError()));

    std::wstring classIdText = ManifestLiveTileOopRuntimeClassId();
    HSTRING classId = nullptr;
    hr = WindowsCreateString(classIdText.c_str(), static_cast<UINT32>(classIdText.size()), &classId);
    if (FAILED(hr))
    {
        AppxActivationTrace(L"WindowsCreateString failed for Live Tile OOP server class, hresult=0x" +
            Hex64(static_cast<uint32_t>(hr)));
        if (g_liveTileOopServerDoneEvent)
        {
            CloseHandle(g_liveTileOopServerDoneEvent);
            g_liveTileOopServerDoneEvent = nullptr;
        }
        if (shouldUninit)
            RoUninitialize();
        return 0;
    }

    PFNGETACTIVATIONFACTORY callback = LiveTileOopGetActivationFactory;
    RO_REGISTRATION_COOKIE cookie = {};
    hr = RoRegisterActivationFactories(&classId, &callback, 1, &cookie);
    WindowsDeleteString(classId);

    if (FAILED(hr))
    {
        AppxActivationTrace(L"RoRegisterActivationFactories failed for Live Tile OOP server, hresult=0x" +
            Hex64(static_cast<uint32_t>(hr)));
        if (g_liveTileOopServerDoneEvent)
        {
            CloseHandle(g_liveTileOopServerDoneEvent);
            g_liveTileOopServerDoneEvent = nullptr;
        }
        if (shouldUninit)
            RoUninitialize();
        return 0;
    }

    AppxActivationTrace(L"Live Tile OOP server activation factory registered for " + classIdText);

    DWORD waitResult = g_liveTileOopServerDoneEvent ?
        WaitForSingleObject(g_liveTileOopServerDoneEvent, 30000) :
        SleepEx(30000, TRUE);
    if (waitResult == WAIT_TIMEOUT)
        AppxActivationTrace(L"Live Tile OOP server timed out waiting for activation");
    else
        AppxActivationTrace(L"Live Tile OOP server activation completed, waitResult=" + std::to_wstring(waitResult));

    RoRevokeActivationFactories(cookie);

    if (g_liveTileOopServerDoneEvent)
    {
        CloseHandle(g_liveTileOopServerDoneEvent);
        g_liveTileOopServerDoneEvent = nullptr;
    }

    if (shouldUninit)
        RoUninitialize();

    return 0;
}

static int HandleAppxActivationAndExit()
{
    if (!g_commandLine.appxActivationServer)
        return -1;

    if (IsLiveTileOopServerActivation())
        return RunLiveTileOopServerAndExit();

    AppxActivationTrace(L"received AppX activation argument: " + g_commandLine.appxActivationServerName);

    // In Windows 8/8.1 manifest mode this process is only a package-identity
    // helper. It is not a real RT UI app and it should not try to become the
    // resident tray/wallpaper-monitor process. Its only job is to perform the
    // TileUpdateManager call while Windows has given it package identity, then
    // exit quickly so the immersive host frame is closed instead of timing out.
    ConfigureAppxStubHostIniForActivation();
    if (!g_iniPath.empty())
        AppxActivationTrace(L"using host INI for activation stub: " + g_iniPath);

    LoadRuntimeSettingsFromIni(false);

    std::wstring failureMessage;
    if (Appx_Update_LiveTileFromPendingXmlForActivationStub(failureMessage))
    {
        DeleteFileW(PendingLiveTileXmlPath().c_str());
        AppxActivationTrace(L"Live Tile update completed under package identity");
        return 0;
    }

    AppxActivationTrace(L"Live Tile update failed under package identity: " + failureMessage);
    return 0;
}

static std::wstring StartupString(const wchar_t* key, const wchar_t* fallback)
{
    std::wstring value = IniReadS(L"Strings", key, fallback);
    return value.empty() ? std::wstring(fallback) : value;
}

static void ReleaseSingleInstanceMutex()
{
    if (g_singleInstanceMutex)
    {
        ReleaseMutex(g_singleInstanceMutex);
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
    }
}

static void WaitForModeSwitchParentExitIfRequested()
{
    DWORD pid = g_commandLine.waitForPid;
    if (pid == 0 || pid == GetCurrentProcessId())
        return;

    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h)
        return;

    WaitForSingleObject(h, 30000);
    CloseHandle(h);
}

static bool EnsureIniReadyForCommandLineSettings()
{
    bool initialIniCreated = EnsureInitialIniTemplate();
    TextNormalizeResult normalizeResult = initialIniCreated
        ? TextNormalizeResult::NoChange
        : NormalizeIniToUtf8BomIfNeeded();
    if (normalizeResult == TextNormalizeResult::Failed)
    {
        DWORD err = GetLastError();
        g_commandLine.applyError = L"Failed to normalize INI file before applying command-line settings (error " +
            std::to_wstring(err == ERROR_SUCCESS ? ERROR_INVALID_DATA : err) + L").";
        return false;
    }

    EnsureIniDefaults();
    EnsureIniStringDefaults();
    UpgradeRenamedStringDefaults();
    return true;
}

static bool ApplyCommandLineSettingsForExistingInstance()
{
    if (!CommandLineHasPersistentSettings())
        return true;

    if (!EnsureIniReadyForCommandLineSettings())
        return false;

    if (!ValidateCommandLineWallpaperPath())
        return false;

    return ApplyCommandLinePersistentSettings();
}

static bool StoreCommandLineWallpaperRequest()
{
    std::wstring wp;
    if (!CommandLineWallpaper(wp))
        return true;

    if (!EnsureIniReadyForCommandLineSettings())
        return false;

    if (!ValidateCommandLineWallpaperPath())
        return false;

    if (!WriteIniValue(L"State", L"CommandLineWallpaperRequest", wp.c_str()))
    {
        DWORD err = GetLastError();
        if (err == ERROR_SUCCESS)
            err = ERROR_WRITE_FAULT;
        g_commandLine.applyError = L"Failed to write command-line wallpaper request (error " +
            std::to_wstring(err) + L").";
        return false;
    }

    return true;
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) noexcept
{
    switch (ctrlType)
    {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
    {
        g_consoleControlShutdownPending = true;
        HWND h = g_shutdownWindow.load();
        if (h)
            PostMessageW(h, WM_APP_REQUEST_SHUTDOWN, ctrlType, 0);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hi, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    auto appStart = steady_clock::now();
    ConfigureDpiAwareness();

    g_exePath = GetModulePath();
    if (g_exePath.empty())
        return 1;

    if (!ParseCommandLineOptions())
    {
        ShowCommandLineMessage(g_commandLine.parseError, true);
        return 2;
    }

    std::wstring dir = GetExeDir();
    std::wstring base = g_exePath.substr(g_exePath.find_last_of(L"\\/")+1);
    base = base.substr(0, base.find_last_of(L'.'));
    g_iniPath = g_commandLine.customIniPath ? g_commandLine.iniPath : dir + L"\\" + base + L".ini";
    ConfigureSingleInstanceIdentity(dir, g_iniPath, g_commandLine.customIniPath);
    WaitForModeSwitchParentExitIfRequested();

    if (g_commandLine.helpRequested)
    {
        ShowCommandLineMessage(CommandLineUsageText(), false);
        return 0;
    }

    std::wstring comHelperManifestPath;
    if (CommandLineComRegistrationHelper(comHelperManifestPath))
        return RunComRegistrationHelper(comHelperManifestPath);

    bool consoleCtrlHandlerInstalled = SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE) != FALSE;
    DWORD consoleCtrlHandlerError = consoleCtrlHandlerInstalled ? ERROR_SUCCESS : GetLastError();

    SetLastError(ERROR_SUCCESS);
    g_singleInstanceMessage = RegisterWindowMessageW(g_singleInstanceMessageName.c_str());
    DWORD singleInstanceMessageError = g_singleInstanceMessage ? ERROR_SUCCESS : GetLastError();
    if (!g_singleInstanceMessage && singleInstanceMessageError == ERROR_SUCCESS)
        singleInstanceMessageError = ERROR_INVALID_DATA;

    SetLastError(ERROR_SUCCESS);
    g_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
    DWORD taskbarCreatedMessageError = g_taskbarCreatedMessage ? ERROR_SUCCESS : GetLastError();
    if (!g_taskbarCreatedMessage && taskbarCreatedMessageError == ERROR_SUCCESS)
        taskbarCreatedMessageError = ERROR_INVALID_DATA;

    int appxActivationExitCode = HandleAppxActivationAndExit();
    if (appxActivationExitCode >= 0)
        return appxActivationExitCode;

    SetLastError(ERROR_SUCCESS);
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, g_singleInstanceMutexName.c_str());
    DWORD singleInstanceMutexLastError = GetLastError();
    DWORD singleInstanceMutexFailure = ERROR_SUCCESS;
    if (!g_singleInstanceMutex)
    {
        singleInstanceMutexFailure = singleInstanceMutexLastError;
    }
    else if (singleInstanceMutexLastError == ERROR_ALREADY_EXISTS)
    {
        WPARAM singleInstanceRequest = SINGLE_INSTANCE_REQUEST_SHOW;
        if (CommandLineShouldExit())
        {
            if (!ApplyCommandLineSettingsForExistingInstance())
            {
                ShowCommandLineMessage(g_commandLine.applyError, true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }
            singleInstanceRequest = SINGLE_INSTANCE_REQUEST_EXIT;
        }
        else if (CommandLineShouldGenerateNow())
        {
            if (!ApplyCommandLineSettingsForExistingInstance() || !StoreCommandLineWallpaperRequest())
            {
                ShowCommandLineMessage(g_commandLine.applyError, true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }
            singleInstanceRequest = SINGLE_INSTANCE_REQUEST_GENERATE;
        }
        else if (CommandLineShouldRegenerateManifest())
        {
            if (!ApplyCommandLineSettingsForExistingInstance())
            {
                ShowCommandLineMessage(g_commandLine.applyError, true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }
            singleInstanceRequest = SINGLE_INSTANCE_REQUEST_REGENERATE_MANIFEST;
        }
        else if (CommandLineHasPersistentSettings())
        {
            if (!ApplyCommandLineSettingsForExistingInstance())
            {
                ShowCommandLineMessage(g_commandLine.applyError, true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }
            singleInstanceRequest = SINGLE_INSTANCE_REQUEST_RELOAD_SETTINGS;
        }

        if (CommandLineTrayIconChanged())
            singleInstanceRequest |= SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON;
        if (CommandLineShouldRegenerateManifest())
            singleInstanceRequest |= SINGLE_INSTANCE_FLAG_REGENERATE_MANIFEST;

        if (!SignalExistingInstance(singleInstanceRequest))
        {
            if ((singleInstanceRequest & SINGLE_INSTANCE_REQUEST_MASK) != SINGLE_INSTANCE_REQUEST_SHOW)
            {
                ShowCommandLineMessage(L"Failed to signal the running GenerateAssets instance.", true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }

            if (IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0 &&
                IniReadI(L"Settings", L"NotifyOnAlreadyRunning", 1) != 0)
            {
                std::wstring title = StartupString(L"NotificationTitle", L"Desktop Tile Generator");
                std::wstring message = StartupString(L"NotificationAlreadyRunning", L"Desktop Tile Generator is already running.");
                MessageBoxW(nullptr,
                    message.c_str(),
                    title.c_str(),
                    MB_OK | MB_ICONINFORMATION);
            }
        }
        CloseHandle(g_singleInstanceMutex);
        g_singleInstanceMutex = nullptr;
        return 0;
    }

    if (CommandLineShouldExit())
    {
        if (!ApplyCommandLineSettingsForExistingInstance())
        {
            ShowCommandLineMessage(g_commandLine.applyError, true);
            ReleaseSingleInstanceMutex();
            return 2;
        }
        ReleaseSingleInstanceMutex();
        return 0;
    }

    auto iniStart = steady_clock::now();
    bool initialIniCreated = EnsureInitialIniTemplate();
    TextNormalizeResult iniEncodingNormalizeResult = initialIniCreated
        ? TextNormalizeResult::NoChange
        : NormalizeIniToUtf8BomIfNeeded();
    if (iniEncodingNormalizeResult == TextNormalizeResult::Failed)
        QueueStartupWarning(L"[!] Failed to normalize INI file to UTF-8 with BOM (error " + std::to_wstring(GetLastError()) + L").");

    // defaults
    EnsureIniDefaults();
    EnsureIniStringDefaults();
    UpgradeRenamedStringDefaults();
    if (!ValidateCommandLineWallpaperPath())
    {
        ShowCommandLineMessage(g_commandLine.applyError, true);
        ReleaseSingleInstanceMutex();
        return 2;
    }
    if (!ApplyCommandLinePersistentSettings())
    {
        ShowCommandLineMessage(g_commandLine.applyError, true);
        ReleaseSingleInstanceMutex();
        return 2;
    }
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    auto iniDone = steady_clock::now();
    LoadRuntimeSettingsFromIni(true);

    // GDI+
    GdiplusStartupInput in; ULONG_PTR tk;
    if (GdiplusStartup(&tk,&in,nullptr)!=Ok){ MessageBoxW(nullptr, g_ui.gdiPlusStartupFailedMessage.c_str(), g_ui.gdiPlusStartupFailedTitle.c_str(), 0); return 1; }
    auto gdiDone = steady_clock::now();
    std::wstring previousForceShutdownCleanupWarning;
    if (ConsumePreviousForceShutdownCleanupWarning(previousForceShutdownCleanupWarning))
        QueueStartupWarning(previousForceShutdownCleanupWarning);

    LogText(g_ui.programStarting);
    Log(g_ui.timingLog.c_str(), g_ui.timingPreLogInitialization.c_str(), ElapsedMs(appStart, gdiDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingIniDefaultStringLoading.c_str(), ElapsedMs(iniStart, iniDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGdiStartup.c_str(), ElapsedMs(iniDone, gdiDone));
    Log(g_ui.exeLabel.c_str(), g_exePath.c_str());
    Log(g_ui.iniLabel.c_str(), g_iniPath.c_str());
    Log(g_ui.logFileLabel.c_str(), GetLogPathCopy().c_str());
    LogCommandLineSummary();
    FlushStartupWarnings();

    std::wstring startupManifestPath;
    bool startupManifestCreated = false;
    DWORD startupManifestError = ERROR_SUCCESS;
    bool startupManifestReady = false;
    if (CommandLineShouldRegenerateManifest())
        startupManifestReady = RegenerateAppxManifestFromConfig(startupManifestPath, startupManifestError);
    else
        startupManifestReady = EnsureAppxManifest(startupManifestPath, startupManifestCreated, startupManifestError);

    Log(g_ui.singleInstanceScopeSummary.c_str(), g_singleInstanceMutexName.c_str());
    if (iniEncodingNormalizeResult == TextNormalizeResult::Changed)
        LogText(g_ui.iniEncodingNormalized);
    if (startupManifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), startupManifestPath.c_str());
    else if (CommandLineShouldRegenerateManifest() && startupManifestReady)
        Log(g_ui.appxManifestRegenerated.c_str(), startupManifestPath.c_str());
    else if (!startupManifestReady)
    {
        Log((CommandLineShouldRegenerateManifest()
            ? g_ui.appxManifestRegenerateFailed
            : g_ui.appxManifestCreateFailed).c_str(), startupManifestPath.c_str());
        LogWin32Failure(CommandLineShouldRegenerateManifest()
            ? L"Regenerate AppxManifest.xml"
            : L"Create AppxManifest.xml", startupManifestError);
    }
    Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
    Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
    Log(g_ui.generateScaleAutoSummary.c_str(), StateEnabled(g_generateScaleAuto));
    Log(g_ui.generateScaleSummary.c_str(), 100, StateEnabled(g_generateScale100));
    Log(g_ui.generateScaleSummary.c_str(), 125, StateEnabled(g_generateScale125));
    Log(g_ui.generateScaleSummary.c_str(), 150, StateEnabled(g_generateScale150));
    Log(g_ui.generateScaleSummary.c_str(), 200, StateEnabled(g_generateScale200));
    Log(g_ui.generateScaleSummary.c_str(), 400, StateEnabled(g_generateScale400));
    Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
    Log(g_ui.powerShellRegistrationSummary.c_str(), UsePowerShell() ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());
    Log(g_ui.liveTileUpdateSummary.c_str(), LiveTileUpdateModeSummaryText().c_str());
    Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());
    Log(g_ui.generatedAssetCacheSummary.c_str(),
        StateEnabled(GeneratedAssetCacheEnabled()),
        GeneratedAssetCacheMaxEntries());
    Log(g_ui.generatedAssetPrecacheSummary.c_str(),
        StateEnabled(GeneratedAssetPrecacheEnabled()),
        GeneratedAssetPrecacheMaxFiles());
    Log(g_ui.comRegistrationHelperSummary.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 1) != 0));
    Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    int liveWallpaperCaptureRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
    std::wstring liveWallpaperCaptureRefreshText = LiveWallpaperRefreshDisplayText(liveWallpaperCaptureRefreshMs);
    Log(g_ui.liveWallpaperCaptureSummary.c_str(),
        StateEnabled(IniReadI(L"Settings", L"LiveWallpaperCapture", 1) != 0),
        StateEnabled(IniReadI(L"Settings", L"LiveWallpaperCaptureLively", 1) != 0),
        StateEnabled(IniReadI(L"Settings", L"LiveWallpaperCaptureWallpaperEngine", 1) != 0),
        IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000),
        liveWallpaperCaptureRefreshText.c_str());
    int liveWallpaperCaptureStartupRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
    int liveWallpaperCaptureStartupRefreshDurationMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
    Log(g_ui.liveWallpaperCaptureStartupRefreshSummary.c_str(),
        LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshMs).c_str(),
        LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshDurationMs).c_str());
    Log(g_ui.liveWallpaperCaptureScreenFallbackState.c_str(),
        IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) != 0 ? 1 : 0);
    if (singleInstanceMessageError != ERROR_SUCCESS)
        LogWin32Failure(L"RegisterWindowMessageW(single-instance)", singleInstanceMessageError);
    if (taskbarCreatedMessageError != ERROR_SUCCESS)
        LogWin32Failure(L"RegisterWindowMessageW(TaskbarCreated)", taskbarCreatedMessageError);
    if (!consoleCtrlHandlerInstalled)
        LogWin32Failure(L"SetConsoleCtrlHandler", consoleCtrlHandlerError);

    if (CommandLineShouldRegenerateManifest() && !CommandLineShouldGenerateNow())
    {
        GdiplusShutdown(tk);
        ReleaseSingleInstanceMutex();
        return startupManifestReady ? 0 : 1;
    }

    // Window
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=WINDOW_CLASS_NAME;
    if (!RegisterClassW(&wc))
    {
        LogWin32Failure(L"RegisterClassW");
        GdiplusShutdown(tk);
        return 1;
    }
    g_hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, g_instanceWindowTitle.c_str(), 0, 0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd)
    {
        LogWin32Failure(L"CreateWindowExW");
        GdiplusShutdown(tk);
        return 1;
    }
    g_shutdownWindow = g_hwnd;

    DeleteFileW(AppxActivationRequestPath().c_str());
    if (!SetTimer(g_hwnd, APPX_ACTIVATION_REQUEST_TIMER_ID, 750, nullptr))
        LogWin32Failure(L"SetTimer(APPX_ACTIVATION_REQUEST_TIMER_ID)");

    // Tray
    if (g_tray){
        DWORD err = 0;
        if (!EnsureTrayIcon(&err))
        {
            LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
            g_tray = false;
        }
    }

    if (!previousForceShutdownCleanupWarning.empty())
    {
        MessageBoxW(
            g_hwnd,
            previousForceShutdownCleanupWarning.c_str(),
            g_ui.previousForceShutdownCleanupTitle.c_str(),
            MB_OK | MB_ICONWARNING);
    }

    if (g_consoleControlShutdownPending.exchange(false))
        RequestGracefulShutdown(g_hwnd);

    if (singleInstanceMutexFailure != ERROR_SUCCESS)
    {
        int exitCode = HandleSingleInstanceMutexFailure(singleInstanceMutexFailure);
        if (exitCode != 0)
        {
            TrayRemove();
            GdiplusShutdown(tk);
            return exitCode;
        }
    }

    if (CommandLineLiveTileModeChanged())
    {
        if (CommandLineShouldGenerateOnce())
            SetLiveTileModeReregistrationPending(true);
        else
            QueueLiveTileModeReregistration();
    }

    if (CommandLineShouldGenerateOnce())
    {
        std::wstring wp;
        if (!CommandLineWallpaper(wp))
            wp = GetWallpaper();

        bool ok = false;
        if (!wp.empty())
            ok = Generate(wp.c_str(), g_ui.commandLineOnceReason.c_str());
        else
            LogText(g_ui.startupGenerationSkippedNoWallpaper);

        JoinGeneratedAssetPrecache();
        TrayRemove();
        GdiplusShutdown(tk);
        ReleaseSingleInstanceMutex();
        return ok ? 0 : 1;
    }

    bool queuedPendingLiveTileModeReregistration = false;
    if (!g_shutdownRequested && LiveTileModeReregistrationPending())
        queuedPendingLiveTileModeReregistration = QueueLiveTileModeReregistration();

    bool forceStartupGeneration = CommandLineShouldForceStartupGeneration();
    std::wstring commandLineWallpaper;
    bool hasCommandLineWallpaper = CommandLineWallpaper(commandLineWallpaper);
    if (!g_shutdownRequested && !queuedPendingLiveTileModeReregistration &&
        (g_generateOnStartup || forceStartupGeneration || hasCommandLineWallpaper))
    {
        std::wstring wp = hasCommandLineWallpaper ? commandLineWallpaper : GetWallpaper();
        if (!wp.empty())
        {
            if (!forceStartupGeneration && !hasCommandLineWallpaper && StartupGenerationCanSkip(wp))
            {
                LogText(g_ui.startupGenerationSkippedUnchanged);
            }
            else
            {
                LogText(g_ui.startupGenerationEnabled);
                QueueGenerate(wp, hasCommandLineWallpaper ? g_ui.commandLineWallpaperReason :
                    (forceStartupGeneration ? g_ui.commandLineForceGenerateReason : g_ui.startupGenerationReason));
            }
        }
        else
        {
            LogText(g_ui.startupGenerationSkippedNoWallpaper);
        }
    }

    std::thread th;
    if (!CommandLineShouldSkipMonitor())
    {
        try
        {
            th = std::thread(PollThread);
        }
        catch (const std::exception& ex)
        {
            std::wstring message = ExceptionTextToWide(ex);
            Log(g_ui.pollThreadStartFailed.c_str(), message.c_str());
        }
        catch (...)
        {
            std::wstring message = UnknownExceptionText();
            Log(g_ui.pollThreadStartFailed.c_str(), message.c_str());
        }
    }

    MSG msg;
    while (GetMessageW(&msg,nullptr,0,0))
    { TranslateMessage(&msg); DispatchMessageW(&msg); }

    KillTimer(g_hwnd, APPX_ACTIVATION_REQUEST_TIMER_ID);
    g_running=false;
    WakePollThread();
    if (th.joinable()) th.join();
    JoinQueuedGenerations();
    JoinGeneratedAssetPrecache();

    MaybeClearLiveTileOnShutdown();

    TrayRemove();
    GdiplusShutdown(tk);
    ReleaseSingleInstanceMutex();
    ExecutePendingRelaunchAfterExit();
    return 0;
}


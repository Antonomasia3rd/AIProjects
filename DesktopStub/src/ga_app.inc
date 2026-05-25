// -----------------------------------------------------------------------------
// Window proc
// -----------------------------------------------------------------------------
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

static void ApplySettingsChangedByCommandLine()
{
    LiveTileUpdateMode liveTileBefore = ConfiguredLiveTileUpdateMode();
    ReloadRuntimeSettingsFromIni();
    ApplyTrayIconSettingFromIni();

    if (liveTileBefore != ConfiguredLiveTileUpdateMode())
        QueueLiveTileModeReregistration();

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

    QueueGenerate(wp, L"command-line generation request");
}

static void HandleSingleInstanceRequest(WPARAM request)
{
    switch (request)
    {
    case SINGLE_INSTANCE_REQUEST_EXIT:
        RequestGracefulShutdown(g_hwnd);
        return;

    case SINGLE_INSTANCE_REQUEST_RELOAD_SETTINGS:
        ApplySettingsChangedByCommandLine();
        return;

    case SINGLE_INSTANCE_REQUEST_GENERATE:
        ApplySettingsChangedByCommandLine();
        QueueCommandLineGenerationRequest();
        return;

    case SINGLE_INSTANCE_REQUEST_SHOW:
    default:
        HandleExistingInstanceRequest();
        return;
    }
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

    return ApplyCommandLinePersistentSettings();
}

static bool StoreCommandLineWallpaperRequest()
{
    std::wstring wp;
    if (!CommandLineWallpaper(wp))
        return true;

    if (!EnsureIniReadyForCommandLineSettings())
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

        if (!SignalExistingInstance(singleInstanceRequest))
        {
            if (singleInstanceRequest == SINGLE_INSTANCE_REQUEST_SHOW &&
                IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0 &&
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
    bool startupManifestReady = EnsureAppxManifest(startupManifestPath, startupManifestCreated, startupManifestError);

    Log(g_ui.singleInstanceScopeSummary.c_str(), g_singleInstanceMutexName.c_str());
    if (iniEncodingNormalizeResult == TextNormalizeResult::Changed)
        LogText(g_ui.iniEncodingNormalized);
    if (startupManifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), startupManifestPath.c_str());
    else if (!startupManifestReady)
    {
        Log(g_ui.appxManifestCreateFailed.c_str(), startupManifestPath.c_str());
        LogWin32Failure(L"Create AppxManifest.xml", startupManifestError);
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
    Log(g_ui.generatedAssetCacheSummary.c_str(),
        StateEnabled(GeneratedAssetCacheEnabled()),
        GeneratedAssetCacheMaxEntries());
    Log(g_ui.generatedAssetPrecacheSummary.c_str(),
        StateEnabled(GeneratedAssetPrecacheEnabled()),
        GeneratedAssetPrecacheMaxFiles());
    Log(g_ui.comRegistrationHelperSummary.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 1) != 0));
    Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    if (singleInstanceMessageError != ERROR_SUCCESS)
        LogWin32Failure(L"RegisterWindowMessageW(single-instance)", singleInstanceMessageError);
    if (taskbarCreatedMessageError != ERROR_SUCCESS)
        LogWin32Failure(L"RegisterWindowMessageW(TaskbarCreated)", taskbarCreatedMessageError);
    if (!consoleCtrlHandlerInstalled)
        LogWin32Failure(L"SetConsoleCtrlHandler", consoleCtrlHandlerError);

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
            ok = Generate(wp.c_str(), L"command-line --once");
        else
            LogText(g_ui.startupGenerationSkippedNoWallpaper);

        JoinGeneratedAssetPrecache();
        TrayRemove();
        GdiplusShutdown(tk);
        ReleaseSingleInstanceMutex();
        return ok ? 0 : 1;
    }

    bool forceStartupGeneration = CommandLineShouldForceStartupGeneration();
    std::wstring commandLineWallpaper;
    bool hasCommandLineWallpaper = CommandLineWallpaper(commandLineWallpaper);
    if (!g_shutdownRequested && (g_generateOnStartup || forceStartupGeneration || hasCommandLineWallpaper))
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
                QueueGenerate(wp, hasCommandLineWallpaper ? L"command-line wallpaper" :
                    (forceStartupGeneration ? L"command-line --generate" : g_ui.startupGenerationEnabled));
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

    g_running=false;
    WakePollThread();
    if (th.joinable()) th.join();
    JoinQueuedGenerations();
    JoinGeneratedAssetPrecache();

    TrayRemove();
    GdiplusShutdown(tk);
    ReleaseSingleInstanceMutex();
    return 0;
}


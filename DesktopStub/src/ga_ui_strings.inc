std::wstring Win32ErrorString(DWORD err)
{
    wchar_t* buf = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    DWORD len = FormatMessageW(flags, nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);

    std::wstring msg;
    if (len && buf)
    {
        msg.assign(buf, buf + len);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L'.' || msg.back() == L' '))
            msg.pop_back();
    }
    else
    {
        msg = g_ui.unknownError.empty() ? L"Unknown error" : g_ui.unknownError;
    }

    if (buf) LocalFree(buf);
    return msg;
}

static std::wstring FormatWideV(const wchar_t* fmt, va_list ap)
{
    if (!fmt)
        return L"";

    va_list measure;
    va_copy(measure, ap);
    int needed = _vscwprintf(fmt, measure);
    va_end(measure);

    if (needed < 0)
        return L"";

    std::vector<wchar_t> buf(static_cast<size_t>(needed) + 1, L'\0');
    _vsnwprintf_s(buf.data(), buf.size(), _TRUNCATE, fmt, ap);
    std::wstring result(buf.data());
    return result;
}

std::wstring FormatWide(const wchar_t* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    std::wstring result = FormatWideV(fmt, ap);
    va_end(ap);
    return result;
}

template <size_t N>
static void CopyTruncated(wchar_t (&dest)[N], const std::wstring& text)
{
    wcsncpy_s(dest, N, text.c_str(), _TRUNCATE);
}

void NotifyTrayBalloon(const std::wstring& title, const std::wstring& msg, DWORD icon = NIIF_INFO, bool force = false)
{
    NOTIFYICONDATAW nid{};
    {
        std::lock_guard<std::mutex> lk(g_trayMutex);
        if ((!force && !g_tray) || (!force && !g_notificationsEnabled) || !g_nid.hWnd)
            return;
        nid = g_nid;
    }
    nid.uFlags = NIF_INFO;
    CopyTruncated(nid.szInfoTitle, title);
    CopyTruncated(nid.szInfo, msg);
    nid.dwInfoFlags = icon;
    nid.uTimeout = static_cast<UINT>(ClampInt(g_notificationTimeoutMs.load(), 1000, 60000));
    if (!Shell_NotifyIconW(NIM_MODIFY, &nid))
        LogWin32Failure(L"Shell_NotifyIconW(NIM_MODIFY)");
}

static TextNormalizeResult NormalizeIniToUtf8BomIfNeeded()
{
    return NormalizeTextFileToUtf8Bom(g_iniPath);
}

void EnsureIniDefaults()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
    {
        QueueStartupWarning(L"[!] Failed to read INI while applying setting defaults (error " + std::to_wstring(GetLastError()) + L").");
        return;
    }

    bool haveText = true;
    auto doc = ParseIniDocument(iniText);
    bool changed = false;

    for (const auto& d : g_defaults)
    {
        std::wstring value;
        if (!haveText || !ReadIniValueFromDoc(doc, d.section, d.key, value))
        {
            if (!WriteIniValueToText(iniText, d.section, d.key, d.value))
            {
                QueueStartupWarning(L"[!] Failed to update INI setting default: [" + std::wstring(d.section ? d.section : L"") + L"] " + std::wstring(d.key ? d.key : L""));
                return;
            }
            changed = true;
            doc = ParseIniDocument(iniText);
            haveText = true;
        }
    }

    std::vector<IniEntry> powerShellErrorEntries;
    if (!ReadIniSectionFromDoc(doc, POWERSHELL_ERROR_MESSAGES_SECTION, powerShellErrorEntries))
    {
        for (const auto& d : g_powerShellErrorMessageDefaults)
        {
            if (!WriteIniValueToText(iniText, d.section, d.key, d.value))
            {
                QueueStartupWarning(L"[!] Failed to update INI PowerShell error default: [" + std::wstring(d.section ? d.section : L"") + L"] " + std::wstring(d.key ? d.key : L""));
                return;
            }
            changed = true;
            doc = ParseIniDocument(iniText);
        }
    }

    if (changed && !WriteUtf8BomTextFile(g_iniPath, iniText))
    {
        QueueStartupWarning(L"[!] Failed to write INI setting defaults (error " + std::to_wstring(GetLastError()) + L").");
    }
    else if (changed)
    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(iniText);
    }
}


void LoadUiStrings()
{
    g_ui.generalTitle = IniReadS(L"Strings", L"GeneralTitle", L"General:");
    g_ui.loggingTitle = IniReadS(L"Strings", L"LoggingTitle", L"Logging:");
    g_ui.wallpaperFittingTitle = IniReadS(L"Strings", L"WallpaperFittingTitle", L"Wallpaper Fitting:");
    g_ui.methodsTitle = IniReadS(L"Strings", L"MethodsTitle", L"Methods:");
    g_ui.assetsTitle = IniReadS(L"Strings", L"AssetsTitle", L"Assets:");
    g_ui.dpiScalesTitle = IniReadS(L"Strings", L"DpiScalesTitle", L"DPI Scales:");
    g_ui.cachingTitle = IniReadS(L"Strings", L"CachingTitle", L"Caching:");
    g_ui.liveTileTitle = IniReadS(L"Strings", L"LiveTileTitle", L"Live Tile:");
    g_ui.tileAppearanceTitle = IniReadS(L"Strings", L"TileAppearanceTitle", L"Tile appearance:");
    g_ui.tileTextTitle = IniReadS(L"Strings", L"TileTextTitle", L"Text overlay");
    g_ui.tileTextEnabled = IniReadS(L"Strings", L"TileTextEnabled", L"Enable text overlay");
    g_ui.tileTextEdit = IniReadS(L"Strings", L"TileTextEdit", L"Edit text...");
    g_ui.tileTextClear = IniReadS(L"Strings", L"TileTextClear", L"Clear text");
    g_ui.tileTextCurrent = IniReadS(L"Strings", L"TileTextCurrent", L"Text: %s");
    g_ui.tileTextSecondaryEdit = IniReadS(L"Strings", L"TileTextSecondaryEdit", L"Edit secondary text...");
    g_ui.tileTextSecondaryClear = IniReadS(L"Strings", L"TileTextSecondaryClear", L"Clear secondary text");
    g_ui.tileTextSecondaryCurrent = IniReadS(L"Strings", L"TileTextSecondaryCurrent", L"Secondary: %s");
    g_ui.tileTextBadgeEdit = IniReadS(L"Strings", L"TileTextBadgeEdit", L"Edit badge text...");
    g_ui.tileTextBadgeClear = IniReadS(L"Strings", L"TileTextBadgeClear", L"Clear badge text");
    g_ui.tileTextBadgeCurrent = IniReadS(L"Strings", L"TileTextBadgeCurrent", L"Badge: %s");
    g_ui.tileTextHorizontalAlign = IniReadS(L"Strings", L"TileTextHorizontalAlign", L"Text horizontal: %s");
    g_ui.tileTextVerticalAlign = IniReadS(L"Strings", L"TileTextVerticalAlign", L"Text vertical: %s");
    g_ui.tileTextBadgeHorizontalAlign = IniReadS(L"Strings", L"TileTextBadgeHorizontalAlign", L"Badge horizontal: %s");
    g_ui.tileTextBadgeVerticalAlign = IniReadS(L"Strings", L"TileTextBadgeVerticalAlign", L"Badge vertical: %s");
    g_ui.tileTextFontSize = IniReadS(L"Strings", L"TileTextFontSize", L"Primary font size: %d px");
    g_ui.tileTextSecondaryFontSize = IniReadS(L"Strings", L"TileTextSecondaryFontSize", L"Secondary font size: %d px");
    g_ui.tileTextBadgeFontSize = IniReadS(L"Strings", L"TileTextBadgeFontSize", L"Badge font size: %d px");
    g_ui.tileTextShadow = IniReadS(L"Strings", L"TileTextShadow", L"Shadow");
    g_ui.tileTextApplyMedium = IniReadS(L"Strings", L"TileTextApplyMedium", L"Apply to Medium tile");
    g_ui.tileTextApplyWide = IniReadS(L"Strings", L"TileTextApplyWide", L"Apply to Wide tile");
    g_ui.tileTextApplyLarge = IniReadS(L"Strings", L"TileTextApplyLarge", L"Apply to Large tile");
    g_ui.tileTextApplySmall = IniReadS(L"Strings", L"TileTextApplySmall", L"Apply to Small tile");
    g_ui.tileTextApplyLogos = IniReadS(L"Strings", L"TileTextApplyLogos", L"Apply to logos");
    g_ui.tileTextEdited = IniReadS(L"Strings", L"TileTextEdited", L"[i] Tile text changed: %s");
    g_ui.tileTextCleared = IniReadS(L"Strings", L"TileTextCleared", L"[i] Tile text cleared.");
    g_ui.tileTextSummary = IniReadS(L"Strings", L"TileTextSummary", L"[i] Tile text overlay: %s (%s)");
    g_ui.tileTextInputTitle = IniReadS(L"Strings", L"TileTextInputTitle", L"Edit tile text");
    g_ui.tileTextSecondaryInputTitle = IniReadS(L"Strings", L"TileTextSecondaryInputTitle", L"Edit secondary tile text");
    g_ui.tileTextBadgeInputTitle = IniReadS(L"Strings", L"TileTextBadgeInputTitle", L"Edit tile badge text");

    g_ui.listenWallpaper = IniReadS(L"Strings", L"ListenWallpaper", L"Listen Wallpaper");
    g_ui.trayIcon = IniReadS(L"Strings", L"TrayIcon", L"Tray Icon (this session)");
    g_ui.usePowerShell = IniReadS(L"Strings", L"UsePowerShell", L"PowerShell only");
    g_ui.experimentalLiveTileUpdate = IniReadS(L"Strings", L"ExperimentalLiveTileUpdate", L"Live Tile update mode");
    g_ui.appxManifestTarget = IniReadS(L"Strings", L"AppxManifestTarget", L"AppxManifest target");
    g_ui.appxManifestTargetWindows10 = IniReadS(L"Strings", L"AppxManifestTargetWindows10", L"Windows 10");
    g_ui.appxManifestTargetWindows81 = IniReadS(L"Strings", L"AppxManifestTargetWindows81", L"Windows 8.1");
    g_ui.appxManifestTargetWindows8 = IniReadS(L"Strings", L"AppxManifestTargetWindows8", L"Windows 8");
    g_ui.win8LiveTileOopHelper = IniReadS(L"Strings", L"Win8LiveTileOopHelper", L"Experimental Win8/8.1 OOP Live Tile helper (disabled; failed with class registration on loose packages)");
    g_ui.win8LiveTileBackgroundTask = IniReadS(L"Strings", L"Win8LiveTileBackgroundTask", L"Experimental Win8/8.1 background task Live Tile helper (disabled; future research)");
    g_ui.win8LiveTileBrokerApp = IniReadS(L"Strings", L"Win8LiveTileBrokerApp", L"Use packaged WinRT broker app for Win8/8.1 Live Tile updates (recommended)");
    g_ui.win8LiveTileOopHelperState = IniReadS(L"Strings", L"Win8LiveTileOopHelperState", L"[i] Win8LiveTileOopHelper = %d");
    g_ui.win8LiveTileBackgroundTaskState = IniReadS(L"Strings", L"Win8LiveTileBackgroundTaskState", L"[i] Win8LiveTileBackgroundTask = %d");
    g_ui.win8LiveTileBrokerAppState = IniReadS(L"Strings", L"Win8LiveTileBrokerAppState", L"[i] Win8LiveTileBrokerApp = %d");
    g_ui.liveTileModeAuto = IniReadS(L"Strings", L"LiveTileModeAuto", L"Auto");
    g_ui.liveTileModeRegistration = IniReadS(L"Strings", L"LiveTileModeRegistration", L"Registration");
    g_ui.liveTileModeLiveTile = IniReadS(L"Strings", L"LiveTileModeLiveTile", L"Live Tile");
    g_ui.liveTileModeEnabled = IniReadS(L"Strings", L"LiveTileModeEnabled", L"Enabled");
    g_ui.liveTileModeDisabled = IniReadS(L"Strings", L"LiveTileModeDisabled", L"Disabled");
    g_ui.liveTileModeTitle = IniReadS(L"Strings", L"LiveTileModeTitle", L"Mode");
    g_ui.liveTileRegistrationTitle = IniReadS(L"Strings", L"LiveTileRegistrationTitle", L"Registration");
    g_ui.liveTileRuntimeTitle = IniReadS(L"Strings", L"LiveTileRuntimeTitle", L"Runtime behavior");
    g_ui.liveTileWindows8Title = IniReadS(L"Strings", L"LiveTileWindows8Title", L"Windows 8 / 8.1");
    g_ui.liveTileRelaunchOnSwitch = IniReadS(L"Strings", L"LiveTileRelaunchOnSwitch", L"Relaunch when runtime mismatches");
    g_ui.clearLiveTileOnShutdown = IniReadS(L"Strings", L"ClearLiveTileOnShutdown", L"Clear Live Tile on shutdown");
    g_ui.liveTileRelaunchToPackagedQueued = IniReadS(L"Strings", L"LiveTileRelaunchToPackagedQueued", L"[i] Live Tile runtime mismatch detected; relaunching through the registered package entry.");
    g_ui.liveTileRelaunchToUnpackagedQueued = IniReadS(L"Strings", L"LiveTileRelaunchToUnpackagedQueued", L"[i] Live Tile runtime mismatch detected; relaunching as unpackaged desktop app.");
    g_ui.liveTileClearOnShutdown = IniReadS(L"Strings", L"LiveTileClearOnShutdown", L"[i] Clearing Live Tile on shutdown.");
    g_ui.liveTileClearOnShutdownSkipped = IniReadS(L"Strings", L"LiveTileClearOnShutdownSkipped", L"[i] Clear Live Tile on shutdown skipped: %s");
    g_ui.liveTileClearOnShutdownSuccess = IniReadS(L"Strings", L"LiveTileClearOnShutdownSuccess", L"[i] Live Tile cleared on shutdown.");
    g_ui.generateOnStartup = IniReadS(L"Strings", L"GenerateOnStartup", L"Generate on Startup");
    g_ui.generateOnStartupOnlyWhenChanged = IniReadS(L"Strings", L"GenerateOnStartupOnlyWhenChanged", L"Generate on Startup only when changed");
    g_ui.hideDisabledEntries = IniReadS(L"Strings", L"HideDisabledEntries", L"Hide disabled entries");
    g_ui.generateDesktopIconForDisabledEntries = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries");
    g_ui.deleteDisabledAssets = IniReadS(L"Strings", L"DeleteDisabledAssets", L"Auto-delete disabled asset files");
    g_ui.wallpaperDetectionFallback = IniReadS(L"Strings", L"WallpaperDetectionFallback", L"Fallback when selected path is invalid");
    g_ui.wallpaperDecodeLowMemory = IniReadS(L"Strings", L"WallpaperDecodeLowMemory", L"Low-memory wallpaper decode");
    g_ui.wallpaperDecodeMaxLongEdgeLabel = IniReadS(L"Strings", L"WallpaperDecodeMaxLongEdgeLabel", L"Wallpaper decode max edge: %s");
    g_ui.wallpaperDecodeAuto = IniReadS(L"Strings", L"WallpaperDecodeAuto", L"auto");
    g_ui.wallpaperDecodePxText = IniReadS(L"Strings", L"WallpaperDecodePxText", L"%d px");
    g_ui.liveWallpaperCaptureTitle = IniReadS(L"Strings", L"LiveWallpaperCaptureTitle", L"Live Wallpaper Detection:");
    g_ui.liveWallpaperCapture = IniReadS(L"Strings", L"LiveWallpaperCapture", L"Capture live wallpaper snapshot");
    g_ui.liveWallpaperCaptureScreenFallback = IniReadS(L"Strings", L"LiveWallpaperCaptureScreenFallback", L"Unsafe screen fallback (may capture desktop)");
    g_ui.liveWallpaperCaptureDelayMsLabel = IniReadS(L"Strings", L"LiveWallpaperCaptureDelayMsLabel", L"Capture delay: %d ms");
    g_ui.liveWallpaperCaptureRefreshMsLabel = IniReadS(L"Strings", L"LiveWallpaperCaptureRefreshMsLabel", L"Capture refresh: %s");
    g_ui.liveWallpaperCaptureStartupRefreshMsLabel = IniReadS(L"Strings", L"LiveWallpaperCaptureStartupRefreshMsLabel", L"Startup recapture: %s");
    g_ui.liveWallpaperCaptureStartupRefreshDurationMsLabel = IniReadS(L"Strings", L"LiveWallpaperCaptureStartupRefreshDurationMsLabel", L"Startup recapture duration: %s");
    g_ui.liveWallpaperCaptureRefreshOnce = IniReadS(L"Strings", L"LiveWallpaperCaptureRefreshOnce", L"once");
    g_ui.liveWallpaperCaptureOff = IniReadS(L"Strings", L"LiveWallpaperCaptureOff", L"off");
    g_ui.liveWallpaperCaptureMsText = IniReadS(L"Strings", L"LiveWallpaperCaptureMsText", L"%d ms");
    g_ui.liveWallpaperProviderNone = IniReadS(L"Strings", L"LiveWallpaperProviderNone", L"None");
    g_ui.liveWallpaperProviderLive = IniReadS(L"Strings", L"LiveWallpaperProviderLive", L"Live wallpaper");
    g_ui.generateScaleAuto = IniReadS(L"Strings", L"GenerateScaleAuto", L"Auto: current DPI scale");
    g_ui.generateScaleAutoPreservesManualToggles = IniReadS(L"Strings", L"GenerateScaleAutoPreservesManualToggles", L"Manual scale checkboxes are remembered but ignored while Auto is enabled.");
    g_ui.generateScale100 = IniReadS(L"Strings", L"GenerateScale100", L"100%");
    g_ui.generateScale125 = IniReadS(L"Strings", L"GenerateScale125", L"125%");
    g_ui.generateScale150 = IniReadS(L"Strings", L"GenerateScale150", L"150%");
    g_ui.generateScale200 = IniReadS(L"Strings", L"GenerateScale200", L"200%");
    g_ui.generateScale400 = IniReadS(L"Strings", L"GenerateScale400", L"400%");
    g_ui.currentDpiScaleLabel = IniReadS(L"Strings", L"CurrentDpiScaleLabel", L"Current DPI: %d%% (asset scale: %d%%)");
    g_ui.generatedAssetCache = IniReadS(L"Strings", L"GeneratedAssetCache", L"Generated asset cache");
    g_ui.generatedAssetPrecache = IniReadS(L"Strings", L"GeneratedAssetPrecache", L"Pre-cache slideshow wallpapers");
    g_ui.generatedAssetCacheMaxEntriesLabel = IniReadS(L"Strings", L"GeneratedAssetCacheMaxEntriesLabel", L"Cache max entries: %d");
    g_ui.generatedAssetPrecacheMaxFilesLabel = IniReadS(L"Strings", L"GeneratedAssetPrecacheMaxFilesLabel", L"Pre-cache max files: %d");
    g_ui.showMenuAsDropdown = IniReadS(L"Strings", L"ShowMenuAsDropdown", L"Show menu as dropdown");
    g_ui.notificationsTitle = IniReadS(L"Strings", L"NotificationsTitle", L"Notifications:");
    g_ui.notificationsEnabled = IniReadS(L"Strings", L"NotificationsEnabled", L"Enable all tray notifications");
    g_ui.notifyOnStart = IniReadS(L"Strings", L"NotifyOnStart", L"Notify when generation starts");
    g_ui.notifyOnSuccess = IniReadS(L"Strings", L"NotifyOnSuccess", L"Notify when generation succeeds");
    g_ui.notifyOnFailure = IniReadS(L"Strings", L"NotifyOnFailure", L"Notify when generation, wallpaper, or registration fails");
    g_ui.notifyOnBusy = IniReadS(L"Strings", L"NotifyOnBusy", L"Notify when generation is already running");
    g_ui.notifyOnAlreadyRunning = IniReadS(L"Strings", L"NotifyOnAlreadyRunning", L"Notify when the app is already running");
    g_ui.notifyOnSingleInstanceFailure = IniReadS(L"Strings", L"NotifyOnSingleInstanceFailure", L"Notify when single-instance protection fails");
    g_ui.showConsole = IniReadS(L"Strings", L"ShowConsole", L"Show Console");
    g_ui.generateNow = IniReadS(L"Strings", L"GenerateNow", L"Generate now");

    g_ui.loggingEnable = IniReadS(L"Strings", L"LoggingEnable", L"Enable");
    g_ui.changePath = IniReadS(L"Strings", L"ChangePath", L"Change path...");
    g_ui.renamePath = IniReadS(L"Strings", L"RenamePath", L"Rename path...");
    g_ui.openInExplorer = IniReadS(L"Strings", L"OpenInExplorer", L"Open in Explorer");
    g_ui.resetDefault = IniReadS(L"Strings", L"ResetDefault", L"Reset to default");
    g_ui.showPowerShellLog = IniReadS(L"Strings", L"ShowPowerShellLog", L"Show PowerShell Log");

    g_ui.logPathPrefix = IniReadS(L"Strings", L"LogPathPrefix", L"Path: ");
    g_ui.logPathTitle = IniReadS(L"Strings", L"LogPathTitle", L"Log Path");
    g_ui.psStatusPrefix = IniReadS(L"Strings", L"PSStatusPrefix", L"PS Status: ");
    g_ui.psStatusTitle = IniReadS(L"Strings", L"PSStatusTitle", L"PS Status");

    g_ui.disableFitting = IniReadS(L"Strings", L"DisableFitting", L"Disable Fitting");
    g_ui.listenFit = IniReadS(L"Strings", L"ListenFit", L"Listen Fit Mode");
    g_ui.fitModePrefix = IniReadS(L"Strings", L"FitModePrefix", L"Fit Mode: ");
    g_ui.fitModeForced = IniReadS(L"Strings", L"FitModeForced", L"Fit Mode: Fill (forced)");

    g_ui.exitText = IniReadS(L"Strings", L"Exit", L"Exit");
    g_ui.shutdownPendingTitle = IniReadS(L"Strings", L"ShutdownPendingTitle", L"Shutdown pending:");
    g_ui.shutdownPendingReason = IniReadS(L"Strings", L"ShutdownPendingReason", L"Waiting for generation/registration: %s");
    g_ui.cancelShutdown = IniReadS(L"Strings", L"CancelShutdown", L"Cancel shutdown");
    g_ui.forceShutdown = IniReadS(L"Strings", L"ForceShutdown", L"Force shutdown");
    g_ui.shutdownDelayedNotification = IniReadS(L"Strings", L"ShutdownDelayedNotification", L"Exit is waiting for generation/registration to finish: %s");
    g_ui.shutdownRequested = IniReadS(L"Strings", L"ShutdownRequested", L"[i] Shutdown requested.");
    g_ui.shutdownCancelled = IniReadS(L"Strings", L"ShutdownCancelled", L"[i] Shutdown cancelled.");
    g_ui.forceShutdownRequested = IniReadS(L"Strings", L"ForceShutdownRequested", L"[!] Force shutdown requested.");
    g_ui.previousForceShutdownCleanupTitle = IniReadS(L"Strings", L"PreviousForceShutdownCleanupTitle", L"Previous force shutdown");
    g_ui.previousForceShutdownCleanupWarning = IniReadS(L"Strings", L"PreviousForceShutdownCleanupWarning", L"Previous run used Force shutdown while work was pending (%s). Skipped cleanup: %s. Downside: %s");

    g_ui.renameDialogTitle = IniReadS(L"Strings", L"RenameDialogTitle", L"Rename Log Path");
    g_ui.okText = IniReadS(L"Strings", L"OKText", L"OK");
    g_ui.cancelText = IniReadS(L"Strings", L"CancelText", L"Cancel");
    g_ui.errorTitle = IniReadS(L"Strings", L"ErrorTitle", L"Error");
    g_ui.pathCannotBeEmpty = IniReadS(L"Strings", L"PathCannotBeEmpty", L"Path cannot be empty.");

    g_ui.noPowerShellOutputTitle = IniReadS(L"Strings", L"NoPowerShellOutputTitle", L"PowerShell Output");
    g_ui.noPowerShellOutput = IniReadS(L"Strings", L"NoPowerShellOutput", L"No PowerShell output.");
    g_ui.powerShellOutputTruncated = IniReadS(L"Strings", L"PowerShellOutputTruncated", L"[i] Output truncated for display; see the log for the full output (%d characters hidden).");

    g_ui.generateNowTitle = IniReadS(L"Strings", L"GenerateNowTitle", L"Generate Now");
    g_ui.wallpaperNotFound = IniReadS(L"Strings", L"WallpaperNotFound", L"Could not detect current wallpaper.");
    g_ui.currentWallpaperNotFound = IniReadS(L"Strings", L"CurrentWallpaperNotFound", L"Could not detect current wallpaper.");

    g_ui.programStarting = IniReadS(L"Strings", L"ProgramStarting", L"Program starting...");
    g_ui.notificationTitle = IniReadS(L"Strings", L"NotificationTitle", L"DesktopStub");
    g_ui.notificationGenerationStarted = IniReadS(L"Strings", L"NotificationGenerationStarted", L"Wallpaper generation started.");
    g_ui.notificationGenerationSuccess = IniReadS(L"Strings", L"NotificationGenerationSuccess", L"Wallpaper generation completed successfully.");
    g_ui.notificationGenerationFailed = IniReadS(L"Strings", L"NotificationGenerationFailed", L"Wallpaper generation failed.");
    g_ui.notificationGenerationBusy = IniReadS(L"Strings", L"NotificationGenerationBusy", L"Wallpaper generation is already running.");
    g_ui.notificationAlreadyRunning = IniReadS(L"Strings", L"NotificationAlreadyRunning", L"DesktopStub is already running.");
    g_ui.notificationAlreadyRunningTrayPersistent = IniReadS(L"Strings", L"NotificationAlreadyRunningTrayPersistent", L"DesktopStub is already running. Tray icon is enabled in the config, so it will stay visible after restart.");
    g_ui.notificationAlreadyRunningTraySession = IniReadS(L"Strings", L"NotificationAlreadyRunningTraySession", L"DesktopStub is already running. Tray icon is visible for this session only; it will be hidden again after restart because TrayIcon=0 in the config.");
    g_ui.notificationSingleInstanceFailure = IniReadS(L"Strings", L"NotificationSingleInstanceFailure", L"Single-instance protection failed; another copy may run.");
    g_ui.notificationTrayIconHidden = IniReadS(L"Strings", L"NotificationTrayIconHidden", L"Tray icon hidden for this session. Run this executable again to restore it.");
    g_ui.manualGenerateTriggered = IniReadS(L"Strings", L"ManualGenerateTriggered", L"Manual generate triggered.");
    g_ui.liveTileModeChanged = IniReadS(L"Strings", L"LiveTileModeChanged", L"Live Tile mode changed.");
    g_ui.manifestTargetChanged = IniReadS(L"Strings", L"ManifestTargetChanged", L"AppxManifest target changed.");
    g_ui.liveTileModeReregistrationQueued = IniReadS(L"Strings", L"LiveTileModeReregistrationQueued", L"[i] Queued one-time Appx re-registration because Live Tile mode changed.");
    g_ui.liveTileModeReregistrationQueuedNoWallpaper = IniReadS(L"Strings", L"LiveTileModeReregistrationQueuedNoWallpaper", L"[!] Live Tile mode changed; one-time Appx re-registration is pending but current wallpaper could not be detected.");
    g_ui.startingWallpaperGeneration = IniReadS(L"Strings", L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s.");
    g_ui.wallpaperSource = IniReadS(L"Strings", L"WallpaperSource", L"Wallpaper source: %s");
    g_ui.wallpaperImageLoaded = IniReadS(L"Strings", L"WallpaperImageLoaded", L"[i] Wallpaper image loaded (%d x %d).");
    g_ui.renderModeSummary = IniReadS(L"Strings", L"RenderModeSummary", L"[i] Render fit mode: %s");
    g_ui.assetsDirectoryReady = IniReadS(L"Strings", L"AssetsDirectoryReady", L"[i] Assets directory ready: %s");
    g_ui.failedLoadWallpaper = IniReadS(L"Strings", L"FailedLoadWallpaper", L"Failed to load wallpaper image.");
    g_ui.iniEncodingNormalized = IniReadS(L"Strings", L"IniEncodingNormalized", L"[i] INI file normalized to UTF-8 with BOM.");
    g_ui.skippingAssetDisabled = IniReadS(L"Strings", L"SkippingAssetDisabled", L"Skipping %s (disabled in settings).");
    g_ui.skippingStaticAssetLiveTileMode = IniReadS(L"Strings", L"SkippingStaticAssetLiveTileMode", L"Skipping %s (static manifest assets are disabled in Live Tile mode).");
    g_ui.generatedDesktopIconAsset = IniReadS(L"Strings", L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s");
    g_ui.activeScales = IniReadS(L"Strings", L"ActiveScales", L"[i] Active scales: %s");
    g_ui.savedAsset = IniReadS(L"Strings", L"SavedAsset", L"Saved %s");
    g_ui.savedScaledAsset = IniReadS(L"Strings", L"SavedScaledAsset", L"Saved %s (scale %d%%)");
    g_ui.generatedAssetCacheHit = IniReadS(L"Strings", L"GeneratedAssetCacheHit", L"[i] Reused cached generated assets: %s");
    g_ui.generatedAssetCacheSaved = IniReadS(L"Strings", L"GeneratedAssetCacheSaved", L"[i] Saved generated asset cache: %s");
    g_ui.generatedAssetCacheSummary = IniReadS(L"Strings", L"GeneratedAssetCacheSummary", L"[i] Generated asset cache: %s (max entries=%d)");
    g_ui.generatedAssetPrecacheSaved = IniReadS(L"Strings", L"GeneratedAssetPrecacheSaved", L"[i] Precached generated assets: %s");
    g_ui.generatedAssetPrecacheSummary = IniReadS(L"Strings", L"GeneratedAssetPrecacheSummary", L"[i] Generated asset pre-cache: %s (max files=%d)");
    g_ui.failedSaveAsset = IniReadS(L"Strings", L"FailedSaveAsset", L"Failed to save %s");
    g_ui.failedSaveScaledAsset = IniReadS(L"Strings", L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)");
    g_ui.savePngInvalidInput = IniReadS(L"Strings", L"SavePngInvalidInput", L"[!] SavePNG skipped for %s: invalid bitmap, path, or PNG encoder.");
    g_ui.savePngGdiFailure = IniReadS(L"Strings", L"SavePngGdiFailure", L"[!] SavePNG GDI+ save failed for %s (status=%d).");
    g_ui.savePngReplaceFailed = IniReadS(L"Strings", L"SavePngReplaceFailed", L"[!] SavePNG replace failed for %s (temp=%s).");
    g_ui.savePngReplaceRetrying = IniReadS(L"Strings", L"SavePngReplaceRetrying", L"[!] SavePNG replace attempt %d/%d failed for %s (error %lu: %s); retrying in %d ms.");
    g_ui.deletedDisabledAsset = IniReadS(L"Strings", L"DeletedDisabledAsset", L"[i] Deleted disabled asset file: %s");
    g_ui.failedDeleteDisabledAsset = IniReadS(L"Strings", L"FailedDeleteDisabledAsset", L"[!] Failed to delete disabled asset file: %s");

    g_ui.reRegisteringManifest = IniReadS(L"Strings", L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets.");
    g_ui.updatingLiveTile = IniReadS(L"Strings", L"UpdatingLiveTile", L"Updating Live Tile from regenerated assets.");
    g_ui.reRegisteringManifestForLiveTileModeChange = IniReadS(L"Strings", L"ReRegisteringManifestForLiveTileModeChange", L"Re-registering AppxManifest once because Live Tile mode changed.");
    g_ui.liveTileModeReregistrationComplete = IniReadS(L"Strings", L"LiveTileModeReregistrationComplete", L"[i] Live Tile mode change re-registration completed.");
    g_ui.appxManifestCreated = IniReadS(L"Strings", L"AppxManifestCreated", L"[i] Created AppxManifest.xml: %s");
    g_ui.appxManifestExisting = IniReadS(L"Strings", L"AppxManifestExisting", L"[i] Using existing AppxManifest.xml: %s");
    g_ui.appxManifestRegenerated = IniReadS(L"Strings", L"AppxManifestRegenerated", L"[i] Regenerated AppxManifest.xml from built-in defaults: %s");
    g_ui.appxManifestCreateFailed = IniReadS(L"Strings", L"AppxManifestCreateFailed", L"[!] Failed to create AppxManifest.xml: %s");
    g_ui.appxManifestRegenerateFailed = IniReadS(L"Strings", L"AppxManifestRegenerateFailed", L"[!] Failed to regenerate AppxManifest.xml: %s");
    g_ui.manifestPath = IniReadS(L"Strings", L"ManifestPath", L"Manifest path: %s");
    g_ui.usingComRegistration = IniReadS(L"Strings", L"UsingComRegistration", L"Using COM Appx registration...");
    g_ui.comRegistrationUri = IniReadS(L"Strings", L"ComRegistrationUri", L"[i] COM registration URI: %s");
    g_ui.comRegistrationTimeoutDetails = IniReadS(L"Strings", L"ComRegistrationTimeoutDetails", L"[i] COM registration timeout=%s, cancel wait=%d ms");
    g_ui.comRegistrationHelperSummary = IniReadS(L"Strings", L"ComRegistrationHelperSummary", L"[i] COM registration isolation helper: %s");
    g_ui.comRegistrationHelperStarted = IniReadS(L"Strings", L"ComRegistrationHelperStarted", L"[i] COM registration helper started (pid=%lu).");
    g_ui.comRegistrationHelperExitCode = IniReadS(L"Strings", L"ComRegistrationHelperExitCode", L"[i] COM registration helper exited with code 0x%08X.");
    g_ui.comRegistrationHelperCrashed = IniReadS(L"Strings", L"ComRegistrationHelperCrashed", L"[!] COM registration helper crashed or was terminated (exit code 0x%08X).");
    g_ui.comRegistrationHelperTimedOut = IniReadS(L"Strings", L"ComRegistrationHelperTimedOut", L"[!] COM registration helper timed out.");
    g_ui.comRegistrationHelperTerminateFailed = IniReadS(L"Strings", L"ComRegistrationHelperTerminateFailed", L"[!] Failed to terminate timed-out COM registration helper (pid=%lu).");
    g_ui.comRegistrationHelperStillRunning = IniReadS(L"Strings", L"ComRegistrationHelperStillRunning", L"[!] COM registration helper is still running after terminate wait (pid=%lu).");
    g_ui.comRegistrationHelperTerminated = IniReadS(L"Strings", L"ComRegistrationHelperTerminated", L"[i] COM registration helper terminated after timeout (pid=%lu).");
    g_ui.invalidManifestPath = IniReadS(L"Strings", L"InvalidManifestPath", L"Invalid manifest path.");
    g_ui.comRegistrationSuccess = IniReadS(L"Strings", L"ComRegistrationSuccess", L"COM registration success.");
    g_ui.comRegistrationTimedOut = IniReadS(L"Strings", L"ComRegistrationTimedOut", L"COM registration timed out.");
    g_ui.comRegistrationFailed = IniReadS(L"Strings", L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration.");
    g_ui.comRegistrationCancelStillRunning = IniReadS(L"Strings", L"ComRegistrationCancelStillRunning", L"COM registration is still running after cancel; PowerShell fallback skipped to avoid concurrent registration.");
    g_ui.comRegistrationFallbackSkippedAfterTimeout = IniReadS(L"Strings", L"ComRegistrationFallbackSkippedAfterTimeout", L"COM registration timeout is unresolved; PowerShell fallback skipped to avoid concurrent registration.");
    g_ui.comRegistrationException = IniReadS(L"Strings", L"ComRegistrationException", L"COM registration threw exception: 0x%08X");
    g_ui.comExceptionMessage = IniReadS(L"Strings", L"ComExceptionMessage", L"COM message: %s");
    g_ui.launchingPowerShellRegistration = IniReadS(L"Strings", L"LaunchingPowerShellRegistration", L"Launching PowerShell registration...");
    g_ui.powerShellCommand = IniReadS(L"Strings", L"PowerShellCommand", L"Command: %s");
    g_ui.powerShellLaunchDetails = IniReadS(L"Strings", L"PowerShellLaunchDetails", L"[i] PowerShell executable: %s (timeout=%s, poll=%d ms, terminate wait=%d ms)");
    g_ui.powerShellProcessStarted = IniReadS(L"Strings", L"PowerShellProcessStarted", L"[i] PowerShell process started (pid=%lu).");
    g_ui.powerShellTerminateRequested = IniReadS(L"Strings", L"PowerShellTerminateRequested", L"[!] Terminating timed-out PowerShell process (pid=%lu).");
    g_ui.powerShellTerminateFailed = IniReadS(L"Strings", L"PowerShellTerminateFailed", L"[!] Failed to terminate timed-out PowerShell process (pid=%lu).");
    g_ui.powerShellStillRunningAfterTimeout = IniReadS(L"Strings", L"PowerShellStillRunningAfterTimeout", L"[!] PowerShell process is still running after terminate wait (pid=%lu).");
    g_ui.powerShellTerminatedAfterTimeout = IniReadS(L"Strings", L"PowerShellTerminatedAfterTimeout", L"[i] PowerShell process terminated after timeout (pid=%lu).");
    g_ui.powerShellCompleted = IniReadS(L"Strings", L"PowerShellCompleted", L"PowerShell registration completed successfully.");
    g_ui.powerShellTimedOut = IniReadS(L"Strings", L"PowerShellTimedOut", L"PowerShell registration timed out.");
    g_ui.powerShellErrorSideloadDisabled = IniReadS(L"Strings", L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!");
    g_ui.powerShellErrorCode = IniReadS(L"Strings", L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X.");
    g_ui.powerShellOutputFollows = IniReadS(L"Strings", L"PowerShellOutputFollows", L"PowerShell output follows:");
    g_ui.powerShellRegistrationFailed = IniReadS(L"Strings", L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully.");
    g_ui.assetGenerationFinished = IniReadS(L"Strings", L"AssetGenerationFinished", L"Asset generation and registration finished successfully.");
    g_ui.assetGenerationLiveTileFinished = IniReadS(L"Strings", L"AssetGenerationLiveTileFinished", L"Asset generation and Live Tile update finished successfully.");
    g_ui.assetGenerationFailed = IniReadS(L"Strings", L"AssetGenerationFailed", L"Asset generation did not complete successfully.");
    g_ui.appRegistrationFailed = IniReadS(L"Strings", L"AppRegistrationFailed", L"App registration did not complete successfully.");
    g_ui.liveTileUpdateSuccess = IniReadS(L"Strings", L"LiveTileUpdateSuccess", L"Live Tile update completed successfully.");
    g_ui.liveTileUpdateFailed = IniReadS(L"Strings", L"LiveTileUpdateFailed", L"Live Tile update did not complete successfully.");
    g_ui.liveTileUpdateRequiresIdentity = IniReadS(L"Strings", L"LiveTileUpdateRequiresIdentity", L"Live Tile update requires package identity; launch through the registered package entry.");
    g_ui.liveTileUpdateNoUsableAssets = IniReadS(L"Strings", L"LiveTileUpdateNoUsableAssets", L"Live Tile update failed: no valid tile assets were available.");
    g_ui.liveTileUpdateException = IniReadS(L"Strings", L"LiveTileUpdateException", L"Live Tile update threw exception: 0x%08X");
    g_ui.liveTileUpdateMessage = IniReadS(L"Strings", L"LiveTileUpdateMessage", L"Live Tile message: %s");
    g_ui.liveTilePackageIdentity = IniReadS(L"Strings", L"LiveTilePackageIdentity", L"[i] Live Tile package identity: %s");
    g_ui.generationUnhandledException = IniReadS(L"Strings", L"GenerationUnhandledException", L"[!] Generation worker caught an unhandled exception: %s");
    g_ui.generationThreadStartFailed = IniReadS(L"Strings", L"GenerationThreadStartFailed", L"[!] Failed to start generation worker thread: %s");
    g_ui.pollThreadUnhandledException = IniReadS(L"Strings", L"PollThreadUnhandledException", L"[!] Wallpaper poll thread stopped after an unhandled exception: %s");
    g_ui.pollThreadStartFailed = IniReadS(L"Strings", L"PollThreadStartFailed", L"[!] Failed to start wallpaper poll thread: %s");
    g_ui.unknownException = IniReadS(L"Strings", L"UnknownException", L"unknown exception");
    g_ui.skippingRegistrationDueToAssetFailure = IniReadS(L"Strings", L"SkippingRegistrationDueToAssetFailure", L"[!] Skipping Appx registration because asset generation failed.");
    g_ui.skippingLiveTileUpdateDueToAssetFailure = IniReadS(L"Strings", L"SkippingLiveTileUpdateDueToAssetFailure", L"[!] Skipping Live Tile update because asset generation failed.");
    g_ui.createAssetsDirectoryOperation = IniReadS(L"Strings", L"CreateAssetsDirectoryOperation", L"Create Assets directory");
    g_ui.missingManifestAssets = IniReadS(L"Strings", L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing or invalid: %s");
    g_ui.notificationMissingManifestAssets = IniReadS(L"Strings", L"NotificationMissingManifestAssets", L"Registration failed; one or more manifest assets are missing or invalid.");
    g_ui.registrationBlockedUnresolved = IniReadS(L"Strings", L"RegistrationBlockedUnresolved", L"[!] Registration skipped because a previous registration is unresolved: %s");
    g_ui.registrationUnresolvedMarked = IniReadS(L"Strings", L"RegistrationUnresolvedMarked", L"[!] Registration marked unresolved: %s");
    g_ui.registrationUnresolvedCleared = IniReadS(L"Strings", L"RegistrationUnresolvedCleared", L"[i] Previous unresolved registration finished: %s");
    g_ui.registrationUnresolvedExpired = IniReadS(L"Strings", L"RegistrationUnresolvedExpired", L"[!] Previous unresolved registration block expired; allowing registration again: %s");

    g_ui.changeConfirmed = IniReadS(L"Strings", L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce.");
    g_ui.wallpaperAndFitChangeDetected = IniReadS(L"Strings", L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected.");
    g_ui.wallpaperChangeDetected = IniReadS(L"Strings", L"WallpaperChangeDetected", L"Wallpaper change detected.");
    g_ui.wallpaperDetectionEmptyDuringPoll = IniReadS(L"Strings", L"WallpaperDetectionEmptyDuringPoll", L"[!] Wallpaper change ignored: wallpaper detection returned empty.");
    g_ui.wallpaperDetectionEmptyUsingLast = IniReadS(L"Strings", L"WallpaperDetectionEmptyUsingLast", L"[!] Wallpaper detection returned empty during a fit/DPI-only change; using the last valid wallpaper path: %s");
    g_ui.wallpaperDetectionMethodFailed = IniReadS(L"Strings", L"WallpaperDetectionMethodFailed", L"[!] Wallpaper detection method %s did not return a wallpaper path.");
    g_ui.wallpaperDetectionMethodReturnedInvalid = IniReadS(L"Strings", L"WallpaperDetectionMethodReturnedInvalid", L"[!] Wallpaper detection method %s returned an unusable path: %s");
    g_ui.wallpaperDetectionFallbackSelected = IniReadS(L"Strings", L"WallpaperDetectionFallbackSelected", L"[i] Wallpaper detection fallback selected %s: %s");
    g_ui.wallpaperDetectionFallbackDisabled = IniReadS(L"Strings", L"WallpaperDetectionFallbackDisabled", L"[!] Wallpaper detection fallback disabled; using %s result as-is.");
    g_ui.fitModeChangeDetected = IniReadS(L"Strings", L"FitModeChangeDetected", L"Fit mode change detected.");
    g_ui.dpiScaleChangeDetected = IniReadS(L"Strings", L"DpiScaleChangeDetected", L"DPI scale change detected.");
    g_ui.enabledText = IniReadS(L"Strings", L"EnabledText", L"enabled");
    g_ui.disabledText = IniReadS(L"Strings", L"DisabledText", L"disabled");
    g_ui.onText = IniReadS(L"Strings", L"OnText", L"on");
    g_ui.offText = IniReadS(L"Strings", L"OffText", L"off");

    g_ui.trayTip = IniReadS(L"Strings", L"TrayTip", L"DesktopStub");
    g_ui.exeLabel = IniReadS(L"Strings", L"ExeLabel", L"[i] EXE: %s");
    g_ui.iniLabel = IniReadS(L"Strings", L"IniLabel", L"[i] INI: %s");
    g_ui.logFileLabel = IniReadS(L"Strings", L"LogFileLabel", L"[i] Log file: %s");
    g_ui.timingLog = IniReadS(L"Strings", L"TimingLog", L"[i] Timing: %s = %d ms");
    g_ui.timingPreLogInitialization = IniReadS(L"Strings", L"TimingPreLogInitialization", L"pre-log initialization");
    g_ui.timingIniDefaultStringLoading = IniReadS(L"Strings", L"TimingIniDefaultStringLoading", L"INI/default/string loading");
    g_ui.timingGdiStartup = IniReadS(L"Strings", L"TimingGdiStartup", L"GDI+ startup");
    g_ui.timingAssetGeneration = IniReadS(L"Strings", L"TimingAssetGeneration", L"asset generation");
    g_ui.timingAppRegistration = IniReadS(L"Strings", L"TimingAppRegistration", L"app registration");
    g_ui.timingLiveTileUpdate = IniReadS(L"Strings", L"TimingLiveTileUpdate", L"Live Tile update");
    g_ui.timingGenerationTotal = IniReadS(L"Strings", L"TimingGenerationTotal", L"generation total");
    g_ui.powerShellRegistrationSummary = IniReadS(L"Strings", L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s");
    g_ui.usePowerShellSummary = IniReadS(L"Strings", L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)");
    g_ui.liveTileUpdateSummary = IniReadS(L"Strings", L"LiveTileUpdateSummary", L"[i] Live Tile update mode: %s");
    g_ui.manifestTargetSummary = IniReadS(L"Strings", L"ManifestTargetSummary", L"[i] AppxManifest target: %s");
    g_ui.wallpaperDetectionFallbackSummary = IniReadS(L"Strings", L"WallpaperDetectionFallbackSummary", L"[i] Wallpaper detection fallback on invalid path: %s");
    g_ui.liveWallpaperCaptureSummary = IniReadS(L"Strings", L"LiveWallpaperCaptureSummary", L"[i] Live wallpaper capture: %s (delay=%d ms, refresh=%s)");
    g_ui.liveWallpaperCaptureDelayState = IniReadS(L"Strings", L"LiveWallpaperCaptureDelayState", L"[i] LiveWallpaperCaptureDelayMs = %d");
    g_ui.liveWallpaperCaptureRefreshState = IniReadS(L"Strings", L"LiveWallpaperCaptureRefreshState", L"[i] LiveWallpaperCaptureRefreshMs = %d");
    g_ui.liveWallpaperCaptureStartupRefreshState = IniReadS(L"Strings", L"LiveWallpaperCaptureStartupRefreshState", L"[i] LiveWallpaperCaptureStartupRefreshMs = %d");
    g_ui.liveWallpaperCaptureStartupRefreshDurationState = IniReadS(L"Strings", L"LiveWallpaperCaptureStartupRefreshDurationState", L"[i] LiveWallpaperCaptureStartupRefreshDurationMs = %d");
    g_ui.liveWallpaperCaptureScreenFallbackState = IniReadS(L"Strings", L"LiveWallpaperCaptureScreenFallbackState", L"[i] LiveWallpaperCaptureScreenFallback = %d");
    g_ui.liveWallpaperCaptureStartupRefreshSummary = IniReadS(L"Strings", L"LiveWallpaperCaptureStartupRefreshSummary", L"[i] Live wallpaper startup recapture: every %s for %s");
    g_ui.liveWallpaperCaptureNoWindow = IniReadS(L"Strings", L"LiveWallpaperCaptureNoWindow", L"[!] Live wallpaper capture could not find a source window for %s.");
    g_ui.liveWallpaperCaptureSnapshotPathFailed = IniReadS(L"Strings", L"LiveWallpaperCaptureSnapshotPathFailed", L"[!] Live wallpaper capture could not create a snapshot path for %s.");
    g_ui.liveWallpaperCaptureSnapshotFailed = IniReadS(L"Strings", L"LiveWallpaperCaptureSnapshotFailed", L"[!] Live wallpaper snapshot capture failed for %s.");
    g_ui.liveWallpaperCaptureSnapshotRejectedBlank = IniReadS(L"Strings", L"LiveWallpaperCaptureSnapshotRejectedBlank", L"[!] Live wallpaper snapshot rejected because the captured frame was blank or black for %s.");
    g_ui.liveWallpaperCaptureSnapshotRejectedTooSmall = IniReadS(L"Strings", L"LiveWallpaperCaptureSnapshotRejectedTooSmall", L"[!] Live wallpaper snapshot rejected because the candidate window was too small for %s: %d x %d (minimum %d x %d).");
    g_ui.liveWallpaperCaptureSnapshotCaptured = IniReadS(L"Strings", L"LiveWallpaperCaptureSnapshotCaptured", L"[i] Live wallpaper snapshot captured from %s: %s");
    g_ui.trayIconSummary = IniReadS(L"Strings", L"TrayIconSummary", L"[i] Tray icon: %s");
    g_ui.consoleSummary = IniReadS(L"Strings", L"ConsoleSummary", L"[i] Console %s.");
    g_ui.loggingSummary = IniReadS(L"Strings", L"LoggingSummary", L"[i] Logging %s.");
    g_ui.generateOnStartupSummary = IniReadS(L"Strings", L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s");
    g_ui.generateOnStartupOnlyWhenChangedSummary = IniReadS(L"Strings", L"GenerateOnStartupOnlyWhenChangedSummary", L"[i] Startup generation only when changed: %s");
    g_ui.hideDisabledEntriesSummary = IniReadS(L"Strings", L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s");
    g_ui.generateDesktopIconForDisabledEntriesSummary = IniReadS(L"Strings", L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s");
    g_ui.deleteDisabledAssetsSummary = IniReadS(L"Strings", L"DeleteDisabledAssetsSummary", L"[i] Auto-delete disabled asset files: %s");
    g_ui.generateScaleAutoSummary = IniReadS(L"Strings", L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s");
    g_ui.generateScaleSummary = IniReadS(L"Strings", L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s");
    g_ui.showMenuAsDropdownSummary = IniReadS(L"Strings", L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s");
    g_ui.notificationsSummary = IniReadS(L"Strings", L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)");
    g_ui.powerShellEnabledMode = IniReadS(L"Strings", L"PowerShellEnabledMode", L"PowerShell enabled");
    g_ui.comPreferredMode = IniReadS(L"Strings", L"ComPreferredMode", L"COM preferred with PowerShell fallback");
    g_ui.consoleAllocated = IniReadS(L"Strings", L"ConsoleAllocated", L"[i] Console allocated.");
    g_ui.startupGenerationEnabled = IniReadS(L"Strings", L"StartupGenerationEnabled", L"[i] Startup generation enabled.");
    g_ui.startupGenerationSkippedUnchanged = IniReadS(L"Strings", L"StartupGenerationSkippedUnchanged", L"[i] Startup generation skipped: wallpaper/config unchanged.");
    g_ui.startupGenerationSkippedNoWallpaper = IniReadS(L"Strings", L"StartupGenerationSkippedNoWallpaper", L"[!] Startup generation skipped: no wallpaper detected.");
    g_ui.startupGenerationReason = IniReadS(L"Strings", L"StartupGenerationReason", L"startup generation");
    g_ui.commandLineGenerationRequestReason = IniReadS(L"Strings", L"CommandLineGenerationRequestReason", L"command-line generation request");
    g_ui.commandLineOnceReason = IniReadS(L"Strings", L"CommandLineOnceReason", L"command-line --once");
    g_ui.commandLineWallpaperReason = IniReadS(L"Strings", L"CommandLineWallpaperReason", L"command-line wallpaper");
    g_ui.commandLineForceGenerateReason = IniReadS(L"Strings", L"CommandLineForceGenerateReason", L"command-line --generate");
    g_ui.manifestRegenerateReason = IniReadS(L"Strings", L"ManifestRegenerateReason", L"manifest regeneration");
    g_ui.commandLineWallpaperPathInvalid = IniReadS(L"Strings", L"CommandLineWallpaperPathInvalid", L"Wallpaper path does not exist or is not a file: %s");
    g_ui.commandLineManifestSettingIgnored = IniReadS(L"Strings", L"CommandLineManifestSettingIgnored", L"Manifest INI setting is no longer supported: %s. Edit AppxManifest.xml directly or use --regenerate-manifest to restore the built-in default manifest.");
    g_ui.manualGenerationRequested = IniReadS(L"Strings", L"ManualGenerationRequested", L"[i] Manual generation requested from the tray menu.");
    g_ui.generationRequestBusy = IniReadS(L"Strings", L"GenerationRequestBusy", L"[!] Generation request ignored: another operation is already running.");
    g_ui.emptyWallpaper = IniReadS(L"Strings", L"EmptyWallpaper", L"[!] Empty wallpaper.");
    g_ui.futureLogEntriesNewPath = IniReadS(L"Strings", L"FutureLogEntriesNewPath", L"[i] Future log entries will be written to the new path.");
    g_ui.futureLogEntriesRenamedPath = IniReadS(L"Strings", L"FutureLogEntriesRenamedPath", L"[i] Future log entries will be written to the renamed path.");
    g_ui.futureLogEntriesDefaultPath = IniReadS(L"Strings", L"FutureLogEntriesDefaultPath", L"[i] Future log entries will be written to the default path.");
    g_ui.listenWallpaperState = IniReadS(L"Strings", L"ListenWallpaperState", L"[i] ListenWallpaper = %d");
    g_ui.listenFitState = IniReadS(L"Strings", L"ListenFitState", L"[i] ListenFit = %d");
    g_ui.disableFittingState = IniReadS(L"Strings", L"DisableFittingState", L"[i] DisableFitting = %d");
    g_ui.assetToggleState = IniReadS(L"Strings", L"AssetToggleState", L"[i] Asset %s %s.");
    g_ui.comRegistrationStatusFailed = IniReadS(L"Strings", L"ComRegistrationStatusFailed", L"[!] COM registration failed (status=%d)");
    g_ui.comRegistrationAsyncError = IniReadS(L"Strings", L"ComRegistrationAsyncError", L"[!] COM registration async error: 0x%08X");
    g_ui.comRegistrationDeploymentError = IniReadS(L"Strings", L"ComRegistrationDeploymentError", L"[!] COM deployment error: 0x%08X");
    g_ui.comRegistrationDeploymentMessage = IniReadS(L"Strings", L"ComRegistrationDeploymentMessage", L"COM deployment message: %s");
    g_ui.gdiPlusStartupFailedTitle = IniReadS(L"Strings", L"GdiPlusStartupFailedTitle", L"GDI+");
    g_ui.gdiPlusStartupFailedMessage = IniReadS(L"Strings", L"GdiPlusStartupFailedMessage", L"GDI+ startup failed");
    g_ui.gdiPlusOperationFailed = IniReadS(L"Strings", L"GdiPlusOperationFailed", L"[!] GDI+ %s failed (status=%d).");
    g_ui.win32FailureFmt = IniReadS(L"Strings", L"Win32FailureFmt", L"[!] %s failed (error %lu: %s)");
    g_ui.logPathChanged = IniReadS(L"Strings", L"LogPathChanged", L"[i] Log path changed to: %s");
    g_ui.logPathRenamed = IniReadS(L"Strings", L"LogPathRenamed", L"[i] Log path renamed to: %s");
    g_ui.logPathReset = IniReadS(L"Strings", L"LogPathReset", L"[i] Log path reset to default: %s");
    g_ui.logWriteFailed = IniReadS(L"Strings", L"LogWriteFailed", L"[!] Log file write failed (error %lu: %s): %s");
    g_ui.iniWriteFailed = IniReadS(L"Strings", L"IniWriteFailed", L"[!] Failed to write INI value [%s] %s (error %lu: %s).");
    g_ui.listenFitAutoDisabled = IniReadS(L"Strings", L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting.");
    g_ui.singleInstanceScopeSummary = IniReadS(L"Strings", L"SingleInstanceScopeSummary", L"[i] Single-instance scope: %s");
    g_ui.singleInstanceMutexFailure = IniReadS(L"Strings", L"SingleInstanceMutexFailure", L"[!] Single-instance mutex creation failed (error %lu: %s); action=%s.");

    g_ui.fitFill = IniReadS(L"Strings", L"FitFill", L"Fill");
    g_ui.fitFit = IniReadS(L"Strings", L"FitFit", L"Fit");
    g_ui.fitStretch = IniReadS(L"Strings", L"FitStretch", L"Stretch");
    g_ui.fitCenter = IniReadS(L"Strings", L"FitCenter", L"Center");
    g_ui.fitTile = IniReadS(L"Strings", L"FitTile", L"Tile");
    g_ui.fitSpan = IniReadS(L"Strings", L"FitSpan", L"Span");

    g_ui.notifyOnStartState = IniReadS(L"Strings", L"NotifyOnStartState", L"[i] NotifyOnStart = %d");
    g_ui.notifyOnSuccessState = IniReadS(L"Strings", L"NotifyOnSuccessState", L"[i] NotifyOnSuccess = %d");
    g_ui.notifyOnFailureState = IniReadS(L"Strings", L"NotifyOnFailureState", L"[i] NotifyOnFailure = %d");
    g_ui.notifyOnBusyState = IniReadS(L"Strings", L"NotifyOnBusyState", L"[i] NotifyOnBusy = %d");
    g_ui.notifyOnAlreadyRunningState = IniReadS(L"Strings", L"NotifyOnAlreadyRunningState", L"[i] NotifyOnAlreadyRunning = %d");
    g_ui.notifyOnSingleInstanceFailureState = IniReadS(L"Strings", L"NotifyOnSingleInstanceFailureState", L"[i] NotifyOnSingleInstanceFailure = %d");
    g_ui.unspecifiedReason = IniReadS(L"Strings", L"UnspecifiedReason", L"an unspecified reason");
    g_ui.changeDetected = IniReadS(L"Strings", L"ChangeDetected", L"change detected");
    g_ui.advancedTitle = IniReadS(L"Strings", L"AdvancedTitle", L"Advanced:");
    g_ui.pollIntervalLabel = IniReadS(L"Strings", L"PollIntervalLabel", L"Poll interval: %d ms");
    g_ui.confirmMsLabel = IniReadS(L"Strings", L"ConfirmMsLabel", L"Confirm: %d ms");
    g_ui.debounceMinMsLabel = IniReadS(L"Strings", L"DebounceMinMsLabel", L"Debounce min: %d ms");
    g_ui.pollInitialDebounceBypassMsLabel = IniReadS(L"Strings", L"PollInitialDebounceBypassMsLabel", L"Initial debounce bypass: %d ms");
    g_ui.iniCacheRefreshMsLabel = IniReadS(L"Strings", L"IniCacheRefreshMsLabel", L"INI cache refresh: %d ms");
    g_ui.wallpaperDetectionLogRepeatMsLabel = IniReadS(L"Strings", L"WallpaperDetectionLogRepeatMsLabel", L"Wallpaper detection log repeat: %d ms");
    g_ui.notificationTimeoutMsLabel = IniReadS(L"Strings", L"NotificationTimeoutMsLabel", L"Notification timeout: %d ms");
    g_ui.shutdownInitialNoticeMsLabel = IniReadS(L"Strings", L"ShutdownInitialNoticeMsLabel", L"Shutdown first notice: %d ms");
    g_ui.shutdownRepeatNoticeMsLabel = IniReadS(L"Strings", L"ShutdownRepeatNoticeMsLabel", L"Shutdown repeat notice: %d ms");
    g_ui.singleInstanceSignalRetriesLabel = IniReadS(L"Strings", L"SingleInstanceSignalRetriesLabel", L"Single-instance signal retries: %d");
    g_ui.singleInstanceSignalDelayMsLabel = IniReadS(L"Strings", L"SingleInstanceSignalDelayMsLabel", L"Single-instance signal delay: %d ms");
    g_ui.comRegistrationTimeoutMsLabel = IniReadS(L"Strings", L"ComRegistrationTimeoutMsLabel", L"COM registration timeout: %s");
    g_ui.comRegistrationCancelWaitMsLabel = IniReadS(L"Strings", L"ComRegistrationCancelWaitMsLabel", L"COM cancel wait: %d ms");
    g_ui.comRegistrationHelperLabel = IniReadS(L"Strings", L"ComRegistrationHelperLabel", L"COM isolation helper: %s");
    g_ui.trayIconHiddenDelayMsLabel = IniReadS(L"Strings", L"TrayIconHiddenDelayMsLabel", L"Tray hide warning delay: %d ms");
    g_ui.logBufferLinesLabel = IniReadS(L"Strings", L"LogBufferLinesLabel", L"Log buffer lines: %d");
    g_ui.logAppendLockWaitMsLabel = IniReadS(L"Strings", L"LogAppendLockWaitMsLabel", L"Log append lock wait: %d ms");
    g_ui.powerShellExeLabel = IniReadS(L"Strings", L"PowerShellExeLabel", L"PowerShell executable: %s");
    g_ui.powerShellPollMsLabel = IniReadS(L"Strings", L"PowerShellPollMsLabel", L"PowerShell poll: %d ms");
    g_ui.powerShellTimeoutMsLabel = IniReadS(L"Strings", L"PowerShellTimeoutMsLabel", L"PowerShell timeout: %s");
    g_ui.powerShellTerminateWaitMsLabel = IniReadS(L"Strings", L"PowerShellTerminateWaitMsLabel", L"PowerShell terminate wait: %d ms");
    g_ui.powerShellOutputMessageBoxCharsLabel = IniReadS(L"Strings", L"PowerShellOutputMessageBoxCharsLabel", L"PowerShell output dialog limit: %d chars");
    g_ui.registrationUnresolvedBlockMsLabel = IniReadS(L"Strings", L"RegistrationUnresolvedBlockMsLabel", L"Unresolved registration block: %d ms");
    g_ui.savePngReplaceRetriesLabel = IniReadS(L"Strings", L"SavePngReplaceRetriesLabel", L"PNG replace retries: %d");
    g_ui.savePngReplaceRetryDelayMsLabel = IniReadS(L"Strings", L"SavePngReplaceRetryDelayMsLabel", L"PNG replace retry delay: %d ms");
    g_ui.timeoutInfiniteText = IniReadS(L"Strings", L"TimeoutInfiniteText", L"infinite");
    g_ui.manifestTitle = IniReadS(L"Strings", L"ManifestTitle", L"Manifest:");
    g_ui.manifestDetailsTitle = IniReadS(L"Strings", L"ManifestDetailsTitle", L"Manifest details");
    g_ui.manifestIdentityNameLabel = IniReadS(L"Strings", L"ManifestIdentityNameLabel", L"Manifest identity: %s");
    g_ui.manifestPublisherLabel = IniReadS(L"Strings", L"ManifestPublisherLabel", L"Manifest publisher: %s");
    g_ui.manifestPackageVersionLabel = IniReadS(L"Strings", L"ManifestPackageVersionLabel", L"Manifest version: %s");
    g_ui.manifestDisplayNameLabel = IniReadS(L"Strings", L"ManifestDisplayNameLabel", L"Manifest display name: %s");
    g_ui.manifestPublisherDisplayNameLabel = IniReadS(L"Strings", L"ManifestPublisherDisplayNameLabel", L"Manifest publisher display: %s");
    g_ui.manifestDescriptionLabel = IniReadS(L"Strings", L"ManifestDescriptionLabel", L"Manifest description: %s");
    g_ui.manifestExecutableLabel = IniReadS(L"Strings", L"ManifestExecutableLabel", L"Manifest executable: %s");
    g_ui.manifestApplicationIdLabel = IniReadS(L"Strings", L"ManifestApplicationIdLabel", L"Manifest app id: %s");
    g_ui.manifestTargetLabel = IniReadS(L"Strings", L"ManifestTargetLabel", L"Manifest target: %s");
    g_ui.manifestTargetDeviceFamilyNameLabel = IniReadS(L"Strings", L"ManifestTargetDeviceFamilyNameLabel", L"Manifest target family: %s");
    g_ui.manifestMinVersionLabel = IniReadS(L"Strings", L"ManifestMinVersionLabel", L"Manifest min version: %s");
    g_ui.manifestMaxVersionTestedLabel = IniReadS(L"Strings", L"ManifestMaxVersionTestedLabel", L"Manifest max tested: %s");
    g_ui.manifestResourceLanguageLabel = IniReadS(L"Strings", L"ManifestResourceLanguageLabel", L"Manifest resource language: %s");
    g_ui.manifestPropertiesLogoLabel = IniReadS(L"Strings", L"ManifestPropertiesLogoLabel", L"Manifest properties logo: %s");
    g_ui.manifestSquare150x150LogoLabel = IniReadS(L"Strings", L"ManifestSquare150x150LogoLabel", L"Manifest square 150 logo: %s");
    g_ui.manifestSquare44x44LogoLabel = IniReadS(L"Strings", L"ManifestSquare44x44LogoLabel", L"Manifest square 44 logo: %s");
    g_ui.manifestSquare30x30LogoLabel = IniReadS(L"Strings", L"ManifestSquare30x30LogoLabel", L"Manifest square 30 logo: %s");
    g_ui.manifestSquare71x71LogoLabel = IniReadS(L"Strings", L"ManifestSquare71x71LogoLabel", L"Manifest square 71 logo: %s");
    g_ui.manifestWide310x150LogoLabel = IniReadS(L"Strings", L"ManifestWide310x150LogoLabel", L"Manifest wide 310 logo: %s");
    g_ui.manifestSquare310x310LogoLabel = IniReadS(L"Strings", L"ManifestSquare310x310LogoLabel", L"Manifest square 310 logo: %s");
    g_ui.manifestBackgroundColorLabel = IniReadS(L"Strings", L"ManifestBackgroundColorLabel", L"Manifest background color: %s");
    g_ui.manifestShowNameOnTilesLabel = IniReadS(L"Strings", L"ManifestShowNameOnTilesLabel", L"Manifest show names on: %s");
    g_ui.manifestEntryPointLabel = IniReadS(L"Strings", L"ManifestEntryPointLabel", L"Manifest entry point: %s");
    g_ui.manifestRestrictedCapabilityLabel = IniReadS(L"Strings", L"ManifestRestrictedCapabilityLabel", L"Manifest restricted capability: %s");
    g_ui.manifestSplashScreenImageLabel = IniReadS(L"Strings", L"ManifestSplashScreenImageLabel", L"Manifest splash screen image: %s");
    g_ui.manifestSourceLabel = IniReadS(L"Strings", L"ManifestSourceLabel", L"Manifest source: %s");
    g_ui.manifestSourceExisting = IniReadS(L"Strings", L"ManifestSourceExisting", L"existing AppxManifest.xml");
    g_ui.manifestSourceGenerated = IniReadS(L"Strings", L"ManifestSourceGenerated", L"generated from built-in defaults if missing");
    g_ui.manifestRegenerateNow = IniReadS(L"Strings", L"ManifestRegenerateNow", L"Regenerate AppxManifest.xml from built-in defaults");
    g_ui.wallpaperDetectionMethodLabel = IniReadS(L"Strings", L"WallpaperDetectionMethodLabel", L"Wallpaper detection method: %s");
    g_ui.singleInstanceFailureActionLabel = IniReadS(L"Strings", L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s");
    g_ui.singleInstanceActionIgnore = IniReadS(L"Strings", L"SingleInstanceActionIgnore", L"Ignore");
    g_ui.singleInstanceActionWarn = IniReadS(L"Strings", L"SingleInstanceActionWarn", L"Warn");
    g_ui.singleInstanceActionExit = IniReadS(L"Strings", L"SingleInstanceActionExit", L"Exit");
    g_ui.singleInstanceActionCrash = IniReadS(L"Strings", L"SingleInstanceActionCrash", L"Crash");
    g_ui.unknownError = IniReadS(L"Strings", L"UnknownError", L"Unknown error");

    g_ui.logFileFilter = IniReadS(L"Strings", L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||");
}

static const wchar_t* FindStringDefault(const wchar_t* key, const wchar_t* fallback)
{
    for (const auto& d : g_stringDefaults)
    {
        if (IEquals(d.key, key))
            return d.value;
    }
    return fallback;
}

static std::vector<std::wstring> ExtractFormatTokens(const std::wstring& fmt)
{
    std::vector<std::wstring> tokens;
    for (size_t i = 0; i < fmt.size(); ++i)
    {
        if (fmt[i] != L'%')
            continue;

        if (i + 1 < fmt.size() && fmt[i + 1] == L'%')
        {
            ++i;
            continue;
        }

        if (fmt.compare(i, 4, L"%08X") == 0)
        {
            tokens.push_back(L"%08X");
            i += 3;
        }
        else if (fmt.compare(i, 3, L"%lu") == 0)
        {
            tokens.push_back(L"%lu");
            i += 2;
        }
        else if (fmt.compare(i, 2, L"%s") == 0)
        {
            tokens.push_back(L"%s");
            i += 1;
        }
        else if (fmt.compare(i, 2, L"%d") == 0)
        {
            tokens.push_back(L"%d");
            i += 1;
        }
        else
        {
            tokens.push_back(L"<invalid>");
        }
    }
    return tokens;
}

static bool FormatTokensMatch(const std::wstring& fmt, std::initializer_list<const wchar_t*> expected)
{
    std::vector<std::wstring> actual = ExtractFormatTokens(fmt);
    if (actual.size() != expected.size())
        return false;

    size_t i = 0;
    for (const wchar_t* token : expected)
    {
        if (actual[i++] != token)
            return false;
    }
    return true;
}

static void RequireFormat(std::wstring& text, const wchar_t* key, std::initializer_list<const wchar_t*> expected)
{
    if (!FormatTokensMatch(text, expected))
    {
        QueueStartupWarning(L"[!] Invalid format tokens in [Strings] " + std::wstring(key ? key : L"") + L"; using built-in default for this session.");
        text = FindStringDefault(key, L"");
    }
}

static void ValidateFormatStrings()
{
    RequireFormat(g_ui.exeLabel, L"ExeLabel", { L"%s" });
    RequireFormat(g_ui.iniLabel, L"IniLabel", { L"%s" });
    RequireFormat(g_ui.logFileLabel, L"LogFileLabel", { L"%s" });
    RequireFormat(g_ui.timingLog, L"TimingLog", { L"%s", L"%d" });
    RequireFormat(g_ui.powerShellRegistrationSummary, L"PowerShellRegistrationSummary", { L"%s" });
    RequireFormat(g_ui.usePowerShellSummary, L"UsePowerShellSummary", { L"%d", L"%s" });
    RequireFormat(g_ui.liveTileUpdateSummary, L"LiveTileUpdateSummary", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackSummary, L"WallpaperDetectionFallbackSummary", { L"%s" });
    RequireFormat(g_ui.win8LiveTileOopHelperState, L"Win8LiveTileOopHelperState", { L"%d" });
    RequireFormat(g_ui.win8LiveTileBackgroundTaskState, L"Win8LiveTileBackgroundTaskState", { L"%d" });
    RequireFormat(g_ui.win8LiveTileBrokerAppState, L"Win8LiveTileBrokerAppState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureSummary, L"LiveWallpaperCaptureSummary", { L"%s", L"%d", L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureDelayState, L"LiveWallpaperCaptureDelayState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureRefreshState, L"LiveWallpaperCaptureRefreshState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureStartupRefreshState, L"LiveWallpaperCaptureStartupRefreshState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureStartupRefreshDurationState, L"LiveWallpaperCaptureStartupRefreshDurationState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureScreenFallbackState, L"LiveWallpaperCaptureScreenFallbackState", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureStartupRefreshSummary, L"LiveWallpaperCaptureStartupRefreshSummary", { L"%s", L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureNoWindow, L"LiveWallpaperCaptureNoWindow", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureSnapshotPathFailed, L"LiveWallpaperCaptureSnapshotPathFailed", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureSnapshotFailed, L"LiveWallpaperCaptureSnapshotFailed", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureSnapshotRejectedBlank, L"LiveWallpaperCaptureSnapshotRejectedBlank", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureSnapshotRejectedTooSmall, L"LiveWallpaperCaptureSnapshotRejectedTooSmall", { L"%s", L"%d", L"%d", L"%d", L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureSnapshotCaptured, L"LiveWallpaperCaptureSnapshotCaptured", { L"%s", L"%s" });
    RequireFormat(g_ui.trayIconSummary, L"TrayIconSummary", { L"%s" });
    RequireFormat(g_ui.consoleSummary, L"ConsoleSummary", { L"%s" });
    RequireFormat(g_ui.loggingSummary, L"LoggingSummary", { L"%s" });
    RequireFormat(g_ui.generateOnStartupSummary, L"GenerateOnStartupSummary", { L"%s" });
    RequireFormat(g_ui.generateOnStartupOnlyWhenChangedSummary, L"GenerateOnStartupOnlyWhenChangedSummary", { L"%s" });
    RequireFormat(g_ui.hideDisabledEntriesSummary, L"HideDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.generateDesktopIconForDisabledEntriesSummary, L"GenerateDesktopIconForDisabledEntriesSummary", { L"%s" });
    RequireFormat(g_ui.deleteDisabledAssetsSummary, L"DeleteDisabledAssetsSummary", { L"%s" });
    RequireFormat(g_ui.generateScaleAutoSummary, L"GenerateScaleAutoSummary", { L"%s" });
    RequireFormat(g_ui.generateScaleSummary, L"GenerateScaleSummary", { L"%d", L"%s" });
    RequireFormat(g_ui.currentDpiScaleLabel, L"CurrentDpiScaleLabel", { L"%d", L"%d" });
    RequireFormat(g_ui.showMenuAsDropdownSummary, L"ShowMenuAsDropdownSummary", { L"%s" });
    RequireFormat(g_ui.tileTextCurrent, L"TileTextCurrent", { L"%s" });
    RequireFormat(g_ui.tileTextSecondaryCurrent, L"TileTextSecondaryCurrent", { L"%s" });
    RequireFormat(g_ui.tileTextBadgeCurrent, L"TileTextBadgeCurrent", { L"%s" });
    RequireFormat(g_ui.tileTextHorizontalAlign, L"TileTextHorizontalAlign", { L"%s" });
    RequireFormat(g_ui.tileTextVerticalAlign, L"TileTextVerticalAlign", { L"%s" });
    RequireFormat(g_ui.tileTextBadgeHorizontalAlign, L"TileTextBadgeHorizontalAlign", { L"%s" });
    RequireFormat(g_ui.tileTextBadgeVerticalAlign, L"TileTextBadgeVerticalAlign", { L"%s" });
    RequireFormat(g_ui.tileTextFontSize, L"TileTextFontSize", { L"%d" });
    RequireFormat(g_ui.tileTextSecondaryFontSize, L"TileTextSecondaryFontSize", { L"%d" });
    RequireFormat(g_ui.tileTextBadgeFontSize, L"TileTextBadgeFontSize", { L"%d" });
    RequireFormat(g_ui.tileTextEdited, L"TileTextEdited", { L"%s" });
    RequireFormat(g_ui.tileTextSummary, L"TileTextSummary", { L"%s", L"%s" });
    RequireFormat(g_ui.notificationsSummary, L"NotificationsSummary", { L"%s", L"%s", L"%s", L"%s", L"%s", L"%s", L"%s" });
    RequireFormat(g_ui.listenWallpaperState, L"ListenWallpaperState", { L"%d" });
    RequireFormat(g_ui.listenFitState, L"ListenFitState", { L"%d" });
    RequireFormat(g_ui.disableFittingState, L"DisableFittingState", { L"%d" });
    RequireFormat(g_ui.assetToggleState, L"AssetToggleState", { L"%s", L"%s" });
    RequireFormat(g_ui.comRegistrationStatusFailed, L"ComRegistrationStatusFailed", { L"%d" });
    RequireFormat(g_ui.comRegistrationAsyncError, L"ComRegistrationAsyncError", { L"%08X" });
    RequireFormat(g_ui.comRegistrationDeploymentError, L"ComRegistrationDeploymentError", { L"%08X" });
    RequireFormat(g_ui.comRegistrationDeploymentMessage, L"ComRegistrationDeploymentMessage", { L"%s" });
    RequireFormat(g_ui.liveTileUpdateException, L"LiveTileUpdateException", { L"%08X" });
    RequireFormat(g_ui.liveTileUpdateMessage, L"LiveTileUpdateMessage", { L"%s" });
    RequireFormat(g_ui.liveTilePackageIdentity, L"LiveTilePackageIdentity", { L"%s" });
    RequireFormat(g_ui.gdiPlusOperationFailed, L"GdiPlusOperationFailed", { L"%s", L"%d" });
    RequireFormat(g_ui.win32FailureFmt, L"Win32FailureFmt", { L"%s", L"%lu", L"%s" });
    RequireFormat(g_ui.logPathChanged, L"LogPathChanged", { L"%s" });
    RequireFormat(g_ui.logPathRenamed, L"LogPathRenamed", { L"%s" });
    RequireFormat(g_ui.logPathReset, L"LogPathReset", { L"%s" });
    RequireFormat(g_ui.logWriteFailed, L"LogWriteFailed", { L"%lu", L"%s", L"%s" });
    RequireFormat(g_ui.iniWriteFailed, L"IniWriteFailed", { L"%s", L"%s", L"%lu", L"%s" });
    RequireFormat(g_ui.singleInstanceScopeSummary, L"SingleInstanceScopeSummary", { L"%s" });
    RequireFormat(g_ui.singleInstanceMutexFailure, L"SingleInstanceMutexFailure", { L"%lu", L"%s", L"%s" });
    RequireFormat(g_ui.notifyOnStartState, L"NotifyOnStartState", { L"%d" });
    RequireFormat(g_ui.notifyOnSuccessState, L"NotifyOnSuccessState", { L"%d" });
    RequireFormat(g_ui.notifyOnFailureState, L"NotifyOnFailureState", { L"%d" });
    RequireFormat(g_ui.notifyOnBusyState, L"NotifyOnBusyState", { L"%d" });
    RequireFormat(g_ui.notifyOnAlreadyRunningState, L"NotifyOnAlreadyRunningState", { L"%d" });
    RequireFormat(g_ui.notifyOnSingleInstanceFailureState, L"NotifyOnSingleInstanceFailureState", { L"%d" });
    RequireFormat(g_ui.pollIntervalLabel, L"PollIntervalLabel", { L"%d" });
    RequireFormat(g_ui.confirmMsLabel, L"ConfirmMsLabel", { L"%d" });
    RequireFormat(g_ui.debounceMinMsLabel, L"DebounceMinMsLabel", { L"%d" });
    RequireFormat(g_ui.pollInitialDebounceBypassMsLabel, L"PollInitialDebounceBypassMsLabel", { L"%d" });
    RequireFormat(g_ui.iniCacheRefreshMsLabel, L"IniCacheRefreshMsLabel", { L"%d" });
    RequireFormat(g_ui.wallpaperDetectionLogRepeatMsLabel, L"WallpaperDetectionLogRepeatMsLabel", { L"%d" });
    RequireFormat(g_ui.notificationTimeoutMsLabel, L"NotificationTimeoutMsLabel", { L"%d" });
    RequireFormat(g_ui.shutdownInitialNoticeMsLabel, L"ShutdownInitialNoticeMsLabel", { L"%d" });
    RequireFormat(g_ui.shutdownRepeatNoticeMsLabel, L"ShutdownRepeatNoticeMsLabel", { L"%d" });
    RequireFormat(g_ui.singleInstanceSignalRetriesLabel, L"SingleInstanceSignalRetriesLabel", { L"%d" });
    RequireFormat(g_ui.singleInstanceSignalDelayMsLabel, L"SingleInstanceSignalDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.comRegistrationTimeoutMsLabel, L"ComRegistrationTimeoutMsLabel", { L"%s" });
    RequireFormat(g_ui.comRegistrationCancelWaitMsLabel, L"ComRegistrationCancelWaitMsLabel", { L"%d" });
    RequireFormat(g_ui.comRegistrationHelperLabel, L"ComRegistrationHelperLabel", { L"%s" });
    RequireFormat(g_ui.trayIconHiddenDelayMsLabel, L"TrayIconHiddenDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.logBufferLinesLabel, L"LogBufferLinesLabel", { L"%d" });
    RequireFormat(g_ui.logAppendLockWaitMsLabel, L"LogAppendLockWaitMsLabel", { L"%d" });
    RequireFormat(g_ui.powerShellExeLabel, L"PowerShellExeLabel", { L"%s" });
    RequireFormat(g_ui.powerShellPollMsLabel, L"PowerShellPollMsLabel", { L"%d" });
    RequireFormat(g_ui.powerShellTimeoutMsLabel, L"PowerShellTimeoutMsLabel", { L"%s" });
    RequireFormat(g_ui.powerShellTerminateWaitMsLabel, L"PowerShellTerminateWaitMsLabel", { L"%d" });
    RequireFormat(g_ui.powerShellOutputMessageBoxCharsLabel, L"PowerShellOutputMessageBoxCharsLabel", { L"%d" });
    RequireFormat(g_ui.registrationUnresolvedBlockMsLabel, L"RegistrationUnresolvedBlockMsLabel", { L"%d" });
    RequireFormat(g_ui.savePngReplaceRetriesLabel, L"SavePngReplaceRetriesLabel", { L"%d" });
    RequireFormat(g_ui.savePngReplaceRetryDelayMsLabel, L"SavePngReplaceRetryDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.manifestIdentityNameLabel, L"ManifestIdentityNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestPublisherLabel, L"ManifestPublisherLabel", { L"%s" });
    RequireFormat(g_ui.manifestPackageVersionLabel, L"ManifestPackageVersionLabel", { L"%s" });
    RequireFormat(g_ui.manifestDisplayNameLabel, L"ManifestDisplayNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestPublisherDisplayNameLabel, L"ManifestPublisherDisplayNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestDescriptionLabel, L"ManifestDescriptionLabel", { L"%s" });
    RequireFormat(g_ui.manifestExecutableLabel, L"ManifestExecutableLabel", { L"%s" });
    RequireFormat(g_ui.manifestApplicationIdLabel, L"ManifestApplicationIdLabel", { L"%s" });
    RequireFormat(g_ui.manifestTargetLabel, L"ManifestTargetLabel", { L"%s" });
    RequireFormat(g_ui.manifestTargetDeviceFamilyNameLabel, L"ManifestTargetDeviceFamilyNameLabel", { L"%s" });
    RequireFormat(g_ui.manifestMinVersionLabel, L"ManifestMinVersionLabel", { L"%s" });
    RequireFormat(g_ui.manifestMaxVersionTestedLabel, L"ManifestMaxVersionTestedLabel", { L"%s" });
    RequireFormat(g_ui.manifestResourceLanguageLabel, L"ManifestResourceLanguageLabel", { L"%s" });
    RequireFormat(g_ui.manifestPropertiesLogoLabel, L"ManifestPropertiesLogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestSquare150x150LogoLabel, L"ManifestSquare150x150LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestSquare44x44LogoLabel, L"ManifestSquare44x44LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestSquare30x30LogoLabel, L"ManifestSquare30x30LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestSquare71x71LogoLabel, L"ManifestSquare71x71LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestWide310x150LogoLabel, L"ManifestWide310x150LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestSquare310x310LogoLabel, L"ManifestSquare310x310LogoLabel", { L"%s" });
    RequireFormat(g_ui.manifestBackgroundColorLabel, L"ManifestBackgroundColorLabel", { L"%s" });
    RequireFormat(g_ui.manifestShowNameOnTilesLabel, L"ManifestShowNameOnTilesLabel", { L"%s" });
    RequireFormat(g_ui.manifestEntryPointLabel, L"ManifestEntryPointLabel", { L"%s" });
    RequireFormat(g_ui.manifestRestrictedCapabilityLabel, L"ManifestRestrictedCapabilityLabel", { L"%s" });
    RequireFormat(g_ui.manifestSplashScreenImageLabel, L"ManifestSplashScreenImageLabel", { L"%s" });
    RequireFormat(g_ui.manifestSourceLabel, L"ManifestSourceLabel", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureDelayMsLabel, L"LiveWallpaperCaptureDelayMsLabel", { L"%d" });
    RequireFormat(g_ui.liveWallpaperCaptureRefreshMsLabel, L"LiveWallpaperCaptureRefreshMsLabel", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureStartupRefreshMsLabel, L"LiveWallpaperCaptureStartupRefreshMsLabel", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureStartupRefreshDurationMsLabel, L"LiveWallpaperCaptureStartupRefreshDurationMsLabel", { L"%s" });
    RequireFormat(g_ui.liveWallpaperCaptureMsText, L"LiveWallpaperCaptureMsText", { L"%d" });
    RequireFormat(g_ui.wallpaperDecodeMaxLongEdgeLabel, L"WallpaperDecodeMaxLongEdgeLabel", { L"%s" });
    RequireFormat(g_ui.wallpaperDecodePxText, L"WallpaperDecodePxText", { L"%d" });
    RequireFormat(g_ui.wallpaperDetectionMethodLabel, L"WallpaperDetectionMethodLabel", { L"%s" });
    RequireFormat(g_ui.singleInstanceFailureActionLabel, L"SingleInstanceFailureActionLabel", { L"%s" });
    RequireFormat(g_ui.generatedAssetCacheMaxEntriesLabel, L"GeneratedAssetCacheMaxEntriesLabel", { L"%d" });
    RequireFormat(g_ui.generatedAssetPrecacheMaxFilesLabel, L"GeneratedAssetPrecacheMaxFilesLabel", { L"%d" });
    RequireFormat(g_ui.shutdownPendingReason, L"ShutdownPendingReason", { L"%s" });
    RequireFormat(g_ui.shutdownDelayedNotification, L"ShutdownDelayedNotification", { L"%s" });
    RequireFormat(g_ui.previousForceShutdownCleanupWarning, L"PreviousForceShutdownCleanupWarning", { L"%s", L"%s", L"%s" });

    RequireFormat(g_ui.commandLineWallpaperPathInvalid, L"CommandLineWallpaperPathInvalid", { L"%s" });
    RequireFormat(g_ui.commandLineManifestSettingIgnored, L"CommandLineManifestSettingIgnored", { L"%s" });
    RequireFormat(g_ui.manifestTargetSummary, L"ManifestTargetSummary", { L"%s" });
    RequireFormat(g_ui.startingWallpaperGeneration, L"StartingWallpaperGeneration", { L"%s" });
    RequireFormat(g_ui.wallpaperSource, L"WallpaperSource", { L"%s" });
    RequireFormat(g_ui.wallpaperImageLoaded, L"WallpaperImageLoaded", { L"%d", L"%d" });
    RequireFormat(g_ui.renderModeSummary, L"RenderModeSummary", { L"%s" });
    RequireFormat(g_ui.assetsDirectoryReady, L"AssetsDirectoryReady", { L"%s" });
    RequireFormat(g_ui.skippingAssetDisabled, L"SkippingAssetDisabled", { L"%s" });
    RequireFormat(g_ui.skippingStaticAssetLiveTileMode, L"SkippingStaticAssetLiveTileMode", { L"%s" });
    RequireFormat(g_ui.generatedDesktopIconAsset, L"GeneratedDesktopIconAsset", { L"%s" });
    RequireFormat(g_ui.activeScales, L"ActiveScales", { L"%s" });
    RequireFormat(g_ui.savedAsset, L"SavedAsset", { L"%s" });
    RequireFormat(g_ui.savedScaledAsset, L"SavedScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.generatedAssetCacheHit, L"GeneratedAssetCacheHit", { L"%s" });
    RequireFormat(g_ui.generatedAssetCacheSaved, L"GeneratedAssetCacheSaved", { L"%s" });
    RequireFormat(g_ui.generatedAssetCacheSummary, L"GeneratedAssetCacheSummary", { L"%s", L"%d" });
    RequireFormat(g_ui.generatedAssetPrecacheSaved, L"GeneratedAssetPrecacheSaved", { L"%s" });
    RequireFormat(g_ui.generatedAssetPrecacheSummary, L"GeneratedAssetPrecacheSummary", { L"%s", L"%d" });
    RequireFormat(g_ui.failedSaveAsset, L"FailedSaveAsset", { L"%s" });
    RequireFormat(g_ui.failedSaveScaledAsset, L"FailedSaveScaledAsset", { L"%s", L"%d" });
    RequireFormat(g_ui.savePngInvalidInput, L"SavePngInvalidInput", { L"%s" });
    RequireFormat(g_ui.savePngGdiFailure, L"SavePngGdiFailure", { L"%s", L"%d" });
    RequireFormat(g_ui.savePngReplaceFailed, L"SavePngReplaceFailed", { L"%s", L"%s" });
    RequireFormat(g_ui.savePngReplaceRetrying, L"SavePngReplaceRetrying", { L"%d", L"%d", L"%s", L"%lu", L"%s", L"%d" });
    RequireFormat(g_ui.deletedDisabledAsset, L"DeletedDisabledAsset", { L"%s" });
    RequireFormat(g_ui.failedDeleteDisabledAsset, L"FailedDeleteDisabledAsset", { L"%s" });
    RequireFormat(g_ui.appxManifestCreated, L"AppxManifestCreated", { L"%s" });
    RequireFormat(g_ui.appxManifestExisting, L"AppxManifestExisting", { L"%s" });
    RequireFormat(g_ui.appxManifestRegenerated, L"AppxManifestRegenerated", { L"%s" });
    RequireFormat(g_ui.appxManifestCreateFailed, L"AppxManifestCreateFailed", { L"%s" });
    RequireFormat(g_ui.appxManifestRegenerateFailed, L"AppxManifestRegenerateFailed", { L"%s" });
    RequireFormat(g_ui.manifestPath, L"ManifestPath", { L"%s" });
    RequireFormat(g_ui.comRegistrationUri, L"ComRegistrationUri", { L"%s" });
    RequireFormat(g_ui.comRegistrationTimeoutDetails, L"ComRegistrationTimeoutDetails", { L"%s", L"%d" });
    RequireFormat(g_ui.comRegistrationHelperSummary, L"ComRegistrationHelperSummary", { L"%s" });
    RequireFormat(g_ui.comRegistrationHelperStarted, L"ComRegistrationHelperStarted", { L"%lu" });
    RequireFormat(g_ui.comRegistrationHelperExitCode, L"ComRegistrationHelperExitCode", { L"%08X" });
    RequireFormat(g_ui.comRegistrationHelperCrashed, L"ComRegistrationHelperCrashed", { L"%08X" });
    RequireFormat(g_ui.comRegistrationHelperTerminateFailed, L"ComRegistrationHelperTerminateFailed", { L"%lu" });
    RequireFormat(g_ui.comRegistrationHelperStillRunning, L"ComRegistrationHelperStillRunning", { L"%lu" });
    RequireFormat(g_ui.comRegistrationHelperTerminated, L"ComRegistrationHelperTerminated", { L"%lu" });
    RequireFormat(g_ui.comRegistrationException, L"ComRegistrationException", { L"%08X" });
    RequireFormat(g_ui.comExceptionMessage, L"ComExceptionMessage", { L"%s" });
    RequireFormat(g_ui.powerShellCommand, L"PowerShellCommand", { L"%s" });
    RequireFormat(g_ui.powerShellLaunchDetails, L"PowerShellLaunchDetails", { L"%s", L"%s", L"%d", L"%d" });
    RequireFormat(g_ui.powerShellProcessStarted, L"PowerShellProcessStarted", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminateRequested, L"PowerShellTerminateRequested", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminateFailed, L"PowerShellTerminateFailed", { L"%lu" });
    RequireFormat(g_ui.powerShellStillRunningAfterTimeout, L"PowerShellStillRunningAfterTimeout", { L"%lu" });
    RequireFormat(g_ui.powerShellTerminatedAfterTimeout, L"PowerShellTerminatedAfterTimeout", { L"%lu" });
    RequireFormat(g_ui.powerShellErrorCode, L"PowerShellErrorCode", { L"%08X" });
    RequireFormat(g_ui.powerShellOutputTruncated, L"PowerShellOutputTruncated", { L"%d" });
    RequireFormat(g_ui.generationUnhandledException, L"GenerationUnhandledException", { L"%s" });
    RequireFormat(g_ui.generationThreadStartFailed, L"GenerationThreadStartFailed", { L"%s" });
    RequireFormat(g_ui.pollThreadUnhandledException, L"PollThreadUnhandledException", { L"%s" });
    RequireFormat(g_ui.pollThreadStartFailed, L"PollThreadStartFailed", { L"%s" });
    RequireFormat(g_ui.missingManifestAssets, L"MissingManifestAssets", { L"%s" });
    RequireFormat(g_ui.registrationBlockedUnresolved, L"RegistrationBlockedUnresolved", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedMarked, L"RegistrationUnresolvedMarked", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedCleared, L"RegistrationUnresolvedCleared", { L"%s" });
    RequireFormat(g_ui.registrationUnresolvedExpired, L"RegistrationUnresolvedExpired", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionMethodFailed, L"WallpaperDetectionMethodFailed", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionEmptyUsingLast, L"WallpaperDetectionEmptyUsingLast", { L"%s" });
    RequireFormat(g_ui.wallpaperDetectionMethodReturnedInvalid, L"WallpaperDetectionMethodReturnedInvalid", { L"%s", L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackSelected, L"WallpaperDetectionFallbackSelected", { L"%s", L"%s" });
    RequireFormat(g_ui.wallpaperDetectionFallbackDisabled, L"WallpaperDetectionFallbackDisabled", { L"%s" });
}

static std::wstring EnsureTrailingSpace(std::wstring s)
{
    if (!s.empty() && s.back() != L' ')
        s.push_back(L' ');
    return s;
}

static void NormalizeSeparatorStrings()
{
    g_ui.logPathPrefix = EnsureTrailingSpace(g_ui.logPathPrefix);
    g_ui.psStatusPrefix = EnsureTrailingSpace(g_ui.psStatusPrefix);
    g_ui.fitModePrefix = EnsureTrailingSpace(g_ui.fitModePrefix);
}


// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
static std::atomic<bool> g_running(true), g_console(false), g_logging(true), g_tray(true);
static std::atomic<bool> g_listenWallpaper(true);
static std::atomic<bool> g_listenFit(true);
static std::atomic<bool> g_disableFitting(false);
static std::atomic<bool> g_wallpaperDetectionFallbackOnInvalid(true);
static std::atomic<bool> g_generateOnStartup(true);
static std::atomic<bool> g_generateOnStartupOnlyWhenChanged(true);
static std::atomic<int> g_liveTileUpdateMode(static_cast<int>(LiveTileUpdateMode::Auto));
static std::atomic<bool> g_liveTileRelaunchOnSwitch(true);
static std::atomic<bool> g_clearLiveTileOnShutdown(true);
static std::atomic<int> g_relaunchAfterExit(0);
static std::atomic<int> g_manifestCompatibilityTarget(static_cast<int>(ManifestCompatibilityTarget::Windows10));
static std::atomic<bool> g_hideDisabled(false);
static std::atomic<bool> g_showMenuAsDropdown(true);
static std::atomic<bool> g_notificationsEnabled(true);
static std::atomic<bool> g_notifyOnStart(false);
static std::atomic<bool> g_notifyOnSuccess(false);
static std::atomic<bool> g_notifyOnFailure(true);
static std::atomic<bool> g_notifyOnBusy(false);
static std::atomic<bool> g_notifyOnAlreadyRunning(false);
static std::atomic<bool> g_notifyOnSingleInstanceFailure(true);
static std::atomic<bool> g_generateDesktopIconForDisabledEntries(true);
static std::atomic<bool> g_deleteDisabledAssets(false);
static std::atomic<bool> g_generateScaleAuto(true);
static std::atomic<bool> g_generateScale100(true);
static std::atomic<bool> g_generateScale125(true);
static std::atomic<bool> g_generateScale150(true);
static std::atomic<bool> g_generateScale200(false);
static std::atomic<bool> g_generateScale400(false);
static std::atomic<int> g_notificationTimeoutMs(4000);
static std::atomic<int> g_shutdownInitialNoticeMs(1000);
static std::atomic<int> g_shutdownRepeatNoticeMs(5000);
static std::atomic<int> g_trayIconHiddenDelayMs(750);
static std::atomic<int> g_logBufferLines(1024);
static std::atomic<bool> g_trimWorkingSetOnIdle(true);
static std::atomic<bool> g_compactCrtHeapOnIdle(true);
static std::atomic<bool> g_keepGdiPlusLoaded(false);
static std::atomic<int> g_pollInitialDebounceBypassMs(5000);
static std::atomic<int> g_wallpaperDetectionLogRepeatMs(60000);
static std::atomic<int> g_powerShellOutputMessageBoxChars(6000);
static std::mutex g_consoleApplyMutex;
static std::atomic<bool> g_consoleAttached(false);
static std::mutex g_startupWarningMutex;
static std::vector<std::wstring> g_startupWarnings;
static std::mutex g_registrationUnresolvedMutex;
static bool g_registrationUnresolved = false;
static HANDLE g_registrationUnresolvedProcess = nullptr;
static DWORD g_registrationUnresolvedPid = 0;
static steady_clock::time_point g_registrationUnresolvedSince{};
static std::wstring g_registrationUnresolvedReason;
static std::mutex g_generationMutex;
static std::mutex g_generationQueueMutex;
static std::condition_variable g_generationQueueCv;
static std::thread g_generationWorker;
static std::atomic<bool> g_generationActive(false);
static std::atomic<bool> g_shutdownRequested(false);
static std::mutex g_generationStateMutex;
static std::wstring g_generationActiveReason;
static bool g_generationWorkerStarted = false;
static bool g_generationWorkerStop = false;
static bool g_generationQueued = false;
static std::wstring g_generationQueuedWallpaper;
static std::wstring g_generationQueuedReason;
static std::mutex g_pollWakeMutex;
static std::condition_variable g_pollWakeCv;
static std::atomic<unsigned long> g_pollWakeGeneration(0);
static constexpr UINT WM_APP_SHUTDOWN_READY = WM_APP + 1;
static constexpr UINT WM_APP_REQUEST_SHUTDOWN = WM_APP + 2;
static constexpr UINT_PTR SHUTDOWN_NOTICE_TIMER_ID = 1;
static constexpr UINT_PTR APPX_ACTIVATION_REQUEST_TIMER_ID = 2;
static std::atomic<HWND> g_shutdownWindow(nullptr);
static std::atomic<bool> g_consoleControlShutdownPending(false);

static void QueueStartupWarning(const std::wstring& warning)
{
    std::lock_guard<std::mutex> lk(g_startupWarningMutex);
    g_startupWarnings.push_back(warning);
}

static const IniDefault g_defaults[] = {
    // Settings
    {L"Settings", L"PollIntervalMs", L"2000"},
    {L"Settings", L"ConfirmMs", L"800"},
    {L"Settings", L"DebounceMinMs", L"1200"},
    {L"Settings", L"PollInitialDebounceBypassMs", L"5000"},
    {L"Settings", L"IniCacheRefreshMs", L"250"},
    {L"Settings", L"Logging", L"1"},
    {L"Settings", L"LogPath", L""},
    {L"Settings", L"LogBufferLines", L"1024"},
    {L"Settings", L"LogAppendLockWaitMs", L"1000"},
    {L"Settings", L"TrimWorkingSetOnIdle", L"1"},
    {L"Settings", L"CompactCrtHeapOnIdle", L"1"},
    {L"Settings", L"KeepGdiPlusLoaded", L"0"},
    {L"Settings", L"TrayIcon", L"1"},
    {L"Settings", L"TrayIconHiddenDelayMs", L"750"},
    {L"Settings", L"ShowConsole", L"0"},
    {L"Settings", L"GenerateOnStartup", L"1"},
    {L"Settings", L"GenerateOnStartupOnlyWhenChanged", L"1"},
    {L"Settings", L"ListenWallpaper", L"1"},
    {L"Settings", L"WallpaperDetectionMethod", L"TranscodedImageCache"},
    {L"Settings", L"WallpaperDetectionFallbackOnInvalid", L"1"},
    {L"Settings", L"WallpaperDetectionLogRepeatMs", L"60000"},
    {L"Settings", L"WallpaperDecodeLowMemory", L"1"},
    {L"Settings", L"WallpaperDecodeMaxLongEdge", L"0"},
    {L"Settings", L"LiveWallpaperCapture", L"1"},
    {L"Settings", L"LiveWallpaperCaptureDelayMs", L"2500"},
    {L"Settings", L"LiveWallpaperCaptureRefreshMs", L"10000"},
    {L"Settings", L"LiveWallpaperCaptureScreenFallback", L"0"},
    {L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", L"1000"},
    {L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", L"0"},
    {L"Settings", L"GeneratedAssetCache", L"1"},
    {L"Settings", L"GeneratedAssetCacheMaxEntries", L"64"},
    {L"Settings", L"GeneratedAssetPrecache", L"1"},
    {L"Settings", L"GeneratedAssetPrecacheMaxFiles", L"16"},
    {L"Settings", L"ListenFit", L"1"},
    {L"Settings", L"DisableFitting", L"0"},
    {L"Settings", L"UsePowerShell", L"0"},
    {L"Settings", L"ExperimentalLiveTileUpdate", L"Auto"},
    {L"Settings", L"LiveTileRelaunchOnSwitch", L"1"},
    {L"Settings", L"ClearLiveTileOnShutdown", L"1"},
    {L"Settings", L"AppxManifestTarget", L"Windows10"},
    {L"Settings", L"Win8LiveTileOopHelper", L"0"},
    {L"Settings", L"Win8LiveTileBackgroundTask", L"0"},
    {L"Settings", L"Win8LiveTileBrokerApp", L"1"},
    {L"Settings", L"HideDisabledEntries", L"0"},
    {L"Settings", L"GenerateDesktopIconForDisabledEntries", L"1"},
    {L"Settings", L"DeleteDisabledAssets", L"0"},
    {L"Settings", L"GenerateScaleAuto", L"1"},
    {L"Settings", L"GenerateScale100", L"1"},
    {L"Settings", L"GenerateScale125", L"1"},
    {L"Settings", L"GenerateScale150", L"1"},
    {L"Settings", L"GenerateScale200", L"0"},
    {L"Settings", L"GenerateScale400", L"0"},
    {L"Settings", L"ShowMenuAsDropdown", L"1"},
    {L"Settings", L"NotificationsEnabled", L"1"},
    {L"Settings", L"NotifyOnStart", L"0"},
    {L"Settings", L"NotifyOnSuccess", L"0"},
    {L"Settings", L"NotifyOnFailure", L"1"},
    {L"Settings", L"NotifyOnBusy", L"0"},
    {L"Settings", L"NotifyOnAlreadyRunning", L"0"},
    {L"Settings", L"NotifyOnSingleInstanceFailure", L"1"},
    {L"Settings", L"NotificationTimeoutMs", L"4000"},
    {L"Settings", L"ShutdownInitialNoticeMs", L"1000"},
    {L"Settings", L"ShutdownRepeatNoticeMs", L"5000"},
    {L"Settings", L"SingleInstanceSignalRetries", L"50"},
    {L"Settings", L"SingleInstanceSignalDelayMs", L"100"},
    {L"Settings", L"SingleInstanceFailureAction", L"Warn"},
    {L"Settings", L"ComRegistrationTimeoutMs", L"120000"},
    {L"Settings", L"ComRegistrationCancelWaitMs", L"5000"},
    {L"Settings", L"ComRegistrationUseHelper", L"0"},
    {L"Settings", L"PowerShellExe", L"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe"},
    {L"Settings", L"PowerShellPollMs", L"50"},
    {L"Settings", L"PowerShellTimeoutMs", L"120000"},
    {L"Settings", L"PowerShellTerminateWaitMs", L"5000"},
    {L"Settings", L"PowerShellOutputMessageBoxChars", L"6000"},
    {L"Settings", L"RegistrationUnresolvedBlockMs", L"300000"},
    {L"Settings", L"SavePngReplaceRetries", L"3"},
    {L"Settings", L"SavePngReplaceRetryDelayMs", L"75"},


    // Assets (IMPORTANT: correct defaults)
    {L"Assets", L"StoreLogo", L"0"},
    {L"Assets", L"MediumTile", L"1"},
    {L"Assets", L"Square44x44Logo", L"0"},
    {L"Assets", L"Square30x30Logo", L"0"},
    {L"Assets", L"SmallTile", L"0"},
    {L"Assets", L"WideTile", L"1"},
    {L"Assets", L"LargeTile", L"1"},
    {L"Assets", L"SplashScreen", L"0"},

    // Tile text overlay
    {L"TileText", L"Enabled", L"0"},
    {L"TileText", L"Text", L""},
    {L"TileText", L"SecondaryText", L""},
    {L"TileText", L"BadgeText", L""},
    {L"TileText", L"Font", L"Segoe UI"},
    {L"TileText", L"FontSize", L"18"},
    {L"TileText", L"SecondaryFontSize", L"12"},
    {L"TileText", L"BadgeFontSize", L"40"},
    {L"TileText", L"Bold", L"0"},
    {L"TileText", L"SecondaryBold", L"0"},
    {L"TileText", L"BadgeBold", L"0"},
    {L"TileText", L"Italic", L"0"},
    {L"TileText", L"Color", L"#FFFFFFFF"},
    {L"TileText", L"SecondaryColor", L"#CCFFFFFF"},
    {L"TileText", L"BadgeColor", L"#FFFFFFFF"},
    {L"TileText", L"Shadow", L"1"},
    {L"TileText", L"ShadowColor", L"#AA000000"},
    {L"TileText", L"HorizontalAlign", L"Center"},
    {L"TileText", L"VerticalAlign", L"Top"},
    {L"TileText", L"BadgeHorizontalAlign", L"Right"},
    {L"TileText", L"BadgeVerticalAlign", L"Top"},
    {L"TileText", L"MarginX", L"8"},
    {L"TileText", L"MarginY", L"10"},
    {L"TileText", L"LineGap", L"2"},
    {L"TileText", L"MaxSecondaryLines", L"2"},
    {L"TileText", L"ApplyToMediumTile", L"1"},
    {L"TileText", L"ApplyToWideTile", L"1"},
    {L"TileText", L"ApplyToLargeTile", L"1"},
    {L"TileText", L"ApplyToSmallTile", L"0"},
    {L"TileText", L"ApplyToLogos", L"0"},

};

static const IniDefault g_powerShellErrorMessageDefaults[] = {
    {POWERSHELL_ERROR_MESSAGES_SECTION, L"0x80073CFF", L"PowerShell error: Enable sideloading first!"},
    {POWERSHELL_ERROR_MESSAGES_SECTION, L"0x80073CF9", L"PowerShell error: AppX install failed. If output also shows 0x80070002, a file referenced by AppxManifest.xml is missing or the manifest/assets path cannot be found."},
    {POWERSHELL_ERROR_MESSAGES_SECTION, L"0x80073D02", L"PowerShell error: AppX registration failed because the package is currently in use. Close the running DesktopStub package and retry."},
    {POWERSHELL_ERROR_MESSAGES_SECTION, L"0x80070002", L"PowerShell error: file not found during AppX registration. Check that AppxManifest.xml and every referenced asset file exist."},
};

struct StringDefault {
    const wchar_t* key;
    const wchar_t* value;
};

static const StringDefault g_stringDefaults[] = {
    {L"EnabledText", L"enabled"},
    {L"DisabledText", L"disabled"},
    {L"OnText", L"on"},
    {L"OffText", L"off"},

    {L"TrayTip", L"DesktopStub"},
    {L"ExeLabel", L"[i] EXE: %s"},
    {L"IniLabel", L"[i] INI: %s"},
    {L"LogFileLabel", L"[i] Log file: %s"},
    {L"TimingLog", L"[i] Timing: %s = %d ms"},
    {L"TimingPreLogInitialization", L"pre-log initialization"},
    {L"TimingIniDefaultStringLoading", L"INI/default/string loading"},
    {L"TimingGdiStartup", L"GDI+ startup"},
    {L"TimingAssetGeneration", L"asset generation"},
    {L"TimingAppRegistration", L"app registration"},
    {L"TimingLiveTileUpdate", L"Live Tile update"},
    {L"TimingGenerationTotal", L"generation total"},
    {L"PowerShellRegistrationSummary", L"[i] PowerShell registration: %s"},
    {L"UsePowerShellSummary", L"[i] UsePowerShell = %d (%s)"},
    {L"LiveTileUpdateSummary", L"[i] Live Tile update mode: %s"},
    {L"ManifestTargetSummary", L"[i] AppxManifest target: %s"},
    {L"WallpaperDetectionFallbackSummary", L"[i] Wallpaper detection fallback on invalid path: %s"},
    {L"LiveWallpaperCaptureSummary", L"[i] Live wallpaper capture: %s (delay=%d ms, refresh=%s)"},
    {L"LiveWallpaperCaptureDelayState", L"[i] LiveWallpaperCaptureDelayMs = %d"},
    {L"LiveWallpaperCaptureRefreshState", L"[i] LiveWallpaperCaptureRefreshMs = %d"},
    {L"LiveWallpaperCaptureStartupRefreshState", L"[i] LiveWallpaperCaptureStartupRefreshMs = %d"},
    {L"LiveWallpaperCaptureStartupRefreshDurationState", L"[i] LiveWallpaperCaptureStartupRefreshDurationMs = %d"},
    {L"LiveWallpaperCaptureScreenFallbackState", L"[i] LiveWallpaperCaptureScreenFallback = %d"},
    {L"LiveWallpaperCaptureStartupRefreshSummary", L"[i] Live wallpaper startup recapture: every %s for %s"},
    {L"LiveWallpaperCaptureStartupRefreshOffSummary", L"[i] Live wallpaper startup recapture: off"},
    {L"LiveWallpaperCaptureNoWindow", L"[!] Live wallpaper capture could not find a source window for %s."},
    {L"LiveWallpaperCaptureSnapshotPathFailed", L"[!] Live wallpaper capture could not create a snapshot path for %s."},
    {L"LiveWallpaperCaptureSnapshotFailed", L"[!] Live wallpaper snapshot capture failed for %s."},
    {L"LiveWallpaperCaptureSnapshotRejectedBlank", L"[!] Live wallpaper snapshot rejected because the captured frame was blank or black for %s."},
    {L"LiveWallpaperCaptureSnapshotRejectedTooSmall", L"[!] Live wallpaper snapshot rejected because the candidate window was too small for %s: %d x %d (minimum %d x %d)."},
    {L"LiveWallpaperCaptureSnapshotCaptured", L"[i] Live wallpaper snapshot captured from %s: %s"},
    {L"Win8LiveTileOopHelperState", L"[i] Win8LiveTileOopHelper = %d"},
    {L"Win8LiveTileBackgroundTaskState", L"[i] Win8LiveTileBackgroundTask = %d"},
    {L"Win8LiveTileBrokerAppState", L"[i] Win8LiveTileBrokerApp = %d"},
    {L"TrayIconSummary", L"[i] Tray icon: %s"},
    {L"ConsoleSummary", L"[i] Console %s."},
    {L"TrimWorkingSetOnIdleSummary", L"[i] Idle working-set trimming: %s"},
    {L"CompactCrtHeapOnIdleSummary", L"[i] Idle CRT heap compaction: %s"},
    {L"KeepGdiPlusLoadedSummary", L"[i] Keep GDI+ loaded after image work: %s"},
    {L"LoggingSummary", L"[i] Logging %s."},
    {L"GenerateOnStartupSummary", L"[i] Generate on Startup: %s"},
    {L"GenerateOnStartupOnlyWhenChangedSummary", L"[i] Startup generation only when changed: %s"},
    {L"HideDisabledEntriesSummary", L"[i] Hide disabled entries: %s"},
    {L"GenerateDesktopIconForDisabledEntriesSummary", L"[i] Generate Desktop Icon for disabled entries: %s"},
    {L"DeleteDisabledAssetsSummary", L"[i] Auto-delete disabled asset files: %s"},
    {L"GenerateScaleAutoSummary", L"[i] Generate current DPI scale automatically: %s"},
    {L"GenerateScaleSummary", L"[i] Generate %d%% scale assets: %s"},
    {L"ShowMenuAsDropdownSummary", L"[i] Show menu as dropdown: %s"},
    {L"NotificationsSummary", L"[i] Tray notifications: %s (generation start=%s, generation success=%s, generation failure=%s, busy warning=%s, already running=%s, protection failure=%s)"},
    {L"PowerShellEnabledMode", L"PowerShell enabled"},
    {L"ComPreferredMode", L"COM preferred with PowerShell fallback"},
    {L"ConsoleAllocated", L"[i] Console allocated."},
    {L"StartupGenerationEnabled", L"[i] Startup generation enabled."},
    {L"StartupGenerationRestoringClearedLiveTile", L"[i] Startup generation restoring Live Tile because it was cleared on shutdown."},
    {L"StartupGenerationReason", L"startup generation"},
    {L"StartupLiveTileRestoreReason", L"startup Live Tile restore"},
    {L"CommandLineGenerationRequestReason", L"command-line generation request"},
    {L"CommandLineOnceReason", L"command-line --once"},
    {L"CommandLineWallpaperReason", L"command-line wallpaper"},
    {L"CommandLineForceGenerateReason", L"command-line --generate"},
    {L"ManifestRegenerateReason", L"manifest regeneration"},
    {L"CommandLineWallpaperPathInvalid", L"Wallpaper path does not exist or is not a file: %s"},
    {L"CommandLineManifestSettingIgnored", L"Manifest INI setting is no longer supported: %s. Edit AppxManifest.xml directly or use --regenerate-manifest to restore the built-in default manifest."},
    {L"StartupGenerationSkippedUnchanged", L"[i] Startup generation skipped: wallpaper/config unchanged."},
    {L"StartupGenerationSkippedNoWallpaper", L"[!] Startup generation skipped: no wallpaper detected."},
    {L"ManualGenerationRequested", L"[i] Manual generation requested from the tray menu."},
    {L"GenerationRequestBusy", L"[!] Generation request ignored: another operation is already running."},
    {L"EmptyWallpaper", L"[!] Empty wallpaper."},
    {L"IniEncodingNormalized", L"[i] INI file normalized to UTF-8 with BOM."},
    {L"FutureLogEntriesNewPath", L"[i] Future log entries will be written to the new path."},
    {L"FutureLogEntriesRenamedPath", L"[i] Future log entries will be written to the renamed path."},
    {L"FutureLogEntriesDefaultPath", L"[i] Future log entries will be written to the default path."},
    {L"ListenWallpaperState", L"[i] ListenWallpaper = %d"},
    {L"ListenFitState", L"[i] ListenFit = %d"},
    {L"DisableFittingState", L"[i] DisableFitting = %d"},
    {L"AssetToggleState", L"[i] Asset %s %s."},
    {L"ComRegistrationStatusFailed", L"[!] COM registration failed (status=%d)"},
    {L"ComRegistrationAsyncError", L"[!] COM registration async error: 0x%08X"},
    {L"ComRegistrationDeploymentError", L"[!] COM deployment error: 0x%08X"},
    {L"ComRegistrationDeploymentMessage", L"COM deployment message: %s"},
    {L"GdiPlusStartupFailedTitle", L"GDI+"},
    {L"GdiPlusStartupFailedMessage", L"GDI+ startup failed"},
    {L"Win32FailureFmt", L"[!] %s failed (error %lu: %s)"},
    {L"LogPathChanged", L"[i] Log path changed to: %s"},
    {L"LogPathRenamed", L"[i] Log path renamed to: %s"},
    {L"LogPathReset", L"[i] Log path reset to default: %s"},
    {L"LogWriteFailed", L"[!] Log file write failed (error %lu: %s): %s"},
    {L"IniWriteFailed", L"[!] Failed to write INI value [%s] %s (error %lu: %s)."},
    {L"ListenFitAutoDisabled", L"[i] ListenFit auto-disabled due to DisableFitting."},
    {L"SingleInstanceScopeSummary", L"[i] Single-instance scope: %s"},
    {L"GdiPlusOperationFailed", L"[!] GDI+ %s failed (status=%d)."},

    {L"FitFill", L"Fill"},
    {L"FitFit", L"Fit"},
    {L"FitStretch", L"Stretch"},
    {L"FitCenter", L"Center"},
    {L"FitTile", L"Tile"},
    {L"FitSpan", L"Span"},

    {L"NotifyOnStartState", L"[i] NotifyOnStart = %d"},
    {L"NotifyOnSuccessState", L"[i] NotifyOnSuccess = %d"},
    {L"NotifyOnFailureState", L"[i] NotifyOnFailure = %d"},
    {L"NotifyOnBusyState", L"[i] NotifyOnBusy = %d"},
    {L"NotifyOnAlreadyRunningState", L"[i] NotifyOnAlreadyRunning = %d"},
    {L"NotifyOnSingleInstanceFailureState", L"[i] NotifyOnSingleInstanceFailure = %d"},
    {L"UnspecifiedReason", L"an unspecified reason"},
    {L"ChangeDetected", L"change detected"},
    {L"AdvancedTitle", L"Advanced:"},
    {L"CachingTitle", L"Caching:"},
    {L"PollIntervalLabel", L"Poll interval: %d ms"},
    {L"ConfirmMsLabel", L"Confirm: %d ms"},
    {L"DebounceMinMsLabel", L"Debounce min: %d ms"},
    {L"PollInitialDebounceBypassMsLabel", L"Initial debounce bypass: %d ms"},
    {L"IniCacheRefreshMsLabel", L"INI cache refresh: %d ms"},
    {L"WallpaperDetectionLogRepeatMsLabel", L"Wallpaper detection log repeat: %d ms"},
    {L"NotificationTimeoutMsLabel", L"Notification timeout: %d ms"},
    {L"ShutdownInitialNoticeMsLabel", L"Shutdown first notice: %d ms"},
    {L"ShutdownRepeatNoticeMsLabel", L"Shutdown repeat notice: %d ms"},
    {L"SingleInstanceSignalRetriesLabel", L"Single-instance signal retries: %d"},
    {L"SingleInstanceSignalDelayMsLabel", L"Single-instance signal delay: %d ms"},
    {L"ComRegistrationTimeoutMsLabel", L"COM registration timeout: %s"},
    {L"ComRegistrationCancelWaitMsLabel", L"COM cancel wait: %d ms"},
    {L"ComRegistrationHelperLabel", L"COM isolation helper: %s"},
    {L"TrayIconHiddenDelayMsLabel", L"Tray hide warning delay: %d ms"},
    {L"LogBufferLinesLabel", L"Log buffer lines: %d"},
    {L"LogAppendLockWaitMsLabel", L"Log append lock wait: %d ms"},
    {L"TrimWorkingSetOnIdle", L"Trim working set when idle"},
    {L"TrimWorkingSetOperation", L"SetProcessWorkingSetSize(trim idle working set)"},
    {L"CompactCrtHeapOnIdle", L"Compact CRT heap when idle"},
    {L"KeepGdiPlusLoaded", L"Keep GDI+ loaded after image work"},
    {L"PowerShellExeLabel", L"PowerShell executable: %s"},
    {L"PowerShellPollMsLabel", L"PowerShell poll: %d ms"},
    {L"PowerShellTimeoutMsLabel", L"PowerShell timeout: %s"},
    {L"PowerShellTerminateWaitMsLabel", L"PowerShell terminate wait: %d ms"},
    {L"PowerShellOutputMessageBoxCharsLabel", L"PowerShell output dialog limit: %d chars"},
    {L"RegistrationUnresolvedBlockMsLabel", L"Unresolved registration block: %d ms"},
    {L"SavePngReplaceRetriesLabel", L"PNG replace retries: %d"},
    {L"SavePngReplaceRetryDelayMsLabel", L"PNG replace retry delay: %d ms"},
    {L"TimeoutInfiniteText", L"infinite"},
    {L"ManifestTitle", L"Manifest:"},
    {L"ManifestDetailsTitle", L"Manifest details"},
    {L"ManifestIdentityNameLabel", L"Manifest identity: %s"},
    {L"ManifestPublisherLabel", L"Manifest publisher: %s"},
    {L"ManifestPackageVersionLabel", L"Manifest version: %s"},
    {L"ManifestDisplayNameLabel", L"Manifest display name: %s"},
    {L"ManifestPublisherDisplayNameLabel", L"Manifest publisher display: %s"},
    {L"ManifestDescriptionLabel", L"Manifest description: %s"},
    {L"ManifestExecutableLabel", L"Manifest executable: %s"},
    {L"ManifestApplicationIdLabel", L"Manifest app id: %s"},
    {L"ManifestTargetLabel", L"Manifest target: %s"},
    {L"ManifestTargetDeviceFamilyNameLabel", L"Manifest target family: %s"},
    {L"ManifestMinVersionLabel", L"Manifest min version: %s"},
    {L"ManifestMaxVersionTestedLabel", L"Manifest max tested: %s"},
    {L"ManifestResourceLanguageLabel", L"Manifest resource language: %s"},
    {L"ManifestPropertiesLogoLabel", L"Manifest properties logo: %s"},
    {L"ManifestSquare150x150LogoLabel", L"Manifest square 150 logo: %s"},
    {L"ManifestSquare44x44LogoLabel", L"Manifest square 44 logo: %s"},
    {L"ManifestSquare30x30LogoLabel", L"Manifest square 30 logo: %s"},
    {L"ManifestSquare71x71LogoLabel", L"Manifest square 71 logo: %s"},
    {L"ManifestWide310x150LogoLabel", L"Manifest wide 310 logo: %s"},
    {L"ManifestSquare310x310LogoLabel", L"Manifest square 310 logo: %s"},
    {L"ManifestBackgroundColorLabel", L"Manifest background color: %s"},
    {L"ManifestShowNameOnTilesLabel", L"Manifest show names on: %s"},
    {L"ManifestEntryPointLabel", L"Manifest entry point: %s"},
    {L"ManifestRestrictedCapabilityLabel", L"Manifest restricted capability: %s"},
    {L"ManifestSplashScreenImageLabel", L"Manifest splash screen image: %s"},
    {L"ManifestSourceLabel", L"Manifest source: %s"},
    {L"ManifestSourceExisting", L"existing AppxManifest.xml"},
    {L"ManifestSourceGenerated", L"generated from built-in defaults if missing"},
    {L"ManifestRegenerateNow", L"Regenerate AppxManifest.xml from built-in defaults"},
    {L"WallpaperDetectionMethodLabel", L"Wallpaper detection method: %s"},
    {L"WallpaperDetectionFallback", L"Fallback when selected path is invalid"},
    {L"WallpaperDecodeLowMemory", L"Low-memory wallpaper decode"},
    {L"WallpaperDecodeMaxLongEdgeLabel", L"Wallpaper decode max edge: %s"},
    {L"WallpaperDecodeAuto", L"auto"},
    {L"WallpaperDecodePxText", L"%d px"},
    {L"LiveWallpaperCaptureTitle", L"Live Wallpaper Detection:"},
    {L"LiveWallpaperCapture", L"Capture live wallpaper snapshot"},
    {L"LiveWallpaperCaptureScreenFallback", L"Unsafe screen fallback (may capture desktop)"},
    {L"LiveWallpaperCaptureDelayMsLabel", L"Capture delay: %d ms"},
    {L"LiveWallpaperCaptureRefreshMsLabel", L"Capture refresh: %s"},
    {L"LiveWallpaperCaptureStartupRefreshMsLabel", L"Startup recapture: %s"},
    {L"LiveWallpaperCaptureStartupRefreshDurationMsLabel", L"Startup recapture duration: %s"},
    {L"LiveWallpaperCaptureRefreshOnce", L"once"},
    {L"LiveWallpaperCaptureOff", L"off"},
    {L"LiveWallpaperCaptureMsText", L"%d ms"},
    {L"LiveWallpaperProviderNone", L"None"},
    {L"LiveWallpaperProviderLive", L"Live wallpaper"},
    {L"SingleInstanceFailureActionLabel", L"Single-instance failure action: %s"},
    {L"SingleInstanceActionIgnore", L"Ignore"},
    {L"SingleInstanceActionWarn", L"Warn"},
    {L"SingleInstanceActionExit", L"Exit"},
    {L"SingleInstanceActionCrash", L"Crash"},
    {L"UnknownError", L"Unknown error"},
    {L"LogFileFilter", L"Log files (*.log)|*.log|All files (*.*)|*.*||"},

    {L"GeneralTitle", L"General:"},
    {L"WallpaperTitle", L"Wallpaper:"},
    {L"AppearanceAssetsTitle", L"Appearance && assets:"},
    {L"DiagnosticsTitle", L"Diagnostics:"},
    {L"LoggingTitle", L"Logging:"},
    {L"WallpaperFittingTitle", L"Wallpaper fitting:"},
    {L"WallpaperDetectionTitle", L"Wallpaper detection"},
    {L"WallpaperDecodeTitle", L"Decode memory"},
    {L"MethodsTitle", L"Methods:"},
    {L"AssetsTitle", L"Assets:"},
    {L"DpiScalesTitle", L"DPI scales:"},
    {L"GeneratedAssetCache", L"Generated asset cache"},
    {L"GeneratedAssetPrecache", L"Pre-cache slideshow wallpapers"},
    {L"GeneratedAssetCacheMaxEntriesLabel", L"Cache max entries: %d"},
    {L"GeneratedAssetPrecacheMaxFilesLabel", L"Pre-cache max files: %d"},
    {L"GeneratedAssetPrecacheInactive", L"[i] Generated asset pre-cache is configured on but inactive because generated asset cache is disabled or max entries is 0."},
    {L"ListenWallpaper", L"Listen Wallpaper"},
    {L"TrayIcon", L"Tray Icon (this session)"},
    {L"UsePowerShell", L"PowerShell only"},
    {L"ExperimentalLiveTileUpdate", L"Live Tile update mode"},
    {L"AppxManifestTarget", L"AppxManifest target"},
    {L"AppxManifestTargetWindows10", L"Windows 10"},
    {L"AppxManifestTargetWindows81", L"Windows 8.1"},
    {L"AppxManifestTargetWindows8", L"Windows 8"},
    {L"Win8LiveTileOopHelper", L"Experimental Win8/8.1 OOP Live Tile helper (disabled; failed with class registration on loose packages)"},
    {L"Win8LiveTileBackgroundTask", L"Experimental Win8/8.1 background task Live Tile helper (disabled; future research)"},
    {L"Win8LiveTileBrokerApp", L"Use packaged WinRT broker app for Win8/8.1 Live Tile updates (recommended)"},
    {L"LiveTileModeAuto", L"Auto"},
    {L"LiveTileModeRegistration", L"Registration"},
    {L"LiveTileModeLiveTile", L"Live Tile"},
    {L"LiveTileModeEnabled", L"Enabled"},
    {L"LiveTileModeDisabled", L"Disabled"},
    {L"LiveTileModeTitle", L"Mode"},
    {L"LiveTileRegistrationTitle", L"Registration"},
    {L"LiveTileRuntimeTitle", L"Runtime behavior"},
    {L"LiveTileWindows8Title", L"Windows 8 / 8.1"},
    {L"LiveTileRelaunchOnSwitch", L"Relaunch when runtime mismatches"},
    {L"ClearLiveTileOnShutdown", L"Clear Live Tile on shutdown"},
    {L"LiveTileRelaunchToPackagedQueued", L"[i] Live Tile runtime mismatch detected; relaunching through the registered package entry."},
    {L"LiveTileRelaunchToUnpackagedQueued", L"[i] Live Tile runtime mismatch detected; relaunching as unpackaged desktop app."},
    {L"LiveTileClearOnShutdown", L"[i] Clearing Live Tile on shutdown."},
    {L"LiveTileClearOnShutdownSkipped", L"[i] Clear Live Tile on shutdown skipped: %s"},
    {L"LiveTileClearOnShutdownSuccess", L"[i] Live Tile cleared on shutdown."},
    {L"TileAppearanceTitle", L"Tile appearance:"},
    {L"TileTextTitle", L"Text overlay"},
    {L"TileTextFont", L"Font: %s"},
    {L"TileTextEnabled", L"Enable text overlay"},
    {L"TileTextEdit", L"Edit text..."},
    {L"TileTextClear", L"Clear text"},
    {L"TileTextCurrent", L"Text: %s"},
    {L"TileTextSecondaryEdit", L"Edit secondary text..."},
    {L"TileTextSecondaryClear", L"Clear secondary text"},
    {L"TileTextSecondaryCurrent", L"Secondary: %s"},
    {L"TileTextBadgeEdit", L"Edit badge text..."},
    {L"TileTextBadgeClear", L"Clear badge text"},
    {L"TileTextBadgeCurrent", L"Badge: %s"},
    {L"TileTextHorizontalAlign", L"Text horizontal: %s"},
    {L"TileTextVerticalAlign", L"Text vertical: %s"},
    {L"TileTextBadgeHorizontalAlign", L"Badge horizontal: %s"},
    {L"TileTextBadgeVerticalAlign", L"Badge vertical: %s"},
    {L"TileTextFontSize", L"Primary font size: %d px"},
    {L"TileTextSecondaryFontSize", L"Secondary font size: %d px"},
    {L"TileTextBadgeFontSize", L"Badge font size: %d px"},
    {L"TileTextBold", L"Primary bold"},
    {L"TileTextSecondaryBold", L"Secondary bold"},
    {L"TileTextBadgeBold", L"Badge bold"},
    {L"TileTextItalic", L"Italic"},
    {L"TileTextColor", L"Primary color: %s"},
    {L"TileTextSecondaryColor", L"Secondary color: %s"},
    {L"TileTextBadgeColor", L"Badge color: %s"},
    {L"TileTextShadow", L"Shadow"},
    {L"TileTextShadowColor", L"Shadow color: %s"},
    {L"TileTextMarginX", L"Margin X: %d px"},
    {L"TileTextMarginY", L"Margin Y: %d px"},
    {L"TileTextLineGap", L"Line gap: %d px"},
    {L"TileTextMaxSecondaryLines", L"Max secondary lines: %d"},
    {L"TileTextApplyMedium", L"Apply to Medium tile"},
    {L"TileTextApplyWide", L"Apply to Wide tile"},
    {L"TileTextApplyLarge", L"Apply to Large tile"},
    {L"TileTextApplySmall", L"Apply to Small tile"},
    {L"TileTextApplyLogos", L"Apply to logos"},
    {L"TileTextEdited", L"[i] Tile text changed: %s"},
    {L"TileTextCleared", L"[i] Tile text cleared."},
    {L"TileTextSettingChanged", L"[i] Tile text setting changed: %s = %s"},
    {L"TileTextIntInputInvalid", L"Value must be an integer from %d to %d."},
    {L"TileTextSummary", L"[i] Tile text overlay: %s (%s)"},
    {L"TileTextInputTitle", L"Edit tile text"},
    {L"TileTextSecondaryInputTitle", L"Edit secondary tile text"},
    {L"TileTextBadgeInputTitle", L"Edit tile badge text"},
    {L"TileTextFontInputTitle", L"Edit tile text font"},
    {L"TileTextColorInputTitle", L"Edit primary tile text color"},
    {L"TileTextSecondaryColorInputTitle", L"Edit secondary tile text color"},
    {L"TileTextBadgeColorInputTitle", L"Edit tile badge color"},
    {L"TileTextShadowColorInputTitle", L"Edit tile text shadow color"},
    {L"TileTextMarginXInputTitle", L"Edit tile text horizontal margin"},
    {L"TileTextMarginYInputTitle", L"Edit tile text vertical margin"},
    {L"TileTextLineGapInputTitle", L"Edit tile text line gap"},
    {L"TileTextMaxSecondaryLinesInputTitle", L"Edit max secondary tile text lines"},
    {L"GenerateOnStartup", L"Generate on Startup"},
    {L"GenerateOnStartupOnlyWhenChanged", L"Generate on Startup only when changed"},
    {L"HideDisabledEntries", L"Hide disabled entries"},
    {L"GenerateDesktopIconForDisabledEntries", L"Generate Desktop Icon for disabled entries"},
    {L"DeleteDisabledAssets", L"Auto-delete disabled asset files"},
    {L"GenerateScaleAuto", L"Auto: current DPI scale"},
    {L"GenerateScaleAutoPreservesManualToggles", L"Manual scale checkboxes are remembered but ignored while Auto is enabled."},
    {L"GenerateScale100", L"100%"},
    {L"GenerateScale125", L"125%"},
    {L"GenerateScale150", L"150%"},
    {L"GenerateScale200", L"200%"},
    {L"GenerateScale400", L"400%"},
    {L"CurrentDpiScaleLabel", L"Current DPI: %d%% (asset scale: %d%%)"},
    {L"ShowMenuAsDropdown", L"Show menu as dropdown"},
    {L"NotificationsTitle", L"Notifications:"},
    {L"NotificationsEnabled", L"Enable all tray notifications"},
    {L"NotifyOnStart", L"Notify when generation starts"},
    {L"NotifyOnSuccess", L"Notify when generation succeeds"},
    {L"NotifyOnFailure", L"Notify when generation, wallpaper, or registration fails"},
    {L"NotifyOnBusy", L"Notify when generation is already running"},
    {L"NotifyOnAlreadyRunning", L"Notify when the app is already running"},
    {L"NotifyOnSingleInstanceFailure", L"Notify when single-instance protection fails"},
    {L"ShowConsole", L"Show Console"},
    {L"GenerateNow", L"Generate now"},
    {L"LoggingEnable", L"Enable"},
    {L"ChangePath", L"Change path..."},
    {L"RenamePath", L"Rename path..."},
    {L"OpenInExplorer", L"Open in Explorer"},
    {L"ResetDefault", L"Reset to default"},
    {L"ShowPowerShellLog", L"Show PowerShell Log"},
    {L"LogPathPrefix", L"Path: "},
    {L"LogPathTitle", L"Log Path"},
    {L"PSStatusPrefix", L"PS Status: "},
    {L"PSStatusTitle", L"PS Status"},
    {L"DisableFitting", L"Disable Fitting"},
    {L"ListenFit", L"Listen Fit Mode"},
    {L"FitModePrefix", L"Fit Mode: "},
    {L"FitModeForced", L"Fit Mode: Fill (forced)"},
    {L"Exit", L"Exit"},
    {L"ShutdownPendingTitle", L"Shutdown pending:"},
    {L"ShutdownPendingReason", L"Waiting for generation/registration: %s"},
    {L"CancelShutdown", L"Cancel shutdown"},
    {L"ForceShutdown", L"Force shutdown"},
    {L"ShutdownDelayedNotification", L"Exit is waiting for generation/registration to finish: %s"},
    {L"ShutdownRequested", L"[i] Shutdown requested."},
    {L"ShutdownCancelled", L"[i] Shutdown cancelled."},
    {L"ForceShutdownRequested", L"[!] Force shutdown requested."},
    {L"PreviousForceShutdownCleanupTitle", L"Previous force shutdown"},
    {L"PreviousForceShutdownCleanupWarning", L"Previous run used Force shutdown while work was pending (%s). Skipped cleanup: %s. Downside: %s"},
    {L"RenameDialogTitle", L"Rename Log Path"},
    {L"OKText", L"OK"},
    {L"CancelText", L"Cancel"},
    {L"ErrorTitle", L"Error"},
    {L"PathCannotBeEmpty", L"Path cannot be empty."},
    {L"NoPowerShellOutputTitle", L"PowerShell Output"},
    {L"NoPowerShellOutput", L"No PowerShell output."},
    {L"PowerShellOutputTruncated", L"[i] Output truncated for display; see the log for the full output (%d characters hidden)."},
    {L"GenerateNowTitle", L"Generate Now"},
    {L"WallpaperNotFound", L"Could not detect current wallpaper."},
    {L"CurrentWallpaperNotFound", L"Could not detect current wallpaper."},
    {L"ProgramStarting", L"Program starting..."},
    {L"NotificationTitle", L"DesktopStub"},
    {L"NotificationGenerationStarted", L"Wallpaper generation started."},
    {L"NotificationGenerationSuccess", L"Wallpaper generation completed successfully."},
    {L"NotificationGenerationFailed", L"Wallpaper generation failed."},
    {L"NotificationGenerationBusy", L"Wallpaper generation is already running."},
    {L"NotificationAlreadyRunning", L"DesktopStub is already running."},
    {L"NotificationAlreadyRunningTrayPersistent", L"DesktopStub is already running. Tray icon is enabled in the config, so it will stay visible after restart."},
    {L"NotificationAlreadyRunningTraySession", L"DesktopStub is already running. Tray icon is visible for this session only; it will be hidden again after restart because TrayIcon=0 in the config."},
    {L"NotificationSingleInstanceFailure", L"Single-instance protection failed; another copy may run."},
    {L"NotificationTrayIconHidden", L"Tray icon hidden for this session. Run this executable again to restore it."},
    {L"ManualGenerateTriggered", L"Manual generate triggered."},
    {L"LiveTileModeChanged", L"Live Tile mode changed."},
    {L"ManifestTargetChanged", L"AppxManifest target changed."},
    {L"LiveTileModeReregistrationQueued", L"[i] Queued one-time Appx re-registration because Live Tile mode changed."},
    {L"LiveTileModeReregistrationQueuedNoWallpaper", L"[!] Live Tile mode changed; one-time Appx re-registration is pending but current wallpaper could not be detected."},
    {L"StartingWallpaperGeneration", L"Starting wallpaper generation: %s"},
    {L"WallpaperSource", L"Wallpaper source: %s"},
    {L"WallpaperImageLoaded", L"[i] Wallpaper image loaded (%d x %d)."},
    {L"WallpaperImageLoadedWic", L"[i] Wallpaper image loaded with low-memory WIC decode: %lu x %lu (max long edge=%d)."},
    {L"WallpaperImageLoadedGdiWicFallback", L"[i] Wallpaper image loaded with GDI+ after low-memory WIC decode fallback: %d x %d (requested max long edge=%d)."},
    {L"WallpaperImageLoadedWicDownscaled", L"[i] Wallpaper image loaded with low-memory WIC decode: %lu x %lu -> %lu x %lu (max long edge=%d)."},
    {L"WallpaperImageLoadedWicMemoryConstrained", L"[i] Wallpaper image loaded with low-memory WIC decode: %lu x %lu -> %lu x %lu (requested %lu x %lu; memory-limited from max long edge=%d)."},
    {L"RenderModeSummary", L"[i] Render fit mode: %s"},
    {L"AssetsDirectoryReady", L"[i] Assets directory ready: %s"},
    {L"FailedLoadWallpaper", L"Failed to load wallpaper image."},
    {L"SkippingAssetDisabled", L"Skipping %s (disabled in settings)."},
    {L"SkippingStaticAssetLiveTileMode", L"Skipping %s (static manifest assets are disabled in Live Tile mode)."},
    {L"GeneratedDesktopIconAsset", L"Generated desktop icon for disabled %s"},
    {L"ActiveScales", L"[i] Active scales: %s"},
    {L"SavedAsset", L"Saved %s"},
    {L"SavedScaledAsset", L"Saved %s (scale %d%%)"},
    {L"GeneratedAssetCacheHit", L"[i] Reused cached generated assets: %s"},
    {L"GeneratedAssetCacheSaved", L"[i] Saved generated asset cache: %s"},
    {L"GeneratedAssetCacheSummary", L"[i] Generated asset cache: %s (max entries=%d)"},
    {L"GeneratedAssetPrecacheSaved", L"[i] Precached generated assets: %s"},
    {L"GeneratedAssetPrecacheSummary", L"[i] Generated asset pre-cache: %s (max files=%d)"},
    {L"FailedSaveAsset", L"Failed to save %s"},
    {L"FailedSaveScaledAsset", L"Failed to save %s (scale %d%%)"},
    {L"SavePngInvalidInput", L"[!] SavePNG skipped for %s: invalid bitmap, path, or PNG encoder."},
    {L"SavePngGdiFailure", L"[!] SavePNG GDI+ save failed for %s (status=%d)."},
    {L"SavePngReplaceFailed", L"[!] SavePNG replace failed for %s (temp=%s)."},
    {L"SavePngReplaceRetrying", L"[!] SavePNG replace attempt %d/%d failed for %s (error %lu: %s); retrying in %d ms."},
    {L"DeletedDisabledAsset", L"[i] Deleted disabled asset file: %s"},
    {L"FailedDeleteDisabledAsset", L"[!] Failed to delete disabled asset file: %s"},
    {L"ReRegisteringManifest", L"Re-registering AppxManifest due to regenerated assets."},
    {L"UpdatingLiveTile", L"Updating Live Tile from regenerated assets."},
    {L"ReRegisteringManifestForLiveTileModeChange", L"Re-registering AppxManifest once because Live Tile mode changed."},
    {L"LiveTileModeReregistrationComplete", L"[i] Live Tile mode change re-registration completed."},
    {L"AppxManifestCreated", L"[i] Created AppxManifest.xml: %s"},
    {L"AppxManifestExisting", L"[i] Using existing AppxManifest.xml: %s"},
    {L"AppxManifestRegenerated", L"[i] Regenerated AppxManifest.xml from built-in defaults: %s"},
    {L"AppxManifestCreateFailed", L"[!] Failed to create AppxManifest.xml: %s"},
    {L"AppxManifestRegenerateFailed", L"[!] Failed to regenerate AppxManifest.xml: %s"},
    {L"ManifestPath", L"Manifest path: %s"},
    {L"UsingComRegistration", L"Using COM Appx registration..."},
    {L"ComRegistrationUri", L"[i] COM registration URI: %s"},
    {L"ComRegistrationTimeoutDetails", L"[i] COM registration timeout=%s, cancel wait=%d ms"},
    {L"ComRegistrationHelperSummary", L"[i] COM registration isolation helper: %s"},
    {L"ComRegistrationHelperStarted", L"[i] COM registration helper started (pid=%lu)."},
    {L"ComRegistrationHelperExitCode", L"[i] COM registration helper exited with code 0x%08X."},
    {L"ComRegistrationHelperCrashed", L"[!] COM registration helper crashed or was terminated (exit code 0x%08X)."},
    {L"ComRegistrationHelperTimedOut", L"[!] COM registration helper timed out."},
    {L"ComRegistrationHelperTerminateFailed", L"[!] Failed to terminate timed-out COM registration helper (pid=%lu)."},
    {L"ComRegistrationHelperStillRunning", L"[!] COM registration helper is still running after terminate wait (pid=%lu)."},
    {L"ComRegistrationHelperTerminated", L"[i] COM registration helper terminated after timeout (pid=%lu)."},
    {L"InvalidManifestPath", L"Invalid manifest path."},
    {L"ComRegistrationSuccess", L"COM registration success."},
    {L"ComRegistrationTimedOut", L"COM registration timed out."},
    {L"ComRegistrationFailed", L"COM registration failed; falling back to PowerShell registration."},
    {L"ComRegistrationCancelStillRunning", L"COM registration is still running after cancel; PowerShell fallback skipped to avoid concurrent registration."},
    {L"ComRegistrationFallbackSkippedAfterTimeout", L"COM registration timeout is unresolved; PowerShell fallback skipped to avoid concurrent registration."},
    {L"ComRegistrationException", L"COM registration threw exception: 0x%08X"},
    {L"ComExceptionMessage", L"COM message: %s"},
    {L"LaunchingPowerShellRegistration", L"Launching PowerShell registration..."},
    {L"PowerShellCommand", L"Command: %s"},
    {L"PowerShellLaunchDetails", L"[i] PowerShell executable: %s (timeout=%s, poll=%d ms, terminate wait=%d ms)"},
    {L"PowerShellProcessStarted", L"[i] PowerShell process started (pid=%lu)."},
    {L"PowerShellTerminateRequested", L"[!] Terminating timed-out PowerShell process (pid=%lu)."},
    {L"PowerShellTerminateFailed", L"[!] Failed to terminate timed-out PowerShell process (pid=%lu)."},
    {L"PowerShellStillRunningAfterTimeout", L"[!] PowerShell process is still running after terminate wait (pid=%lu)."},
    {L"PowerShellTerminatedAfterTimeout", L"[i] PowerShell process terminated after timeout (pid=%lu)."},
    {L"PowerShellCompleted", L"PowerShell registration completed successfully."},
    {L"PowerShellTimedOut", L"PowerShell registration timed out."},
    {L"PowerShellErrorSideloadDisabled", L"PowerShell error: Enable sideloading first!"},
    {L"PowerShellErrorCode", L"PowerShell registration failed with exit code 0x%08X."},
    {L"PowerShellOutputFollows", L"PowerShell output follows:"},
    {L"PowerShellRegistrationFailed", L"PowerShell registration did not complete successfully."},
    {L"AssetGenerationFinished", L"Asset generation and registration finished successfully."},
    {L"AssetGenerationLiveTileFinished", L"Asset generation and Live Tile update finished successfully."},
    {L"AssetGenerationFailed", L"Asset generation did not complete successfully."},
    {L"AppRegistrationFailed", L"App registration did not complete successfully."},
    {L"LiveTileUpdateSuccess", L"Live Tile update completed successfully."},
    {L"LiveTileUpdateFailed", L"Live Tile update did not complete successfully."},
    {L"LiveTileUpdateRequiresIdentity", L"Live Tile update requires package identity; launch through the registered package entry."},
    {L"LiveTileUpdateNoUsableAssets", L"Live Tile update failed: no valid tile assets were available."},
    {L"LiveTileUpdateException", L"Live Tile update threw exception: 0x%08X"},
    {L"LiveTileUpdateMessage", L"Live Tile message: %s"},
    {L"LiveTilePackageIdentity", L"[i] Live Tile package identity: %s"},
    {L"LiveTilePackagedHelperReportedSuccess", L"[i] Packaged Live Tile helper reported success."},
    {L"LiveTilePackagedBrokerReportedSuccess", L"[i] Packaged WinRT Live Tile broker reported success."},
    {L"LiveTilePackagedHelperNoSuccess", L"Packaged Live Tile helper did not report success after activation: %s (package=%s, pid=%lu)."},
    {L"GenerationUnhandledException", L"[!] Generation worker caught an unhandled exception: %s"},
    {L"GenerationThreadStartFailed", L"[!] Failed to start generation worker thread: %s"},
    {L"PollThreadUnhandledException", L"[!] Wallpaper poll thread stopped after an unhandled exception: %s"},
    {L"PollThreadStartFailed", L"[!] Failed to start wallpaper poll thread: %s"},
    {L"UnknownException", L"unknown exception"},
    {L"SkippingRegistrationDueToAssetFailure", L"[!] Skipping Appx registration because asset generation failed."},
    {L"SkippingLiveTileUpdateDueToAssetFailure", L"[!] Skipping Live Tile update because asset generation failed."},
    {L"CreateAssetsDirectoryOperation", L"Create Assets directory"},
    {L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing or invalid: %s"},
    {L"NotificationMissingManifestAssets", L"Registration failed; one or more manifest assets are missing or invalid."},
    {L"RegistrationBlockedUnresolved", L"[!] Registration skipped because a previous registration is unresolved: %s"},
    {L"RegistrationUnresolvedMarked", L"[!] Registration marked unresolved: %s"},
    {L"RegistrationUnresolvedCleared", L"[i] Previous unresolved registration finished: %s"},
    {L"RegistrationUnresolvedExpired", L"[!] Previous unresolved registration block expired; allowing registration again: %s"},
    {L"ChangeConfirmed", L"Change confirmed; regeneration allowed after debounce."},
    {L"WallpaperAndFitChangeDetected", L"Wallpaper and fit mode change detected."},
    {L"WallpaperChangeDetected", L"Wallpaper change detected."},
    {L"WallpaperDetectionEmptyDuringPoll", L"[!] Wallpaper change ignored: wallpaper detection returned empty."},
    {L"WallpaperDetectionEmptyUsingLast", L"[!] Wallpaper detection returned empty during a fit/DPI-only change; using the last valid wallpaper path: %s"},
    {L"WallpaperDetectionMethodFailed", L"[!] Wallpaper detection method %s did not return a wallpaper path."},
    {L"WallpaperDetectionMethodReturnedInvalid", L"[!] Wallpaper detection method %s returned an unusable path: %s"},
    {L"WallpaperDetectionFallbackSelected", L"[i] Wallpaper detection fallback selected %s: %s"},
    {L"WallpaperDetectionFallbackDisabled", L"[!] Wallpaper detection fallback disabled; using %s result as-is."},
    {L"FitModeChangeDetected", L"Fit mode change detected."},
    {L"DpiScaleChangeDetected", L"DPI scale change detected."},
    {L"SingleInstanceMutexFailure", L"[!] Single-instance mutex creation failed (error %lu: %s); action=%s."},
};

static void EnsureIniStringDefaults()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
    {
        QueueStartupWarning(L"[!] Failed to read INI while applying string defaults (error " + std::to_wstring(GetLastError()) + L").");
        return;
    }

    bool haveText = true;
    auto doc = ParseIniDocument(iniText);
    bool changed = false;

    for (const auto& d : g_stringDefaults)
    {
        std::wstring value;
        if (!haveText || !ReadIniValueFromDoc(doc, L"Strings", d.key, value))
        {
            if (!WriteIniValueToText(iniText, L"Strings", d.key, d.value))
            {
                QueueStartupWarning(L"[!] Failed to update INI string default: " + std::wstring(d.key ? d.key : L""));
                return;
            }
            changed = true;
            doc = ParseIniDocument(iniText);
            haveText = true;
        }
    }

    if (changed && !WriteUtf8BomTextFile(g_iniPath, iniText))
    {
        QueueStartupWarning(L"[!] Failed to write INI string defaults (error " + std::to_wstring(GetLastError()) + L").");
    }
    else if (changed)
    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(iniText);
    }
}

static std::wstring BuildInitialIniTemplate()
{
    std::vector<std::wstring> lines;
    lines.push_back(L"; ============================================================");
    lines.push_back(L"; DesktopStub - Configuration");
    lines.push_back(L"; ============================================================");
    lines.push_back(L"; Keys and values are quoted so ; and # can be used literally");
    lines.push_back(L"; inside strings. Keep required printf tokens such as %s/%d.");
    lines.push_back(L"; ============================================================");
    lines.push_back(L"");
    lines.push_back(L"[Settings]");
    lines.push_back(L"; General runtime behaviour.");
    for (const auto& d : g_defaults)
    {
        if (wcscmp(d.section, L"Settings") == 0)
            lines.push_back(FormatIniAssignment(d.key, d.value));
    }
    lines.push_back(L"");
    lines.push_back(L"; WallpaperDetectionMethod options:");
    lines.push_back(L"; TranscodedImageCache, SystemParametersInfo, DesktopWallpaperCOM,");
    lines.push_back(L"; TranscodedWallpaperFile, Auto.");
    lines.push_back(L"; Live wallpaper capture support scans non-icon WorkerW desktop-host");
    lines.push_back(L"; trees and can capture a still frame internally instead of changing");
    lines.push_back(L"; the user's real wallpaper.");
    lines.push_back(L"");
    lines.push_back(L"[PowerShellErrorMessages]");
    lines.push_back(L"; Optional HRESULT/error-code matches for PowerShell registration failures.");
    lines.push_back(L"; Add lines like \"0x80070002\" = \"Readable reason\".");
    lines.push_back(L"; Delete an entry, or set its value empty, to stop using that special case.");
    for (const auto& d : g_powerShellErrorMessageDefaults)
        lines.push_back(FormatIniAssignment(d.key, d.value));
    lines.push_back(L"");
    lines.push_back(L"[Assets]");
    lines.push_back(L"; Toggle which AppX tile/logo assets are generated.");
    for (const auto& d : g_defaults)
    {
        if (wcscmp(d.section, L"Assets") == 0)
            lines.push_back(FormatIniAssignment(d.key, d.value));
    }
    lines.push_back(L"");
    lines.push_back(L"[TileText]");
    lines.push_back(L"; Optional text overlay for tiles. Live Tile mode uses XML text; static mode bakes the text into generated PNG assets.");
    for (const auto& d : g_defaults)
    {
        if (wcscmp(d.section, L"TileText") == 0)
            lines.push_back(FormatIniAssignment(d.key, d.value));
    }
    lines.push_back(L"");
    lines.push_back(L"[Strings]");
    lines.push_back(L"; User-visible strings. Keep format tokens unchanged.");
    for (const auto& d : g_stringDefaults)
        lines.push_back(FormatIniAssignment(d.key, d.value));

    return JoinLines(lines) + L"\r\n";
}

static bool EnsureInitialIniTemplate()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::vector<BYTE> bytes;
    DWORD attrs = GetFileAttributesW(g_iniPath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES)
    {
        if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            SetLastError(ERROR_ACCESS_DENIED);
            return false;
        }

        if (!ReadWholeFileBytes(g_iniPath, bytes))
            return false;
        if (!bytes.empty())
            return false;
    }
    else
    {
        DWORD err = GetLastError();
        if (!IsMissingFileError(err))
        {
            SetLastError(err);
            return false;
        }
    }

    std::wstring text = BuildInitialIniTemplate();
    if (!WriteUtf8BomTextFile(g_iniPath, text))
        return false;

    {
        std::lock_guard<std::mutex> lk(g_iniCacheMutex);
        SetIniCacheUnlocked(text);
    }
    return true;
}

static void UpgradeStringDefaultIfUnmodified(const wchar_t* key, const wchar_t* oldValue, const wchar_t* newValue)
{
    std::wstring current = IniReadS(L"Strings", key, L"");
    if (current == oldValue)
        IniWrite(L"Strings", key, newValue);
}

static void RepairBrokenPowerShellOutputTruncatedDefault()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
        return;

    std::vector<std::wstring> lines = SplitLines(iniText);
    if (!lines.empty())
        StripLeadingBom(lines[0]);

    bool inStrings = false;
    bool changed = false;
    const std::wstring repaired = FormatIniAssignment(
        L"PowerShellOutputTruncated",
        L"[i] Output truncated for display; see the log for the full output (%d characters hidden).");

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::wstring trimmed = TrimCopy(lines[i]);
        if (trimmed.empty() || trimmed.front() == L';' || trimmed.front() == L'#')
            continue;

        std::wstring sectionName;
        if (TryParseIniSectionHeader(lines[i], sectionName))
        {
            inStrings = IEquals(sectionName, L"Strings");
            continue;
        }

        if (!inStrings)
            continue;

        size_t eq = lines[i].find(L'=');
        if (eq == std::wstring::npos)
            continue;

        std::wstring key = ParseIniName(lines[i].substr(0, eq));
        if (!IEquals(key, L"PowerShellOutputTruncated"))
            continue;

        if (TrimCopy(lines[i].substr(eq + 1)) != L"\"")
            break;

        lines[i] = repaired;
        changed = true;

        size_t j = i + 1;
        if (j < lines.size() && TrimCopy(lines[j]).empty())
            lines.erase(lines.begin() + static_cast<std::vector<std::wstring>::difference_type>(j));
        if (j < lines.size() && lines[j].find(L"[i] Output truncated for display;") != std::wstring::npos)
            lines.erase(lines.begin() + static_cast<std::vector<std::wstring>::difference_type>(j));
        if (j < lines.size() && lines[j].find(L"characters hidden).\"") != std::wstring::npos)
            lines.erase(lines.begin() + static_cast<std::vector<std::wstring>::difference_type>(j));
        break;
    }

    if (!changed)
        return;

    TrimTrailingEmptyLines(lines);
    std::wstring text = JoinLines(lines);
    if (!text.empty())
        text += L"\r\n";
    if (!WriteUtf8BomTextFile(g_iniPath, text))
        return;

    std::lock_guard<std::mutex> lk(g_iniCacheMutex);
    SetIniCacheUnlocked(text);
}


static bool IsLegacyManifestSettingKey(const std::wstring& key)
{
    return ToLowerCopy(TrimCopy(key)).rfind(L"manifest", 0) == 0;
}

static void RemoveObsoleteManifestIniSettings()
{
    std::lock_guard<std::mutex> writeLock(g_iniWriteMutex);

    std::wstring iniText;
    if (!ReadIniFileForMutation(iniText))
        return;

    std::vector<std::wstring> lines = SplitLines(iniText);
    if (!lines.empty())
        StripLeadingBom(lines[0]);

    std::vector<std::wstring> kept;
    kept.reserve(lines.size());
    bool inManifest = false;
    bool inSettings = false;
    bool changed = false;

    for (const std::wstring& line : lines)
    {
        std::wstring sectionName;
        if (TryParseIniSectionHeader(line, sectionName))
        {
            inManifest = IEquals(sectionName, L"Manifest");
            inSettings = IEquals(sectionName, L"Settings");
            if (inManifest)
            {
                changed = true;
                continue;
            }
        }

        if (inManifest)
        {
            changed = true;
            continue;
        }

        if (inSettings)
        {
            std::wstring trimmed = TrimCopy(line);
            if (!trimmed.empty() && trimmed.front() != L';' && trimmed.front() != L'#')
            {
                size_t eq = line.find(L'=');
                if (eq != std::wstring::npos)
                {
                    std::wstring key = ParseIniName(line.substr(0, eq));
                    if (IsLegacyManifestSettingKey(key))
                    {
                        changed = true;
                        continue;
                    }
                }
            }
        }

        kept.push_back(line);
    }

    if (!changed)
        return;

    TrimTrailingEmptyLines(kept);
    std::wstring text = JoinLines(kept);
    if (!text.empty())
        text += L"\r\n";
    if (!WriteUtf8BomTextFile(g_iniPath, text))
        return;

    std::lock_guard<std::mutex> lk(g_iniCacheMutex);
    SetIniCacheUnlocked(text);
}

static void UpgradeTileTextDefaultAlignment()
{
    if (IniReadI(L"State", L"TileTextVerticalAlignDefaultMigrated", 0) != 0)
        return;

    std::wstring current = ToLowerCopy(TrimCopy(IniReadS(L"TileText", L"VerticalAlign", L"")));
    if (current.empty() || current == L"bottom")
        IniWrite(L"TileText", L"VerticalAlign", L"Top");

    IniWrite(L"State", L"TileTextVerticalAlignDefaultMigrated", L"1");
}

static void UpgradeRenamedStringDefaults()
{
    RepairBrokenPowerShellOutputTruncatedDefault();
    UpgradeStringDefaultIfUnmodified(L"UsePowerShell", L"Use PowerShell", L"PowerShell only");
    UpgradeStringDefaultIfUnmodified(L"TrayIcon", L"Tray Icon", L"Tray Icon (this session)");
    UpgradeStringDefaultIfUnmodified(L"ExperimentalLiveTileUpdate", L"Experimental Live Tile update", L"Live Tile update mode");
    UpgradeStringDefaultIfUnmodified(L"LiveTileUpdateSummary", L"[i] Experimental Live Tile update: %s", L"[i] Live Tile update mode: %s");
    UpgradeStringDefaultIfUnmodified(L"LiveTileRelaunchOnSwitch", L"Relaunch on switch", L"Relaunch when runtime mismatches");
    UpgradeStringDefaultIfUnmodified(L"LiveTileRelaunchToPackagedQueued", L"[i] Live Tile mode changed; relaunching through the registered package entry.", L"[i] Live Tile runtime mismatch detected; relaunching through the registered package entry.");
    UpgradeStringDefaultIfUnmodified(L"LiveTileRelaunchToUnpackagedQueued", L"[i] Live Tile mode changed; relaunching as unpackaged desktop app.", L"[i] Live Tile runtime mismatch detected; relaunching as unpackaged desktop app.");
    UpgradeStringDefaultIfUnmodified(L"MissingManifestAssets", L"[!] Registration failed and manifest assets are missing: %s", L"[!] Registration failed and manifest assets are missing or invalid: %s");
    UpgradeStringDefaultIfUnmodified(L"NotificationMissingManifestAssets", L"Registration failed because one or more manifest assets are missing.", L"Registration failed; one or more manifest assets are missing or invalid.");
    UpgradeStringDefaultIfUnmodified(L"NotificationMissingManifestAssets", L"Registration failed because one or more manifest assets are missing or invalid.", L"Registration failed; one or more manifest assets are missing or invalid.");
    UpgradeStringDefaultIfUnmodified(L"PowerShellOutputTruncated", L"\"", L"[i] Output truncated for display; see the log for the full output (%d characters hidden).");
    UpgradeStringDefaultIfUnmodified(L"PowerShellOutputTruncated", L"\r\n\r\n[i] Output truncated for display; see the log for the full output (%d characters hidden).", L"[i] Output truncated for display; see the log for the full output (%d characters hidden).");
    UpgradeStringDefaultIfUnmodified(L"ComRegistrationTimeoutMsLabel", L"COM registration timeout: %d ms", L"COM registration timeout: %s");
    UpgradeStringDefaultIfUnmodified(L"PowerShellTimeoutMsLabel", L"PowerShell timeout: %d ms", L"PowerShell timeout: %s");
    UpgradeStringDefaultIfUnmodified(L"ComRegistrationTimeoutDetails", L"[i] COM registration timeout=%d ms, cancel wait=%d ms", L"[i] COM registration timeout=%s, cancel wait=%d ms");
    UpgradeStringDefaultIfUnmodified(L"PowerShellLaunchDetails", L"[i] PowerShell executable: %s (timeout=%d ms, poll=%d ms, terminate wait=%d ms)", L"[i] PowerShell executable: %s (timeout=%s, poll=%d ms, terminate wait=%d ms)");
    UpgradeStringDefaultIfUnmodified(L"StartingWallpaperGeneration", L"Starting wallpaper generation due to %s.", L"Starting wallpaper generation: %s");
    UpgradeStringDefaultIfUnmodified(L"AppxManifestRegenerated", L"[i] Regenerated AppxManifest.xml from configured defaults: %s", L"[i] Regenerated AppxManifest.xml from built-in defaults: %s");
    UpgradeStringDefaultIfUnmodified(L"ManifestSourceGenerated", L"generated from INI if missing", L"generated from built-in defaults if missing");
    UpgradeStringDefaultIfUnmodified(L"ManifestRegenerateNow", L"Regenerate AppxManifest.xml from config", L"Regenerate AppxManifest.xml from built-in defaults");
    UpgradeStringDefaultIfUnmodified(L"WallpaperImageLoadedWic", L"[i] Wallpaper image loaded with WIC: %lu x %lu.", L"[i] Wallpaper image loaded with low-memory WIC decode: %lu x %lu (max long edge=%d).");
    UpgradeTileTextDefaultAlignment();
    RemoveObsoleteManifestIniSettings();
}


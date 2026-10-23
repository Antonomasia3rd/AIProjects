// -----------------------------------------------------------------------------
// Window proc
// -----------------------------------------------------------------------------
static bool ConsumeAppxActivationRequestFile();

static void ApplyTrayIconSettingFromIni()
{
    bool desiredTray = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    bool trayPresent = TrayIconPresent();
    // Desired state is independent from whether Explorer accepted the current
    // shell object. TaskbarCreated must be able to retry a transient add
    // failure later.
    g_tray = desiredTray;

    if (desiredTray)
    {
        if (!trayPresent)
        {
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
            {
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
                return;
            }
        }
        else
        {
            DWORD err = 0;
            if (!UpdateTrayIconTooltip(&err))
                LogWin32Failure(L"Shell_NotifyIconW(NIM_MODIFY tooltip)", err);
        }
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
    LiveTileTemplateStyle templateStyleBefore = ConfiguredLiveTileTemplateStyle();
    LiveTileBranding brandingBefore = ConfiguredLiveTileBranding();
    ManifestCompatibilityTarget manifestTargetBefore = ConfiguredManifestCompatibilityTarget();
    ReloadRuntimeSettingsFromIni();
    EnsureIniStringDefaults();
    UpgradeRenamedStringDefaults();
    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    std::wstring startupError;
    if (!ReconcileDesktopStubStartupShortcutFromConfig(startupError))
        Log(L"[!] Could not synchronize the shell:startup shortcut after settings reload: %s", startupError.c_str());
    QueueDesktopStubPackagedStartupSync();
    if (applyTrayIcon)
        ApplyTrayIconSettingFromIni();
    else if (TrayIconPresent())
    {
        DWORD tooltipError = ERROR_SUCCESS;
        if (!UpdateTrayIconTooltip(&tooltipError))
            LogWin32Failure(L"Shell_NotifyIconW(NIM_MODIFY tooltip)", tooltipError);
    }

    if (liveTileBefore != ConfiguredLiveTileUpdateMode())
        QueueLiveTileModeReregistration();
    bool templateStyleChanged = templateStyleBefore != ConfiguredLiveTileTemplateStyle();
    bool brandingChanged = brandingBefore != ConfiguredLiveTileBranding();
    if (templateStyleChanged)
    {
        Log(g_ui.liveTileTemplateStyleSummary.c_str(), LiveTileTemplateStyleSummaryText().c_str());
        Log(g_ui.liveTileBrandingSummary.c_str(), LiveTileBrandingSummaryText().c_str());
    }
    if (brandingChanged && !templateStyleChanged)
        Log(g_ui.liveTileBrandingSummary.c_str(), LiveTileBrandingSummaryText().c_str());
    if (templateStyleChanged || brandingChanged)
    {
        std::wstring wp = CurrentGenerationBackground();
        if (!wp.empty())
            DESKTOPSTUB_CONTENT_QUEUE_GENERATE(
                wp,
                brandingChanged
                    ? g_ui.liveTileBrandingChangedReason
                    : g_ui.liveTileTemplateChangedReason);
    }
    if (manifestTargetBefore != ConfiguredManifestCompatibilityTarget())
        Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());

    WakePollThread();
}

static std::wstring ConsumeCommandLineWallpaperRequest()
{
    std::wstring wallpaper = IniReadS(L"State", L"CommandLineWallpaperRequest", L"");
    if (!wallpaper.empty() && !IniWrite(L"State", L"CommandLineWallpaperRequest", L""))
        return L"";
    return wallpaper;
}

static void QueueCommandLineGenerationRequest()
{
    std::wstring wp = CurrentGenerationBackground(ConsumeCommandLineWallpaperRequest());

    if (wp.empty())
    {
        LogText(CompositionRequested()
            ? L"[!] Requested tile update skipped: no content background is available."
            : g_ui.startupGenerationSkippedNoWallpaper);
        return;
    }

    DESKTOPSTUB_CONTENT_QUEUE_GENERATE(wp, g_ui.commandLineGenerationRequestReason);
}

static constexpr ULONG_PTR TILE_INSTANCE_COPY_DATA = 0x41495054;
static constexpr UINT WM_APP_TILE_INSTANCE_ACTIVATE = WM_APP + 126;
static std::deque<std::pair<std::wstring, bool>> g_tileInstanceActivations;
static constexpr ULONG_PTR TILE_ACTIVATION_NOTICE_COPY_DATA=0x4149504e;
static constexpr UINT WM_APP_TILE_ACTIVATION_NOTICE=WM_APP+130;
static std::deque<std::pair<std::wstring,bool>> g_tileActivationNotices;
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testForwardTileActivationNotice)(const std::wstring&,bool)=nullptr;
static void (*g_testTileActivationNotice)(const std::wstring&,const std::wstring&,bool)=nullptr;
#endif
static bool TileActivationNoticeId(const std::wstring& id)
{
    return !id.empty()&&id.size()<=128&&std::all_of(id.begin(),id.end(),[](wchar_t ch){return(ch>=L'A'&&ch<=L'Z')||(ch>=L'a'&&ch<=L'z')||(ch>=L'0'&&ch<=L'9')||ch==L'_'||ch==L'-'||ch==L'.';});
}
static bool ForwardTileActivationNotice(const std::wstring& id,bool missing)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testForwardTileActivationNotice&&g_testForwardTileActivationNotice(id,missing);
#else
    if(g_hwnd||!TileActivationNoticeId(id))return false;
    const auto target=FindWindowW(WINDOW_CLASS_NAME,g_instanceWindowTitle.empty()?nullptr:g_instanceWindowTitle.c_str());if(!target)return false;
    const auto text=(missing?L"0:":L"1:")+id;
    COPYDATASTRUCT data{TILE_ACTIVATION_NOTICE_COPY_DATA,static_cast<DWORD>((text.size()+1)*sizeof(wchar_t)),const_cast<wchar_t*>(text.c_str())};DWORD_PTR accepted=0;
    return SendMessageTimeoutW(target,WM_COPYDATA,0,reinterpret_cast<LPARAM>(&data),SMTO_ABORTIFHUNG|SMTO_BLOCK,3000,&accepted)&&accepted==TRUE;
#endif
}
static void NotifyTileActivationProblem(const std::wstring& requestedId,const std::wstring& detail,bool missing)
{
    const auto id=TileActivationNoticeId(requestedId)?requestedId:L"tile";
    if(!detail.empty())LogText(L"[!] Start tile activation: "+detail);
    if(!NotificationAllowed(true)||!IniReadI(L"Settings",L"NotificationsEnabled",1))return;
    if(ForwardTileActivationNotice(id,missing))return;
    const auto message=missing?IniReadS(L"Strings",L"NotificationUnconfiguredPin",L"This Start pin is not in the current tile settings. Add a tile from the tray menu, restore the saved settings, or unpin the old entry."):
        IniReadS(L"Strings",L"NotificationTileActivationFailed",L"This tile could not be opened. Check its settings and source availability, then try again. Saved settings and pins were kept.");
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testTileActivationNotice){g_testTileActivationNotice(id,message,missing);return;}
#endif
    NotifyUserOperation((missing?L"unconfigured-pin-":L"tile-activation-failed-")+id,IniReadS(L"Strings",L"NotificationTitle",L"DesktopStub"),message,NIIF_WARNING,true);
}
static void NotifyUnconfiguredStartPin(const std::wstring& id){NotifyTileActivationProblem(id,L"The pin is absent from the current profile.",true);}
static bool ReadTileInstanceSourceActivation(const std::wstring& arguments, std::wstring& url)
{
    int count = 0;
    auto args = CommandLineToArgvW(arguments.c_str(), &count);
    std::wstring profile, candidate, issue;
    { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    const bool valid = args && count == 4 && IEquals(args[0], L"--ini") && IEquals(args[2], L"--open-url") &&
        IEquals(aip::MakeAbsolutePath(args[1]), aip::MakeAbsolutePath(profile)) &&
        ValidateRssFeedUrlSetting(args[3], false, candidate, issue);
    if (args) LocalFree(args);
    if (valid) url = std::move(candidate);
    return valid;
}
static bool OpenGalleryFolder(const std::wstring& directory);
static bool ResolveGalleryRegistrationActivation(const std::wstring& id, std::wstring& folder)
{
    std::wstring error;
    const auto view=CaptureGalleryConfigurationView(error,false);
    if (!view) return false;
    for (const auto& tile:view->configuration.tiles)
        if (aip::gallery::GalleryRegistrationId(view->configuration.groupId,tile.id)==id && TileRegistrationReadyForId(id)) {
            auto path=aip::gallery::EffectiveFolder(view->configuration,tile);
            if (PathIsRelativeW(path.c_str())) path=ParentDirectoryOfPath(view->profile)+L"\\"+path;
            path=aip::MakeAbsolutePath(path);
            if (!path.empty() && GalleryDirectoryExists(path)) { folder=std::move(path);return true; }
            return false;
        }
    return false;
}
static bool ActivateTileInstance(HWND owner, const std::wstring& id, bool signIn)
{
    if (!signIn && id.compare(0,8,L"gallery_")==0) {
        std::wstring issue;const auto gallery=CaptureGalleryConfigurationView(issue,true);
        if(!gallery){NotifyTileActivationProblem(id,issue,false);return false;}
        for(const auto& tile:gallery->configuration.tiles)if(aip::gallery::GalleryRegistrationId(gallery->configuration.groupId,tile.id)==id){
            const auto nativeId=aip::gallery::NativeTileId(gallery->configuration.groupId,tile.id);
            if(tile.clickAction==L"Setup")return OpenGalleryTilePolicySetup(owner,nativeId);
            std::wstring folder;
            if(ResolveGalleryRegistrationActivation(id,folder)&&OpenGalleryFolder(folder)){NotifyGalleryImageFallback();return true;}
            NotifyTileActivationProblem(id,L"The configured gallery folder or Registration identity is unavailable.",false);return false;
        }
        NotifyUnconfiguredStartPin(id);return false;
    }
    aip::content::TileInstanceConfiguration cfg; std::wstring error;
    if (!ReadCurrentTileInstances(cfg, error)){NotifyTileActivationProblem(id,error,false);return false;}
    for (const auto& tile : cfg.tiles) if (tile.id == id) {
        if(signIn&&tile.type==aip::content::TileInstanceType::Calendar)return OpenGoogleCalendarSetup(owner,tile.id);
        if(signIn&&tile.type==aip::content::TileInstanceType::Wuwa)return OpenKuroSignInForTile(owner,tile.id);
        const auto resource = aip::content::DescribeTileInstanceType(tile.type).notesResource;
        if (resource >= 0) {
            const auto& account = tile.configuration.notes.accounts[resource];
            const bool missing = account.uid.empty() || (account.token.empty() && account.tokenProtected.empty()) ||
                (account.userId.empty() && account.userIdProtected.empty());
            if (signIn || (tile.clickAction == L"setup" && missing && tile.error.empty()))
                return OpenEmbeddedNotesSignInForTile(owner, resource, tile.id);
        }
        if (const auto* action = aip::content::FindTileAction(tile.clickAction)) {
            int step = IniReadClampedI(aip::content::TileInstanceSection(id).c_str(), L"VolumeStepPercent", 5, 1, 100);
            QueueMediaTileAction(owner, action->id, step); return true;
        }
        if (tile.clickAction == L"source") for (const auto& frame : CurrentSecondarySnapshots())
            if (frame.secondaryTileId == aip::content::TileInstanceNativeId(tile)) {
                std::wstring url;
                if (ReadTileInstanceSourceActivation(frame.activationArguments, url)) {
                    ShellExecuteW(owner, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL); return true;
                }
            }
        return OpenTileInstanceSetup(owner, id);
    }
    NotifyUnconfiguredStartPin(id);
    return false;
}
static void HandleSingleInstanceRequest(WPARAM request)
{
    const auto action = request & SINGLE_INSTANCE_REQUEST_MASK;
    if (action >= SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE &&
        action < SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE + aip::content::TileActionCount) {
        QueueMediaTileAction(g_hwnd, aip::content::TileActions[action - SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE].id); return;
    }
    if (action >= SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE &&
        action < SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE + aip::content::SecondaryTileCount) {
        const auto& tile = aip::content::SecondaryTiles[action - SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE];
        if (TileInstanceModeRequested()) { ActivateTileInstance(g_hwnd, tile.id, (request & SINGLE_INSTANCE_FLAG_TILE_SIGN_IN) != 0); return; }
        if ((request & SINGLE_INSTANCE_FLAG_TILE_SIGN_IN) && std::wstring(tile.source) == L"Notes")
            OpenEmbeddedNotesSignIn(g_hwnd, tile.notesResource);
        else {
            const auto click = IniReadS(aip::content::SecondaryTileSection(tile.id).c_str(), L"ClickAction",
                std::wstring(tile.id) == L"smtc" ? L"toggle" : L"setup");
            if (const auto* media = aip::content::FindTileAction(click)) QueueMediaTileAction(g_hwnd, media->id);
            else OpenSecondaryTileSetup(g_hwnd, tile.id);
        }
        return;
    }
    if (action >= SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE && action <= SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE + 3) {
        RequestContentNotesRefresh(static_cast<int>(action - SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE)); return;
    }
    bool applyTrayIcon = (request & SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON) != 0;
    bool regenerateManifest = (request & SINGLE_INSTANCE_FLAG_REGENERATE_MANIFEST) != 0;

    switch (request & SINGLE_INSTANCE_REQUEST_MASK)
    {
    case SINGLE_INSTANCE_REQUEST_SETUP_DESKTOP:
        PostMessageW(g_hwnd,WM_APP_REQUEST_DESKTOP_HOST_SETUP,0,0);return;
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

    case SINGLE_INSTANCE_REQUEST_SHOW_CONSOLE:
        HandleExistingInstanceShowConsoleRequest();
        return;

    case SINGLE_INSTANCE_REQUEST_SHOW:
    default:
        HandleExistingInstanceRequest();
        return;
    }
}

static void MaybeClearLiveTileOnShutdown()
{
    if (g_relaunchAfterExit.load() == 0) ClearOwnedRegistrationTilesOnShutdown();
    if (!AnyTileInstanceClearsOnShutdown())
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
    if (Appx_Clear_Or_Request_LiveTile(failure,TileInstanceModeRequested()))
    {
        SetLiveTileClearedOnShutdown(UseExperimentalLiveTileUpdate()&&TileInstancePrimaryClearsOnShutdown());
        LogText(g_ui.liveTileClearOnShutdownSuccess);
    }
    else
    {
        Log(g_ui.liveTileClearOnShutdownSkipped.c_str(), failure.empty() ? g_ui.unknownError.c_str() : failure.c_str());
    }
}

static std::wstring ModeSwitchRelaunchArguments(bool forceGenerate, bool waitForThisProcessToExit, bool generateOnce)
{
    std::wstring args = L"--launched-from-mode-switch";
    if (waitForThisProcessToExit)
        args += L" " + std::wstring(WAIT_FOR_PID_ARG) + L" " + std::to_wstring(GetCurrentProcessId());
    if (generateOnce)
        args += L" --once";
    else if (forceGenerate)
        args += L" --generate";
    if (g_commandLine.customIniPath)
        args += L" --ini " + QuoteCommandLineArg(g_iniPath);
    return args;
}

static bool LiveTileRuntimeConditionMismatch(bool& shouldRunPackaged)
{
    if (!g_liveTileRelaunchOnSwitch.load())
        return false;

    if (TileInstanceModeRequested()) {
        aip::content::TileInstanceConfiguration instances; std::wstring error;
        if (!ReadCurrentTileInstances(instances,error)) return false;
        bool liveRequired = std::any_of(instances.tiles.begin(), instances.tiles.end(), [](const auto& tile) {
            return tile.policy.Mode() == L"LiveTile";
        });
        const auto gallery = CaptureGalleryConfigurationView(error,false);
        if (gallery) liveRequired |= std::any_of(gallery->configuration.tiles.begin(), gallery->configuration.tiles.end(), [](const auto& tile) {
            return tile.delivery.Mode() == L"LiveTile";
        });
        // The one host can update separate static packages while packaged.
        // Auto preserves its current runtime; static tiles never force it out
        // of the identity required by a sibling's Live Tile.
        shouldRunPackaged = true;
        return liveRequired && !CurrentProcessHasPackageIdentity();
    }

    if (!LiveTileModeHasFixedRuntime(
            ConfiguredLiveTileUpdateMode(),
            ConfiguredManifestCompatibilityTarget(),
            shouldRunPackaged))
        return false;

    return CurrentProcessHasPackageIdentity() != shouldRunPackaged;
}

static bool QueueLiveTileRuntimeConditionRelaunch(HWND h)
{
    if(g_commandLine.setupDesktop||g_desktopHostSetupPending.load()||g_desktopHostSetupRecovered)return false;
    bool shouldRunPackaged = false;
    if (!LiveTileRuntimeConditionMismatch(shouldRunPackaged))
    {
        // Cold-starting from the Start tile already gives this process package
        // identity, so the runtime condition can look "correct" even when the
        // one-time manifest refresh for LiveTile mode was never performed.
        // Bounce through the unpackaged process first so AppxManifest can be
        // re-registered with the Desktop/static icon before the packaged Live
        // Tile runtime continues.
        if (g_liveTileRelaunchOnSwitch.load() &&
            CurrentProcessHasPackageIdentity() &&
            FixedPackagedLiveTileModeNeedsUnpackagedReregistration())
        {
            SetLiveTileModeReregistrationPending(true);
            g_relaunchAfterExit = 2;
            LogText(g_ui.liveTileModeReregistrationQueued);
            LogText(g_ui.liveTileStaticIconRefreshPending);
            RequestGracefulShutdown(h);
            return true;
        }

        return false;
    }

    if (shouldRunPackaged)
    {
        if (TileInstanceModeRequested()) {
            const auto manifest = CurrentManifestDisplayInfo();
            std::wstring family, full, error;
            if (!IEquals(FileNameFromPath(manifest.executable),FileNameFromPath(g_exePath)) ||
                !FindRegisteredPackageFamilyNameForManifest(manifest,family,full,error)) {
                Log(L"[!] Live Tile migration needs DesktopStub's full-trust host package to be installed first. %ls",error.c_str());
                return false;
            }
            g_relaunchAfterExit = 1;
            LogText(g_ui.liveTileRelaunchToPackagedQueued);
            RequestGracefulShutdown(h);
            return true;
        }
        // We are unpackaged while the fixed Live Tile mode requires package
        // identity. Re-register first so the package entry points at the latest
        // manifest/assets, then exit and relaunch through the registered package.
        g_relaunchAfterExit = 1;
        if (!QueueLiveTileModeReregistration())
        {
            g_relaunchAfterExit = 0;
            return false;
        }

        LogText(g_ui.liveTileRelaunchToPackagedQueued);
        RequestGracefulShutdown(h);
        return true;
    }

    // We are packaged while the fixed Registration/disabled mode requires the
    // normal unpackaged desktop process. This happens when the Start tile is
    // clicked while no resident instance is running after Live Tile was disabled.
    // The unpackaged relaunch waits for this packaged PID to exit before it
    // touches the manifest, avoiding "the app is still running" registration
    // failures.
    SetLiveTileModeReregistrationPending(true);
    g_relaunchAfterExit = 2;
    LogText(g_ui.liveTileRelaunchToUnpackagedQueued);
    RequestGracefulShutdown(h);
    return true;
}

static bool StartUnpackagedRelaunchViaCmdBroker(const std::wstring& arguments, DWORD* errorOut)
{
    if (errorOut)
        *errorOut = ERROR_SUCCESS;

    std::wstring systemDir = aip::GetSystemDirectoryPath();
    if (systemDir.empty())
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    std::wstring cmdPath = systemDir + L"\\cmd.exe";
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
    std::wstring arguments = ModeSwitchRelaunchArguments(false, true, CommandLineShouldGenerateOnce());

    // Use an unpackaged system broker instead of ShellExecute'ing the registered
    // EXE directly from the packaged process. Direct relaunch can preserve the
    // package identity, which makes the following manifest registration fail
    // with "the app is still running". The new instance also waits for this PID
    // to exit before touching the manifest.
    DWORD brokerError = ERROR_SUCCESS;
    if (StartUnpackagedRelaunchViaCmdBroker(arguments, &brokerError))
    {
        LogText(g_ui.liveTileRelaunchedUnpackaged);
        return true;
    }

    LogWin32Failure(L"CreateProcessW(cmd.exe unpackaged relaunch broker)", brokerError);
    if (StartUnpackagedRelaunchDirect(arguments))
    {
        LogText(g_ui.liveTileRelaunchedUnpackagedDirectFallback);
        return true;
    }

    return false;
}

static bool StartPackagedRelaunchAfterExit()
{
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring failure;
    std::wstring arguments = ModeSwitchRelaunchArguments(true, false, CommandLineShouldGenerateOnce());
    if (ActivateRegisteredPackageApplication(manifestInfo, arguments.c_str(), failure))
    {
        LogText(g_ui.liveTileRelaunchedPackaged);
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
            LogText(g_ui.liveTilePackagedRelaunchSkippedPending);
            return;
        }
        StartPackagedRelaunchAfterExit();
    }
    else if (relaunch == 2)
        StartUnpackagedRelaunchAfterExit();
}

LRESULT CALLBACK WndProc(HWND h,UINT m,WPARAM w,LPARAM l)
{
    if(m==WM_APP_NOTIFICATION_CONFIRMATION){DrainUserConfirmations();return 0;}
    if(m==WM_APP_TILE_ACTIVATION_NOTICE){
        if(!g_tileActivationNotices.empty()){const auto notice=std::move(g_tileActivationNotices.front());g_tileActivationNotices.pop_front();
            if(!g_shutdownState.IsRequested())NotifyTileActivationProblem(notice.first,L"",notice.second);}
        return 0;
    }
    if(m==WM_APP_REQUEST_DESKTOP_HOST_SETUP){if(!g_shutdownState.IsRequested())RequestDesktopHostSetup(h);return 0;}
    if(m==WM_APP_DESKTOP_HOST_SETUP_COMPLETE){CompleteDesktopHostSetup(h);return 0;}
    if (m == WM_COPYDATA) {
        const auto* data = reinterpret_cast<const COPYDATASTRUCT*>(l);
        if(data&&data->dwData==TILE_ACTIVATION_NOTICE_COPY_DATA){
            if(!data->lpData||data->cbData<4*sizeof(wchar_t)||data->cbData>131*sizeof(wchar_t)||data->cbData%sizeof(wchar_t)||g_tileActivationNotices.size()>=64||g_shutdownState.IsRequested())return FALSE;
            const auto* text=static_cast<const wchar_t*>(data->lpData);const auto count=data->cbData/sizeof(wchar_t);
            if(text[count-1]!=L'\0'||(text[0]!=L'0'&&text[0]!=L'1')||text[1]!=L':')return FALSE;
            const std::wstring id(text+2,count-3);if(!TileActivationNoticeId(id))return FALSE;
            g_tileActivationNotices.emplace_back(id,text[0]==L'0');
            if(!PostMessageW(h,WM_APP_TILE_ACTIVATION_NOTICE,0,0)){g_tileActivationNotices.pop_back();return FALSE;}
            return TRUE;
        }
        if (!data || data->dwData != TILE_INSTANCE_COPY_DATA || !data->lpData || data->cbData < 4 * sizeof(wchar_t) ||
            data->cbData > 58 * sizeof(wchar_t) || data->cbData % sizeof(wchar_t)) return FALSE;
        const auto* raw = static_cast<const wchar_t*>(data->lpData);
        const auto count = data->cbData / sizeof(wchar_t);
        if (raw[count - 1] != L'\0' || (raw[0] != L'0' && raw[0] != L'1') || raw[1] != L':' || g_tileInstanceActivations.size() >= 64) return FALSE;
        const std::wstring id(raw + 2, count - 3);
        if (!aip::content::ValidTileInstanceId(id) || g_shutdownState.IsRequested()) return FALSE;
        g_tileInstanceActivations.emplace_back(id, raw[0] == L'1');
        if (!PostMessageW(h, WM_APP_TILE_INSTANCE_ACTIVATE, 0, 0)) { g_tileInstanceActivations.pop_back(); return FALSE; }
        return TRUE; // COM/browser work must run after SendMessage unwinds.
    }
    if (m == WM_APP_TILE_INSTANCE_ACTIVATE) {
        if (!g_tileInstanceActivations.empty()) {
            const auto request = std::move(g_tileInstanceActivations.front()); g_tileInstanceActivations.pop_front();
            if (!g_shutdownState.IsRequested()) ActivateTileInstance(h, request.first, request.second);
        }
        return 0;
    }
    if (SecondaryTileHandleMessage(h, m, w, l)) return 0;
    if (MediaTileActionHandleMessage(h, m)) return 0;
    if (m == WM_POWERBROADCAST && (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)) {
        g_contentCaps.restart = true; CaptureContentLogicalCapsState(); WakePollThread(); return TRUE;
    }
    if (m == WM_DEVICECHANGE) { g_contentCaps.restart = true; WakePollThread(); }
    if (m == WM_APP_CONTENT_LOGICAL_CAPS) {
        return CaptureContentLogicalCapsState();
    }
    if (m == WM_APP_CONTENT_FRAME_READY) {
        g_contentFrameReadyPosted = false;
        if (!g_shutdownState.IsRequested()) QueueLiveTileRuntimeConditionRelaunch(h);
        return 0;
    }
    if (m == WM_CLOSE || m == WM_QUERYENDSESSION || m == WM_APP_REQUEST_SHUTDOWN)
        CaptureContentLogicalCapsState();
    if (g_singleInstanceMessage && m == g_singleInstanceMessage)
    {
        HandleSingleInstanceRequest(w);
        return aip::INSTANCE_REQUEST_HANDLED;
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
        if (g_shutdownState.IsRequested())
        {
            CaptureContentLogicalCapsState();
            PostQuitMessage(0);
        }
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
        if (!g_shutdownState.IsRequested())
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            return 0;
        }

        if (!GenerationWorkPending())
        {
            KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
            CaptureContentLogicalCapsState();
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
        ClearUserConfirmations();g_tileActivationNotices.clear();
        CaptureContentLogicalCapsState();
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
    (void)customIniPath;

    // Scope by the effective INI path, not only by the containing directory.
    // This preserves the legacy portable behavior where copying/renaming the
    // executable in the same folder gives each name its own INI and its own
    // resident instance, for example GenerateAssets.exe -> GenerateAssets.ini
    // and DeskStub.exe -> DeskStub.ini.
    std::wstring scope = !iniPath.empty() ? iniPath : exeDir;
    std::wstring productBase = SanitizeManifestToken(ProductRuntimeBaseName(), L"DesktopStub");

    aip::InstanceIdentity identity = aip::BuildPathScopedInstanceIdentity(
        productBase,
        productBase + L".RestoreRunningInstance",
        WINDOW_CLASS_NAME,
        productBase,
        scope);
    g_singleInstanceMutexName = std::move(identity.mutexName);
    g_singleInstanceMessageName = std::move(identity.messageName);
    g_instanceWindowTitle = std::move(identity.windowTitle);
}

static bool SignalExistingInstance(WPARAM request = SINGLE_INSTANCE_REQUEST_SHOW)
{
    int retries = IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200);
    int delayMs = IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000);
    return aip::SendInstanceWindowRequest(
        WINDOW_CLASS_NAME,
        g_instanceWindowTitle.empty() ? nullptr : g_instanceWindowTitle.c_str(),
        g_singleInstanceMessage,
        request,
        retries,
        delayMs,
        10000);
}
static bool SignalTileInstanceActivation(const std::wstring& id, bool signIn)
{
    if (!aip::content::ValidTileInstanceId(id)) return false;
    const auto payload = (signIn ? L"1:" : L"0:") + id;
    COPYDATASTRUCT data{TILE_INSTANCE_COPY_DATA, static_cast<DWORD>((payload.size() + 1) * sizeof(wchar_t)), const_cast<wchar_t*>(payload.c_str())};
    // The target is this profile's resident window; no path/token is transmitted.
    const auto target = FindWindowW(WINDOW_CLASS_NAME, g_instanceWindowTitle.empty() ? nullptr : g_instanceWindowTitle.c_str());
    DWORD_PTR accepted = 0;
    return target && SendMessageTimeoutW(target, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
        SMTO_ABORTIFHUNG | SMTO_BLOCK, 3000, &accepted) && accepted == TRUE;
}

static bool TryGetAlreadyRunningRequest(WPARAM& request)
{
    switch (CurrentAlreadyRunningAction())
    {
    case AlreadyRunningAction::Generate:
        request = SINGLE_INSTANCE_REQUEST_GENERATE;
        return true;
    case AlreadyRunningAction::ShowConsole:
        request = SINGLE_INSTANCE_REQUEST_SHOW_CONSOLE;
        return true;
    case AlreadyRunningAction::Ignore:
        return false;
    case AlreadyRunningAction::ShowTray:
    default:
        request = SINGLE_INSTANCE_REQUEST_SHOW;
        return true;
    }
}

static std::wstring AppxActivationTraceLogPath()
{
    return GetExeDir() + L"\\" + PortableHostBaseName() + L".appxactivation.log";
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
    return GetExeDir() + L"\\" + PortableHostBaseName() + L".appxactivation.request";
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
    Log(g_ui.appxActivationRequestReceived.c_str(), TrimCopy(text).c_str());
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

    std::vector<wchar_t> buffer(512);
    bool ok = false;
    for (;;)
    {
        DWORD len = static_cast<DWORD>(buffer.size());
        if (QueryFullProcessImageNameW(h, 0, buffer.data(), &len))
        {
            if (len != 0)
            {
                path.assign(buffer.data(), len);
                ok = true;
            }
            break;
        }
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || buffer.size() >= 32768)
            break;
        buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)));
    }
    CloseHandle(h);
    return ok;
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
        if (!QueryProcessImagePath(pe.th32ProcessID, imagePath) ||
            !SamePathI(imagePath, hostPath))
            continue;

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

    std::wstring systemDir = aip::GetSystemDirectoryPath();
    if (systemDir.empty())
    {
        DWORD err = GetLastError();
        if (errorOut)
            *errorOut = err == ERROR_SUCCESS ? ERROR_GEN_FAILURE : err;
        return false;
    }

    std::wstring cmdPath = systemDir + L"\\cmd.exe";
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

    if (CommandLineRequestsLiveTileClearOnly())
    {
        LoadUiStrings();
        ValidateFormatStrings();
        NormalizeSeparatorStrings();
        std::wstring clearFailure;
        if (Appx_Clear_LiveTileBare(clearFailure,TileInstanceModeRequested()))
        {
            AppxActivationTrace(L"Live Tile cleared under package identity");
            return 0;
        }

        AppxActivationTrace(L"Live Tile clear failed under package identity: " + clearFailure);
        return 1;
    }

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

static int HandlePackagedLiveTileClearOnlyAndExit()
{
    if (!CommandLineRequestsLiveTileClearOnly())
        return -1;

    if (!CurrentProcessHasPackageIdentity())
    {
        AppxActivationTrace(L"Live Tile clear request rejected because the helper has no package identity");
        return 1;
    }

    LoadUiStrings();
    ValidateFormatStrings();
    NormalizeSeparatorStrings();
    LoadRuntimeSettingsFromIni(false);

    std::wstring failure;
    if (Appx_Clear_LiveTileBare(failure,TileInstanceModeRequested()))
    {
        AppxActivationTrace(L"Live Tile cleared under package identity");
        return 0;
    }

    AppxActivationTrace(L"Live Tile clear failed under package identity: " +
        (failure.empty() ? std::wstring(L"unknown error") : failure));
    return 1;
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
        if (!h)
            return TRUE;
        if (PostMessageW(h, WM_APP_REQUEST_SHUTDOWN, ctrlType, 0))
            return TRUE;
        g_consoleControlShutdownPending = false;
        return FALSE;
    }
    default:
        return FALSE;
    }
}

static bool GalleryDirectoryExists(const std::wstring& path)
{
#ifdef DESKTOPSTUB_TEST_GALLERY_DIRECTORY_EXISTS
    return DESKTOPSTUB_TEST_GALLERY_DIRECTORY_EXISTS(path);
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    (void)path; return false;
#else
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#endif
}
static bool ResolveGalleryFolderActivation(const CommandLineOptions& options, std::wstring& result, std::wstring& error)
{
    auto path = options.openFolder;
    const auto& id = options.windowsTileId;
    if (id.compare(0, std::wstring(aip::gallery::NativeTilePrefix).size(), aip::gallery::NativeTilePrefix) == 0) {
        aip::gallery::Configuration configuration; aip::gallery::TileConfiguration tile;
        if (!FindConfiguredGalleryTile(id, configuration, tile)) {
            error = L"This gallery tile is absent from the selected INI. Unpin the old tile from Start, or restore its original INI and .gallery-ledger. Current gallery settings are under Tiles > Photo gallery.";
            return false;
        }
        path = aip::gallery::EffectiveFolder(configuration, tile);
    }
    if (path.empty() || !aip::gallery::detail::Text(path, 32768, false)) {
        error = L"The gallery directory is empty or invalid."; return false;
    }
    std::wstring profile; { std::lock_guard<std::mutex> lock(g_pathMutex); profile = g_iniPath; }
    if (PathIsRelativeW(path.c_str())) path = ParentDirectoryOfPath(profile) + L"\\" + path;
    path = aip::MakeAbsolutePath(path);
    if (path.empty() || !GalleryDirectoryExists(path)) {
        error = L"The gallery folder is unavailable. Check Tiles > Photo gallery > Choose photo folder in DesktopStub."; return false;
    }
    result = std::move(path); error.clear(); return true;
}
static bool OpenGalleryFolder(const std::wstring& directory)
{
    if (!GalleryDirectoryExists(directory)) return false;
#ifdef DESKTOPSTUB_TEST_GALLERY_OPEN_FOLDER
    return DESKTOPSTUB_TEST_GALLERY_OPEN_FOLDER(directory);
#elif defined(DESKTOPSTUB_INERT_HARDWARE)
    return false;
#else
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
#endif
}

static bool HandleGalleryImageActivation(CommandLineOptions& options,int& result,bool* folderFallback=nullptr)
{
    if(folderFallback)*folderFallback=false;
    if(!options.galleryFolderActivation)return false;
    auto nativeId=options.windowsTileId;std::wstring token=options.galleryImageToken;
    GalleryShownActivation shown;
    if(token.empty()&&ReadGalleryShownActivation(shown)&&shown.tileId.compare(0,11,L"AipGallery_")==0){
        if(nativeId.empty())nativeId=shown.tileId;
        if(nativeId==shown.tileId)ReadGalleryClickPayload(shown.arguments,nativeId,token);
    }
    if(!token.empty()&&nativeId.empty()){aip::gallery::activation::Token decoded;if(aip::gallery::activation::Decode(token,decoded))nativeId=aip::gallery::NativeTileId(decoded.group,decoded.tile);}
    aip::gallery::Configuration cfg;aip::gallery::TileConfiguration tile;std::wstring issue;
    if(!ReadGalleryConfiguration(cfg,issue)){
        NotifyTileActivationProblem(nativeId,issue,false);result=2;return true;
    }
    const auto row=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& value){return !nativeId.empty()&&aip::gallery::NativeTileId(cfg.groupId,value.id)==nativeId;});
    const bool configured=row!=cfg.tiles.end();if(configured)tile=*row;
    if(!nativeId.empty()&&!configured){NotifyUnconfiguredStartPin(nativeId);result=2;return true;}
    if(configured&&tile.clickAction==L"Setup"){
        options.tileSetupKey=aip::gallery::GalleryRegistrationId(cfg.groupId,tile.id);options.galleryFolderActivation=false;
        options.openFolder.clear();options.galleryImageToken.clear();return false;
    }
    if(!token.empty()){
        std::wstring image,error;
        if(ResolveGalleryImageToken(token,nativeId,image,error)&&OpenResolvedGalleryImage(image)){result=0;return true;}
        if(!error.empty())LogText(L"[!] Gallery photo activation fallback: "+error);
        if(!configured){NotifyTileActivationProblem(nativeId,error,false);result=2;return true;}
    }
    if(configured){options.windowsTileId=nativeId;if(folderFallback)*folderFallback=true;}
    return false; // Existing folder handler remains the honest no-metadata fallback.
}
static void ReadChaseableRssActivation(CommandLineOptions& options)
{
    if(options.tileSetupKey.empty()||options.tileSignIn||options.renderOnly||options.configureOnly||!options.settings.empty()||!options.openUrl.empty())return;
    aip::content::TileInstanceConfiguration cfg;std::wstring error;if(!ReadCurrentTileInstances(cfg,error))return;
    const auto tile=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& item){return item.id==options.tileSetupKey;});
    if(tile==cfg.tiles.end()||tile->type!=aip::content::TileInstanceType::Rss||tile->clickAction!=L"source")return;
    GalleryShownActivation shown;std::wstring url;
    if(ReadGalleryShownActivation(shown)&&shown.tileId==aip::content::TileInstanceNativeId(*tile)&&ReadTileInstanceSourceActivation(shown.arguments,url)){
        options.openUrl=std::move(url);options.tileSetupKey.clear();
    }
}

int WINAPI wWinMain(_In_ HINSTANCE hi, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int)
{
    // Capture children inherit only their private output/reply handles. Handle
    // this isolated endpoint before sidecars, resident signaling or integration.
    int captureArgc = 0;
    LPWSTR* captureArgv = CommandLineToArgvW(GetCommandLineW(), &captureArgc);
    if (!captureArgv) return 2;
    std::vector<std::wstring> captureArguments;
    for (int argument = 0; argument < captureArgc; ++argument)
        captureArguments.emplace_back(captureArgv[argument] ? captureArgv[argument] : L"");
    LocalFree(captureArgv);
    aip::capture::HelperRequest captureRequest;
    bool captureRecognized = false;
    const bool captureValid = aip::capture::ParseHelperArguments(captureArguments, captureRequest, captureRecognized);
    if (captureRecognized)
    {
        if (!captureValid) return 2;
        ConfigureDpiAwareness();
        return RunLiveWallpaperCaptureHelper(captureRequest);
    }
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

    aip::SidecarPaths sidecarPaths;
    std::wstring sidecarError;
    if (!aip::TryBuildSidecarPathsFromExecutable(
            g_exePath,
            L"DesktopStub",
            g_commandLine.customIniPath ? g_commandLine.iniPath : L"",
            sidecarPaths,
            &sidecarError))
    {
        ShowCommandLineMessage(L"Invalid INI path: " + sidecarError, true);
        return 2;
    }
    g_exePath = sidecarPaths.exePath;
    g_iniPath = sidecarPaths.configPath;

    std::wstring dir = sidecarPaths.exeDir.empty() ? GetExeDir() : sidecarPaths.exeDir;
    ConfigureSingleInstanceIdentity(dir, g_iniPath, g_commandLine.customIniPath);

    if (g_commandLine.helpRequested)
    {
        // Keep --help side-effect-free for baseline app behavior: do not create,
        // normalize, or repair the INI just to print command-line help.
        ShowCommandLineMessage(CommandLineUsageText(), false);
        return 0;
    }
    if (CommandLineShouldShowVersion())
    {
        ShowCommandLineMessage(CommandLineVersionText(), false);
        return 0;
    }

    int galleryClickResult=0;bool galleryFolderFallback=false;
    if(HandleGalleryImageActivation(g_commandLine,galleryClickResult,&galleryFolderFallback))return galleryClickResult;
    if (g_commandLine.galleryFolderActivation) {
        std::wstring folder, error;
        if (!ResolveGalleryFolderActivation(g_commandLine, folder, error) || !OpenGalleryFolder(folder)) {
            if (error.empty()) error = L"Windows could not open the gallery directory.";
            NotifyTileActivationProblem(g_commandLine.windowsTileId,error,false);return 2;
        }
        if(galleryFolderFallback)NotifyGalleryImageFallback();
        return 0; // No resident, INI creation, providers, registration or Startup.
    }

    ReadChaseableRssActivation(g_commandLine);

    if (!g_commandLine.openUrl.empty()) {
        if (g_commandLine.renderOnly || g_commandLine.configureOnly || !g_commandLine.settings.empty()) return 2;
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", g_commandLine.openUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32 ? 0 : 2;
    }
    std::wstring notesImportError;
    if (!ExpandCommandLineNotesImports(notesImportError) || !ExpandCommandLineHardwareImports(notesImportError) || !ExpandCommandLineNowPlayingImports(notesImportError)) {
        aip::WriteCommandLineText(notesImportError, true); return 2;
    }
    if (g_commandLine.notesRefresh >= 0) {
        if (g_commandLine.renderOnly || g_commandLine.configureOnly || !g_commandLine.settings.empty() || !g_commandLine.presetName.empty() ||
            g_commandLine.generateOnce || g_commandLine.forceGenerateOnStartup || g_commandLine.exitRequested ||
            g_commandLine.regenerateManifest || g_commandLine.appxActivationServer || g_commandLine.comRegistrationHelper) {
            aip::WriteCommandLineText(L"--notes-refresh cannot be combined with settings, offline, activation, or rendering commands.", true); return 2;
        }
        g_singleInstanceMessage = RegisterWindowMessageW(g_singleInstanceMessageName.c_str());
        if (!g_singleInstanceMessage || !SignalExistingInstance(SINGLE_INSTANCE_REQUEST_NOTES_REFRESH_BASE + g_commandLine.notesRefresh)) {
            aip::WriteCommandLineText(L"Notes refresh requires an existing resident for this INI profile.", true); return 2;
        }
        aip::WriteCommandLineText(L"Requested Notes refresh from the existing resident.", false); return 0;
    }
    if (g_commandLine.renderOnly || g_commandLine.configureOnly)
        return RunRenderOnlyCommand();

    WaitForModeSwitchParentExitIfRequested();

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
    g_taskbarCreatedMessage = aip::RegisterTaskbarCreatedMessage();
    DWORD taskbarCreatedMessageError = g_taskbarCreatedMessage ? ERROR_SUCCESS : GetLastError();
    if (!g_taskbarCreatedMessage && taskbarCreatedMessageError == ERROR_SUCCESS)
        taskbarCreatedMessageError = ERROR_INVALID_DATA;

    int appxActivationExitCode = HandleAppxActivationAndExit();
    if (appxActivationExitCode >= 0)
        return appxActivationExitCode;

    int packagedLiveTileClearExitCode = HandlePackagedLiveTileClearOnlyAndExit();
    if (packagedLiveTileClearExitCode >= 0)
        return packagedLiveTileClearExitCode;

    SetLastError(ERROR_SUCCESS);
    g_singleInstanceMutex = CreateMutexW(nullptr, TRUE, g_singleInstanceMutexName.c_str());
    DWORD singleInstanceMutexLastError = GetLastError();
    DWORD singleInstanceMutexFailure = ERROR_SUCCESS;
    if (!g_singleInstanceMutex)
    {
        singleInstanceMutexFailure = singleInstanceMutexLastError;
        ErrorAction earlySingleInstanceFailureAction = CurrentSingleInstanceFailureAction();
        if (earlySingleInstanceFailureAction == ErrorAction::Exit)
        {
            std::wstring message = L"Failed to create DesktopStub single-instance mutex before initialization (error " +
                std::to_wstring(singleInstanceMutexFailure) + L": " + Win32ErrorString(singleInstanceMutexFailure) + L").";
            ShowCommandLineMessage(message, true);
            return 1;
        }
        if (earlySingleInstanceFailureAction == ErrorAction::Crash)
        {
            RaiseFailFastException(nullptr, nullptr, 0);
        }
    }
    else if (singleInstanceMutexLastError == ERROR_ALREADY_EXISTS)
    {
        WPARAM singleInstanceRequest = SINGLE_INSTANCE_REQUEST_SHOW;
        bool explicitSingleInstanceRequest = false;
        if(g_commandLine.setupDesktop){singleInstanceRequest=SINGLE_INSTANCE_REQUEST_SETUP_DESKTOP;explicitSingleInstanceRequest=true;}
        else if (!g_commandLine.mediaAction.empty()) {
            for (std::size_t index = 0; index < aip::content::TileActionCount; ++index)
                if (g_commandLine.mediaAction == aip::content::TileActions[index].id)
                    singleInstanceRequest = SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE + index;
            explicitSingleInstanceRequest = true;
        }
        else if (!g_commandLine.tileSetupKey.empty())
        {
            if (TileInstanceModeRequested()) {
                const bool delivered = SignalTileInstanceActivation(g_commandLine.tileSetupKey, g_commandLine.tileSignIn);
                CloseHandle(g_singleInstanceMutex); g_singleInstanceMutex = nullptr;
                if (!delivered) ShowCommandLineMessage(L"Could not deliver the tile activation to this profile's resident.", true);
                return delivered ? 0 : 2;
            }
            for (std::size_t index = 0; index < aip::content::SecondaryTileCount; ++index)
                if (g_commandLine.tileSetupKey == aip::content::SecondaryTiles[index].id)
                    singleInstanceRequest = SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE + index;
            if (g_commandLine.tileSignIn) singleInstanceRequest |= SINGLE_INSTANCE_FLAG_TILE_SIGN_IN;
            explicitSingleInstanceRequest = true;
        }
        else if (CommandLineShouldExit())
        {
            if (!ApplyCommandLineSettingsForExistingInstance())
            {
                ShowCommandLineMessage(g_commandLine.applyError, true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }
            singleInstanceRequest = SINGLE_INSTANCE_REQUEST_EXIT;
            explicitSingleInstanceRequest = true;
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
            explicitSingleInstanceRequest = true;
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
            explicitSingleInstanceRequest = true;
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
            explicitSingleInstanceRequest = true;
        }
        else if (!TryGetAlreadyRunningRequest(singleInstanceRequest))
        {
            CloseHandle(g_singleInstanceMutex);
            g_singleInstanceMutex = nullptr;
            return 0;
        }

        if (CommandLineTrayIconChanged())
            singleInstanceRequest |= SINGLE_INSTANCE_FLAG_APPLY_TRAY_ICON;
        if (CommandLineShouldRegenerateManifest())
            singleInstanceRequest |= SINGLE_INSTANCE_FLAG_REGENERATE_MANIFEST;

        if (!SignalExistingInstance(singleInstanceRequest))
        {
            if (explicitSingleInstanceRequest)
            {
                ShowCommandLineMessage(
                    StartupString(L"CommandLineSignalExistingFailed", L"Failed to signal the running DesktopStub instance."),
                    true);
                CloseHandle(g_singleInstanceMutex);
                g_singleInstanceMutex = nullptr;
                return 2;
            }

            if (IniReadI(L"Settings", L"NotificationsEnabled", 1) != 0 &&
                IniReadI(L"Settings", L"NotifyOnAlreadyRunning", 0) != 0)
            {
                std::wstring title = StartupString(L"NotificationTitle", L"DesktopStub");
                std::wstring message = StartupString(L"NotificationAlreadyRunning", L"DesktopStub is already running.");
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
    std::wstring tileMigrationError;
    if (!EnsureTileInstanceProfile(tileMigrationError)) {
        QueueStartupWarning(L"Tile configuration conversion was not applied: " + tileMigrationError);
    }

    // GDI+ is intentionally started lazily only when bitmap work is needed.
    auto gdiDone = iniDone;
    std::wstring startupShortcutError;
    bool shouldReconcileStartupShortcut =
        !CommandLineShouldGenerateOnce() &&
        (!CommandLineShouldRegenerateManifest() || CommandLineShouldGenerateNow());
    if (shouldReconcileStartupShortcut &&
        !ReconcileDesktopStubStartupShortcutFromConfig(startupShortcutError))
    {
        QueueStartupWarning(
            L"[!] Could not synchronize the shell:startup shortcut: " +
            startupShortcutError);
    }
    std::wstring previousForceShutdownCleanupWarning;
    if (ConsumePreviousForceShutdownCleanupWarning(previousForceShutdownCleanupWarning))
        QueueStartupWarning(previousForceShutdownCleanupWarning);

    LogText(g_ui.programStarting);
    Log(g_ui.desktopStubVersionSummary.c_str(), DesktopStubVersionDisplayText().c_str());
    Log(g_ui.timingLog.c_str(), g_ui.timingPreLogInitialization.c_str(), ElapsedMs(appStart, gdiDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingIniDefaultStringLoading.c_str(), ElapsedMs(iniStart, iniDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGdiStartup.c_str(), ElapsedMs(iniDone, gdiDone));
    Log(g_ui.exeLabel.c_str(), g_exePath.c_str());
    Log(g_ui.iniLabel.c_str(), g_iniPath.c_str());
    Log(g_ui.logFileLabel.c_str(), GetLogPathCopy().c_str());
    LogCommandLineSummary();
    RecoverInterruptedDesktopHostSetup();
    FlushStartupWarnings();

    if (singleInstanceMutexFailure != ERROR_SUCCESS)
    {
        int exitCode = HandleSingleInstanceMutexFailure(singleInstanceMutexFailure);
        if (exitCode != 0)
        {
            ShutdownGdiPlusForExit();
            return exitCode;
        }
    }

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
    Log(g_ui.runAtStartupSummary.c_str(), StateEnabled(IniReadI(L"Settings", L"RunAtStartup", 0) != 0));
    Log(g_ui.trimWorkingSetOnIdleSummary.c_str(), StateEnabled(g_trimWorkingSetOnIdle));
    Log(g_ui.idleTrimIntervalMsSummary.c_str(),
        LiveWallpaperOffDisplayText(IniReadClampedI(L"Settings", L"IdleTrimIntervalMs", 60000, 0, 3600000)).c_str());
    Log(g_ui.compactCrtHeapOnIdleSummary.c_str(), StateEnabled(g_compactCrtHeapOnIdle));
    Log(g_ui.keepGdiPlusLoadedSummary.c_str(), StateEnabled(g_keepGdiPlusLoaded));
    Log(g_ui.powerShellRegistrationSummary.c_str(), UsePowerShell() ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());
    Log(g_ui.liveTileUpdateSummary.c_str(), LiveTileUpdateModeSummaryText().c_str());
    Log(g_ui.liveTileTemplateStyleSummary.c_str(), LiveTileTemplateStyleSummaryText().c_str());
    Log(g_ui.liveTileBrandingSummary.c_str(), LiveTileBrandingSummaryText().c_str());
    Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());
    Log(g_ui.generatedAssetCacheSummary.c_str(),
        StateEnabled(GeneratedAssetCacheEnabled()),
        GeneratedAssetCacheMaxEntries());
    Log(g_ui.generatedAssetPrecacheSummary.c_str(),
        StateEnabled(GeneratedAssetPrecacheEnabled()),
        GeneratedAssetPrecacheMaxFiles());
    if (IniReadI(L"Settings", L"GeneratedAssetPrecache", 0) != 0 && !GeneratedAssetPrecacheEnabled())
        LogText(g_ui.generatedAssetPrecacheInactive);
    std::wstring tileTextSummary = TileTextPrimaryText();
    std::wstring tileTextSecondarySummary = TileTextSecondaryText();
    std::wstring tileTextBadgeSummary = TileTextBadgeText();
    if (!tileTextSecondarySummary.empty())
        tileTextSummary += (tileTextSummary.empty() ? L"" : L" / ") + tileTextSecondarySummary;
    if (!tileTextBadgeSummary.empty())
        tileTextSummary += std::wstring(tileTextSummary.empty() ? L"" : L" / ") + L"badge=" + tileTextBadgeSummary;
    Log(g_ui.tileTextSummary.c_str(),
        StateEnabled(IniReadI(L"TileText", L"Enabled", 0) != 0),
        tileTextSummary.c_str());
    Log(g_ui.comRegistrationHelperSummary.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0));
    Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    int liveWallpaperCaptureRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
    std::wstring liveWallpaperCaptureRefreshText = LiveWallpaperRefreshDisplayText(liveWallpaperCaptureRefreshMs);
    Log(g_ui.liveWallpaperCaptureSummary.c_str(),
        StateEnabled(IniReadI(L"Settings", L"LiveWallpaperCapture", 0) != 0),
        IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000),
        liveWallpaperCaptureRefreshText.c_str());
    int liveWallpaperCaptureStartupRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
    int liveWallpaperCaptureStartupRefreshDurationMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
    if (liveWallpaperCaptureStartupRefreshMs <= 0 || liveWallpaperCaptureStartupRefreshDurationMs <= 0)
    {
        LogText(g_ui.liveWallpaperCaptureStartupRefreshOffSummary);
    }
    else
    {
        Log(g_ui.liveWallpaperCaptureStartupRefreshSummary.c_str(),
            LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshMs).c_str(),
            LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshDurationMs).c_str());
    }
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
        ShutdownGdiPlusForExit();
        ReleaseSingleInstanceMutex();
        return startupManifestReady ? 0 : 1;
    }

    // Window
    WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=hi; wc.lpszClassName=WINDOW_CLASS_NAME;
    if (!RegisterClassW(&wc))
    {
        LogWin32Failure(L"RegisterClassW");
        ShutdownGdiPlusForExit();
        return 1;
    }
    g_hwnd = CreateWindowExW(0, WINDOW_CLASS_NAME, g_instanceWindowTitle.c_str(), 0, 0, 0, 0, 0, nullptr, nullptr, hi, nullptr);
    if (!g_hwnd)
    {
        LogWin32Failure(L"CreateWindowExW");
        ShutdownGdiPlusForExit();
        return 1;
    }
    g_shutdownWindow = g_hwnd;

    // Finish/roll back a proven interrupted gallery transaction before any
    // gallery provider or initial composed frame can observe the reset ledger.
    std::wstring galleryRecoveryError;
    if (!RecoverPendingGalleryTransaction(galleryRecoveryError))
        Log(L"[!] Gallery recovery: %ls", galleryRecoveryError.c_str());

    DeleteFileW(AppxActivationRequestPath().c_str());
    if (!SetTimer(g_hwnd, APPX_ACTIVATION_REQUEST_TIMER_ID, 750, nullptr))
        LogWin32Failure(L"SetTimer(APPX_ACTIVATION_REQUEST_TIMER_ID)");

    // Tray. One-shot command-line generation should not touch Explorer's
    // notification area; in practice Shell_NotifyIcon can block for several
    // seconds while Explorer is busy, which makes --once look hung even though
    // all useful work is logged to file.
    if (CommandLineShouldGenerateOnce())
    {
        g_tray = false;
    }
    else if (g_tray)
    {
        DWORD err = 0;
        if (!EnsureTrayIcon(&err))
        {
            LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
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

    // Existing pins need their own static logos after a program upgrade. This
    // refresh updates known destinations only and never opens a pin prompt.
    if (!g_shutdownState.IsRequested()) RequestRefreshExistingSecondaryTileLogos(g_hwnd);
    bool queuedLiveTileRuntimeRelaunch = g_commandLine.setupDesktop;
    if(g_commandLine.setupDesktop&&!g_shutdownState.IsRequested())RequestDesktopHostSetup(g_hwnd);
    if (!g_commandLine.mediaAction.empty()) {
        for (std::size_t index = 0; index < aip::content::TileActionCount; ++index)
            if (g_commandLine.mediaAction == aip::content::TileActions[index].id)
                PostMessageW(g_hwnd, g_singleInstanceMessage, SINGLE_INSTANCE_REQUEST_MEDIA_ACTION_BASE + index, 0);
    }
    if (!g_commandLine.tileSetupKey.empty()) {
        if (TileInstanceModeRequested()) {
            g_tileInstanceActivations.emplace_back(g_commandLine.tileSetupKey, g_commandLine.tileSignIn);
            PostMessageW(g_hwnd, WM_APP_TILE_INSTANCE_ACTIVATE, 0, 0);
        } else
        for (std::size_t index = 0; index < aip::content::SecondaryTileCount; ++index)
            if (g_commandLine.tileSetupKey == aip::content::SecondaryTiles[index].id)
                PostMessageW(g_hwnd, g_singleInstanceMessage, SINGLE_INSTANCE_REQUEST_TILE_SETUP_BASE + index |
                    (g_commandLine.tileSignIn ? SINGLE_INSTANCE_FLAG_TILE_SIGN_IN : 0), 0);
    }
    // Publish an initial composed frame before mode reconciliation can request
    // registration/relaunch. This starts asynchronous providers but does not
    // wait for them or queue a premature tile update. The poller starts later.
    if (!g_commandLine.setupDesktop&&!g_shutdownState.IsRequested() && CompositionRequested())
        ContentEngineTick(false, false);
    if (!g_commandLine.setupDesktop&&!g_shutdownState.IsRequested())
        queuedLiveTileRuntimeRelaunch = QueueLiveTileRuntimeConditionRelaunch(g_hwnd);

    if (!queuedLiveTileRuntimeRelaunch && CommandLineLiveTileModeChanged())
    {
        if (CommandLineShouldGenerateOnce())
            SetLiveTileModeReregistrationPending(true);
        else
            QueueLiveTileModeReregistration();
    }

    if (!queuedLiveTileRuntimeRelaunch && CommandLineShouldGenerateOnce())
    {
        bool contentReady = !CompositionRequested() || ContentEngineTick(true);
        std::wstring wallpaperOverride;
        CommandLineWallpaper(wallpaperOverride);
        std::wstring wp = CurrentGenerationBackground(wallpaperOverride);

        bool ok = false;
        if (contentReady && !wp.empty())
            ok = Generate(wp.c_str(), g_ui.commandLineOnceReason.c_str());
        else
            LogText(g_ui.startupGenerationSkippedNoWallpaper);

        StopContentProviders();
        JoinGeneratedAssetPrecache();
        TrayRemove();
        ShutdownGdiPlusForExit();
        ReleaseSingleInstanceMutex();
        return ok ? 0 : 1;
    }

    bool queuedPendingLiveTileModeReregistration = false;
    if (!g_shutdownState.IsRequested() && !queuedLiveTileRuntimeRelaunch && !g_desktopHostSetupRecovered && LiveTileModeReregistrationPending())
        queuedPendingLiveTileModeReregistration = QueueLiveTileModeReregistration();

    ContentSource contentSource = ConfiguredContentSource();
    Log(L"Content source: %ls", ContentSourceName(contentSource));

    if (!CompositionRequested())
    {
        bool forceStartupGeneration = CommandLineShouldForceStartupGeneration();
        bool liveTileNeedsStartupRestore = StartupLiveTileClearedStateNeedsGeneration();
        std::wstring commandLineWallpaper;
        bool hasCommandLineWallpaper = CommandLineWallpaper(commandLineWallpaper);
        if (!g_shutdownState.IsRequested() && !queuedLiveTileRuntimeRelaunch && !queuedPendingLiveTileModeReregistration &&
            (g_generateOnStartup || forceStartupGeneration || liveTileNeedsStartupRestore || hasCommandLineWallpaper))
        {
            std::wstring wp = hasCommandLineWallpaper ? commandLineWallpaper : GetWallpaper();
            if (!wp.empty())
            {
                if (!forceStartupGeneration && !liveTileNeedsStartupRestore && !hasCommandLineWallpaper && StartupGenerationCanSkip(wp))
                {
                    LogText(g_ui.startupGenerationSkippedUnchanged);
                }
                else
                {
                    LogText(liveTileNeedsStartupRestore
                        ? g_ui.startupGenerationRestoringClearedLiveTile
                        : g_ui.startupGenerationEnabled);
                    QueueGenerate(wp, hasCommandLineWallpaper ? g_ui.commandLineWallpaperReason :
                        (forceStartupGeneration ? g_ui.commandLineForceGenerateReason :
                            (liveTileNeedsStartupRestore ? g_ui.startupLiveTileRestoreReason : g_ui.startupGenerationReason)));
                }
            }
            else
            {
                LogText(g_ui.startupGenerationSkippedNoWallpaper);
            }
        }

        if (!GenerationWorkPending())
            TrimIdleWorkingSetIfConfigured();
    }
    else
    {
        if (!g_shutdownState.IsRequested() && !queuedLiveTileRuntimeRelaunch && !queuedPendingLiveTileModeReregistration)
        {
            ContentEngineTick();
        }
    }

    std::thread th;
    QueueDesktopStubPackagedStartupSync();
    // --no-monitor disables desktop wallpaper/fit/DPI observation. Contents
    // still need their data scheduler, including when enabled later in the tray.
    if (!g_shutdownState.IsRequested())
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

    MSG msg = {};
    int messageLoopExitCode = 0;
    for (;;)
    {
        BOOL messageResult = GetMessageW(&msg, nullptr, 0, 0);
        if (messageResult == 0)
        {
            break;
        }
        if (messageResult == -1)
        {
            LogWin32Failure(L"GetMessageW", GetLastError());
            messageLoopExitCode = 1;
            break;
        }
        if(IsGoogleCalendarSetupMessage(msg))continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ShutdownSecondaryTileUi();
    CloseEmbeddedNotesSignIns();
    CloseKuroSignIns();
    CloseGoogleCalendarSetups();
    ShutdownMediaTileActions();
    KillTimer(g_hwnd, APPX_ACTIVATION_REQUEST_TIMER_ID);
    g_running=false;
    WakePollThread();
    if (th.joinable()) th.join();
    StopDesktopStubPackagedStartup();
    StopContentProviders();
    JoinQueuedGenerations();
    JoinGeneratedAssetPrecache();

    MaybeClearLiveTileOnShutdown();

    TrayRemove();
    ShutdownGdiPlusForExit();
    ReleaseSingleInstanceMutex();
    ExecutePendingRelaunchAfterExit();
    return messageLoopExitCode;
}


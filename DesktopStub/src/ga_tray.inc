// -----------------------------------------------------------------------------
// Tray
// -----------------------------------------------------------------------------
enum {
    ID_EXIT=1001,

    // General
    ID_USE_PS,
    ID_TRAYICON,
    ID_CONSOLE,
    ID_LOG,
    ID_LIVE_TILE_MODE_AUTO,
    ID_LIVE_TILE_MODE_REGISTRATION,
    ID_LIVE_TILE_MODE_LIVE_TILE,
    ID_LOGFILE,
    ID_LOG_RENAME,
    ID_LOG_OPEN,
    ID_LOG_RESET,
    ID_GENERATE_NOW,
    ID_CANCEL_SHUTDOWN,
    ID_FORCE_SHUTDOWN,
    ID_GENERATE_STARTUP,
    ID_GENERATE_STARTUP_CHANGED_ONLY,
    ID_HIDE_DISABLED,
    ID_GENERATE_DESKTOP_ICON_DISABLED,
    ID_DELETE_DISABLED_ASSETS,
    ID_SHOW_MENU_DROPDOWN,
    ID_NOTIF_ENABLED,
    ID_NOTIFY_START,
    ID_NOTIFY_SUCCESS,
    ID_NOTIFY_FAILURE,
    ID_NOTIFY_BUSY,
    ID_NOTIFY_ALREADY_RUNNING,
    ID_NOTIFY_SINGLE_INSTANCE_FAILURE,
    ID_SHOW_PSLOG,
    ID_LISTEN_WP,
    ID_LISTEN_FIT,
    ID_DISABLE_FIT,
    ID_WP_METHOD_TRANSCODED_CACHE,
    ID_WP_METHOD_SYSTEM_PARAMETERS,
    ID_WP_METHOD_DESKTOP_WALLPAPER_COM,
    ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE,
    ID_WP_METHOD_AUTO,
    ID_WP_METHOD_FALLBACK,
    ID_SCALE_AUTO,
    ID_SCALE_100,
    ID_SCALE_125,
    ID_SCALE_150,
    ID_SCALE_200,
    ID_SCALE_400,
    ID_GENERATED_ASSET_CACHE,
    ID_GENERATED_ASSET_PRECACHE,
    ID_MANIFEST_REGENERATE,

    // Assets
    ID_A1=2001, ID_A2, ID_A3, ID_A4, ID_A5, ID_A6
};

static bool EnsureTrayIcon(DWORD* err = nullptr)
{
    if (err) *err = ERROR_SUCCESS;

    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd)
    {
        g_tray = true;
        return true;
    }

    if (!g_hwnd)
    {
        if (err) *err = ERROR_INVALID_WINDOW_HANDLE;
        return false;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize=sizeof(nid);
    nid.hWnd=g_hwnd; nid.uID=1;
    nid.uFlags= NIF_MESSAGE|NIF_ICON|NIF_TIP;
    nid.uCallbackMessage=WM_USER+1;
    HICON ic = LoadIconW(nullptr,IDI_APPLICATION);
    if (!ic)
    {
        if (err) *err = GetLastError();
        return false;
    }
    nid.hIcon=ic;
    CopyTruncated(nid.szTip, g_ui.trayTip);

    if (!Shell_NotifyIconW(NIM_ADD,&nid))
    {
        if (err) *err = GetLastError();
        return false;
    }

    g_nid = nid;
    g_tray = true;
    return true;
}

void TrayRemove()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    if (g_nid.hWnd){
        SetLastError(ERROR_SUCCESS);
        if (!Shell_NotifyIconW(NIM_DELETE,&g_nid))
        {
            DWORD err = GetLastError();
            if (err != ERROR_SUCCESS)
                LogWin32Failure(L"Shell_NotifyIconW(NIM_DELETE)", err);
        }
        g_nid = {};
    }
}

static bool TrayIconPresent()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    return g_nid.hWnd != nullptr;
}

static void ForgetTrayIconHandle()
{
    std::lock_guard<std::mutex> lk(g_trayMutex);
    g_nid = {};
}

void ShowPSLog(HWND h)
{
    std::wstring psOut;
    std::wstring psMsg;
    bool psErr = false;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        psOut = g_psOut;
        psMsg = g_psMsg;
        psErr = g_psErr;
    }
    psOut = FormatPowerShellOutput(psOut);
    psMsg = FormatPowerShellOutput(psMsg);

    if (psOut.empty() && !psMsg.empty())
        psOut = psMsg;
    else if (!psOut.empty() && !psMsg.empty() && psOut.find(psMsg) == std::wstring::npos)
        psOut = psMsg + L"\r\n\r\n" + psOut;

    if (psOut.empty()){
        MessageBoxW(h, g_ui.noPowerShellOutput.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    int maxChars = g_powerShellOutputMessageBoxChars.load();
    if (maxChars > 0 && psOut.size() > static_cast<size_t>(maxChars))
    {
        size_t hidden = psOut.size() - static_cast<size_t>(maxChars);
        psOut = psOut.substr(0, static_cast<size_t>(maxChars));
        psOut += L"\r\n\r\n";
        psOut += FormatWide(g_ui.powerShellOutputTruncated.c_str(), static_cast<int>(std::min<size_t>(hidden, INT_MAX)));
    }
    MessageBoxW(h, psOut.c_str(), g_ui.noPowerShellOutputTitle.c_str(), MB_OK | (psErr ? MB_ICONERROR : MB_ICONINFORMATION));
}

INT_PTR CALLBACK RenameDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) noexcept
{
    static std::wstring* pPath = nullptr;

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        pPath = (std::wstring*)lParam;
        SetDlgItemTextW(hDlg, 1001, pPath->c_str()); // edit box
        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDOK:
        {
            HWND edit = GetDlgItem(hDlg, 1001);
            int len = GetWindowTextLengthW(edit);
            std::vector<wchar_t> buf((size_t)len + 1);
            GetWindowTextW(edit, buf.data(), (int)buf.size());

            if (buf[0])
            {
                *pPath = buf.data();
                EndDialog(hDlg, IDOK);
            }
            else
            {
                MessageBoxW(hDlg, g_ui.pathCannotBeEmpty.c_str(), g_ui.errorTitle.c_str(), MB_OK | MB_ICONWARNING);
            }
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}

static bool AppendMenuChecked(HMENU menu, UINT flags, UINT_PTR id, const wchar_t* text)
{
    if (!menu)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        LogWin32Failure(L"AppendMenuW");
        return false;
    }

    if (!AppendMenuW(menu, flags, id, text))
    {
        LogWin32Failure(L"AppendMenuW");
        return false;
    }

    return true;
}

void Menu(HWND h)
{
    ReloadRuntimeSettingsFromIni();

    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    if (!m)
    {
        LogWin32Failure(L"CreatePopupMenu(root)");
        return;
    }
    bool showMenuAsDropdown = g_showMenuAsDropdown;

    auto addTo = [&](HMENU target, UINT id, const std::wstring& t, bool chk=false, bool en=true){
        if (g_hideDisabled && !en)
            return;
        UINT flags = MF_STRING;
        if (chk)
            flags |= MF_CHECKED;
        if (!en)
            flags |= MF_DISABLED;
        AppendMenuChecked(target, flags, id, t.c_str());
    };

    if (g_shutdownRequested)
    {
        if (!GenerationWorkPending())
        {
            DestroyMenu(m);
            PostQuitMessage(0);
            return;
        }

        std::wstring reason = FormatWide(g_ui.shutdownPendingReason.c_str(), CurrentGenerationReason().c_str());
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, g_ui.shutdownPendingTitle.c_str());
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, reason.c_str());
        AppendMenuChecked(m, MF_STRING, ID_CANCEL_SHUTDOWN, g_ui.cancelShutdown.c_str());
        AppendMenuChecked(m, MF_STRING, ID_FORCE_SHUTDOWN, g_ui.forceShutdown.c_str());

        SetForegroundWindow(h);
        UINT cmd = static_cast<UINT>(TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr));
        DestroyMenu(m);

        if (cmd == ID_CANCEL_SHUTDOWN)
            CancelRequestedShutdown(h);
        else if (cmd == ID_FORCE_SHUTDOWN)
            ForceShutdownNow();
        return;
    }

    auto menuTitle = [](const std::wstring& text) {
        std::wstring title = TrimCopy(text);
        while (!title.empty() && (title.back() == L':' || iswspace(title.back())))
            title.pop_back();
        return title.empty() ? text : title;
    };

    auto beginSection = [&](const std::wstring& title) -> HMENU {
        if (showMenuAsDropdown)
        {
            HMENU submenu = CreatePopupMenu();
            if (!submenu)
            {
                LogWin32Failure(L"CreatePopupMenu(section)");
                return m;
            }
            return submenu;
        }
        AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, title.c_str());
        return m;
    };

    auto endSection = [&](HMENU target, const std::wstring& title) {
        if (showMenuAsDropdown)
        {
            if (!target || target == m)
                return;
            std::wstring popupTitle = menuTitle(title);
            if (!AppendMenuChecked(m, MF_POPUP, (UINT_PTR)target, popupTitle.c_str()))
                DestroyMenu(target);
        }
        else
        {
            AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);
        }
    };

    addTo(m, ID_SHOW_MENU_DROPDOWN, g_ui.showMenuAsDropdown.c_str(), showMenuAsDropdown);
    AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);

    HMENU generalMenu = beginSection(g_ui.generalTitle);
    addTo(generalMenu, ID_LISTEN_WP, g_ui.listenWallpaper.c_str(), g_listenWallpaper);
    addTo(generalMenu, ID_TRAYICON, g_ui.trayIcon.c_str(), g_tray);
    addTo(generalMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
    {
        LiveTileUpdateMode liveTileMode = ConfiguredLiveTileUpdateMode();
        addTo(generalMenu, ID_LIVE_TILE_MODE_AUTO, g_ui.experimentalLiveTileUpdate + L": " + g_ui.liveTileModeAuto, liveTileMode == LiveTileUpdateMode::Auto);
        addTo(generalMenu, ID_LIVE_TILE_MODE_REGISTRATION, g_ui.experimentalLiveTileUpdate + L": " + g_ui.liveTileModeRegistration, liveTileMode == LiveTileUpdateMode::Registration);
        addTo(generalMenu, ID_LIVE_TILE_MODE_LIVE_TILE, g_ui.experimentalLiveTileUpdate + L": " + g_ui.liveTileModeLiveTile, liveTileMode == LiveTileUpdateMode::LiveTile);
    }
    addTo(generalMenu, ID_GENERATE_STARTUP, g_ui.generateOnStartup.c_str(), g_generateOnStartup);
    addTo(generalMenu, ID_GENERATE_STARTUP_CHANGED_ONLY, g_ui.generateOnStartupOnlyWhenChanged.c_str(), g_generateOnStartupOnlyWhenChanged, g_generateOnStartup);
    addTo(generalMenu, ID_HIDE_DISABLED, g_ui.hideDisabledEntries.c_str(), g_hideDisabled);
    addTo(generalMenu, ID_CONSOLE, g_ui.showConsole.c_str(), g_console);
    addTo(generalMenu, ID_GENERATE_NOW, g_ui.generateNow.c_str());
    endSection(generalMenu, g_ui.generalTitle);

    HMENU notificationsMenu = beginSection(g_ui.notificationsTitle);
    addTo(notificationsMenu, ID_NOTIF_ENABLED, g_ui.notificationsEnabled.c_str(), g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_START, g_ui.notifyOnStart.c_str(), g_notifyOnStart, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SUCCESS, g_ui.notifyOnSuccess.c_str(), g_notifyOnSuccess, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_FAILURE, g_ui.notifyOnFailure.c_str(), g_notifyOnFailure, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_BUSY, g_ui.notifyOnBusy.c_str(), g_notifyOnBusy, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_ALREADY_RUNNING, g_ui.notifyOnAlreadyRunning.c_str(), g_notifyOnAlreadyRunning, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SINGLE_INSTANCE_FAILURE, g_ui.notifyOnSingleInstanceFailure.c_str(), g_notifyOnSingleInstanceFailure, g_notificationsEnabled);
    endSection(notificationsMenu, g_ui.notificationsTitle);

    HMENU loggingMenu = beginSection(g_ui.loggingTitle);
    addTo(loggingMenu, ID_LOG, g_ui.loggingEnable.c_str(), g_logging);
    addTo(loggingMenu, ID_LOGFILE, g_ui.changePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RENAME, g_ui.renamePath.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_OPEN, g_ui.openInExplorer.c_str(), false, g_logging);
    addTo(loggingMenu, ID_LOG_RESET, g_ui.resetDefault.c_str(), false, g_logging);
    addTo(loggingMenu, ID_SHOW_PSLOG, g_ui.showPowerShellLog.c_str());

    std::wstring pathLine = g_ui.logPathPrefix + GetLogPathCopy();
    HMENU hPath = CreatePopupMenu();
    if (!hPath)
        LogWin32Failure(L"CreatePopupMenu(log path)");
    else
    {
        AppendMenuChecked(hPath, MF_STRING | MF_DISABLED, 0, pathLine.c_str());
        if (!AppendMenuChecked(loggingMenu, MF_POPUP, (UINT_PTR)hPath, g_ui.logPathTitle.c_str()))
            DestroyMenu(hPath);
    }

    std::wstring psLine = g_ui.psStatusPrefix;
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        if (!g_psMsg.empty())
            psLine += g_psMsg;
        else if (g_psErr)
            psLine += g_ui.errorTitle.c_str();
        else
            psLine += g_ui.okText.c_str();
    }

    HMENU hPs = CreatePopupMenu();
    if (!hPs)
        LogWin32Failure(L"CreatePopupMenu(PowerShell status)");
    else
    {
        AppendMenuChecked(hPs, MF_STRING | MF_DISABLED, 0, psLine.c_str());
        if (!AppendMenuChecked(loggingMenu, MF_POPUP, (UINT_PTR)hPs, g_ui.psStatusTitle.c_str()))
            DestroyMenu(hPs);
    }
    endSection(loggingMenu, g_ui.loggingTitle);

    HMENU fittingMenu = beginSection(g_ui.wallpaperFittingTitle);
    addTo(fittingMenu, ID_DISABLE_FIT, g_ui.disableFitting.c_str(), g_disableFitting);
    addTo(fittingMenu, ID_LISTEN_FIT, g_ui.listenFit.c_str(), g_listenFit, !g_disableFitting);
    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    std::wstring fitLine;
    if (g_disableFitting)
        fitLine = g_ui.fitModeForced;
    else {
        fitLine = g_ui.fitModePrefix;
        fitLine += FitModeToString(mode);
    }
    AppendMenuChecked(fittingMenu, MF_STRING | MF_DISABLED, 0, fitLine.c_str());
    endSection(fittingMenu, g_ui.wallpaperFittingTitle);

    HMENU methodsMenu = beginSection(g_ui.methodsTitle);
    WallpaperDetectionMethod wallpaperMethod = CurrentWallpaperDetectionMethod();
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_CACHE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedImageCache), wallpaperMethod == WallpaperDetectionMethod::TranscodedImageCache);
    addTo(methodsMenu, ID_WP_METHOD_SYSTEM_PARAMETERS, WallpaperDetectionMethodName(WallpaperDetectionMethod::SystemParametersInfo), wallpaperMethod == WallpaperDetectionMethod::SystemParametersInfo);
    addTo(methodsMenu, ID_WP_METHOD_DESKTOP_WALLPAPER_COM, WallpaperDetectionMethodName(WallpaperDetectionMethod::DesktopWallpaperCOM), wallpaperMethod == WallpaperDetectionMethod::DesktopWallpaperCOM);
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedWallpaperFile), wallpaperMethod == WallpaperDetectionMethod::TranscodedWallpaperFile);
    addTo(methodsMenu, ID_WP_METHOD_AUTO, WallpaperDetectionMethodName(WallpaperDetectionMethod::Auto), wallpaperMethod == WallpaperDetectionMethod::Auto);
    addTo(methodsMenu, ID_WP_METHOD_FALLBACK, g_ui.wallpaperDetectionFallback.c_str(), g_wallpaperDetectionFallbackOnInvalid);
    std::wstring wallpaperDetectionLine = FormatWide(g_ui.wallpaperDetectionMethodLabel.c_str(), WallpaperDetectionMethodName(wallpaperMethod));
    AppendMenuChecked(methodsMenu, MF_STRING | MF_DISABLED, 0, wallpaperDetectionLine.c_str());
    endSection(methodsMenu, g_ui.methodsTitle);

    HMENU assetsMenu = beginSection(g_ui.assetsTitle);
    addTo(assetsMenu, ID_GENERATE_DESKTOP_ICON_DISABLED, g_ui.generateDesktopIconForDisabledEntries.c_str(), g_generateDesktopIconForDisabledEntries);
    addTo(assetsMenu, ID_DELETE_DISABLED_ASSETS, g_ui.deleteDisabledAssets.c_str(), g_deleteDisabledAssets, !g_generateDesktopIconForDisabledEntries);
    const wchar_t* keys[] = {
        L"StoreLogo",L"MediumTile",L"Square44x44Logo",
        L"SmallTile",L"WideTile",L"LargeTile"
    };
    for (int i=0;i<6;i++)
        addTo(assetsMenu, static_cast<UINT>(ID_A1 + i), keys[i], IniReadI(L"Assets",keys[i],0)!=0);
    endSection(assetsMenu, g_ui.assetsTitle);

    HMENU scaleMenu = beginSection(g_ui.dpiScalesTitle);
    addTo(scaleMenu, ID_SCALE_AUTO, g_ui.generateScaleAuto.c_str(), g_generateScaleAuto);
    // Manual scale items are greyed out while Auto is active (they are ignored by GetConfiguredScales)
    bool manualScalesEnabled = !g_generateScaleAuto;
    addTo(scaleMenu, ID_SCALE_100, g_ui.generateScale100.c_str(), g_generateScale100, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_125, g_ui.generateScale125.c_str(), g_generateScale125, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_150, g_ui.generateScale150.c_str(), g_generateScale150, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_200, g_ui.generateScale200.c_str(), g_generateScale200, manualScalesEnabled);
    addTo(scaleMenu, ID_SCALE_400, g_ui.generateScale400.c_str(), g_generateScale400, manualScalesEnabled);
    std::wstring currentDpiLine = FormatWide(g_ui.currentDpiScaleLabel.c_str(), CurrentDpiPercent(), CurrentAssetScale());
    AppendMenuChecked(scaleMenu, MF_STRING | MF_DISABLED, 0, currentDpiLine.c_str());
    endSection(scaleMenu, g_ui.dpiScalesTitle);

    HMENU cachingMenu = beginSection(g_ui.cachingTitle);
    bool cacheEnabled = GeneratedAssetCacheEnabled();
    bool cacheCanStore = cacheEnabled && GeneratedAssetCacheMaxEntries() > 0;
    bool precacheConfigured = IniReadI(L"Settings", L"GeneratedAssetPrecache", 1) != 0;
    addTo(cachingMenu, ID_GENERATED_ASSET_CACHE, g_ui.generatedAssetCache.c_str(), cacheEnabled);
    addTo(cachingMenu, ID_GENERATED_ASSET_PRECACHE, g_ui.generatedAssetPrecache.c_str(), precacheConfigured, cacheCanStore);
    std::wstring cacheMaxEntriesLine = FormatWide(g_ui.generatedAssetCacheMaxEntriesLabel.c_str(), GeneratedAssetCacheMaxEntries());
    std::wstring precacheMaxFilesLine = FormatWide(g_ui.generatedAssetPrecacheMaxFilesLabel.c_str(), GeneratedAssetPrecacheMaxFiles());
    AppendMenuChecked(cachingMenu, MF_STRING | MF_DISABLED, 0, cacheMaxEntriesLine.c_str());
    AppendMenuChecked(cachingMenu, MF_STRING | MF_DISABLED, 0, precacheMaxFilesLine.c_str());
    endSection(cachingMenu, g_ui.cachingTitle);

    HMENU manifestMenu = beginSection(g_ui.manifestTitle);
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring manifestSourceLine = FormatWide(g_ui.manifestSourceLabel.c_str(), manifestInfo.existing ? g_ui.manifestSourceExisting.c_str() : g_ui.manifestSourceGenerated.c_str());
    std::wstring manifestIdentityLine = FormatWide(g_ui.manifestIdentityNameLabel.c_str(), manifestInfo.identityName.c_str());
    std::wstring manifestPublisherLine = FormatWide(g_ui.manifestPublisherLabel.c_str(), manifestInfo.publisher.c_str());
    std::wstring manifestPackageVersionLine = FormatWide(g_ui.manifestPackageVersionLabel.c_str(), manifestInfo.packageVersion.c_str());
    std::wstring manifestDisplayNameLine = FormatWide(g_ui.manifestDisplayNameLabel.c_str(), manifestInfo.displayName.c_str());
    std::wstring manifestPublisherDisplayNameLine = FormatWide(g_ui.manifestPublisherDisplayNameLabel.c_str(), manifestInfo.publisherDisplayName.c_str());
    std::wstring manifestDescriptionLine = FormatWide(g_ui.manifestDescriptionLabel.c_str(), manifestInfo.description.c_str());
    std::wstring manifestExecutableLine = FormatWide(g_ui.manifestExecutableLabel.c_str(), manifestInfo.executable.c_str());
    std::wstring manifestApplicationIdLine = FormatWide(g_ui.manifestApplicationIdLabel.c_str(), manifestInfo.applicationId.c_str());
    std::wstring manifestTargetDeviceFamilyNameLine = FormatWide(g_ui.manifestTargetDeviceFamilyNameLabel.c_str(), manifestInfo.targetDeviceFamilyName.c_str());
    std::wstring manifestMinVersionLine = FormatWide(g_ui.manifestMinVersionLabel.c_str(), manifestInfo.minVersion.c_str());
    std::wstring manifestMaxVersionTestedLine = FormatWide(g_ui.manifestMaxVersionTestedLabel.c_str(), manifestInfo.maxVersionTested.c_str());
    std::wstring manifestResourceLanguageLine = FormatWide(g_ui.manifestResourceLanguageLabel.c_str(), manifestInfo.resourceLanguage.c_str());
    std::wstring manifestPropertiesLogoLine = FormatWide(g_ui.manifestPropertiesLogoLabel.c_str(), manifestInfo.propertiesLogo.c_str());
    std::wstring manifestSquare150x150LogoLine = FormatWide(g_ui.manifestSquare150x150LogoLabel.c_str(), manifestInfo.square150x150Logo.c_str());
    std::wstring manifestSquare44x44LogoLine = FormatWide(g_ui.manifestSquare44x44LogoLabel.c_str(), manifestInfo.square44x44Logo.c_str());
    std::wstring manifestSquare71x71LogoLine = FormatWide(g_ui.manifestSquare71x71LogoLabel.c_str(), manifestInfo.square71x71Logo.c_str());
    std::wstring manifestWide310x150LogoLine = FormatWide(g_ui.manifestWide310x150LogoLabel.c_str(), manifestInfo.wide310x150Logo.c_str());
    std::wstring manifestSquare310x310LogoLine = FormatWide(g_ui.manifestSquare310x310LogoLabel.c_str(), manifestInfo.square310x310Logo.c_str());
    std::wstring manifestBackgroundColorLine = FormatWide(g_ui.manifestBackgroundColorLabel.c_str(), manifestInfo.backgroundColor.c_str());
    std::wstring manifestShowNameOnTilesLine = FormatWide(g_ui.manifestShowNameOnTilesLabel.c_str(), manifestInfo.showNameOnTiles.c_str());
    std::wstring manifestEntryPointLine = FormatWide(g_ui.manifestEntryPointLabel.c_str(), manifestInfo.entryPoint.c_str());
    std::wstring manifestRestrictedCapabilityLine = FormatWide(g_ui.manifestRestrictedCapabilityLabel.c_str(), manifestInfo.restrictedCapability.c_str());
    addTo(manifestMenu, ID_MANIFEST_REGENERATE, g_ui.manifestRegenerateNow.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestSourceLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestIdentityLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestPackageVersionLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestDisplayNameLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestPublisherDisplayNameLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestDescriptionLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestExecutableLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestApplicationIdLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestTargetDeviceFamilyNameLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestMinVersionLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestMaxVersionTestedLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestResourceLanguageLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestPropertiesLogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestSquare150x150LogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestSquare44x44LogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestSquare71x71LogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestWide310x150LogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestSquare310x310LogoLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestBackgroundColorLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestShowNameOnTilesLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestEntryPointLine.c_str());
    AppendMenuChecked(manifestMenu, MF_STRING | MF_DISABLED, 0, manifestRestrictedCapabilityLine.c_str());
    endSection(manifestMenu, g_ui.manifestTitle);

    HMENU advancedMenu = beginSection(g_ui.advancedTitle);
    std::wstring pollLine = FormatWide(g_ui.pollIntervalLabel.c_str(), IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000));
    std::wstring confirmLine = FormatWide(g_ui.confirmMsLabel.c_str(), IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000));
    std::wstring debounceLine = FormatWide(g_ui.debounceMinMsLabel.c_str(), IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000));
    std::wstring pollInitialBypassLine = FormatWide(g_ui.pollInitialDebounceBypassMsLabel.c_str(), IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000));
    std::wstring iniCacheRefreshLine = FormatWide(g_ui.iniCacheRefreshMsLabel.c_str(), IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000));
    std::wstring wallpaperDetectionLogRepeatLine = FormatWide(g_ui.wallpaperDetectionLogRepeatMsLabel.c_str(), IniReadClampedI(L"Settings", L"WallpaperDetectionLogRepeatMs", 60000, 0, 3600000));
    std::wstring notificationTimeoutLine = FormatWide(g_ui.notificationTimeoutMsLabel.c_str(), IniReadClampedI(L"Settings", L"NotificationTimeoutMs", 4000, 1000, 60000));
    std::wstring shutdownInitialLine = FormatWide(g_ui.shutdownInitialNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownInitialNoticeMs", 1000, 250, 60000));
    std::wstring shutdownRepeatLine = FormatWide(g_ui.shutdownRepeatNoticeMsLabel.c_str(), IniReadClampedI(L"Settings", L"ShutdownRepeatNoticeMs", 5000, 250, 60000));
    std::wstring signalRetriesLine = FormatWide(g_ui.singleInstanceSignalRetriesLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalRetries", 50, 0, 200));
    std::wstring signalDelayLine = FormatWide(g_ui.singleInstanceSignalDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"SingleInstanceSignalDelayMs", 100, 0, 5000));
    int comRegistrationTimeoutMs = IniReadClampedI(L"Settings", L"ComRegistrationTimeoutMs", 120000, 0, 600000);
    int powerShellTimeoutMs = IniReadClampedI(L"Settings", L"PowerShellTimeoutMs", 120000, 0, 600000);
    std::wstring comRegistrationTimeoutText = TimeoutDisplayText(comRegistrationTimeoutMs);
    std::wstring powerShellTimeoutText = TimeoutDisplayText(powerShellTimeoutMs);
    std::wstring comRegistrationTimeoutLine = FormatWide(g_ui.comRegistrationTimeoutMsLabel.c_str(), comRegistrationTimeoutText.c_str());
    std::wstring comRegistrationCancelWaitLine = FormatWide(g_ui.comRegistrationCancelWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"ComRegistrationCancelWaitMs", 5000, 0, 60000));
    std::wstring comRegistrationHelperLine = FormatWide(g_ui.comRegistrationHelperLabel.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 1) != 0));
    std::wstring trayHideDelayLine = FormatWide(g_ui.trayIconHiddenDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000));
    std::wstring logBufferLine = FormatWide(g_ui.logBufferLinesLabel.c_str(), IniReadClampedI(L"Settings", L"LogBufferLines", 1024, 1, 100000));
    std::wstring logAppendLockWaitLine = FormatWide(g_ui.logAppendLockWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"LogAppendLockWaitMs", 1000, 0, 60000));
    std::wstring powerShellExeLine = FormatWide(g_ui.powerShellExeLabel.c_str(), ReadPowerShellExe().c_str());
    std::wstring powerShellPollLine = FormatWide(g_ui.powerShellPollMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellPollMs", 50, 10, 1000));
    std::wstring powerShellTimeoutLine = FormatWide(g_ui.powerShellTimeoutMsLabel.c_str(), powerShellTimeoutText.c_str());
    std::wstring powerShellTerminateWaitLine = FormatWide(g_ui.powerShellTerminateWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellTerminateWaitMs", 5000, 0, 60000));
    std::wstring powerShellOutputLimitLine = FormatWide(g_ui.powerShellOutputMessageBoxCharsLabel.c_str(), IniReadClampedI(L"Settings", L"PowerShellOutputMessageBoxChars", 6000, 0, 60000));
    std::wstring registrationUnresolvedBlockLine = FormatWide(g_ui.registrationUnresolvedBlockMsLabel.c_str(), IniReadClampedI(L"Settings", L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000));
    std::wstring savePngReplaceRetriesLine = FormatWide(g_ui.savePngReplaceRetriesLabel.c_str(), IniReadClampedI(L"Settings", L"SavePngReplaceRetries", 3, 0, 20));
    std::wstring savePngReplaceRetryDelayLine = FormatWide(g_ui.savePngReplaceRetryDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"SavePngReplaceRetryDelayMs", 75, 0, 5000));
    std::wstring singleInstanceLine = FormatWide(g_ui.singleInstanceFailureActionLabel.c_str(), ErrorActionName(CurrentSingleInstanceFailureAction()));
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, pollLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, confirmLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, debounceLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, pollInitialBypassLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, iniCacheRefreshLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, wallpaperDetectionLogRepeatLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, notificationTimeoutLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, shutdownInitialLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, shutdownRepeatLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, signalRetriesLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, signalDelayLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, comRegistrationTimeoutLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, comRegistrationCancelWaitLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, comRegistrationHelperLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, trayHideDelayLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, logBufferLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, logAppendLockWaitLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellExeLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellPollLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellTimeoutLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellTerminateWaitLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, powerShellOutputLimitLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, registrationUnresolvedBlockLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, savePngReplaceRetriesLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, savePngReplaceRetryDelayLine.c_str());
    AppendMenuChecked(advancedMenu, MF_STRING | MF_DISABLED, 0, singleInstanceLine.c_str());
    endSection(advancedMenu, g_ui.advancedTitle);

    if (showMenuAsDropdown)
        AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);
    addTo(m, ID_EXIT, g_ui.exitText.c_str());

    SetForegroundWindow(h);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr));
    DestroyMenu(m);

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0");
        Log(g_ui.usePowerShellSummary.c_str(), v, v ? g_ui.powerShellEnabledMode.c_str() : g_ui.comPreferredMode.c_str());
    }
    break;

    case ID_LIVE_TILE_MODE_AUTO:
    case ID_LIVE_TILE_MODE_REGISTRATION:
    case ID_LIVE_TILE_MODE_LIVE_TILE:
    {
        LiveTileUpdateMode newLiveTileMode = LiveTileUpdateMode::Auto;
        if (cmd == ID_LIVE_TILE_MODE_REGISTRATION)
            newLiveTileMode = LiveTileUpdateMode::Registration;
        else if (cmd == ID_LIVE_TILE_MODE_LIVE_TILE)
            newLiveTileMode = LiveTileUpdateMode::LiveTile;

        if (ConfiguredLiveTileUpdateMode() != newLiveTileMode)
        {
            g_liveTileUpdateMode = static_cast<int>(newLiveTileMode);
            IniWrite(L"Settings", L"ExperimentalLiveTileUpdate", LiveTileUpdateModeIniValue(newLiveTileMode).c_str());
            Log(g_ui.liveTileUpdateSummary.c_str(), LiveTileUpdateModeSummaryText().c_str());
            QueueLiveTileModeReregistration();
        }
    }
    break;

    case ID_MANIFEST_REGENERATE:
    {
        if (!RegenerateAppxManifestFromConfigAndLog())
            break;

        std::wstring wp = GetWallpaper();
        if (wp.empty())
        {
            MessageBoxW(h,
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
            break;
        }

        QueueGenerate(wp, g_ui.manifestRegenerateNow);
    }
    break;

    case ID_TRAYICON:
    {
        g_tray = !g_tray;

        if (!g_tray)
        {
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationTrayIconHidden, NIIF_WARNING, true);
            Sleep(static_cast<DWORD>(ClampInt(g_trayIconHiddenDelayMs.load(), 0, 10000)));
            TrayRemove();
        }
        else
        {
            DWORD err = 0;
            if (!EnsureTrayIcon(&err))
            {
                LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
                g_tray = false;
            }
        }

        Log(g_ui.trayIconSummary.c_str(), StateEnabled(g_tray));
    }
    break;

    case ID_CONSOLE:
    {
        ApplyConsoleSetting(!g_console, true, true);
    }
    break;

    case ID_LOG:
    {
        g_logging = !g_logging;
        IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0");
        Log(g_ui.loggingSummary.c_str(), StateOn(g_logging));
    }
    break;

    case ID_GENERATE_NOW:
    {
        std::wstring wp = GetWallpaper();

        if (wp.empty())
        {
            MessageBoxW(h,
                g_ui.currentWallpaperNotFound.c_str(),
                g_ui.generateNowTitle.c_str(),
                MB_OK | MB_ICONWARNING);
        }
        else
        {
            LogText(g_ui.manualGenerationRequested);
            QueueGenerate(wp, g_ui.manualGenerateTriggered);
        }
    }
    break;

    case ID_GENERATE_STARTUP:
    {
        g_generateOnStartup = !g_generateOnStartup;
        IniWrite(L"Settings", L"GenerateOnStartup", g_generateOnStartup ? L"1" : L"0");
        Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    }
    break;

    case ID_GENERATE_STARTUP_CHANGED_ONLY:
    {
        if (!g_generateOnStartup)
            break;
        g_generateOnStartupOnlyWhenChanged = !g_generateOnStartupOnlyWhenChanged;
        IniWrite(L"Settings", L"GenerateOnStartupOnlyWhenChanged", g_generateOnStartupOnlyWhenChanged ? L"1" : L"0");
        Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
    }
    break;

    case ID_HIDE_DISABLED:
    {
        g_hideDisabled = !g_hideDisabled;
        IniWrite(L"Settings", L"HideDisabledEntries", g_hideDisabled ? L"1" : L"0");
        Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    }
    break;

    case ID_GENERATE_DESKTOP_ICON_DISABLED:
    {
        g_generateDesktopIconForDisabledEntries = !g_generateDesktopIconForDisabledEntries;
        IniWrite(L"Settings", L"GenerateDesktopIconForDisabledEntries", g_generateDesktopIconForDisabledEntries ? L"1" : L"0");
        Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    }
    break;

    case ID_DELETE_DISABLED_ASSETS:
    {
        if (g_generateDesktopIconForDisabledEntries)
            break;
        g_deleteDisabledAssets = !g_deleteDisabledAssets;
        IniWrite(L"Settings", L"DeleteDisabledAssets", g_deleteDisabledAssets ? L"1" : L"0");
        Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
    }
    break;

    case ID_SCALE_AUTO:
    {
        g_generateScaleAuto = !g_generateScaleAuto;
        IniWrite(L"Settings", L"GenerateScaleAuto", g_generateScaleAuto ? L"1" : L"0");
        Log(g_ui.generateScaleAutoSummary.c_str(), StateEnabled(g_generateScaleAuto));
    }
    break;

    case ID_SCALE_100:
    case ID_SCALE_125:
    case ID_SCALE_150:
    case ID_SCALE_200:
    case ID_SCALE_400:
    {
        int scale = 100;
        std::atomic<bool>* setting = &g_generateScale100;
        const wchar_t* key = L"GenerateScale100";

        if (cmd == ID_SCALE_125) { scale = 125; setting = &g_generateScale125; key = L"GenerateScale125"; }
        else if (cmd == ID_SCALE_150) { scale = 150; setting = &g_generateScale150; key = L"GenerateScale150"; }
        else if (cmd == ID_SCALE_200) { scale = 200; setting = &g_generateScale200; key = L"GenerateScale200"; }
        else if (cmd == ID_SCALE_400) { scale = 400; setting = &g_generateScale400; key = L"GenerateScale400"; }

        bool value = !setting->load();
        *setting = value;
        IniWrite(L"Settings", key, value ? L"1" : L"0");
        Log(g_ui.generateScaleSummary.c_str(), scale, StateEnabled(value));
    }
    break;

    case ID_GENERATED_ASSET_CACHE:
    {
        bool value = !GeneratedAssetCacheEnabled();
        IniWrite(L"Settings", L"GeneratedAssetCache", value ? L"1" : L"0");
        Log(g_ui.generatedAssetCacheSummary.c_str(), StateEnabled(value), GeneratedAssetCacheMaxEntries());
    }
    break;

    case ID_GENERATED_ASSET_PRECACHE:
    {
        if (!GeneratedAssetCacheEnabled() || GeneratedAssetCacheMaxEntries() <= 0)
            break;

        bool value = IniReadI(L"Settings", L"GeneratedAssetPrecache", 1) == 0;
        IniWrite(L"Settings", L"GeneratedAssetPrecache", value ? L"1" : L"0");
        Log(g_ui.generatedAssetPrecacheSummary.c_str(), StateEnabled(value), GeneratedAssetPrecacheMaxFiles());
    }
    break;

    case ID_SHOW_MENU_DROPDOWN:
    {
        g_showMenuAsDropdown = !g_showMenuAsDropdown;
        IniWrite(L"Settings", L"ShowMenuAsDropdown", g_showMenuAsDropdown ? L"1" : L"0");
        Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    }
    break;

    case ID_NOTIF_ENABLED:
    {
        g_notificationsEnabled = !g_notificationsEnabled;
        IniWrite(L"Settings", L"NotificationsEnabled", g_notificationsEnabled ? L"1" : L"0");
        Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    }
    break;

    case ID_NOTIFY_START:
    {
        g_notifyOnStart = !g_notifyOnStart;
        IniWrite(L"Settings", L"NotifyOnStart", g_notifyOnStart ? L"1" : L"0");
        Log(g_ui.notifyOnStartState.c_str(), (int)g_notifyOnStart);
    }
    break;

    case ID_NOTIFY_SUCCESS:
    {
        g_notifyOnSuccess = !g_notifyOnSuccess;
        IniWrite(L"Settings", L"NotifyOnSuccess", g_notifyOnSuccess ? L"1" : L"0");
        Log(g_ui.notifyOnSuccessState.c_str(), (int)g_notifyOnSuccess);
    }
    break;

    case ID_NOTIFY_FAILURE:
    {
        g_notifyOnFailure = !g_notifyOnFailure;
        IniWrite(L"Settings", L"NotifyOnFailure", g_notifyOnFailure ? L"1" : L"0");
        Log(g_ui.notifyOnFailureState.c_str(), (int)g_notifyOnFailure);
    }
    break;

    case ID_NOTIFY_BUSY:
    {
        g_notifyOnBusy = !g_notifyOnBusy;
        IniWrite(L"Settings", L"NotifyOnBusy", g_notifyOnBusy ? L"1" : L"0");
        Log(g_ui.notifyOnBusyState.c_str(), (int)g_notifyOnBusy);
    }
    break;

    case ID_NOTIFY_ALREADY_RUNNING:
    {
        g_notifyOnAlreadyRunning = !g_notifyOnAlreadyRunning;
        IniWrite(L"Settings", L"NotifyOnAlreadyRunning", g_notifyOnAlreadyRunning ? L"1" : L"0");
        Log(g_ui.notifyOnAlreadyRunningState.c_str(), (int)g_notifyOnAlreadyRunning);
    }
    break;

    case ID_NOTIFY_SINGLE_INSTANCE_FAILURE:
    {
        g_notifyOnSingleInstanceFailure = !g_notifyOnSingleInstanceFailure;
        IniWrite(L"Settings", L"NotifyOnSingleInstanceFailure", g_notifyOnSingleInstanceFailure ? L"1" : L"0");
        Log(g_ui.notifyOnSingleInstanceFailureState.c_str(), (int)g_notifyOnSingleInstanceFailure);
    }
    break;

    case ID_SHOW_PSLOG:
        ShowPSLog(h);
        break;

    case ID_EXIT:
        RequestGracefulShutdown(h);
        break;

    case ID_LISTEN_WP:
    {
        g_listenWallpaper = !g_listenWallpaper;
        IniWrite(L"Settings", L"ListenWallpaper", g_listenWallpaper ? L"1" : L"0");
        Log(g_ui.listenWallpaperState.c_str(), (int)g_listenWallpaper);
    }
    break;

    case ID_LISTEN_FIT:
    {
        g_listenFit = !g_listenFit;
        IniWrite(L"Settings", L"ListenFit", g_listenFit ? L"1" : L"0");
        Log(g_ui.listenFitState.c_str(), (int)g_listenFit);
    }
    break;

    case ID_DISABLE_FIT:
    {
        g_disableFitting = !g_disableFitting;
        IniWrite(L"Settings", L"DisableFitting", g_disableFitting ? L"1" : L"0");
        if (g_disableFitting)
        {
            // Turn off listener to save CPU
            if (g_listenFit)
            {
                g_listenFit = false;
                IniWrite(L"Settings", L"ListenFit", L"0");
                LogText(g_ui.listenFitAutoDisabled);
            }
        }
        Log(g_ui.disableFittingState.c_str(), (int)g_disableFitting);
    }
    break;

    case ID_WP_METHOD_TRANSCODED_CACHE:
    case ID_WP_METHOD_SYSTEM_PARAMETERS:
    case ID_WP_METHOD_DESKTOP_WALLPAPER_COM:
    case ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE:
    case ID_WP_METHOD_AUTO:
    {
        WallpaperDetectionMethod method = WallpaperDetectionMethod::TranscodedImageCache;
        if (cmd == ID_WP_METHOD_SYSTEM_PARAMETERS)
            method = WallpaperDetectionMethod::SystemParametersInfo;
        else if (cmd == ID_WP_METHOD_DESKTOP_WALLPAPER_COM)
            method = WallpaperDetectionMethod::DesktopWallpaperCOM;
        else if (cmd == ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE)
            method = WallpaperDetectionMethod::TranscodedWallpaperFile;
        else if (cmd == ID_WP_METHOD_AUTO)
            method = WallpaperDetectionMethod::Auto;

        const wchar_t* methodName = WallpaperDetectionMethodName(method);
        IniWrite(L"Settings", L"WallpaperDetectionMethod", methodName);
        Log(g_ui.wallpaperDetectionMethodLabel.c_str(), methodName);
    }
    break;

    case ID_WP_METHOD_FALLBACK:
    {
        g_wallpaperDetectionFallbackOnInvalid = !g_wallpaperDetectionFallbackOnInvalid;
        IniWrite(L"Settings", L"WallpaperDetectionFallbackOnInvalid", g_wallpaperDetectionFallbackOnInvalid ? L"1" : L"0");
        Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    }
    break;

    case ID_LOGFILE:
    {
        std::vector<wchar_t> file(32768);
        wcsncpy_s(file.data(), file.size(), GetLogPathCopy().c_str(), _TRUNCATE);
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = h;
        std::wstring filter = g_ui.logFileFilter;
        std::replace(filter.begin(), filter.end(), L'|', L'\0');
        filter.push_back(L'\0');
        ofn.lpstrFilter = filter.c_str();
        ofn.lpstrFile = file.data();
        ofn.nMaxFile = (DWORD)file.size();
        ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
        ofn.lpstrDefExt = L"log";
        if (GetSaveFileNameW(&ofn))
        {
            std::wstring newLogPath = file.data();
            SetLogPathCopy(newLogPath);
            IniWrite(L"Settings", L"LogPath", newLogPath.c_str());

            Log(g_ui.logPathChanged.c_str(), newLogPath.c_str());
            LogText(g_ui.futureLogEntriesNewPath);
        }
    }
    break;

    case ID_LOG_RENAME:
    {
        std::wstring newPath = GetLogPathCopy();
        if (ShowRenameDialog(h, newPath) == IDOK)
        {
            SetLogPathCopy(newPath);
            IniWrite(L"Settings", L"LogPath", newPath.c_str());
            Log(g_ui.logPathRenamed.c_str(), newPath.c_str());
            LogText(g_ui.futureLogEntriesRenamedPath);
        }
    }
    break;

    case ID_LOG_OPEN:
    {
        std::wstring logPath = GetLogPathCopy();
        if (!logPath.empty())
        {
            std::wstring param = L"/select,\"" + logPath + L"\"";
            HINSTANCE result = ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOW);
            if ((INT_PTR)result <= 32)
                LogWin32Failure(L"ShellExecuteW(explorer.exe)", static_cast<DWORD>((INT_PTR)result));
        }
    }
    break;

    case ID_LOG_RESET:
    {
        std::wstring newLogPath = DefaultLogPath();
        SetLogPathCopy(newLogPath);
        IniWrite(L"Settings", L"LogPath", L"");
        Log(g_ui.logPathReset.c_str(), newLogPath.c_str());
        LogText(g_ui.futureLogEntriesDefaultPath);
    }
    break;

    default:
        if (cmd>=ID_A1 && cmd<=ID_A6){
            int i = static_cast<int>(cmd) - ID_A1;
            const wchar_t* k = keys[i];
            bool nv = IniReadI(L"Assets",k,0) == 0;
            IniWrite(L"Assets",k,nv?L"1":L"0");
            Log(g_ui.assetToggleState.c_str(), k, StateOn(nv));
        }
        break;
    }
}

static void HandleExistingInstanceRequest()
{
    DWORD err = 0;
    if (!EnsureTrayIcon(&err))
    {
        LogWin32Failure(L"Shell_NotifyIconW(NIM_ADD)", err);
        if (g_notificationsEnabled && g_notifyOnAlreadyRunning)
            MessageBoxW(nullptr, g_ui.notificationAlreadyRunning.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (!g_notificationsEnabled || !g_notifyOnAlreadyRunning)
        return;

    bool trayPersistsAfterRestart = IniReadI(L"Settings", L"TrayIcon", 1) != 0;
    const std::wstring& message = trayPersistsAfterRestart
        ? g_ui.notificationAlreadyRunningTrayPersistent
        : g_ui.notificationAlreadyRunningTraySession;
    NotifyTrayBalloon(g_ui.notificationTitle, message, static_cast<DWORD>(trayPersistsAfterRestart ? NIIF_INFO : NIIF_WARNING), true);
}

static int HandleSingleInstanceMutexFailure(DWORD err)
{
    ErrorAction action = CurrentSingleInstanceFailureAction();
    if (action == ErrorAction::Ignore)
        return 0;

    std::wstring errText = Win32ErrorString(err);
    const wchar_t* actionName = ErrorActionName(action);
    Log(g_ui.singleInstanceMutexFailure.c_str(), err, errText.c_str(), actionName);

    if (g_notificationsEnabled && g_notifyOnSingleInstanceFailure)
    {
        if (action == ErrorAction::Warn && TrayIconPresent())
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationSingleInstanceFailure, NIIF_WARNING, true);
        else
            MessageBoxW(g_hwnd, g_ui.notificationSingleInstanceFailure.c_str(), g_ui.notificationTitle.c_str(), MB_OK | MB_ICONWARNING);
    }

    if (action == ErrorAction::Exit)
        return 1;

    if (action == ErrorAction::Crash)
        RaiseFailFastException(nullptr, nullptr, 0);

    return 0;
}


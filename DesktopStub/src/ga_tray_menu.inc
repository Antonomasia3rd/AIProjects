void Menu(HWND h)
{
    ReloadRuntimeSettingsFromIni();

    POINT pt; GetCursorPos(&pt);
    HMENU m = aip::CreateTrayPopupMenu();
    if (!m)
    {
        LogWin32Failure(L"CreatePopupMenu(root)");
        return;
    }
    bool showMenuAsDropdown = g_showMenuAsDropdown;

    auto addTo = [&](HMENU target, UINT id, const std::wstring& t, bool chk=false, bool en=true){
        if (g_hideDisabled && !en)
            return;
        if (!aip::AppendTrayMenuItem(target, id, t, chk, en))
            LogWin32Failure(L"AppendMenuW");
    };

    if (g_shutdownState.IsRequested())
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
        AfterTrayPopupClosed(h);

        if (cmd == ID_CANCEL_SHUTDOWN)
            CancelRequestedShutdown(h);
        else if (cmd == ID_FORCE_SHUTDOWN)
            ForceShutdownNow();
        return;
    }

    aip::TraySectionLayout sectionLayout(m, showMenuAsDropdown);
    auto beginSection = [&](const std::wstring& title) -> HMENU {
        bool created = false;
        HMENU section = sectionLayout.Begin(title, &created);
        if (!created)
            LogWin32Failure(showMenuAsDropdown ? L"CreatePopupMenu(section)" : L"AppendMenuW(section)");
        return section;
    };

    auto endSection = [&](HMENU target, const std::wstring& title) {
        if (!sectionLayout.End(target, title) &&
            (!showMenuAsDropdown || (target != nullptr && target != m)))
            LogWin32Failure(L"AppendMenuW(section)");
    };

    auto beginNestedMenu = [&](HMENU parent, const wchar_t* logName) -> HMENU {
        HMENU fallback = parent ? parent : m;
        HMENU submenu = aip::BeginTrayNestedMenu(fallback);
        if (submenu == fallback)
        {
            LogWin32Failure(logName);
            return fallback;
        }
        return submenu;
    };

    auto endNestedMenu = [&](HMENU parent, HMENU child, const std::wstring& title) {
        if (!parent || !child)
            return;
        bool fallback = child == parent;
        if (!aip::EndTrayNestedMenu(parent, child, title) && !fallback)
            LogWin32Failure(L"AppendMenuW");
    };

    addTo(m, ID_SHOW_MENU_DROPDOWN, g_ui.showMenuAsDropdown.c_str(), showMenuAsDropdown);
    addTo(m, ID_GENERATE_NOW, g_ui.generateNow.c_str());
    std::wstring versionLine = FormatWide(g_ui.versionLabel.c_str(), DesktopStubVersionDisplayText().c_str());
    AppendMenuChecked(m, MF_STRING | MF_DISABLED, 0, versionLine.c_str());
    AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);

    HMENU generalMenu = beginSection(g_ui.generalTitle);
    addTo(generalMenu, ID_TRAYICON, g_ui.trayIcon.c_str(), g_tray);
    addTo(generalMenu, ID_GENERATE_STARTUP, g_ui.generateOnStartup.c_str(), g_generateOnStartup);
    addTo(generalMenu, ID_GENERATE_STARTUP_CHANGED_ONLY, g_ui.generateOnStartupOnlyWhenChanged.c_str(), g_generateOnStartupOnlyWhenChanged, g_generateOnStartup);
    addTo(generalMenu, ID_HIDE_DISABLED, g_ui.hideDisabledEntries.c_str(), g_hideDisabled);
    addTo(generalMenu, ID_CONSOLE, g_ui.showConsole.c_str(), g_console);
    endSection(generalMenu, g_ui.generalTitle);

    HMENU wallpaperMenu = beginSection(g_ui.wallpaperTitle);
    addTo(wallpaperMenu, ID_LISTEN_WP, g_ui.listenWallpaper.c_str(), g_listenWallpaper);
    HMENU fittingMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper fitting)");
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
    endNestedMenu(wallpaperMenu, fittingMenu, g_ui.wallpaperFittingTitle);

    HMENU methodsMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper detection)");
    WallpaperDetectionMethod wallpaperMethod = CurrentWallpaperDetectionMethod();
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_CACHE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedImageCache), wallpaperMethod == WallpaperDetectionMethod::TranscodedImageCache);
    addTo(methodsMenu, ID_WP_METHOD_SYSTEM_PARAMETERS, WallpaperDetectionMethodName(WallpaperDetectionMethod::SystemParametersInfo), wallpaperMethod == WallpaperDetectionMethod::SystemParametersInfo);
    addTo(methodsMenu, ID_WP_METHOD_DESKTOP_WALLPAPER_COM, WallpaperDetectionMethodName(WallpaperDetectionMethod::DesktopWallpaperCOM), wallpaperMethod == WallpaperDetectionMethod::DesktopWallpaperCOM);
    addTo(methodsMenu, ID_WP_METHOD_TRANSCODED_WALLPAPER_FILE, WallpaperDetectionMethodName(WallpaperDetectionMethod::TranscodedWallpaperFile), wallpaperMethod == WallpaperDetectionMethod::TranscodedWallpaperFile);
    addTo(methodsMenu, ID_WP_METHOD_AUTO, WallpaperDetectionMethodName(WallpaperDetectionMethod::Auto), wallpaperMethod == WallpaperDetectionMethod::Auto);
    addTo(methodsMenu, ID_WP_METHOD_FALLBACK, g_ui.wallpaperDetectionFallback.c_str(), g_wallpaperDetectionFallbackOnInvalid);
    std::wstring wallpaperDetectionLine = FormatWide(g_ui.wallpaperDetectionMethodLabel.c_str(), WallpaperDetectionMethodName(wallpaperMethod));
    AppendMenuChecked(methodsMenu, MF_STRING | MF_DISABLED, 0, wallpaperDetectionLine.c_str());
    endNestedMenu(wallpaperMenu, methodsMenu, g_ui.wallpaperDetectionTitle);

    HMENU wallpaperDecodeMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Wallpaper decode)");
    bool wallpaperDecodeLowMemory = IniReadI(L"Settings", L"WallpaperDecodeLowMemory", 1) != 0;
    int wallpaperDecodeMaxLongEdge = IniReadClampedI(L"Settings", L"WallpaperDecodeMaxLongEdge", 0, 0, 32768);
    std::wstring wallpaperDecodeMaxLongEdgeText = WallpaperDecodeMaxLongEdgeDisplayText(wallpaperDecodeMaxLongEdge);
    std::wstring wallpaperDecodeMaxLongEdgeLine = FormatWide(g_ui.wallpaperDecodeMaxLongEdgeLabel.c_str(), wallpaperDecodeMaxLongEdgeText.c_str());
    addTo(wallpaperDecodeMenu, ID_WALLPAPER_DECODE_LOW_MEMORY, g_ui.wallpaperDecodeLowMemory.c_str(), wallpaperDecodeLowMemory);
    addTo(wallpaperDecodeMenu, ID_WALLPAPER_DECODE_MAX_LONG_EDGE, wallpaperDecodeMaxLongEdgeLine);
    endNestedMenu(wallpaperMenu, wallpaperDecodeMenu, g_ui.wallpaperDecodeTitle);

    HMENU liveWallpaperMenu = beginNestedMenu(wallpaperMenu, L"CreatePopupMenu(Live Wallpaper Detection)");
    {
        bool liveWallpaperCapture = IniReadI(L"Settings", L"LiveWallpaperCapture", 0) != 0;
        bool liveWallpaperCaptureScreenFallback = IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) != 0;
        int liveWallpaperCaptureDelayMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000);
        int liveWallpaperCaptureRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
        int liveWallpaperCaptureStartupRefreshMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
        int liveWallpaperCaptureStartupRefreshDurationMs = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
        std::wstring liveWallpaperCaptureDelayLine = FormatWide(g_ui.liveWallpaperCaptureDelayMsLabel.c_str(), liveWallpaperCaptureDelayMs);
        std::wstring liveWallpaperCaptureRefreshLine = FormatWide(g_ui.liveWallpaperCaptureRefreshMsLabel.c_str(), LiveWallpaperRefreshDisplayText(liveWallpaperCaptureRefreshMs).c_str());
        std::wstring liveWallpaperCaptureStartupRefreshLine = FormatWide(g_ui.liveWallpaperCaptureStartupRefreshMsLabel.c_str(), LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshMs).c_str());
        std::wstring liveWallpaperCaptureStartupRefreshDurationLine = FormatWide(g_ui.liveWallpaperCaptureStartupRefreshDurationMsLabel.c_str(), LiveWallpaperOffDisplayText(liveWallpaperCaptureStartupRefreshDurationMs).c_str());
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE, g_ui.liveWallpaperCapture.c_str(), liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_SCREEN_FALLBACK, g_ui.liveWallpaperCaptureScreenFallback.c_str(), liveWallpaperCaptureScreenFallback, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_DELAY, liveWallpaperCaptureDelayLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_REFRESH, liveWallpaperCaptureRefreshLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH, liveWallpaperCaptureStartupRefreshLine.c_str(), false, liveWallpaperCapture);
        addTo(liveWallpaperMenu, ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH_DURATION, liveWallpaperCaptureStartupRefreshDurationLine.c_str(), false, liveWallpaperCapture);
    }
    endNestedMenu(wallpaperMenu, liveWallpaperMenu, g_ui.liveWallpaperCaptureTitle);
    endSection(wallpaperMenu, g_ui.wallpaperTitle);

    HMENU liveTileMenu = beginSection(g_ui.liveTileTitle);
    {
        LiveTileUpdateMode liveTileMode = ConfiguredLiveTileUpdateMode();

        HMENU modeMenu = CreatePopupMenu();
        if (modeMenu)
        {
            addTo(modeMenu, ID_LIVE_TILE_MODE_LIVE_TILE, g_ui.liveTileModeEnabled.c_str(), liveTileMode == LiveTileUpdateMode::LiveTile);
            addTo(modeMenu, ID_LIVE_TILE_MODE_REGISTRATION, g_ui.liveTileModeDisabled.c_str(), liveTileMode == LiveTileUpdateMode::Registration);
            addTo(modeMenu, ID_LIVE_TILE_MODE_AUTO, g_ui.liveTileModeAuto.c_str(), liveTileMode == LiveTileUpdateMode::Auto);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)modeMenu, g_ui.liveTileModeTitle.c_str()))
                DestroyMenu(modeMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Mode)");
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_LIVE_TILE, g_ui.liveTileModeEnabled.c_str(), liveTileMode == LiveTileUpdateMode::LiveTile);
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_REGISTRATION, g_ui.liveTileModeDisabled.c_str(), liveTileMode == LiveTileUpdateMode::Registration);
            addTo(liveTileMenu, ID_LIVE_TILE_MODE_AUTO, g_ui.liveTileModeAuto.c_str(), liveTileMode == LiveTileUpdateMode::Auto);
        }

        LiveTileTemplateStyle templateStyle = ConfiguredLiveTileTemplateStyle();
        bool templateStyleAvailable =
            ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10;
        HMENU templateMenu = CreatePopupMenu();
        if (templateMenu)
        {
            addTo(templateMenu,
                ID_LIVE_TILE_TEMPLATE_ADAPTIVE,
                g_ui.liveTileTemplateAdaptive.c_str(),
                templateStyle == LiveTileTemplateStyle::Adaptive,
                templateStyleAvailable);
            addTo(templateMenu,
                ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET,
                g_ui.liveTileTemplateWindows81Preset.c_str(),
                templateStyle == LiveTileTemplateStyle::Windows81Preset,
                templateStyleAvailable);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)templateMenu, g_ui.liveTileTemplateTitle.c_str()))
                DestroyMenu(templateMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Template)");
            addTo(liveTileMenu,
                ID_LIVE_TILE_TEMPLATE_ADAPTIVE,
                g_ui.liveTileTemplateAdaptive.c_str(),
                templateStyle == LiveTileTemplateStyle::Adaptive,
                templateStyleAvailable);
            addTo(liveTileMenu,
                ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET,
                g_ui.liveTileTemplateWindows81Preset.c_str(),
                templateStyle == LiveTileTemplateStyle::Windows81Preset,
                templateStyleAvailable);
        }

        HMENU registrationMenu = CreatePopupMenu();
        if (registrationMenu)
        {
            addTo(registrationMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
            addTo(registrationMenu, ID_COM_REGISTRATION_USE_HELPER, FormatWide(g_ui.comRegistrationHelperLabel.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0)), IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)registrationMenu, g_ui.liveTileRegistrationTitle.c_str()))
                DestroyMenu(registrationMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Registration)");
            addTo(liveTileMenu, ID_USE_PS, g_ui.usePowerShell.c_str(), UsePowerShell());
            addTo(liveTileMenu, ID_COM_REGISTRATION_USE_HELPER, FormatWide(g_ui.comRegistrationHelperLabel.c_str(), StateEnabled(IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0)), IniReadI(L"Settings", L"ComRegistrationUseHelper", 0) != 0);
        }

        HMENU runtimeMenu = CreatePopupMenu();
        if (runtimeMenu)
        {
            addTo(runtimeMenu, ID_LIVE_TILE_RELAUNCH_ON_SWITCH, g_ui.liveTileRelaunchOnSwitch.c_str(), g_liveTileRelaunchOnSwitch);
            addTo(runtimeMenu, ID_CLEAR_LIVE_TILE_ON_SHUTDOWN, g_ui.clearLiveTileOnShutdown.c_str(), g_clearLiveTileOnShutdown);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)runtimeMenu, g_ui.liveTileRuntimeTitle.c_str()))
                DestroyMenu(runtimeMenu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Runtime)");
            addTo(liveTileMenu, ID_LIVE_TILE_RELAUNCH_ON_SWITCH, g_ui.liveTileRelaunchOnSwitch.c_str(), g_liveTileRelaunchOnSwitch);
            addTo(liveTileMenu, ID_CLEAR_LIVE_TILE_ON_SHUTDOWN, g_ui.clearLiveTileOnShutdown.c_str(), g_clearLiveTileOnShutdown);
        }

        HMENU win8Menu = CreatePopupMenu();
        if (win8Menu)
        {
            addTo(win8Menu, ID_WIN8_LIVE_TILE_BROKER_APP, g_ui.win8LiveTileBrokerApp.c_str(), IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) != 0);
            addTo(win8Menu, ID_WIN8_LIVE_TILE_BACKGROUND_TASK, g_ui.win8LiveTileBackgroundTask.c_str(), IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) != 0);
            addTo(win8Menu, ID_WIN8_LIVE_TILE_OOP_HELPER, g_ui.win8LiveTileOopHelper.c_str(), IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) != 0);
            if (!AppendMenuChecked(liveTileMenu, MF_POPUP, (UINT_PTR)win8Menu, g_ui.liveTileWindows8Title.c_str()))
                DestroyMenu(win8Menu);
        }
        else
        {
            LogWin32Failure(L"CreatePopupMenu(Live Tile Windows 8)");
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_BROKER_APP, g_ui.win8LiveTileBrokerApp.c_str(), IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) != 0);
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_BACKGROUND_TASK, g_ui.win8LiveTileBackgroundTask.c_str(), IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) != 0);
            addTo(liveTileMenu, ID_WIN8_LIVE_TILE_OOP_HELPER, g_ui.win8LiveTileOopHelper.c_str(), IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) != 0);
        }
    }
    endSection(liveTileMenu, g_ui.liveTileTitle);

    HMENU appearanceAssetsMenu = beginSection(g_ui.appearanceAssetsTitle);
    HMENU tileTextMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Tile Text)");
    {
        HMENU textMenu = tileTextMenu;
        if (!textMenu)
        {
            LogWin32Failure(L"CreatePopupMenu(Tile Text)");
            textMenu = appearanceAssetsMenu;
        }

        auto shortTileText = [](const std::wstring& value) -> std::wstring
        {
            std::wstring text = TrimCopy(value);
            if (text.empty())
                return L"(empty)";
            std::replace(text.begin(), text.end(), L'\r', L' ');
            std::replace(text.begin(), text.end(), L'\n', L' ');
            if (text.size() > 48)
                text = text.substr(0, 45) + L"...";
            return text;
        };

        bool tileTextEnabled = IniReadI(L"TileText", L"Enabled", 0) != 0;
        std::wstring tileText = TrimCopy(IniReadS(L"TileText", L"Text", L""));
        std::wstring secondaryText = TrimCopy(IniReadS(L"TileText", L"SecondaryText", L""));
        std::wstring badgeText = TrimCopy(IniReadS(L"TileText", L"BadgeText", L""));
        std::wstring textLine = FormatWide(g_ui.tileTextCurrent.c_str(), shortTileText(tileText).c_str());
        std::wstring secondaryLine = FormatWide(g_ui.tileTextSecondaryCurrent.c_str(), shortTileText(secondaryText).c_str());
        std::wstring badgeLine = FormatWide(g_ui.tileTextBadgeCurrent.c_str(), shortTileText(badgeText).c_str());

        addTo(textMenu, ID_TILE_TEXT_ENABLED, g_ui.tileTextEnabled.c_str(), tileTextEnabled);
        addTo(textMenu, ID_TILE_TEXT_EDIT, g_ui.tileTextEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_CLEAR, g_ui.tileTextClear.c_str(), false, !tileText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, textLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_EDIT, g_ui.tileTextSecondaryEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_SECONDARY_CLEAR, g_ui.tileTextSecondaryClear.c_str(), false, !secondaryText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, secondaryLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_BADGE_EDIT, g_ui.tileTextBadgeEdit.c_str());
        addTo(textMenu, ID_TILE_TEXT_BADGE_CLEAR, g_ui.tileTextBadgeClear.c_str(), false, !badgeText.empty());
        AppendMenuChecked(textMenu, MF_STRING | MF_DISABLED, 0, badgeLine.c_str());
        addTo(textMenu, ID_TILE_TEXT_APPLY_MEDIUM, g_ui.tileTextApplyMedium.c_str(), IniReadI(L"TileText", L"ApplyToMediumTile", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_WIDE, g_ui.tileTextApplyWide.c_str(), IniReadI(L"TileText", L"ApplyToWideTile", 1) != 0);
        addTo(textMenu, ID_TILE_TEXT_APPLY_LARGE, g_ui.tileTextApplyLarge.c_str(), IniReadI(L"TileText", L"ApplyToLargeTile", 1) != 0);
    }
    endNestedMenu(appearanceAssetsMenu, tileTextMenu, g_ui.tileTextTitle);

    HMENU assetsMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Assets)");
    addTo(assetsMenu, ID_GENERATE_DESKTOP_ICON_DISABLED, g_ui.generateDesktopIconForDisabledEntries.c_str(), g_generateDesktopIconForDisabledEntries);
    addTo(assetsMenu, ID_DELETE_DISABLED_ASSETS, g_ui.deleteDisabledAssets.c_str(), g_deleteDisabledAssets, !g_generateDesktopIconForDisabledEntries);
    for (int i = 0; i < static_cast<int>(_countof(kAssetMenuKeys)); i++)
        addTo(assetsMenu, static_cast<UINT>(ID_A1 + i), kAssetMenuKeys[i], IniReadI(L"Assets", kAssetMenuKeys[i], 0) != 0);
    endNestedMenu(appearanceAssetsMenu, assetsMenu, g_ui.assetsTitle);

    HMENU scaleMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(DPI scales)");
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
    if (g_generateScaleAuto)
        AppendMenuChecked(scaleMenu, MF_STRING | MF_DISABLED, 0, g_ui.generateScaleAutoPreservesManualToggles.c_str());
    endNestedMenu(appearanceAssetsMenu, scaleMenu, g_ui.dpiScalesTitle);

    HMENU cachingMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Caching)");
    bool cacheEnabled = GeneratedAssetCacheEnabled();
    bool cacheCanStore = cacheEnabled && GeneratedAssetCacheMaxEntries() > 0;
    bool precacheConfigured = IniReadI(L"Settings", L"GeneratedAssetPrecache", 0) != 0;
    addTo(cachingMenu, ID_GENERATED_ASSET_CACHE, g_ui.generatedAssetCache.c_str(), cacheEnabled);
    addTo(cachingMenu, ID_GENERATED_ASSET_PRECACHE, g_ui.generatedAssetPrecache.c_str(), precacheConfigured, cacheCanStore);
    std::wstring cacheMaxEntriesLine = FormatWide(g_ui.generatedAssetCacheMaxEntriesLabel.c_str(), GeneratedAssetCacheMaxEntries());
    std::wstring precacheMaxFilesLine = FormatWide(g_ui.generatedAssetPrecacheMaxFilesLabel.c_str(), GeneratedAssetPrecacheMaxFiles());
    addTo(cachingMenu, ID_GENERATED_ASSET_CACHE_MAX_ENTRIES, cacheMaxEntriesLine);
    addTo(cachingMenu, ID_GENERATED_ASSET_PRECACHE_MAX_FILES, precacheMaxFilesLine, false, cacheCanStore);
    endNestedMenu(appearanceAssetsMenu, cachingMenu, g_ui.cachingTitle);

    HMENU manifestMenu = beginNestedMenu(appearanceAssetsMenu, L"CreatePopupMenu(Manifest)");
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::wstring manifestSourceLine = FormatWide(g_ui.manifestSourceLabel.c_str(), manifestInfo.existing ? g_ui.manifestSourceExisting.c_str() : g_ui.manifestSourceGenerated.c_str());
    std::wstring manifestTargetLine = FormatWide(g_ui.manifestTargetLabel.c_str(), manifestInfo.manifestTarget.c_str());
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
    std::wstring manifestSquare30x30LogoLine = FormatWide(g_ui.manifestSquare30x30LogoLabel.c_str(), manifestInfo.square30x30Logo.c_str());
    std::wstring manifestSquare71x71LogoLine = FormatWide(g_ui.manifestSquare71x71LogoLabel.c_str(), manifestInfo.square71x71Logo.c_str());
    std::wstring manifestWide310x150LogoLine = FormatWide(g_ui.manifestWide310x150LogoLabel.c_str(), manifestInfo.wide310x150Logo.c_str());
    std::wstring manifestSquare310x310LogoLine = FormatWide(g_ui.manifestSquare310x310LogoLabel.c_str(), manifestInfo.square310x310Logo.c_str());
    std::wstring manifestBackgroundColorLine = FormatWide(g_ui.manifestBackgroundColorLabel.c_str(), manifestInfo.backgroundColor.c_str());
    std::wstring manifestShowNameOnTilesLine = FormatWide(g_ui.manifestShowNameOnTilesLabel.c_str(), manifestInfo.showNameOnTiles.c_str());
    std::wstring manifestEntryPointLine = FormatWide(g_ui.manifestEntryPointLabel.c_str(), manifestInfo.entryPoint.c_str());
    std::wstring manifestRestrictedCapabilityLine = FormatWide(g_ui.manifestRestrictedCapabilityLabel.c_str(), manifestInfo.restrictedCapability.c_str());
    std::wstring manifestSplashScreenImageLine = FormatWide(g_ui.manifestSplashScreenImageLabel.c_str(), manifestInfo.splashScreenImage.c_str());
    std::wstring manifestLiveTileBrokerExecutableLine = FormatWide(g_ui.manifestLiveTileBrokerExecutableLabel.c_str(), manifestInfo.liveTileBrokerExecutable.c_str());
    std::wstring manifestLiveTileBrokerEntryPointLine = FormatWide(g_ui.manifestLiveTileBrokerEntryPointLabel.c_str(), manifestInfo.liveTileBrokerEntryPoint.c_str());
    std::wstring manifestLiveTileOopServerNameLine = FormatWide(g_ui.manifestLiveTileOopServerNameLabel.c_str(), manifestInfo.liveTileOopServerName.c_str());
    std::wstring manifestLiveTileOopRuntimeClassIdLine = FormatWide(g_ui.manifestLiveTileOopRuntimeClassIdLabel.c_str(), manifestInfo.liveTileOopRuntimeClassId.c_str());
    std::wstring manifestLiveTileBackgroundTaskDllLine = FormatWide(g_ui.manifestLiveTileBackgroundTaskDllLabel.c_str(), manifestInfo.liveTileBackgroundTaskDll.c_str());
    std::wstring manifestLiveTileBackgroundTaskClassIdLine = FormatWide(g_ui.manifestLiveTileBackgroundTaskClassIdLabel.c_str(), manifestInfo.liveTileBackgroundTaskClassId.c_str());
    std::wstring manifestLiveTileBackgroundTaskNameLine = FormatWide(g_ui.manifestLiveTileBackgroundTaskNameLabel.c_str(), manifestInfo.liveTileBackgroundTaskName.c_str());
    addTo(manifestMenu, ID_MANIFEST_REGENERATE, g_ui.manifestRegenerateNow.c_str());
    {
        ManifestCompatibilityTarget manifestTarget = ConfiguredManifestCompatibilityTarget();
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS10, g_ui.appxManifestTargetWindows10.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows10);
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS81, g_ui.appxManifestTargetWindows81.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows81);
        addTo(manifestMenu, ID_MANIFEST_TARGET_WINDOWS8, g_ui.appxManifestTargetWindows8.c_str(), manifestTarget == ManifestCompatibilityTarget::Windows8);
    }

    HMENU manifestDetailsMenu = CreatePopupMenu();
    if (!manifestDetailsMenu)
    {
        LogWin32Failure(L"CreatePopupMenu(manifest details)");
        manifestDetailsMenu = manifestMenu;
    }
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestSourceLine.c_str());
    AppendMenuChecked(manifestDetailsMenu, MF_STRING | MF_DISABLED, 0, manifestTargetLine.c_str());
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_IDENTITY_NAME, manifestIdentityLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_PUBLISHER, manifestPublisherLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_PACKAGE_VERSION, manifestPackageVersionLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_DISPLAY_NAME, manifestDisplayNameLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_PUBLISHER_DISPLAY_NAME, manifestPublisherDisplayNameLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_DESCRIPTION, manifestDescriptionLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_EXECUTABLE, manifestExecutableLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_APPLICATION_ID, manifestApplicationIdLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_TARGET_DEVICE_FAMILY_NAME, manifestTargetDeviceFamilyNameLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_MIN_VERSION, manifestMinVersionLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_MAX_VERSION_TESTED, manifestMaxVersionTestedLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_RESOURCE_LANGUAGE, manifestResourceLanguageLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_PROPERTIES_LOGO, manifestPropertiesLogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SQUARE150_LOGO, manifestSquare150x150LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SQUARE44_LOGO, manifestSquare44x44LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SQUARE30_LOGO, manifestSquare30x30LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SQUARE71_LOGO, manifestSquare71x71LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_WIDE310_LOGO, manifestWide310x150LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SQUARE310_LOGO, manifestSquare310x310LogoLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_BACKGROUND_COLOR, manifestBackgroundColorLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SHOW_NAME_ON_TILES, manifestShowNameOnTilesLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_ENTRY_POINT, manifestEntryPointLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_RESTRICTED_CAPABILITY, manifestRestrictedCapabilityLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_SPLASH_SCREEN_IMAGE, manifestSplashScreenImageLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_BROKER_EXECUTABLE, manifestLiveTileBrokerExecutableLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_BROKER_ENTRY_POINT, manifestLiveTileBrokerEntryPointLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_OOP_SERVER_NAME, manifestLiveTileOopServerNameLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_OOP_RUNTIME_CLASS_ID, manifestLiveTileOopRuntimeClassIdLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_DLL, manifestLiveTileBackgroundTaskDllLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_CLASS_ID, manifestLiveTileBackgroundTaskClassIdLine);
    addTo(manifestDetailsMenu, ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_NAME, manifestLiveTileBackgroundTaskNameLine);
    if (manifestDetailsMenu != manifestMenu)
    {
        if (!AppendMenuChecked(manifestMenu, MF_POPUP, (UINT_PTR)manifestDetailsMenu, g_ui.manifestDetailsTitle.c_str()))
            DestroyMenu(manifestDetailsMenu);
    }
    endNestedMenu(appearanceAssetsMenu, manifestMenu, g_ui.manifestTitle);
    endSection(appearanceAssetsMenu, g_ui.appearanceAssetsTitle);

    HMENU diagnosticsMenu = beginSection(g_ui.diagnosticsTitle);
    HMENU notificationsMenu = beginNestedMenu(diagnosticsMenu, L"CreatePopupMenu(Notifications)");
    addTo(notificationsMenu, ID_NOTIF_ENABLED, g_ui.notificationsEnabled.c_str(), g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_START, g_ui.notifyOnStart.c_str(), g_notifyOnStart, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SUCCESS, g_ui.notifyOnSuccess.c_str(), g_notifyOnSuccess, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_FAILURE, g_ui.notifyOnFailure.c_str(), g_notifyOnFailure, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_BUSY, g_ui.notifyOnBusy.c_str(), g_notifyOnBusy, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_ALREADY_RUNNING, g_ui.notifyOnAlreadyRunning.c_str(), g_notifyOnAlreadyRunning, g_notificationsEnabled);
    addTo(notificationsMenu, ID_NOTIFY_SINGLE_INSTANCE_FAILURE, g_ui.notifyOnSingleInstanceFailure.c_str(), g_notifyOnSingleInstanceFailure, g_notificationsEnabled);
    endNestedMenu(diagnosticsMenu, notificationsMenu, g_ui.notificationsTitle);

    HMENU loggingMenu = beginNestedMenu(diagnosticsMenu, L"CreatePopupMenu(Logging)");
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
    endNestedMenu(diagnosticsMenu, loggingMenu, g_ui.loggingTitle);
    endSection(diagnosticsMenu, g_ui.diagnosticsTitle);

    HMENU advancedMenu = beginSection(g_ui.advancedTitle);
    std::wstring pollLine = FormatWide(g_ui.pollIntervalLabel.c_str(), IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000));
    std::wstring confirmLine = FormatWide(g_ui.confirmMsLabel.c_str(), IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000));
    std::wstring debounceLine = FormatWide(g_ui.debounceMinMsLabel.c_str(), IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000));
    std::wstring pollInitialBypassLine = FormatWide(g_ui.pollInitialDebounceBypassMsLabel.c_str(), IniReadClampedI(L"Settings", L"PollInitialDebounceBypassMs", 5000, 0, 60000));
    std::wstring iniCacheRefreshLine = FormatWide(g_ui.iniCacheRefreshMsLabel.c_str(), IniReadClampedI(L"Settings", L"IniCacheRefreshMs", 250, 0, 60000));
    std::wstring iniWriteLockWaitLine = FormatWide(g_ui.iniWriteLockWaitMsLabel.c_str(), IniReadClampedI(L"Settings", L"IniWriteLockWaitMs", 5000, 0, 60000));
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
    std::wstring trayHideDelayLine = FormatWide(g_ui.trayIconHiddenDelayMsLabel.c_str(), IniReadClampedI(L"Settings", L"TrayIconHiddenDelayMs", 750, 0, 10000));
    std::wstring logBufferLine = FormatWide(g_ui.logBufferLinesLabel.c_str(), IniReadClampedI(L"Settings", L"LogBufferLines", 128, 1, 100000));
    std::wstring idleTrimIntervalLine = FormatWide(g_ui.idleTrimIntervalMsLabel.c_str(), LiveWallpaperOffDisplayText(IniReadClampedI(L"Settings", L"IdleTrimIntervalMs", 60000, 0, 3600000)).c_str());
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
    HMENU advancedTimingMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced timing)");
    addTo(advancedTimingMenu, ID_ADV_POLL_INTERVAL, pollLine);
    addTo(advancedTimingMenu, ID_ADV_CONFIRM_MS, confirmLine);
    addTo(advancedTimingMenu, ID_ADV_DEBOUNCE_MIN_MS, debounceLine);
    addTo(advancedTimingMenu, ID_ADV_POLL_INITIAL_BYPASS_MS, pollInitialBypassLine);
    addTo(advancedTimingMenu, ID_ADV_INI_CACHE_REFRESH_MS, iniCacheRefreshLine);
    addTo(advancedTimingMenu, ID_ADV_INI_WRITE_LOCK_WAIT_MS, iniWriteLockWaitLine);
    addTo(advancedTimingMenu, ID_ADV_WALLPAPER_DETECTION_LOG_REPEAT_MS, wallpaperDetectionLogRepeatLine);
    addTo(advancedTimingMenu, ID_ADV_NOTIFICATION_TIMEOUT_MS, notificationTimeoutLine);
    endNestedMenu(advancedMenu, advancedTimingMenu, g_ui.advancedTimingTitle);

    HMENU advancedShutdownMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced shutdown)");
    addTo(advancedShutdownMenu, ID_ADV_SHUTDOWN_INITIAL_MS, shutdownInitialLine);
    addTo(advancedShutdownMenu, ID_ADV_SHUTDOWN_REPEAT_MS, shutdownRepeatLine);
    addTo(advancedShutdownMenu, ID_ADV_TRAY_ICON_HIDDEN_DELAY_MS, trayHideDelayLine);
    addTo(advancedShutdownMenu, ID_ADV_TRIM_WORKING_SET_ON_IDLE, g_ui.trimWorkingSetOnIdle.c_str(), g_trimWorkingSetOnIdle);
    addTo(advancedShutdownMenu, ID_ADV_IDLE_TRIM_INTERVAL_MS, idleTrimIntervalLine, false, g_trimWorkingSetOnIdle);
    addTo(advancedShutdownMenu, ID_ADV_COMPACT_CRT_HEAP_ON_IDLE, g_ui.compactCrtHeapOnIdle.c_str(), g_compactCrtHeapOnIdle);
    endNestedMenu(advancedMenu, advancedShutdownMenu, g_ui.advancedShutdownTitle);

    HMENU advancedRegistrationMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced registration)");
    addTo(advancedRegistrationMenu, ID_ADV_COM_REGISTRATION_TIMEOUT_MS, comRegistrationTimeoutLine);
    addTo(advancedRegistrationMenu, ID_ADV_COM_REGISTRATION_CANCEL_WAIT_MS, comRegistrationCancelWaitLine);
    addTo(advancedRegistrationMenu, ID_ADV_REGISTRATION_UNRESOLVED_BLOCK_MS, registrationUnresolvedBlockLine);
    endNestedMenu(advancedMenu, advancedRegistrationMenu, g_ui.advancedRegistrationTitle);

    HMENU advancedPowerShellMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced PowerShell)");
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_EXE, powerShellExeLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_POLL_MS, powerShellPollLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_TIMEOUT_MS, powerShellTimeoutLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_TERMINATE_WAIT_MS, powerShellTerminateWaitLine);
    addTo(advancedPowerShellMenu, ID_ADV_POWER_SHELL_OUTPUT_LIMIT, powerShellOutputLimitLine);
    endNestedMenu(advancedMenu, advancedPowerShellMenu, g_ui.advancedPowerShellTitle);

    HMENU advancedLoggingMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced logging)");
    addTo(advancedLoggingMenu, ID_ADV_LOG_BUFFER_LINES, logBufferLine);
    addTo(advancedLoggingMenu, ID_ADV_LOG_APPEND_LOCK_WAIT_MS, logAppendLockWaitLine);
    endNestedMenu(advancedMenu, advancedLoggingMenu, g_ui.advancedLoggingTitle);

    HMENU advancedPngSaveMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced PNG saving)");
    addTo(advancedPngSaveMenu, ID_ADV_SAVE_PNG_REPLACE_RETRIES, savePngReplaceRetriesLine);
    addTo(advancedPngSaveMenu, ID_ADV_SAVE_PNG_REPLACE_RETRY_DELAY_MS, savePngReplaceRetryDelayLine);
    addTo(advancedPngSaveMenu, ID_ADV_KEEP_GDIPLUS_LOADED, g_ui.keepGdiPlusLoaded.c_str(), g_keepGdiPlusLoaded);
    endNestedMenu(advancedMenu, advancedPngSaveMenu, g_ui.advancedPngSaveTitle);

    HMENU advancedSingleInstanceMenu = beginNestedMenu(advancedMenu, L"CreatePopupMenu(Advanced single instance)");
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_SIGNAL_RETRIES, signalRetriesLine);
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_SIGNAL_DELAY_MS, signalDelayLine);
    addTo(advancedSingleInstanceMenu, ID_ADV_SINGLE_INSTANCE_FAILURE_ACTION, singleInstanceLine);
    endNestedMenu(advancedMenu, advancedSingleInstanceMenu, g_ui.advancedSingleInstanceTitle);
    endSection(advancedMenu, g_ui.advancedTitle);

    if (showMenuAsDropdown)
        AppendMenuChecked(m, MF_SEPARATOR, 0, nullptr);
    addTo(m, ID_EXIT, g_ui.exitText.c_str());

    SetForegroundWindow(h);
    UINT cmd = static_cast<UINT>(TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,pt.x,pt.y,0,h,nullptr));
    DestroyMenu(m);
    AfterTrayPopupClosed(h);

    DispatchTrayCommand(h, cmd);
}

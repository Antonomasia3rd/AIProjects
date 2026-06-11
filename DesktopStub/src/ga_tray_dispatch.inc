static void DispatchTrayCommand(HWND h, UINT cmd)
{
    ManifestDisplayInfo manifestInfo;
    if (cmd >= ID_MANIFEST_EDIT_IDENTITY_NAME &&
        cmd <= ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_NAME)
    {
        manifestInfo = CurrentManifestDisplayInfo();
    }

    switch(cmd)
    {
    case ID_USE_PS:
    {
        bool v = !UsePowerShell();
        if (!IniWrite(L"Settings", L"UsePowerShell", v ? L"1" : L"0"))
            return;
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
            bool wasPackaged = CurrentProcessHasPackageIdentity();
            bool relaunchOnSwitch = g_liveTileRelaunchOnSwitch.load();
            g_liveTileUpdateMode = static_cast<int>(newLiveTileMode);
            if (!IniWrite(L"Settings", L"ExperimentalLiveTileUpdate", LiveTileUpdateModeIniValue(newLiveTileMode).c_str()))
                return;
            Log(g_ui.liveTileUpdateSummary.c_str(), LiveTileUpdateModeSummaryText().c_str());

            bool shouldRunPackagedForNewMode = false;
            bool newModeHasFixedRuntime = LiveTileModeHasFixedRuntime(
                newLiveTileMode,
                ConfiguredManifestCompatibilityTarget(),
                shouldRunPackagedForNewMode);
            bool requestPackagedRelaunch = relaunchOnSwitch &&
                newModeHasFixedRuntime &&
                shouldRunPackagedForNewMode &&
                !wasPackaged;
            bool requestUnpackagedRelaunch = relaunchOnSwitch &&
                newModeHasFixedRuntime &&
                !shouldRunPackagedForNewMode &&
                wasPackaged;

            if (requestPackagedRelaunch)
            {
                // Enabling Live Tile from the unpackaged tray process must first
                // re-register the manifest/static assets for the new mode.
                // Otherwise the packaged instance starts before the Start icon has
                // been changed back to the Desktop/disabled static asset.
                g_relaunchAfterExit = 1;
                if (!QueueLiveTileModeReregistration())
                {
                    g_relaunchAfterExit = 0;
                    break;
                }
                LogText(g_ui.liveTileRelaunchToPackagedQueued);
                RequestGracefulShutdown(h);
            }
            else if (requestUnpackagedRelaunch)
            {
                // Disabling from a packaged instance cannot safely call
                // RegisterPackageByFullName while that same package process is
                // still running. Mark it pending, exit the package identity first,
                // then let the unpackaged relaunch do the registration.
                SetLiveTileModeReregistrationPending(true);
                g_relaunchAfterExit = 2;
                LogText(g_ui.liveTileRelaunchToUnpackagedQueued);
                RequestGracefulShutdown(h);
            }
            else
            {
                QueueLiveTileModeReregistration();
            }
        }
    }
    break;

    case ID_LIVE_TILE_TEMPLATE_ADAPTIVE:
    case ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET:
    {
        LiveTileTemplateStyle newStyle =
            cmd == ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET
                ? LiveTileTemplateStyle::Windows81Preset
                : LiveTileTemplateStyle::Adaptive;
        if (ConfiguredLiveTileTemplateStyle() == newStyle)
            break;

        if (!IniWrite(L"Settings", L"LiveTileTemplateStyle", LiveTileTemplateStyleIniValue(newStyle).c_str()))
            return;
        g_liveTileTemplateStyle = static_cast<int>(newStyle);
        Log(g_ui.liveTileTemplateStyleSummary.c_str(), LiveTileTemplateStyleSummaryText().c_str());
        Log(g_ui.liveTileBrandingSummary.c_str(), LiveTileBrandingSummaryText().c_str());

        std::wstring wp = GetWallpaper();
        if (!wp.empty())
            QueueGenerate(wp, g_ui.liveTileTemplateChangedReason);
    }
    break;

    case ID_LIVE_TILE_BRANDING_AUTO:
    case ID_LIVE_TILE_BRANDING_NONE:
    case ID_LIVE_TILE_BRANDING_LOGO:
    case ID_LIVE_TILE_BRANDING_NAME:
    case ID_LIVE_TILE_BRANDING_NAME_AND_LOGO:
    {
        LiveTileBranding newBranding = LiveTileBranding::Auto;
        switch (cmd)
        {
        case ID_LIVE_TILE_BRANDING_NONE:
            newBranding = LiveTileBranding::None;
            break;
        case ID_LIVE_TILE_BRANDING_LOGO:
            newBranding = LiveTileBranding::Logo;
            break;
        case ID_LIVE_TILE_BRANDING_NAME:
            newBranding = LiveTileBranding::Name;
            break;
        case ID_LIVE_TILE_BRANDING_NAME_AND_LOGO:
            newBranding = LiveTileBranding::NameAndLogo;
            break;
        default:
            break;
        }
        if (ConfiguredLiveTileBranding() == newBranding)
            break;

        if (!IniWrite(L"Settings", L"LiveTileBranding", LiveTileBrandingIniValue(newBranding).c_str()))
            return;
        g_liveTileBranding = static_cast<int>(newBranding);
        Log(g_ui.liveTileBrandingSummary.c_str(), LiveTileBrandingSummaryText().c_str());

        std::wstring wp = GetWallpaper();
        if (!wp.empty())
            QueueGenerate(wp, g_ui.liveTileBrandingChangedReason);
    }
    break;

    case ID_LIVE_TILE_RELAUNCH_ON_SWITCH:
    {
        g_liveTileRelaunchOnSwitch = !g_liveTileRelaunchOnSwitch;
        if (!IniWrite(L"Settings", L"LiveTileRelaunchOnSwitch", g_liveTileRelaunchOnSwitch ? L"1" : L"0"))
            return;
    }
    break;

    case ID_CLEAR_LIVE_TILE_ON_SHUTDOWN:
    {
        g_clearLiveTileOnShutdown = !g_clearLiveTileOnShutdown;
        if (!IniWrite(L"Settings", L"ClearLiveTileOnShutdown", g_clearLiveTileOnShutdown ? L"1" : L"0"))
            return;
    }
    break;

    case ID_WIN8_LIVE_TILE_BROKER_APP:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileBrokerApp", 1) == 0;
        if (!IniWrite(L"Settings", L"Win8LiveTileBrokerApp", value ? L"1" : L"0"))
            return;
        Log(g_ui.win8LiveTileBrokerAppState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_WIN8_LIVE_TILE_BACKGROUND_TASK:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileBackgroundTask", 0) == 0;
        if (!IniWrite(L"Settings", L"Win8LiveTileBackgroundTask", value ? L"1" : L"0"))
            return;
        Log(g_ui.win8LiveTileBackgroundTaskState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_WIN8_LIVE_TILE_OOP_HELPER:
    {
        bool value = IniReadI(L"Settings", L"Win8LiveTileOopHelper", 0) == 0;
        if (!IniWrite(L"Settings", L"Win8LiveTileOopHelper", value ? L"1" : L"0"))
            return;
        Log(g_ui.win8LiveTileOopHelperState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_MANIFEST_REGENERATE:
    {
        RegenerateManifestAndQueueGeneration(h);
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
        if (!IniWrite(L"Settings",L"Logging", g_logging?L"1":L"0"))
            return;
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

    case ID_MANIFEST_TARGET_WINDOWS10:
    case ID_MANIFEST_TARGET_WINDOWS81:
    case ID_MANIFEST_TARGET_WINDOWS8:
    {
        ManifestCompatibilityTarget newTarget = ManifestCompatibilityTarget::Windows10;
        if (cmd == ID_MANIFEST_TARGET_WINDOWS81)
            newTarget = ManifestCompatibilityTarget::Windows81;
        else if (cmd == ID_MANIFEST_TARGET_WINDOWS8)
            newTarget = ManifestCompatibilityTarget::Windows8;

        if (ConfiguredManifestCompatibilityTarget() == newTarget)
            break;

        g_manifestCompatibilityTarget = static_cast<int>(newTarget);
        if (!IniWrite(L"Settings", L"AppxManifestTarget", ManifestCompatibilityTargetIniValue(newTarget).c_str()))
            return;
        Log(g_ui.manifestTargetSummary.c_str(), ManifestCompatibilityTargetSummaryText().c_str());
        RegenerateManifestAndQueueGeneration(h);
    }
    break;

    case ID_MANIFEST_EDIT_IDENTITY_NAME:
        PromptManifestStringSetting(h, L"ManifestIdentityName", manifestInfo.identityName);
        break;
    case ID_MANIFEST_EDIT_PUBLISHER:
        PromptManifestStringSetting(h, L"ManifestPublisher", manifestInfo.publisher);
        break;
    case ID_MANIFEST_EDIT_PACKAGE_VERSION:
        PromptManifestStringSetting(h, L"ManifestPackageVersion", manifestInfo.packageVersion);
        break;
    case ID_MANIFEST_EDIT_DISPLAY_NAME:
        PromptManifestStringSetting(h, L"ManifestDisplayName", manifestInfo.displayName);
        break;
    case ID_MANIFEST_EDIT_PUBLISHER_DISPLAY_NAME:
        PromptManifestStringSetting(h, L"ManifestPublisherDisplayName", manifestInfo.publisherDisplayName);
        break;
    case ID_MANIFEST_EDIT_DESCRIPTION:
        PromptManifestStringSetting(h, L"ManifestDescription", manifestInfo.description);
        break;
    case ID_MANIFEST_EDIT_EXECUTABLE:
        PromptManifestStringSetting(h, L"ManifestExecutable", manifestInfo.executable);
        break;
    case ID_MANIFEST_EDIT_APPLICATION_ID:
        PromptManifestStringSetting(h, L"ManifestApplicationId", manifestInfo.applicationId);
        break;
    case ID_MANIFEST_EDIT_TARGET_DEVICE_FAMILY_NAME:
        PromptManifestStringSetting(h, L"ManifestTargetDeviceFamilyName", manifestInfo.targetDeviceFamilyName);
        break;
    case ID_MANIFEST_EDIT_MIN_VERSION:
        PromptManifestStringSetting(h, L"ManifestMinVersion", manifestInfo.minVersion);
        break;
    case ID_MANIFEST_EDIT_MAX_VERSION_TESTED:
        PromptManifestStringSetting(h, L"ManifestMaxVersionTested", manifestInfo.maxVersionTested);
        break;
    case ID_MANIFEST_EDIT_RESOURCE_LANGUAGE:
        PromptManifestStringSetting(h, L"ManifestResourceLanguage", manifestInfo.resourceLanguage);
        break;
    case ID_MANIFEST_EDIT_PROPERTIES_LOGO:
        PromptManifestStringSetting(h, L"ManifestPropertiesLogo", manifestInfo.propertiesLogo);
        break;
    case ID_MANIFEST_EDIT_SQUARE150_LOGO:
        PromptManifestStringSetting(h, L"ManifestSquare150x150Logo", manifestInfo.square150x150Logo);
        break;
    case ID_MANIFEST_EDIT_SQUARE44_LOGO:
        PromptManifestStringSetting(h, L"ManifestSquare44x44Logo", manifestInfo.square44x44Logo);
        break;
    case ID_MANIFEST_EDIT_SQUARE30_LOGO:
        PromptManifestStringSetting(h, L"ManifestSquare30x30Logo", manifestInfo.square30x30Logo);
        break;
    case ID_MANIFEST_EDIT_SQUARE71_LOGO:
        PromptManifestStringSetting(h, L"ManifestSquare71x71Logo", manifestInfo.square71x71Logo);
        break;
    case ID_MANIFEST_EDIT_WIDE310_LOGO:
        PromptManifestStringSetting(h, L"ManifestWide310x150Logo", manifestInfo.wide310x150Logo);
        break;
    case ID_MANIFEST_EDIT_SQUARE310_LOGO:
        PromptManifestStringSetting(h, L"ManifestSquare310x310Logo", manifestInfo.square310x310Logo);
        break;
    case ID_MANIFEST_EDIT_BACKGROUND_COLOR:
        PromptManifestStringSetting(h, L"ManifestBackgroundColor", manifestInfo.backgroundColor);
        break;
    case ID_MANIFEST_EDIT_SHOW_NAME_ON_TILES:
        PromptManifestStringSetting(h, L"ManifestShowNameOnTiles", manifestInfo.showNameOnTiles);
        break;
    case ID_MANIFEST_EDIT_ENTRY_POINT:
        PromptManifestStringSetting(h, L"ManifestEntryPoint", manifestInfo.entryPoint);
        break;
    case ID_MANIFEST_EDIT_RESTRICTED_CAPABILITY:
        PromptManifestStringSetting(h, L"ManifestRestrictedCapability", manifestInfo.restrictedCapability);
        break;
    case ID_MANIFEST_EDIT_SPLASH_SCREEN_IMAGE:
        PromptManifestStringSetting(h, L"ManifestSplashScreenImage", manifestInfo.splashScreenImage);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_BROKER_EXECUTABLE:
        PromptManifestStringSetting(h, L"ManifestLiveTileBrokerExecutable", manifestInfo.liveTileBrokerExecutable);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_BROKER_ENTRY_POINT:
        PromptManifestStringSetting(h, L"ManifestLiveTileBrokerEntryPoint", manifestInfo.liveTileBrokerEntryPoint);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_OOP_SERVER_NAME:
        PromptManifestStringSetting(h, L"ManifestLiveTileOopServerName", manifestInfo.liveTileOopServerName);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_OOP_RUNTIME_CLASS_ID:
        PromptManifestStringSetting(h, L"ManifestLiveTileOopRuntimeClassId", manifestInfo.liveTileOopRuntimeClassId);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_DLL:
        PromptManifestStringSetting(h, L"ManifestLiveTileBackgroundTaskDll", manifestInfo.liveTileBackgroundTaskDll);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_CLASS_ID:
        PromptManifestStringSetting(h, L"ManifestLiveTileBackgroundTaskClassId", manifestInfo.liveTileBackgroundTaskClassId);
        break;
    case ID_MANIFEST_EDIT_LIVE_TILE_BACKGROUND_TASK_NAME:
        PromptManifestStringSetting(h, L"ManifestLiveTileBackgroundTaskName", manifestInfo.liveTileBackgroundTaskName);
        break;

    case ID_GENERATE_STARTUP:
    {
        g_generateOnStartup = !g_generateOnStartup;
        if (!IniWrite(L"Settings", L"GenerateOnStartup", g_generateOnStartup ? L"1" : L"0"))
            return;
        Log(g_ui.generateOnStartupSummary.c_str(), StateEnabled(g_generateOnStartup));
    }
    break;

    case ID_GENERATE_STARTUP_CHANGED_ONLY:
    {
        if (!g_generateOnStartup)
            break;
        g_generateOnStartupOnlyWhenChanged = !g_generateOnStartupOnlyWhenChanged;
        if (!IniWrite(L"Settings", L"GenerateOnStartupOnlyWhenChanged", g_generateOnStartupOnlyWhenChanged ? L"1" : L"0"))
            return;
        Log(g_ui.generateOnStartupOnlyWhenChangedSummary.c_str(), StateEnabled(g_generateOnStartupOnlyWhenChanged));
    }
    break;

    case ID_HIDE_DISABLED:
    {
        g_hideDisabled = !g_hideDisabled;
        if (!IniWrite(L"Settings", L"HideDisabledEntries", g_hideDisabled ? L"1" : L"0"))
            return;
        Log(g_ui.hideDisabledEntriesSummary.c_str(), StateEnabled(g_hideDisabled));
    }
    break;

    case ID_GENERATE_DESKTOP_ICON_DISABLED:
    {
        g_generateDesktopIconForDisabledEntries = !g_generateDesktopIconForDisabledEntries;
        if (!IniWrite(L"Settings", L"GenerateDesktopIconForDisabledEntries", g_generateDesktopIconForDisabledEntries ? L"1" : L"0"))
            return;
        Log(g_ui.generateDesktopIconForDisabledEntriesSummary.c_str(), StateEnabled(g_generateDesktopIconForDisabledEntries));
    }
    break;

    case ID_DELETE_DISABLED_ASSETS:
    {
        if (g_generateDesktopIconForDisabledEntries)
            break;
        g_deleteDisabledAssets = !g_deleteDisabledAssets;
        if (!IniWrite(L"Settings", L"DeleteDisabledAssets", g_deleteDisabledAssets ? L"1" : L"0"))
            return;
        Log(g_ui.deleteDisabledAssetsSummary.c_str(), StateEnabled(g_deleteDisabledAssets));
    }
    break;

    case ID_SCALE_AUTO:
    {
        g_generateScaleAuto = !g_generateScaleAuto;
        if (!IniWrite(L"Settings", L"GenerateScaleAuto", g_generateScaleAuto ? L"1" : L"0"))
            return;
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
        if (!IniWrite(L"Settings", key, value ? L"1" : L"0"))
            return;
        Log(g_ui.generateScaleSummary.c_str(), scale, StateEnabled(value));
    }
    break;

    case ID_TILE_TEXT_ENABLED:
        ToggleTileTextBool(L"Enabled", 0);
        break;

    case ID_TILE_TEXT_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"Text", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextInputTitle, g_ui.tileTextInputPrompt, text, true) == IDOK)
        {
            if (!IniWrite(L"TileText", L"Text", text.c_str()))
                return;
            if (!TrimCopy(text).empty())
                if (!IniWrite(L"TileText", L"Enabled", L"1"))
                    return;
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_CLEAR:
        if (!IniWrite(L"TileText", L"Text", L""))
            return;
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_SECONDARY_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"SecondaryText", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextSecondaryInputTitle, g_ui.tileTextSecondaryInputPrompt, text, true) == IDOK)
        {
            if (!IniWrite(L"TileText", L"SecondaryText", text.c_str()))
                return;
            if (!TrimCopy(text).empty())
                if (!IniWrite(L"TileText", L"Enabled", L"1"))
                    return;
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_SECONDARY_CLEAR:
        if (!IniWrite(L"TileText", L"SecondaryText", L""))
            return;
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_BADGE_EDIT:
    {
        std::wstring text = IniReadS(L"TileText", L"BadgeText", L"");
        if (ShowTextInputDialog(h, g_ui.tileTextBadgeInputTitle, g_ui.tileTextBadgeInputPrompt, text, true) == IDOK)
        {
            if (!IniWrite(L"TileText", L"BadgeText", text.c_str()))
                return;
            if (!TrimCopy(text).empty())
                if (!IniWrite(L"TileText", L"Enabled", L"1"))
                    return;
            Log(g_ui.tileTextEdited.c_str(), text.c_str());
        }
    }
    break;

    case ID_TILE_TEXT_BADGE_CLEAR:
        if (!IniWrite(L"TileText", L"BadgeText", L""))
            return;
        LogText(g_ui.tileTextCleared);
        break;

    case ID_TILE_TEXT_APPLY_MEDIUM:
        ToggleTileTextBool(L"ApplyToMediumTile", 1);
        break;

    case ID_TILE_TEXT_APPLY_WIDE:
        ToggleTileTextBool(L"ApplyToWideTile", 1);
        break;

    case ID_TILE_TEXT_APPLY_LARGE:
        ToggleTileTextBool(L"ApplyToLargeTile", 1);
        break;

    case ID_WALLPAPER_DECODE_LOW_MEMORY:
        ToggleSettingBool(L"WallpaperDecodeLowMemory", 1);
        break;

    case ID_WALLPAPER_DECODE_MAX_LONG_EDGE:
        CycleSettingIntPreset(L"WallpaperDecodeMaxLongEdge", 0, 0, 32768, { 0, 1024, 1920, 2560, 3840, 4096, 7680, 8192, 16384, 32768 });
        break;

    case ID_GENERATED_ASSET_CACHE:
    {
        bool value = !GeneratedAssetCacheEnabled();
        if (!IniWrite(L"Settings", L"GeneratedAssetCache", value ? L"1" : L"0"))
            return;
        Log(g_ui.generatedAssetCacheSummary.c_str(), StateEnabled(value), GeneratedAssetCacheMaxEntries());
    }
    break;

    case ID_GENERATED_ASSET_PRECACHE:
    {
        if (!GeneratedAssetCacheEnabled() || GeneratedAssetCacheMaxEntries() <= 0)
            break;

        bool value = IniReadI(L"Settings", L"GeneratedAssetPrecache", 0) == 0;
        if (!IniWrite(L"Settings", L"GeneratedAssetPrecache", value ? L"1" : L"0"))
            return;
        Log(g_ui.generatedAssetPrecacheSummary.c_str(), StateEnabled(value), GeneratedAssetPrecacheMaxFiles());
    }
    break;

    case ID_GENERATED_ASSET_CACHE_MAX_ENTRIES:
        CycleSettingIntPreset(L"GeneratedAssetCacheMaxEntries", 32, 0, 4096, { 0, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 });
        break;

    case ID_GENERATED_ASSET_PRECACHE_MAX_FILES:
        if (GeneratedAssetCacheEnabled() && GeneratedAssetCacheMaxEntries() > 0)
            CycleSettingIntPreset(L"GeneratedAssetPrecacheMaxFiles", 16, 0, 256, { 0, 4, 8, 16, 32, 64, 128, 256 });
        break;

    case ID_ADV_POLL_INTERVAL:
        CycleSettingIntPreset(L"PollIntervalMs", 2000, 250, 60000, { 250, 500, 1000, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_CONFIRM_MS:
        CycleSettingIntPreset(L"ConfirmMs", 800, 0, 10000, { 0, 250, 500, 800, 1000, 1500, 2500, 5000, 10000 });
        break;

    case ID_ADV_DEBOUNCE_MIN_MS:
        CycleSettingIntPreset(L"DebounceMinMs", 1200, 0, 60000, { 0, 500, 800, 1200, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_POLL_INITIAL_BYPASS_MS:
        CycleSettingIntPreset(L"PollInitialDebounceBypassMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_INI_CACHE_REFRESH_MS:
        CycleSettingIntPreset(L"IniCacheRefreshMs", 250, 0, 60000, { 0, 100, 250, 500, 1000, 2500, 5000, 10000, 60000 });
        break;

    case ID_ADV_WALLPAPER_DETECTION_LOG_REPEAT_MS:
        CycleSettingIntPreset(L"WallpaperDetectionLogRepeatMs", 60000, 0, 3600000, { 0, 10000, 30000, 60000, 300000, 600000, 1800000, 3600000 });
        break;

    case ID_ADV_NOTIFICATION_TIMEOUT_MS:
        CycleSettingIntPreset(L"NotificationTimeoutMs", 4000, 1000, 60000, { 1000, 2000, 4000, 8000, 15000, 30000, 60000 });
        break;

    case ID_ADV_SHUTDOWN_INITIAL_MS:
        CycleSettingIntPreset(L"ShutdownInitialNoticeMs", 1000, 250, 60000, { 250, 500, 1000, 2000, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_SHUTDOWN_REPEAT_MS:
        CycleSettingIntPreset(L"ShutdownRepeatNoticeMs", 5000, 250, 60000, { 250, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_SINGLE_INSTANCE_SIGNAL_RETRIES:
        CycleSettingIntPreset(L"SingleInstanceSignalRetries", 50, 0, 200, { 0, 1, 5, 10, 25, 50, 100, 200 });
        break;

    case ID_ADV_SINGLE_INSTANCE_SIGNAL_DELAY_MS:
        CycleSettingIntPreset(L"SingleInstanceSignalDelayMs", 100, 0, 5000, { 0, 25, 50, 100, 250, 500, 1000, 2500, 5000 });
        break;

    case ID_ADV_COM_REGISTRATION_TIMEOUT_MS:
        CycleSettingIntPreset(L"ComRegistrationTimeoutMs", 120000, 0, 600000, { 0, 30000, 60000, 120000, 180000, 300000, 600000 });
        break;

    case ID_ADV_COM_REGISTRATION_CANCEL_WAIT_MS:
        CycleSettingIntPreset(L"ComRegistrationCancelWaitMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_COM_REGISTRATION_USE_HELPER:
        ToggleSettingBool(L"ComRegistrationUseHelper", 0);
        break;

    case ID_ADV_TRAY_ICON_HIDDEN_DELAY_MS:
        CycleSettingIntPreset(L"TrayIconHiddenDelayMs", 750, 0, 10000, { 0, 250, 500, 750, 1000, 2000, 5000, 10000 });
        break;

    case ID_ADV_LOG_BUFFER_LINES:
        CycleSettingIntPreset(L"LogBufferLines", 128, 1, 100000, { 1, 100, 128, 500, 1024, 2500, 5000, 10000, 50000, 100000 });
        break;

    case ID_ADV_LOG_APPEND_LOCK_WAIT_MS:
        CycleSettingIntPreset(L"LogAppendLockWaitMs", 1000, 0, 60000, { 0, 100, 250, 500, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_TRIM_WORKING_SET_ON_IDLE:
    {
        g_trimWorkingSetOnIdle = !g_trimWorkingSetOnIdle;
        if (!IniWrite(L"Settings", L"TrimWorkingSetOnIdle", g_trimWorkingSetOnIdle ? L"1" : L"0"))
            return;
        Log(g_ui.trimWorkingSetOnIdleSummary.c_str(), StateEnabled(g_trimWorkingSetOnIdle));
        if (g_trimWorkingSetOnIdle)
            TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_IDLE_TRIM_INTERVAL_MS:
    {
        CycleSettingIntPreset(L"IdleTrimIntervalMs", 60000, 0, 3600000, { 0, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000 });
        int intervalMs = IniReadClampedI(L"Settings", L"IdleTrimIntervalMs", 60000, 0, 3600000);
        Log(g_ui.idleTrimIntervalMsSummary.c_str(), LiveWallpaperOffDisplayText(intervalMs).c_str());
        if (intervalMs > 0)
            TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_COMPACT_CRT_HEAP_ON_IDLE:
    {
        g_compactCrtHeapOnIdle = !g_compactCrtHeapOnIdle;
        if (!IniWrite(L"Settings", L"CompactCrtHeapOnIdle", g_compactCrtHeapOnIdle ? L"1" : L"0"))
            return;
        Log(g_ui.compactCrtHeapOnIdleSummary.c_str(), StateEnabled(g_compactCrtHeapOnIdle));
        TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_KEEP_GDIPLUS_LOADED:
    {
        g_keepGdiPlusLoaded = !g_keepGdiPlusLoaded;
        if (!IniWrite(L"Settings", L"KeepGdiPlusLoaded", g_keepGdiPlusLoaded ? L"1" : L"0"))
            return;
        Log(g_ui.keepGdiPlusLoadedSummary.c_str(), StateEnabled(g_keepGdiPlusLoaded));
        ReleaseIdleGdiPlusIfAllowed();
        TrimIdleWorkingSetIfConfigured();
    }
    break;

    case ID_ADV_POWER_SHELL_EXE:
        ChoosePowerShellExe(h);
        break;

    case ID_ADV_POWER_SHELL_POLL_MS:
        CycleSettingIntPreset(L"PowerShellPollMs", 50, 10, 1000, { 10, 25, 50, 100, 250, 500, 1000 });
        break;

    case ID_ADV_POWER_SHELL_TIMEOUT_MS:
        CycleSettingIntPreset(L"PowerShellTimeoutMs", 120000, 0, 600000, { 0, 30000, 60000, 120000, 180000, 300000, 600000 });
        break;

    case ID_ADV_POWER_SHELL_TERMINATE_WAIT_MS:
        CycleSettingIntPreset(L"PowerShellTerminateWaitMs", 5000, 0, 60000, { 0, 1000, 2500, 5000, 10000, 30000, 60000 });
        break;

    case ID_ADV_POWER_SHELL_OUTPUT_LIMIT:
        CycleSettingIntPreset(L"PowerShellOutputMessageBoxChars", 6000, 0, 60000, { 0, 1000, 3000, 6000, 10000, 30000, 60000 });
        g_powerShellOutputMessageBoxChars = IniReadClampedI(L"Settings", L"PowerShellOutputMessageBoxChars", 6000, 0, 60000);
        break;

    case ID_ADV_REGISTRATION_UNRESOLVED_BLOCK_MS:
        CycleSettingIntPreset(L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000, { 0, 30000, 60000, 120000, 300000, 600000, 1800000, 3600000 });
        break;

    case ID_ADV_SAVE_PNG_REPLACE_RETRIES:
        CycleSettingIntPreset(L"SavePngReplaceRetries", 3, 0, 20, { 0, 1, 2, 3, 5, 10, 20 });
        break;

    case ID_ADV_SAVE_PNG_REPLACE_RETRY_DELAY_MS:
        CycleSettingIntPreset(L"SavePngReplaceRetryDelayMs", 75, 0, 5000, { 0, 25, 50, 75, 100, 250, 500, 1000, 2500, 5000 });
        break;

    case ID_ADV_SINGLE_INSTANCE_FAILURE_ACTION:
        CycleSingleInstanceFailureActionSetting();
        break;

    case ID_SHOW_MENU_DROPDOWN:
    {
        g_showMenuAsDropdown = !g_showMenuAsDropdown;
        if (!IniWrite(L"Settings", L"ShowMenuAsDropdown", g_showMenuAsDropdown ? L"1" : L"0"))
            return;
        Log(g_ui.showMenuAsDropdownSummary.c_str(), StateEnabled(g_showMenuAsDropdown));
    }
    break;

    case ID_NOTIF_ENABLED:
    {
        g_notificationsEnabled = !g_notificationsEnabled;
        if (!IniWrite(L"Settings", L"NotificationsEnabled", g_notificationsEnabled ? L"1" : L"0"))
            return;
        Log(g_ui.notificationsSummary.c_str(), StateEnabled(g_notificationsEnabled), StateOn(g_notifyOnStart), StateOn(g_notifyOnSuccess), StateOn(g_notifyOnFailure), StateOn(g_notifyOnBusy), StateOn(g_notifyOnAlreadyRunning), StateOn(g_notifyOnSingleInstanceFailure));
    }
    break;

    case ID_NOTIFY_START:
    {
        g_notifyOnStart = !g_notifyOnStart;
        if (!IniWrite(L"Settings", L"NotifyOnStart", g_notifyOnStart ? L"1" : L"0"))
            return;
        Log(g_ui.notifyOnStartState.c_str(), (int)g_notifyOnStart);
    }
    break;

    case ID_NOTIFY_SUCCESS:
    {
        g_notifyOnSuccess = !g_notifyOnSuccess;
        if (!IniWrite(L"Settings", L"NotifyOnSuccess", g_notifyOnSuccess ? L"1" : L"0"))
            return;
        Log(g_ui.notifyOnSuccessState.c_str(), (int)g_notifyOnSuccess);
    }
    break;

    case ID_NOTIFY_FAILURE:
    {
        g_notifyOnFailure = !g_notifyOnFailure;
        if (!IniWrite(L"Settings", L"NotifyOnFailure", g_notifyOnFailure ? L"1" : L"0"))
            return;
        Log(g_ui.notifyOnFailureState.c_str(), (int)g_notifyOnFailure);
    }
    break;

    case ID_NOTIFY_BUSY:
    {
        g_notifyOnBusy = !g_notifyOnBusy;
        if (!IniWrite(L"Settings", L"NotifyOnBusy", g_notifyOnBusy ? L"1" : L"0"))
            return;
        Log(g_ui.notifyOnBusyState.c_str(), (int)g_notifyOnBusy);
    }
    break;

    case ID_NOTIFY_ALREADY_RUNNING:
    {
        g_notifyOnAlreadyRunning = !g_notifyOnAlreadyRunning;
        if (!IniWrite(L"Settings", L"NotifyOnAlreadyRunning", g_notifyOnAlreadyRunning ? L"1" : L"0"))
            return;
        Log(g_ui.notifyOnAlreadyRunningState.c_str(), (int)g_notifyOnAlreadyRunning);
    }
    break;

    case ID_NOTIFY_SINGLE_INSTANCE_FAILURE:
    {
        g_notifyOnSingleInstanceFailure = !g_notifyOnSingleInstanceFailure;
        if (!IniWrite(L"Settings", L"NotifyOnSingleInstanceFailure", g_notifyOnSingleInstanceFailure ? L"1" : L"0"))
            return;
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
        if (!IniWrite(L"Settings", L"ListenWallpaper", g_listenWallpaper ? L"1" : L"0"))
            return;
        Log(g_ui.listenWallpaperState.c_str(), (int)g_listenWallpaper);
    }
    break;

    case ID_LISTEN_FIT:
    {
        g_listenFit = !g_listenFit;
        if (!IniWrite(L"Settings", L"ListenFit", g_listenFit ? L"1" : L"0"))
            return;
        Log(g_ui.listenFitState.c_str(), (int)g_listenFit);
    }
    break;

    case ID_DISABLE_FIT:
    {
        g_disableFitting = !g_disableFitting;
        if (!IniWrite(L"Settings", L"DisableFitting", g_disableFitting ? L"1" : L"0"))
            return;
        if (g_disableFitting)
        {
            // Turn off listener to save CPU
            if (g_listenFit)
            {
                g_listenFit = false;
                if (!IniWrite(L"Settings", L"ListenFit", L"0"))
                    return;
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
        if (!IniWrite(L"Settings", L"WallpaperDetectionMethod", methodName))
            return;
        Log(g_ui.wallpaperDetectionMethodLabel.c_str(), methodName);
    }
    break;

    case ID_WP_METHOD_FALLBACK:
    {
        g_wallpaperDetectionFallbackOnInvalid = !g_wallpaperDetectionFallbackOnInvalid;
        if (!IniWrite(L"Settings", L"WallpaperDetectionFallbackOnInvalid", g_wallpaperDetectionFallbackOnInvalid ? L"1" : L"0"))
            return;
        Log(g_ui.wallpaperDetectionFallbackSummary.c_str(), StateEnabled(g_wallpaperDetectionFallbackOnInvalid));
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE:
    {
        bool value = IniReadI(L"Settings", L"LiveWallpaperCapture", 0) == 0;
        if (!IniWrite(L"Settings", L"LiveWallpaperCapture", value ? L"1" : L"0"))
            return;
        Log(g_ui.liveWallpaperCaptureSummary.c_str(),
            StateEnabled(value),
            IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000),
            LiveWallpaperRefreshDisplayText(IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000)).c_str());
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_SCREEN_FALLBACK:
    {
        bool value = IniReadI(L"Settings", L"LiveWallpaperCaptureScreenFallback", 0) == 0;
        if (!IniWrite(L"Settings", L"LiveWallpaperCaptureScreenFallback", value ? L"1" : L"0"))
            return;
        Log(g_ui.liveWallpaperCaptureScreenFallbackState.c_str(), value ? 1 : 0);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_DELAY:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureDelayMs", 2500, 0, 30000);
        int next = NextLiveWallpaperCaptureDelayMs(current);
        if (!IniWrite(L"Settings", L"LiveWallpaperCaptureDelayMs", std::to_wstring(next).c_str()))
            return;
        Log(g_ui.liveWallpaperCaptureDelayState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_REFRESH:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureRefreshMs", 10000, 0, 3600000);
        int next = NextLiveWallpaperCaptureRefreshMs(current);
        if (!IniWrite(L"Settings", L"LiveWallpaperCaptureRefreshMs", std::to_wstring(next).c_str()))
            return;
        Log(g_ui.liveWallpaperCaptureRefreshState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", 1000, 0, 3600000);
        int next = NextLiveWallpaperCaptureStartupRefreshMs(current);
        if (!IniWrite(L"Settings", L"LiveWallpaperCaptureStartupRefreshMs", std::to_wstring(next).c_str()))
            return;
        Log(g_ui.liveWallpaperCaptureStartupRefreshState.c_str(), next);
    }
    break;

    case ID_LIVE_WALLPAPER_CAPTURE_STARTUP_REFRESH_DURATION:
    {
        int current = IniReadClampedI(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", 0, 0, 3600000);
        int next = NextLiveWallpaperCaptureStartupRefreshDurationMs(current);
        if (!IniWrite(L"Settings", L"LiveWallpaperCaptureStartupRefreshDurationMs", std::to_wstring(next).c_str()))
            return;
        Log(g_ui.liveWallpaperCaptureStartupRefreshDurationState.c_str(), next);
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
            if (!IniWrite(L"Settings", L"LogPath", newLogPath.c_str()))
                return;

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
            if (!IniWrite(L"Settings", L"LogPath", newPath.c_str()))
                return;
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
        if (!IniWrite(L"Settings", L"LogPath", L""))
            return;
        Log(g_ui.logPathReset.c_str(), newLogPath.c_str());
        LogText(g_ui.futureLogEntriesDefaultPath);
    }
    break;

    default:
        if (cmd >= ID_A1 && cmd < ID_A1 + _countof(kAssetMenuKeys)){
            int i = static_cast<int>(cmd) - ID_A1;
            const wchar_t* k = kAssetMenuKeys[i];
            bool nv = IniReadI(L"Assets",k,0) == 0;
            if (!IniWrite(L"Assets",k,nv?L"1":L"0"))
                return;
            Log(g_ui.assetToggleState.c_str(), k, StateOn(nv));
        }
        break;
    }
}

static void HandleExistingInstanceMessageBoxRequest()
{
    if (!g_notificationsEnabled || !g_notifyOnAlreadyRunning)
        return;

    MessageBoxW(g_hwnd ? g_hwnd : nullptr,
        g_ui.notificationAlreadyRunning.c_str(),
        g_ui.notificationTitle.c_str(),
        MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
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

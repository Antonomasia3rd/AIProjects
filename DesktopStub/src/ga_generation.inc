// -----------------------------------------------------------------------------
// Tile generation
// -----------------------------------------------------------------------------
struct Tile { const wchar_t* name; const wchar_t* file; int w,h; };
static const Tile g_tiles[] = {
    {L"StoreLogo",       L"Assets\\StoreLogo.png", 50,50},
    {L"MediumTile",      L"Assets\\MediumTile.png",150,150},
    {L"Square44x44Logo", L"Assets\\Square44x44Logo.png",44,44},
    {L"SmallTile",       L"Assets\\SmallTile.png",71,71},
    {L"WideTile",        L"Assets\\WideTile.png",310,150},
    {L"LargeTile",       L"Assets\\LargeTile.png",310,310},
};

static bool IsValidGeneratedPng(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (size.QuadPart == 0)
        return false;

    Bitmap probe(path.c_str());
    return probe.GetLastStatus() == Ok && probe.GetWidth() > 0 && probe.GetHeight() > 0;
}

static bool LooksLikeUriPath(const std::wstring& path)
{
    size_t colon = path.find(L':');
    if (colon == std::wstring::npos)
        return false;

    size_t slash = path.find_first_of(L"\\/");
    if (slash != std::wstring::npos && slash < colon)
        return false;

    return !(colon == 1 && path.size() > 2 && (path[2] == L'\\' || path[2] == L'/'));
}

static void AddUniqueManifestAsset(std::vector<std::wstring>& assets, const std::wstring& rawPath)
{
    std::wstring path = TrimCopy(rawPath);
    if (path.empty() || LooksLikeUriPath(path))
        return;

    for (const auto& existing : assets)
    {
        if (IEquals(existing, path))
            return;
    }
    assets.push_back(path);
}

static std::vector<std::wstring> CurrentManifestAssetPaths()
{
    ManifestDisplayInfo info = CurrentManifestDisplayInfo();
    std::vector<std::wstring> assets;
    AddUniqueManifestAsset(assets, info.propertiesLogo);
    AddUniqueManifestAsset(assets, info.square150x150Logo);
    AddUniqueManifestAsset(assets, info.square44x44Logo);
    AddUniqueManifestAsset(assets, info.square71x71Logo);
    AddUniqueManifestAsset(assets, info.wide310x150Logo);
    AddUniqueManifestAsset(assets, info.square310x310Logo);
    return assets;
}

static std::wstring ResolveManifestAssetPath(const std::wstring& exeDir, const std::wstring& assetPath)
{
    if (!PathIsRelativeW(assetPath.c_str()))
        return assetPath;

    if (assetPath.size() >= 1 && (assetPath[0] == L'\\' || assetPath[0] == L'/'))
        return exeDir + assetPath;

    return exeDir + L"\\" + assetPath;
}

static std::wstring FindMissingManifestAssets(const std::wstring& exeDir)
{
    std::wstring missing;
    for (const auto& assetPath : CurrentManifestAssetPaths())
    {
        std::wstring path = ResolveManifestAssetPath(exeDir, assetPath);
        if (!IsValidGeneratedPng(path))
        {
            if (!missing.empty())
                missing += L", ";
            missing += assetPath;
        }
    }
    return missing;
}

static void ClearRegistrationUnresolvedLocked()
{
    if (g_registrationUnresolvedProcess)
    {
        CloseHandle(g_registrationUnresolvedProcess);
        g_registrationUnresolvedProcess = nullptr;
    }
    g_registrationUnresolved = false;
    g_registrationUnresolvedPid = 0;
    g_registrationUnresolvedReason.clear();
    g_registrationUnresolvedSince = steady_clock::time_point{};
}

static void MarkRegistrationUnresolved(const std::wstring& reason, HANDLE processHandle, DWORD processId)
{
    HANDLE oldHandle = nullptr;
    std::wstring storedReason = reason.empty() ? L"unknown registration state" : reason;
    {
        std::lock_guard<std::mutex> lk(g_registrationUnresolvedMutex);
        oldHandle = g_registrationUnresolvedProcess;
        g_registrationUnresolved = true;
        g_registrationUnresolvedProcess = processHandle;
        g_registrationUnresolvedPid = processId;
        g_registrationUnresolvedSince = steady_clock::now();
        g_registrationUnresolvedReason = storedReason;
    }

    if (oldHandle && oldHandle != processHandle)
        CloseHandle(oldHandle);

    Log(g_ui.registrationUnresolvedMarked.c_str(), storedReason.c_str());
}

static bool RegistrationBlockedByUnresolved(std::wstring& reason)
{
    std::wstring clearedReason;
    std::wstring expiredReason;
    bool blocked = false;

    {
        std::lock_guard<std::mutex> lk(g_registrationUnresolvedMutex);
        if (!g_registrationUnresolved)
            return false;

        bool stillUnresolved = true;
        if (g_registrationUnresolvedProcess)
        {
            DWORD waitResult = WaitForSingleObject(g_registrationUnresolvedProcess, 0);
            if (waitResult == WAIT_OBJECT_0)
            {
                clearedReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
            else if (waitResult == WAIT_FAILED)
            {
                clearedReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
        }

        if (stillUnresolved)
        {
            int blockMs = IniReadClampedI(L"Settings", L"RegistrationUnresolvedBlockMs", 300000, 0, 3600000);
            if (blockMs <= 0 || ElapsedMs(g_registrationUnresolvedSince, steady_clock::now()) >= blockMs)
            {
                expiredReason = g_registrationUnresolvedReason;
                ClearRegistrationUnresolvedLocked();
                stillUnresolved = false;
            }
        }

        if (stillUnresolved)
        {
            reason = g_registrationUnresolvedReason;
            if (g_registrationUnresolvedPid)
                reason += L" (pid=" + std::to_wstring(g_registrationUnresolvedPid) + L")";
            blocked = true;
        }
    }

    if (!clearedReason.empty())
        Log(g_ui.registrationUnresolvedCleared.c_str(), clearedReason.c_str());
    if (!expiredReason.empty())
        Log(g_ui.registrationUnresolvedExpired.c_str(), expiredReason.c_str());

    return blocked;
}

static bool StartupGeneratedAssetsPresent(const std::wstring& exeDir)
{
    std::vector<int> scales = GetConfiguredScales();
    for (const auto& t : g_tiles)
    {
        std::wstring basePath = exeDir + L"\\" + t.file;

        bool generatedByCurrentConfig =
            IniReadI(L"Assets", t.name, 0) != 0 ||
            g_generateDesktopIconForDisabledEntries;
        if (!generatedByCurrentConfig)
            continue;

        if (!IsValidGeneratedPng(basePath))
            return false;

        for (int scale : scales)
        {
            std::wstring scaledPath = ScaleAssetPath(basePath, scale);
            if (!IsValidGeneratedPng(scaledPath))
                return false;
        }
    }
    return true;
}

static void HashWide(uint64_t& hash, const std::wstring& text)
{
    static constexpr uint64_t FNV_PRIME = 1099511628211ull;
    const BYTE* bytes = reinterpret_cast<const BYTE*>(text.data());
    size_t len = text.size() * sizeof(wchar_t);
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    hash ^= 0xff;
    hash *= FNV_PRIME;
}

static std::wstring Hex64(uint64_t value)
{
    wchar_t buf[32];
    swprintf(buf, _countof(buf), L"%016llX", static_cast<unsigned long long>(value));
    return buf;
}

static std::wstring FileFingerprint(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return path + L"|missing";

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    ULARGE_INTEGER time{};
    time.LowPart = data.ftLastWriteTime.dwLowDateTime;
    time.HighPart = data.ftLastWriteTime.dwHighDateTime;

    return path + L"|size=" + Hex64(size.QuadPart) + L"|mtime=" + Hex64(time.QuadPart);
}

static void AppendRectFingerprint(std::wstring& out, const wchar_t* name, const RECT& rect)
{
    out += name;
    out += L"=" + IntToWString(rect.left) + L"," + IntToWString(rect.top) + L"," +
        IntToWString(rect.right) + L"," + IntToWString(rect.bottom) + L";";
}

static std::wstring BuildGenerationStateKey(const std::wstring& wallpaperPath)
{
    std::wstring state;
    state += L"v=2;";
    state += L"wallpaper=" + FileFingerprint(wallpaperPath) + L";";
    state += L"exe=" + FileFingerprint(g_exePath) + L";";
    state += L"manifest=" + FileFingerprint(GetExeDir() + L"\\AppxManifest.xml") + L";";
    state += L"method=" + std::wstring(WallpaperDetectionMethodName(CurrentWallpaperDetectionMethod())) + L";";
    state += L"methodFallback=" + IntToWString(g_wallpaperDetectionFallbackOnInvalid ? 1 : 0) + L";";
    state += L"usePowerShell=" + IntToWString(UsePowerShell() ? 1 : 0) + L";";
    state += L"disableFitting=" + IntToWString(g_disableFitting ? 1 : 0) + L";";
    state += L"fit=" + IntToWString(static_cast<int>(g_disableFitting ? FitMode::Fill : GetWallpaperFit())) + L";";
    state += L"desktopIconForDisabled=" + IntToWString(g_generateDesktopIconForDisabledEntries ? 1 : 0) + L";";
    state += L"deleteDisabledAssets=" + IntToWString(g_deleteDisabledAssets ? 1 : 0) + L";";
    state += L"scaleAuto=" + IntToWString(g_generateScaleAuto ? 1 : 0) + L";";

    std::vector<int> scales = GetConfiguredScales();
    state += L"scales=";
    for (int scale : scales)
        state += IntToWString(scale) + L",";
    state += L";";

    RECT primaryRect{};
    if (GetPrimaryMonitorRect(primaryRect))
        AppendRectFingerprint(state, L"primary", primaryRect);
    RECT virtualRect{};
    if (GetVirtualDesktopRect(virtualRect))
        AppendRectFingerprint(state, L"virtual", virtualRect);

    for (const auto& t : g_tiles)
        state += std::wstring(t.name) + L"=" + IntToWString(IniReadI(L"Assets", t.name, 0) ? 1 : 0) + L";";

    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, state);
    return Hex64(hash);
}

static bool StartupGenerationCanSkip(const std::wstring& wallpaperPath)
{
    if (!g_generateOnStartupOnlyWhenChanged)
        return false;

    std::wstring last = IniReadS(L"State", L"LastSuccessfulGenerationKey", L"");
    if (last.empty() || !IEquals(last, BuildGenerationStateKey(wallpaperPath)))
        return false;

    return StartupGeneratedAssetsPresent(GetExeDir());
}

static void StoreSuccessfulGenerationState(const std::wstring& wallpaperPath)
{
    std::wstring key = BuildGenerationStateKey(wallpaperPath);
    if (!key.empty())
        IniWrite(L"State", L"LastSuccessfulGenerationKey", key.c_str());
}

bool Generate(const wchar_t* wp, const wchar_t* reason = nullptr)
{
    auto generationStart = steady_clock::now();
    std::unique_lock<std::mutex> opLock(g_generationMutex, std::defer_lock);
    if (!opLock.try_lock())
    {
        LogText(g_ui.generationRequestBusy);
        if (g_notifyOnBusy)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationBusy, NIIF_WARNING);
        return false;
    }

    ReloadRuntimeSettingsFromIni();

    if (!wp || !*wp)
    {
        LogText(g_ui.emptyWallpaper);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.wallpaperNotFound, NIIF_WARNING);
        return false;
    }

    const wchar_t* why = (reason && *reason) ? reason : g_ui.unspecifiedReason.c_str();
    Log(g_ui.startingWallpaperGeneration.c_str(), why);
    Log(g_ui.wallpaperSource.c_str(), wp);

    if (g_notifyOnStart)
        NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationStarted, NIIF_INFO);

    Bitmap* src = new Bitmap(wp);
    if (src->GetLastStatus() != Ok)
    {
        LogText(g_ui.failedLoadWallpaper);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.failedLoadWallpaper, NIIF_ERROR);
        delete src;
        return false;
    }
    Log(g_ui.wallpaperImageLoaded.c_str(), static_cast<int>(src->GetWidth()), static_cast<int>(src->GetHeight()));

    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    Bitmap* preparedWallpaper = nullptr;
    Bitmap* renderSource = src;
    FitMode renderMode = mode;
    if (mode == FitMode::Tile || mode == FitMode::Span)
    {
        preparedWallpaper = (mode == FitMode::Tile)
            ? BuildTiledPrimaryBitmap(src)
            : BuildSpannedPrimaryBitmap(src);
        if (preparedWallpaper)
        {
            renderSource = preparedWallpaper;
            renderMode = FitMode::Fill;
        }
        else
        {
            renderMode = FitMode::Fill;
        }
    }
    Log(g_ui.renderModeSummary.c_str(), FitModeToString(renderMode));

    std::wstring exeDir = GetExeDir();
    std::wstring assetsDir = exeDir + L"\\Assets";
    DWORD assetsDirError = ERROR_SUCCESS;
    bool assetsDirReady = EnsureDirectoryExists(assetsDir, &assetsDirError);
    if (!assetsDirReady)
        LogWin32Failure(g_ui.createAssetsDirectoryOperation.c_str(), assetsDirError);
    else
        Log(g_ui.assetsDirectoryReady.c_str(), assetsDir.c_str());

    bool assetsOk = assetsDirReady;
    std::vector<int> scales = GetConfiguredScales();
    {
        std::wstring scaleList;
        for (size_t i = 0; i < scales.size(); ++i)
        {
            if (i > 0) scaleList += L", ";
            scaleList += IntToWString(scales[i]) + L"%";
        }
        Log(g_ui.activeScales.c_str(), scaleList.c_str());
    }
    if (assetsDirReady)
    {
        for (auto& t : g_tiles)
        {
            std::wstring outPath = exeDir + L"\\" + t.file;
            if (!IniReadI(L"Assets", t.name, 0))
            {
                if (!g_generateDesktopIconForDisabledEntries)
                {
                    Log(g_ui.skippingAssetDisabled.c_str(), t.name);
                    if (g_deleteDisabledAssets && !DeleteDisabledAssetFiles(outPath))
                        assetsOk = false;
                    continue;
                }

                // Decode the embedded 30x30 icon and write it as-is to the base path,
                // then write the same 30x30 image to each scaled variant path.
                {
                    Bitmap* icon = LoadEmbeddedDesktopIcon();
                    if (!icon)
                    {
                        assetsOk = false;
                        Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                        continue;
                    }

                    bool ok = SavePNG(icon, outPath.c_str());
                    if (ok)
                        Log(g_ui.generatedDesktopIconAsset.c_str(), outPath.c_str());
                    else
                        Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());

                    for (int scale : scales)
                    {
                        std::wstring scaledPath = ScaleAssetPath(outPath, scale);
                        bool scaledOk = SavePNG(icon, scaledPath.c_str());
                        if (scaledOk)
                            Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                        else
                            Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                        ok = ok && scaledOk;
                    }

                    delete icon;
                    if (!ok) assetsOk = false;
                }
                continue;
            }

            auto* o = ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0));
            bool saved = false;
            if (o)
            {
                saved = SavePNG(o, outPath.c_str());
                if (saved)
                    Log(g_ui.savedAsset.c_str(), outPath.c_str());
                else
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                delete o;
            }

            for (int scale : scales)
            {
                int scaledW = ScalePixels(t.w, scale);
                int scaledH = ScalePixels(t.h, scale);
                auto* scaled = ResizeBitmapToMode(renderSource, scaledW, scaledH, renderMode, Color(255, 0, 0, 0));
                std::wstring scaledPath = ScaleAssetPath(outPath, scale);
                if (scaled)
                {
                    bool scaledSaved = SavePNG(scaled, scaledPath.c_str());
                    if (scaledSaved)
                        Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                    else
                        Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                    saved = scaledSaved && saved;
                    delete scaled;
                }
                else
                {
                    Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                    saved = false;
                }
            }

            if (!saved)
                assetsOk = false;
        }
    }
    delete preparedWallpaper;
    delete src;

    auto assetsDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(), g_ui.timingAssetGeneration.c_str(), ElapsedMs(generationStart, assetsDone));

    if (!assetsOk)
    {
        auto generationDone = steady_clock::now();
        LogText(g_ui.skippingRegistrationDueToAssetFailure);
        Log(g_ui.timingLog.c_str(), g_ui.timingAppRegistration.c_str(), 0);
        Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, generationDone));
        LogText(g_ui.assetGenerationFailed);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
        return false;
    }

    LogText(g_ui.reRegisteringManifest);

    std::wstring manifestPath;
    bool manifestCreated = false;
    DWORD manifestError = ERROR_SUCCESS;
    bool manifestReady = EnsureAppxManifest(manifestPath, manifestCreated, manifestError);
    if (manifestCreated)
        Log(g_ui.appxManifestCreated.c_str(), manifestPath.c_str());
    if (!manifestReady)
    {
        Log(g_ui.appxManifestCreateFailed.c_str(), manifestPath.c_str());
        LogWin32Failure(L"Create AppxManifest.xml", manifestError);
    }
    else
    {
        Log(g_ui.manifestPath.c_str(), manifestPath.c_str());
    }

    bool registrationOk = false;
    std::wstring registrationFailureMessage;
    auto setPowerShellFailureMessage = [&]()
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        registrationFailureMessage = g_psMsg.empty() ? g_ui.powerShellRegistrationFailed : g_psMsg;
    };

    if (!manifestReady)
    {
        registrationOk = false;
        registrationFailureMessage = FormatWide(g_ui.appxManifestCreateFailed.c_str(), manifestPath.c_str());
    }
    else
    {
        std::wstring unresolvedReason;
        if (RegistrationBlockedByUnresolved(unresolvedReason))
        {
            Log(g_ui.registrationBlockedUnresolved.c_str(), unresolvedReason.c_str());
            registrationOk = false;
            registrationFailureMessage = unresolvedReason;
        }
        else if (!UsePowerShell())
        {
            AppxRegistrationResult comResult = Appx_Register_COM(manifestPath);
            registrationOk = comResult == AppxRegistrationResult::Success;

            if (comResult == AppxRegistrationResult::Failed)
            {
                LogText(g_ui.comRegistrationFailed);
                std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
                registrationOk = PS_Run(ps);
                if (!registrationOk)
                    setPowerShellFailureMessage();
            }
            else if (comResult == AppxRegistrationResult::TimeoutUncertain)
            {
                LogText(g_ui.comRegistrationFallbackSkippedAfterTimeout);
                MarkRegistrationUnresolved(g_ui.comRegistrationFallbackSkippedAfterTimeout, nullptr, 0);
                registrationFailureMessage = g_ui.comRegistrationFallbackSkippedAfterTimeout;
            }
        }
        else
        {
            std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
            registrationOk = PS_Run(ps);
            if (!registrationOk)
                setPowerShellFailureMessage();
        }
    }

    bool ok = assetsOk && registrationOk;
    auto registrationDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(), g_ui.timingAppRegistration.c_str(), ElapsedMs(assetsDone, registrationDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, registrationDone));
    std::wstring missingManifestAssets;
    if (!registrationOk)
    {
        missingManifestAssets = FindMissingManifestAssets(exeDir);
        if (!missingManifestAssets.empty())
            Log(g_ui.missingManifestAssets.c_str(), missingManifestAssets.c_str());
    }

    if (ok)
    {
        StoreSuccessfulGenerationState(wp);
        LogText(g_ui.assetGenerationFinished);
        if (g_notifyOnSuccess)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationSuccess, NIIF_INFO);
    }
    else
    {
        if (!assetsOk)
            LogText(g_ui.assetGenerationFailed);
        if (!registrationOk)
            LogText(g_ui.appRegistrationFailed);
        if (g_notifyOnFailure)
        {
            std::wstring failMsg = registrationFailureMessage.empty()
                ? g_ui.notificationGenerationFailed
                : registrationFailureMessage;
            if (!missingManifestAssets.empty() &&
                (registrationFailureMessage.empty() ||
                    failMsg == g_ui.notificationGenerationFailed ||
                    failMsg == g_ui.appRegistrationFailed ||
                    failMsg == g_ui.powerShellRegistrationFailed))
            {
                failMsg = g_ui.notificationMissingManifestAssets;
            }
            NotifyTrayBalloon(g_ui.notificationTitle, failMsg, NIIF_ERROR);
        }
    }

    return ok;
}

void TrayRemove();

static void WakePollThread()
{
    g_pollWakeGeneration.fetch_add(1, std::memory_order_relaxed);
    g_pollWakeCv.notify_all();
}

static void PollSleepFor(milliseconds duration)
{
    unsigned long observedWake = g_pollWakeGeneration.load(std::memory_order_relaxed);
    std::unique_lock<std::mutex> lk(g_pollWakeMutex);
    g_pollWakeCv.wait_for(lk, duration, [observedWake]
    {
        return g_pollWakeGeneration.load(std::memory_order_relaxed) != observedWake ||
            !g_running.load();
    });
}

static std::wstring CurrentGenerationReason()
{
    if (g_generationActive)
    {
        std::lock_guard<std::mutex> lk(g_generationStateMutex);
        if (!g_generationActiveReason.empty())
            return g_generationActiveReason;
    }

    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationQueued && !g_generationQueuedReason.empty())
            return g_generationQueuedReason;
    }

    std::lock_guard<std::mutex> lk(g_generationStateMutex);
    return g_generationActiveReason.empty() ? g_ui.unspecifiedReason : g_generationActiveReason;
}

static bool GenerationWorkPending()
{
    std::lock_guard<std::mutex> lk(g_generationQueueMutex);
    return g_generationQueued || g_generationActive;
}

static void NotifyShutdownDelayed()
{
    std::wstring reason = CurrentGenerationReason();
    NotifyTrayBalloon(
        g_ui.notificationTitle,
        FormatWide(g_ui.shutdownDelayedNotification.c_str(), reason.c_str()),
        NIIF_WARNING,
        true);
}

static void MaybeCompleteRequestedShutdown()
{
    if (g_shutdownRequested && !GenerationWorkPending() && g_hwnd)
    {
        if (!PostMessageW(g_hwnd, WM_APP_SHUTDOWN_READY, 0, 0))
            LogWin32Failure(L"PostMessageW(WM_APP_SHUTDOWN_READY)");
    }
}

static void MarkGenerationActive(bool active, const std::wstring& reason = L"")
{
    {
        std::lock_guard<std::mutex> lk(g_generationStateMutex);
        g_generationActiveReason = active ? reason : L"";
    }
    g_generationActive = active;
}

static bool SetShutdownNoticeTimer(HWND h, int delayMs, const wchar_t* operation)
{
    if (!h)
        return false;

    SetLastError(ERROR_SUCCESS);
    if (!SetTimer(h, SHUTDOWN_NOTICE_TIMER_ID, static_cast<UINT>(ClampInt(delayMs, 250, 60000)), nullptr))
    {
        LogWin32Failure(operation);
        return false;
    }
    return true;
}

static void RequestGracefulShutdown(HWND h)
{
    if (g_shutdownRequested)
        return;

    g_shutdownRequested = true;
    WakePollThread();
    LogText(g_ui.shutdownRequested);

    if (GenerationWorkPending())
    {
        NotifyShutdownDelayed();
        if (h)
            SetShutdownNoticeTimer(h, g_shutdownInitialNoticeMs.load(), L"SetTimer(SHUTDOWN_NOTICE_TIMER_ID initial)");
        return;
    }

    PostQuitMessage(0);
}

static void CancelRequestedShutdown(HWND h)
{
    if (!g_shutdownRequested)
        return;

    g_shutdownRequested = false;
    WakePollThread();
    if (h)
        KillTimer(h, SHUTDOWN_NOTICE_TIMER_ID);
    LogText(g_ui.shutdownCancelled);
}

static void ForceShutdownNow()
{
    LogText(g_ui.forceShutdownRequested);
    TrayRemove();
    ExitProcess(0);
}

static void GenerationWorkerThread()
{
    for (;;)
    {
        std::wstring wallpaper;
        std::wstring reason;

        {
            std::unique_lock<std::mutex> lk(g_generationQueueMutex);
            g_generationQueueCv.wait(lk, [] { return g_generationWorkerStop || g_generationQueued; });

            if (!g_generationQueued && g_generationWorkerStop)
                break;

            wallpaper = std::move(g_generationQueuedWallpaper);
            reason = std::move(g_generationQueuedReason);
            MarkGenerationActive(true, reason);
            g_generationQueued = false;
        }

        try
        {
            Generate(wallpaper.c_str(), reason.c_str());
        }
        catch (const std::exception& ex)
        {
            std::wstring message = ExceptionTextToWide(ex);
            Log(g_ui.generationUnhandledException.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
        }
        catch (...)
        {
            std::wstring message = UnknownExceptionText();
            Log(g_ui.generationUnhandledException.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
        }
        MarkGenerationActive(false);
        MaybeCompleteRequestedShutdown();
    }
}

static void QueueGenerate(std::wstring wallpaper, std::wstring reason)
{
    if (g_shutdownRequested)
        return;

    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationWorkerStop || g_shutdownRequested)
            return;

        g_generationQueuedWallpaper = std::move(wallpaper);
        g_generationQueuedReason = std::move(reason);
        g_generationQueued = true;

        if (!g_generationWorkerStarted)
        {
            g_generationWorkerStarted = true;
            startWorker = true;
        }
    }

    if (startWorker)
    {
        try
        {
            g_generationWorker = std::thread(GenerationWorkerThread);
        }
        catch (const std::exception& ex)
        {
            std::wstring message = ExceptionTextToWide(ex);
            {
                std::lock_guard<std::mutex> lk(g_generationQueueMutex);
                g_generationWorkerStarted = false;
                g_generationQueued = false;
                g_generationQueuedWallpaper.clear();
                g_generationQueuedReason.clear();
            }
            Log(g_ui.generationThreadStartFailed.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
            MaybeCompleteRequestedShutdown();
            return;
        }
        catch (...)
        {
            std::wstring message = UnknownExceptionText();
            {
                std::lock_guard<std::mutex> lk(g_generationQueueMutex);
                g_generationWorkerStarted = false;
                g_generationQueued = false;
                g_generationQueuedWallpaper.clear();
                g_generationQueuedReason.clear();
            }
            Log(g_ui.generationThreadStartFailed.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
            MaybeCompleteRequestedShutdown();
            return;
        }
    }

    g_generationQueueCv.notify_one();
}

static void JoinQueuedGenerations()
{
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        g_generationWorkerStop = true;
    }

    g_generationQueueCv.notify_one();

    if (g_generationWorker.joinable())
        g_generationWorker.join();
}

// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// Poll thread
// -----------------------------------------------------------------------------
void PollThread()
{
    try
    {
    std::wstring last;
    FitMode lastFit = FitMode::Fill;
    int lastDpiScale = 100;
    bool baselineReady = false;
    auto lastGen = steady_clock::now() - milliseconds(ClampInt(g_pollInitialDebounceBypassMs.load(), 0, 60000));
    bool emptyWallpaperAlreadyReported = false;

    while (g_running)
    {
        int poll = IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000);
        int confirm = IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000);
        int deb = IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000);
        ReloadRuntimeSettingsFromIni();

        if (g_shutdownRequested)
        {
            PollSleepFor(milliseconds(poll));
            continue;
        }

        bool wallpaperListening = g_listenWallpaper;
        bool fitListening = g_listenFit && !g_disableFitting;
        bool dpiListening = g_generateScaleAuto;
        if (!wallpaperListening && !fitListening && !dpiListening)
        {
            baselineReady = false;
            PollSleepFor(milliseconds(poll));
            continue;
        }

        if (!baselineReady)
        {
            last = GetWallpaper();
            lastFit = GetWallpaperFit();
            lastDpiScale = CurrentAssetScale();
            baselineReady = true;
            PollSleepFor(milliseconds(poll));
            continue;
        }

        std::wstring cur = GetWallpaper();
        FitMode curFit = GetWallpaperFit();
        int curDpiScale = CurrentAssetScale();

        bool wallpaperChanged = (cur != last);
        bool fitChanged = (fitListening && curFit != lastFit);
        bool dpiScaleChanged = (dpiListening && curDpiScale != lastDpiScale);

        if ((wallpaperListening && wallpaperChanged) ||
            fitChanged ||
            dpiScaleChanged)
        {
            PollSleepFor(milliseconds(confirm));
            if (!g_running || g_shutdownRequested)
                continue;

            std::wstring cur2 = GetWallpaper();
            FitMode fit2 = GetWallpaperFit();
            int dpiScale2 = CurrentAssetScale();

            bool wallpaperChanged2 = (cur2 != last);
            bool fitChanged2 = (fitListening && fit2 != lastFit);
            bool dpiScaleChanged2 = (dpiListening && dpiScale2 != lastDpiScale);

            if ((wallpaperListening && wallpaperChanged2) ||
                fitChanged2 ||
                dpiScaleChanged2)
            {
                auto now = steady_clock::now();

                if (duration_cast<milliseconds>(now - lastGen).count() >= deb)
                {
                    std::wstring generationWallpaper = cur2;
                    bool wallpaperChangeConfirmed = wallpaperListening && wallpaperChanged2;
                    bool onlyNonWallpaperChange = !wallpaperChangeConfirmed && (fitChanged2 || dpiScaleChanged2);
                    if (generationWallpaper.empty() && onlyNonWallpaperChange && IsUsableWallpaperPath(last))
                    {
                        generationWallpaper = last;
                        Log(g_ui.wallpaperDetectionEmptyUsingLast.c_str(), generationWallpaper.c_str());
                    }

                    if (generationWallpaper.empty())
                    {
                        if (!emptyWallpaperAlreadyReported)
                            LogText(g_ui.wallpaperDetectionEmptyDuringPoll);
                        emptyWallpaperAlreadyReported = true;
                        lastGen = now;
                        continue;
                    }

                    const wchar_t* reason = g_ui.changeDetected.c_str();
                    if (dpiScaleChanged2 && !wallpaperChangeConfirmed && !fitChanged2)
                        reason = g_ui.dpiScaleChangeDetected.c_str();
                    else if (wallpaperChangeConfirmed && fitChanged2)
                        reason = g_ui.wallpaperAndFitChangeDetected.c_str();
                    else if (wallpaperChangeConfirmed)
                        reason = g_ui.wallpaperChangeDetected.c_str();
                    else if (fitChanged2)
                        reason = g_ui.fitModeChangeDetected.c_str();

                    LogText(g_ui.changeConfirmed);

                    QueueGenerate(generationWallpaper, reason);
                    emptyWallpaperAlreadyReported = false;
                    last = generationWallpaper;
                    lastFit = fit2;
                    lastDpiScale = dpiScale2;
                    lastGen = steady_clock::now();
                }
            }
        }
        PollSleepFor(milliseconds(poll));
    }
    }
    catch (const std::exception& ex)
    {
        std::wstring message = ExceptionTextToWide(ex);
        Log(g_ui.pollThreadUnhandledException.c_str(), message.c_str());
    }
    catch (...)
    {
        std::wstring message = UnknownExceptionText();
        Log(g_ui.pollThreadUnhandledException.c_str(), message.c_str());
    }
}


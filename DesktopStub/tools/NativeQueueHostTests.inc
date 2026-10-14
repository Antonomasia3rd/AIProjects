// Real host composition/delta/cache code, with in-memory delivery acknowledgments.
// No product entry point, tile updater, package, provider or visible window runs.
static void TestNativeQueueHost(const std::wstring& bitmapPath)
{
    std::wstring original;
    Check(ReadIniFileForMutation(original), "native queue fixture saves its temporary profile");
    const int mode = g_liveTileUpdateMode, target = g_manifestCompatibilityTarget, style = g_liveTileTemplateStyle;
    const bool available = contentNativeQueueAvailable, active = contentNativeQueueTestActive;
    const bool normalAck = contentQueueNormalAckEnabled;
    const auto requests = contentGenerationRequests;
    const auto queuedBackground = contentQueuedBackground;
    const auto batches = contentQueueBatches;
    const auto assets = g_nativeQueueAssets;
    aip::content::QueueDelta delivery;
    { std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex); delivery = g_contentQueueDelivery; g_contentQueueDelivery.LeaveQueue(); }
    const auto retry = g_contentQueueRetryAfter.load();
    const auto tracePath = ParentDirectoryOfPath(bitmapPath) + L"\\private-helper-trace.log";
    Check(WriteUtf8BomTextFile(tracePath, L"fixture start\r\n"), "create a private helper trace without starting any helper");
    unsigned long long traceOffset = 0;
    Check(TryGetFileSize64(tracePath, traceOffset), "capture the private trace boundary");
    const std::vector<LiveTileSuccessLogWatch> watches = {{tracePath, traceOffset}};
    Check(WriteUtf8BomTextFile(tracePath, L"fixture start\r\npid=1 Live Tile cleared under package identity\r\n") &&
        !WaitForPackagedLiveTileHelperSuccess(watches, 0), "a clear-only helper trace cannot acknowledge an update");
    Check(WaitForPackagedLiveTileHelperSuccess(watches, 0, L"Live Tile cleared under package identity", nullptr),
        "explicit clear watchers still recognize their own operation");
    Check(WriteUtf8BomTextFile(tracePath, L"fixture start\r\npid=1 Live Tile update completed under package identity\r\n") &&
        WaitForPackagedLiveTileHelperSuccess(watches, 0), "ordinary update watchers recognize a new update trace");
    Check(DeleteFileW(tracePath.c_str()) != FALSE, "remove only the private helper trace");
    g_contentQueueRetryAfter = 0;
    contentQueueNormalAckEnabled = true;
    contentNativeQueueTestActive = true; contentNativeQueueAvailable = false;
    contentGenerationRequests = 0; contentQueueBatches.clear();
    g_liveTileUpdateMode = static_cast<int>(LiveTileUpdateMode::LiveTile);
    g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
    g_liveTileTemplateStyle = static_cast<int>(LiveTileTemplateStyle::Adaptive);
    const auto profile = [&](const wchar_t* driver, bool cycle) {
        return std::wstring(L"[Content]\r\nEnabled=1\r\nCount=2\r\nCycleEnabled=") + (cycle ? L"1" : L"0") +
            L"\r\nCycleDriver=" + driver + L"\r\nCycleSeconds=60\r\nRefreshSeconds=86400\r\nTextMode=Auto\r\n"
            L"[Content.1]\r\nBackground=Image\r\nImagePath=" + bitmapPath + L"\r\nTextSources=CustomText\r\nText=First queue text\r\n"
            L"[Content.2]\r\nBackground=Image\r\nImagePath=" + bitmapPath + L"\r\nTextSources=CustomText\r\nText=Second queue text\r\n";
    };
    Check(WriteContentFixture(profile(L"DesktopStub", false)) && ContentEngineTick(), "establish the identical first app-managed frame before native queue entry");
    Check(contentGenerationRequests == 1 && contentQueueBatches.empty(), "app-managed baseline queues one intercepted update");
    contentNativeQueueAvailable = true;
    const auto initial = profile(L"Windows", true);
    Check(WriteContentFixture(initial) && ContentEngineTick(), "two static image/text contents compose into a native queue batch");
    Check(contentQueueBatches.size() == 1 && contentQueueBatches.back().size() == 2 &&
        contentQueueBatches.back()[0].nativeQueueTag == L"Content1" && contentQueueBatches.back()[0].nativeQueueReset &&
        contentQueueBatches.back()[1].nativeQueueTag == L"Content2" && !contentQueueBatches.back()[1].nativeQueueReset,
        "first batch has two stable tags and clears the old OS queue exactly once");
    const auto firstBatch = contentQueueBatches.back();
    const auto firstRequests = contentGenerationRequests;
    for (int n = 0; n < 20; ++n) Check(ContentEngineTick(), "unchanged native queue poll remains usable");
    Check(contentQueueBatches.size() == 1 && contentGenerationRequests == firstRequests,
        "acknowledged unchanged contents do not produce repeated delivery batches");
    auto changed = initial;
    Check(aip::WriteIniValueToText(changed, L"Content.2", L"Text", L"Changed second queue text") && WriteContentFixture(changed) && ContentEngineTick(),
        "a changed content caption composes without querying providers");
    Check(contentQueueBatches.size() == 2 && contentQueueBatches.back().size() == 1 &&
        contentQueueBatches.back()[0].nativeQueueTag == L"Content2" && !contentQueueBatches.back()[0].nativeQueueReset,
        "changing only one caption publishes only its existing queue tag");
    const auto changedFrame = contentQueueBatches.back().front();
    std::wstring originalVisual, changedVisual;
    { ContentRenderScope scope(&firstBatch[1]); originalVisual = BuildGenerationStateKey(bitmapPath); }
    { ContentRenderScope scope(&changedFrame); changedVisual = BuildGenerationStateKey(bitmapPath); }
    Check(originalVisual == changedVisual, "native XML text changes reuse the same visual bitmap cache key");
    auto overlayA = firstBatch[1], overlayB = changedFrame;
    overlayA.textMode = overlayB.textMode = aip::content::TextMode::Overlay;
    { ContentRenderScope scope(&overlayA); originalVisual = BuildGenerationStateKey(bitmapPath); }
    { ContentRenderScope scope(&overlayB); changedVisual = BuildGenerationStateKey(bitmapPath); }
    Check(originalVisual != changedVisual, "baked text changes retain distinct bitmap cache keys");
    const auto beforeExit = contentGenerationRequests;
    contentQueueNormalAckEnabled = false;
    Check(WriteContentFixture(profile(L"DesktopStub", false)) && ContentEngineTick() && contentGenerationRequests == beforeExit + 1,
        "leaving a native queue first requests identical content even when normal delivery will fail");
    { std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
        Check(g_contentQueueDelivery.CommittedCount() == 2, "a failed queue exit preserves the old acknowledged slots until Windows accepts replacement"); }
    g_contentQueueRetryAfter = GetTickCount64() + 100000;
    Check(ContentEngineTick() && contentGenerationRequests == beforeExit + 1, "failed queue-exit delivery observes its retry cooldown");
    g_contentQueueRetryAfter = 0;
    Check(ContentEngineTick() && contentGenerationRequests == beforeExit + 2, "an unacknowledged queue exit retries an unchanged first frame");
    contentQueueNormalAckEnabled = true;
    Check(ContentEngineTick() && contentGenerationRequests == beforeExit + 3, "queue exit continues retrying until a normal delivery is acknowledged");
    { std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
        Check(g_contentQueueDelivery.CommittedCount() == 0, "intercepted normal delivery clears queue acknowledgments"); }
    Check(ContentEngineTick() && contentGenerationRequests == beforeExit + 3, "acknowledged queue exit stops repeated normal updates");
    contentNativeQueueAvailable = false;
    Check(WriteContentFixture(initial) && ContentEngineTick() && contentQueueBatches.size() == 2,
        "unavailable native delivery uses the app-managed path without a native batch");
    aip::content::Configuration fallbackConfiguration; std::wstring fallbackError;
    Check(ReadContentConfiguration(fallbackConfiguration, fallbackError) &&
        ContentTimingWarning(fallbackConfiguration).find(L"package identity") != std::wstring::npos,
        "unavailable direct queue delivery explains the identity/helper fallback");

    contentNativeQueueAvailable = true;
    Check(WriteContentFixture(initial) && ContentEngineTick() && contentQueueBatches.back().size() == 2,
        "reenter a two-frame native queue before testing an empty enabled-content selection");
    auto disabled = initial;
    const auto beforeEmpty = contentGenerationRequests;
    Check(aip::WriteIniValueToText(disabled, L"Content.1", L"Enabled", L"0") &&
        aip::WriteIniValueToText(disabled, L"Content.2", L"Enabled", L"0") && WriteContentFixture(disabled) && ContentEngineTick() &&
        contentGenerationRequests == beforeEmpty + 1,
        "disabling every content requests one ordinary blank replacement for the old Windows queue");
    { ContentRenderScope scope;
        Check(scope.snapshot.active && scope.snapshot.textMode == aip::content::TextMode::Off &&
            scope.snapshot.backgroundId == aip::content::BackgroundId::None,
            "empty enabled-content selection publishes an explicit usable blank frame"); }
    { std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex);
        Check(g_contentQueueDelivery.CommittedCount() == 0, "acknowledged blank replacement leaves no old queued slots"); }
    Check(ContentEngineTick() && contentGenerationRequests == beforeEmpty + 1,
        "an acknowledged empty-selection replacement does not repeat every tick");
    std::wstring numbered = L"[Content]\r\nEnabled=1\r\nCount=10\r\nCycleEnabled=1\r\nCycleDriver=Windows\r\nTextMode=Auto\r\n";
    for (int index = 1; index <= 10; ++index)
        numbered += L"[Content." + std::to_wstring(index) + L"]\r\nEnabled=" + (index >= 9 ? std::wstring(L"1") : std::wstring(L"0")) +
            L"\r\nBackground=Image\r\nImagePath=" + bitmapPath + L"\r\nTextSources=CustomText\r\nText=Slot " + std::to_wstring(index) + L"\r\n";
    Check(WriteContentFixture(numbered) && ContentEngineTick() && contentQueueBatches.back().size() == 2 &&
        contentQueueBatches.back()[0].nativeQueueTag == L"Content9" && contentQueueBatches.back()[1].nativeQueueTag == L"Content10",
        "sparse high-numbered contents keep distinct stable queue tags");
    Check(WriteContentFixture(profile(L"DesktopStub", false)) && ContentEngineTick(),
        "app-managed empty selection fixture first displays an active item");
    auto appEmpty = profile(L"DesktopStub", false);
    Check(aip::WriteIniValueToText(appEmpty, L"Content.1", L"Enabled", L"0") &&
        aip::WriteIniValueToText(appEmpty, L"Content.2", L"Enabled", L"0"), "disable every app-managed content in the private document");
    const auto beforeAppEmpty = contentGenerationRequests;
    Check(WriteContentFixture(appEmpty) && ContentEngineTick() && contentGenerationRequests == beforeAppEmpty + 1,
        "disabling every app-managed item also queues one blank delivery");
    { ContentRenderScope scope; Check(scope.snapshot.textMode == aip::content::TextMode::Off,
        "empty app-managed selection publishes a text-free frame"); }
    Check(ContentEngineTick() && contentGenerationRequests == beforeAppEmpty + 1,
        "an unchanged empty app-managed selection does not repeatedly generate");
    const auto& tile = g_liveTileAssets[0];
    const auto directory = ParentDirectoryOfPath(bitmapPath) + L"\\native-queue-assets";
    Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "create a private asset-retention fixture directory");
    std::vector<std::wstring> created;
    for (int index : {9, 10, 1, 2, 3}) {
        const auto token = Hex64(static_cast<std::uint64_t>(index));
        const auto relative = VersionedLiveTileAssetPath(tile, token);
        const auto path = ResolveManifestAssetPath(directory, relative);
        DWORD error = ERROR_SUCCESS;
        Check(EnsureParentDirectoryForFile(path, &error) && WriteWholeFileBytes(path, {1}), "create only named inert versioned assets");
        HANDLE file = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        FILETIME time{}; time.dwLowDateTime = index >= 9 ? static_cast<DWORD>(index) : static_cast<DWORD>(1000 + index);
        Check(file != INVALID_HANDLE_VALUE && SetFileTime(file, nullptr, nullptr, &time), "give referenced assets older times than unrelated candidates");
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        created.push_back(path);
        if (index >= 9) g_nativeQueueAssets[L"Content" + std::to_wstring(index)] = {{tile.name, tile.file, tile.binding, relative, tile.w, tile.h}};
    }
    DeleteOldVersionedLiveTileAssets(directory, {});
    Check(GetFileAttributesW(created[0].c_str()) != INVALID_FILE_ATTRIBUTES && GetFileAttributesW(created[1].c_str()) != INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(created[2].c_str()) == INVALID_FILE_ATTRIBUTES,
        "pruning protects both older queued slots while removing an unreferenced old asset");
    for (const auto& path : created) if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) DeleteFileW(path.c_str());
    RemoveDirectoryW(ParentDirectoryOfPath(created[0]).c_str()); RemoveDirectoryW(directory.c_str());

    const auto savedSmtcReader = g_contentSmtc.testReader;
    g_contentSmtc.Cancel();
    std::atomic<int> singleSmtcReads{0};
    std::atomic<bool> singleSmtcArtwork{false};
    g_contentSmtc.testReader = [&](auto, bool artwork, const auto&) {
        ++singleSmtcReads; singleSmtcArtwork = artwork;
        aip::content::SmtcSessionData data;
        data.hasSession = true; data.title = L"Single direct tile fixture";
        data.artist = L"Fixture artist"; data.appId = L"Fixture.Player"; data.status = L"Playing";
        return data;
    };
    const auto singleSmtcProfile = std::wstring(L"[Content]\r\nEnabled=1\r\nCount=1\r\nCycleEnabled=1\r\nCycleDriver=Windows\r\n"
        L"RefreshSeconds=86400\r\nTextMode=Auto\r\n[Content.1]\r\nBackground=Image\r\nImagePath=") + bitmapPath + L"\r\nTextSources=SMTC\r\n";
    const auto beforeSingleBatch = contentQueueBatches.size();
    const auto beforeSingleRequests = contentGenerationRequests;
    Check(WriteContentFixture(singleSmtcProfile) && ContentEngineTick(true),
        "single direct Live Tile reads an explicitly injected SMTC snapshot");
    aip::content::Configuration singleSmtcConfiguration; std::wstring singleSmtcError;
    Check(ReadContentConfiguration(singleSmtcConfiguration, singleSmtcError) && ContentDirectLiveDeliveryAvailable() &&
        !ContentNativeQueueAvailable(singleSmtcConfiguration) && g_contentSmtc.key.find(L":events") != std::wstring::npos,
        "single direct Live Tile uses SMTC events independently of native queue eligibility");
    { ContentRenderScope scope;
        Check(!scope.snapshot.nativeQueue && scope.snapshot.text.primary == L"Single direct tile fixture" &&
            singleSmtcReads == 1 && !singleSmtcArtwork,
            "single direct SMTC fixture publishes its ordinary coherent frame"); }
    Check(ContentEngineTick() && contentQueueBatches.size() == beforeSingleBatch &&
        contentGenerationRequests == beforeSingleRequests + 1 && singleSmtcReads == 1,
        "single direct SMTC delivery creates one ordinary update without a native queue batch or repeated read");
    g_contentSmtc.Cancel(); g_contentSmtc.testReader = savedSmtcReader;

    Check(WriteContentFixture(profile(L"DesktopStub", false)) && ContentEngineTick(false, false), "native queue fixture leaves its static host state in app-managed mode");
    Check(WriteContentFixture(original), "native queue fixture restores its original temporary profile");
    g_liveTileUpdateMode = mode; g_manifestCompatibilityTarget = target; g_liveTileTemplateStyle = style;
    contentNativeQueueAvailable = available; contentNativeQueueTestActive = active;
    contentQueueNormalAckEnabled = normalAck;
    contentGenerationRequests = requests; contentQueuedBackground = queuedBackground; contentQueueBatches = batches;
    g_nativeQueueAssets = assets; g_contentQueueRetryAfter = retry;
    { std::lock_guard<std::mutex> lock(g_contentQueueDeliveryMutex); g_contentQueueDelivery = delivery; }
    ContentEngineTick(false, false);
}

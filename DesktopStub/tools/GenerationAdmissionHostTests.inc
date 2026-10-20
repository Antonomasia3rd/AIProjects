// Call the actual Generate entry point. The inert-only hook returns immediately
// after its configuration/target/queue/direct-delivery admission checks, before
// any asset pipeline, package registration, or notification transport operation.
static unsigned generationAdmissionCount = 0;
static bool GenerationAdmissionAccepted(const aip::content::Snapshot&) { ++generationAdmissionCount; return true; }
static void TestGenerationAdmissionHost(const std::wstring& bitmapPath)
{
    using namespace aip::content;
    std::wstring original; Check(ReadIniFileForMutation(original), "generation admission captures the private profile");
    const auto oldHook = g_testGenerateAfterAdmission;
    const auto oldNative = contentNativeQueueAvailable, oldPinned = secondaryTestPinned;
    const auto oldMode = g_liveTileUpdateMode.load(), oldTarget = g_manifestCompatibilityTarget.load();
    const auto oldLogging = g_logging.load(), oldNotifications = g_notificationsEnabled.load();
    const auto oldStart = g_notifyOnStart.load(), oldSuccess = g_notifyOnSuccess.load(), oldFailure = g_notifyOnFailure.load(), oldBusy = g_notifyOnBusy.load();
    const auto oldGallery = galleryHostFrames; const bool oldGalleryValid = galleryHostValid;
    const auto oldGalleryFailures = galleryHostFailures;
    const auto oldLog = GetLogPathCopy();
    g_testGenerateAfterAdmission = GenerationAdmissionAccepted;
    contentNativeQueueAvailable = true; secondaryTestPinned = true;
    const std::wstring settings = L"[Settings]\r\nExperimentalLiveTileUpdate=LiveTile\r\nAppxManifestTarget=Windows10\r\nLogging=0\r\n"
        L"NotificationsEnabled=0\r\nNotifyOnStart=0\r\nNotifyOnSuccess=0\r\nNotifyOnFailure=0\r\nNotifyOnBusy=0\r\nShowConsole=0\r\n"
        L"[State]\r\nLiveTileModeReregistrationPending=0\r\n";
    const std::wstring instanceProfile = settings + L"[Tiles]\r\nInstances=desktop,feed1,clock\r\n"
        L"[Tile.desktop]\r\nType=Wallpaper\r\nPrimary=1\r\nContent.Background=Transparent\r\n"
        L"[Tile.feed1]\r\nType=RSS\r\nRssFeed.FeedUrl=\r\n"
        L"[Tile.clock]\r\nType=PrayerTimes\r\n";
    Check(WriteContentFixture(instanceProfile), "generation admission selects private independent source instances");
    Snapshot source; source.active = true; source.backgroundPath = bitmapPath; source.backgroundId = BackgroundId::Transparent;
    source.secondaryTileId = L"AipSource_feed1"; source.name = L"Independent RSS"; source.text.primary = L"Synthetic source";
    const auto admits = [&](const Snapshot& frame) {
        const auto count = generationAdmissionCount;
        const bool result = Generate(bitmapPath.c_str(), L"inert generation admission fixture", false, &frame);
        return result && generationAdmissionCount == count + 1;
    };
    const auto rejects = [&](const Snapshot& frame) {
        const auto count = generationAdmissionCount;
        return !Generate(bitmapPath.c_str(), L"inert generation admission fixture", false, &frame) && generationAdmissionCount == count;
    };
    Check(admits(source), "actual Generate admits a configured pinned independent source without legacy secondary mappings");
    auto prayer = source; prayer.secondaryTileId = L"AipSource_clock";
    Check(admits(prayer), "actual Generate admits another independent source type through the same native batch boundary");
    secondaryTestPinned = false;
    Check(rejects(source), "actual Generate rejects an independent destination that is no longer pinned");
    secondaryTestPinned = true;
    auto staleLegacy = source; staleLegacy.secondaryTileId = L"AipSource_rss";
    Check(rejects(staleLegacy), "actual Generate rejects legacy fixed-source IDs absent from the current independent tile list");
    auto wrongPrimary = source; wrongPrimary.secondaryTileId = L"AipSource_desktop";
    Check(rejects(wrongPrimary), "a primary owner cannot be addressed as a secondary destination");
    auto primary = source; primary.secondaryTileId.clear(); primary.backgroundId = BackgroundId::Image;
    Check(admits(primary), "actual Generate admits the currently configured primary owner");
    auto removed = instanceProfile;
    Check(WriteIniValueToText(removed,L"Tiles",L"Instances",L"desktop,clock") && WriteContentFixture(removed) && rejects(source),
        "a queued independent source is rejected after its configuration is removed");
    auto noPrimary = instanceProfile;
    Check(WriteIniValueToText(noPrimary,L"Tiles",L"Instances",L"feed1,clock") && WriteContentFixture(noPrimary) && rejects(primary),
        "a previously prepared primary frame is rejected after its owner is removed");
    Snapshot retired; retired.active = true; retired.backgroundPath = bitmapPath; retired.backgroundId = BackgroundId::Transparent; retired.textMode = TextMode::Off;
    Check(admits(retired), "removing the primary still admits its retained empty transparent retirement notification");
    retired.text.primary = L"Stale primary text";
    Check(rejects(retired), "the primary retirement exception cannot carry stale visible text");
    auto brokenPrimary = instanceProfile;
    Check(WriteIniValueToText(brokenPrimary,L"Tile.desktop",L"Content.Background",L"invalid-background") && WriteContentFixture(brokenPrimary) && admits(source),
        "an unrelated primary configuration error does not block a healthy independent source at Generate admission");
    Check(WriteContentFixture(instanceProfile), "generation admission restores valid independent configuration");
    contentNativeQueueAvailable = false;
    Check(rejects(source), "independent admission preserves the direct live delivery requirement");
    contentNativeQueueAvailable = true;
    auto gallery = GallerySyntheticWindow(bitmapPath,L"admission"); galleryHostFrames = gallery; galleryHostValid = true;
    Check(admits(gallery.front()), "actual Generate retains gallery receipt admission independently of ordinary source ownership");
    galleryHostValid = false;
    Check(rejects(gallery.front()), "actual Generate still rejects stale gallery receipts");
    std::wstring legacy = settings + L"[Content]\r\nEnabled=1\r\nCount=8\r\n[Tiles]\r\nEnabled=1\r\n";
    for (int i = 1; i <= 8; ++i) legacy += L"[Content." + std::to_wstring(i) + L"]\r\nEnabled=1\r\nBackground=Image\r\nImagePath=" + bitmapPath + L"\r\nTextSources=CustomText\r\n";
    for (const auto& descriptor : SecondaryTiles) legacy += L"[Tile." + std::wstring(descriptor.id) + L"]\r\nEnabled=" +
        (std::wstring(descriptor.id) == L"smtc" ? L"1" : L"0") + L"\r\n";
    Check(WriteContentFixture(legacy), "generation admission uses the existing private legacy mapped-source profile");
    auto mapped = source; mapped.secondaryTileId = L"AipSource_smtc";
    Check(admits(mapped), "legacy mapped source admission remains supported");
    Check(rejects(source), "legacy mode rejects an independent ID absent from its fixed source mapping");
    Check(WriteIniValueToText(legacy,L"Content.2",L"Enabled",L"0") && WriteContentFixture(legacy) && rejects(mapped),
        "legacy source admission still rejects a disabled mapped content entry");
    g_testGenerateAfterAdmission = oldHook;
    contentNativeQueueAvailable = oldNative; secondaryTestPinned = oldPinned;
    galleryHostFrames = oldGallery; galleryHostValid = oldGalleryValid; galleryHostFailures = oldGalleryFailures;
    Check(WriteContentFixture(original), "generation admission restores its private profile");
    LoadRuntimeSettingsFromIni(false,false);
    g_liveTileUpdateMode = oldMode; g_manifestCompatibilityTarget = oldTarget;
    g_logging = oldLogging; g_notificationsEnabled = oldNotifications;
    g_notifyOnStart = oldStart; g_notifyOnSuccess = oldSuccess; g_notifyOnFailure = oldFailure; g_notifyOnBusy = oldBusy;
    SetLogPathCopy(oldLog);
}

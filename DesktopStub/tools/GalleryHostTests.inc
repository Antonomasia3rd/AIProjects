// Synthetic gallery snapshots only. No inventory, native pin, transport,
// registration, account, provider, or user photo is called by these fixtures.
static std::vector<aip::content::Snapshot> galleryHostFrames;
static std::vector<aip::content::Snapshot> galleryHostAcks, galleryHostFailures;
static bool galleryHostValid = true;
static std::vector<aip::content::Snapshot> GalleryTestFrames() { return galleryHostFrames; }
static bool GalleryTestValidate(const aip::content::Snapshot& frame) {
    return galleryHostValid && std::any_of(galleryHostFrames.begin(), galleryHostFrames.end(), [&](const auto& current) {
        return current.secondaryTileId == frame.secondaryTileId && current.secondaryFrameKey == frame.secondaryFrameKey &&
            current.gallerySlot == frame.gallerySlot && current.galleryTicket == frame.galleryTicket;
    });
}
static void GalleryTestAck(const aip::content::Snapshot& frame) { galleryHostAcks.push_back(frame); }
static void GalleryTestFail(const aip::content::Snapshot& frame) { galleryHostFailures.push_back(frame); }
static std::vector<aip::content::Snapshot> GallerySyntheticWindow(const std::wstring& bitmapPath, const std::wstring& member) {
    std::vector<aip::content::Snapshot> frames;
    for (unsigned slot = 0; slot < 5; ++slot) {
        aip::content::Snapshot frame; frame.active = true; frame.backgroundId = aip::content::BackgroundId::Image;
        frame.backgroundPath = bitmapPath; frame.textMode = aip::content::TextMode::Off; frame.name = L"Synthetic gallery";
        frame.galleryFrame = true; frame.galleryGroupId = L"fixture"; frame.galleryTileId = member; frame.galleryEpoch = L"epoch";
        frame.galleryTicket = 1; frame.gallerySlot = slot; frame.gallerySlotCount = 5; frame.secondaryBranding = L"Name";
        frame.secondaryTileId = aip::gallery::NativeTileId(L"fixture", member); frame.nativeQueue = true;
        frame.nativeQueueReset = slot == 0; frame.nativeQueueTag = L"g" + std::to_wstring(slot + 1); frame.nativeQueueLayout = L"synthetic-gallery-layout";
        frame.secondaryFrameKey = member + L":ticket1:" + std::to_wstring(slot); frame.nativeQueueFrameKey = frame.secondaryFrameKey;
        frames.push_back(std::move(frame));
    }
    return frames;
}
static void TestGalleryHost(const std::wstring& bitmapPath)
{
    std::wstring original; ReadIniFileForMutation(original);
    const auto savedDelivery = g_secondaryDelivered;
    const auto savedAssets = g_secondaryTileAssets;
    const bool savedNative = contentNativeQueueAvailable, savedPinned = secondaryTestPinned, savedDisable = g_disableFitting.load();
    const auto savedRetry = g_contentQueueRetryAfter.load();
    contentNativeQueueAvailable = true; secondaryTestPinned = true; galleryHostValid = true; g_contentQueueRetryAfter = 0;
    g_secondaryDelivered.clear(); g_secondaryTileAssets.clear(); galleryHostFrames = GallerySyntheticWindow(bitmapPath, L"one");
    secondaryTestExistsCalls = 0;
    Check(PrepareSecondaryBatch(galleryHostFrames).size() == 5 && secondaryTestExistsCalls == 1,
        "five dirty gallery slots query pin existence once for their logical destination");
    for (const auto& frame : galleryHostFrames) AcknowledgeSecondaryFrame(frame);
    secondaryTestExistsCalls = 0;
    Check(PrepareSecondaryBatch(galleryHostFrames).empty() && secondaryTestExistsCalls == 0,
        "unchanged acknowledged gallery slots perform zero native pin queries");
    auto source = galleryHostFrames.front(); source.galleryFrame = false; source.nativeQueue = false; source.secondaryTileId = L"AipSource_smtc";
    source.secondaryFrameKey = L"unchanged-source"; AcknowledgeSecondaryFrame(source); secondaryTestExistsCalls = 0;
    Check(PrepareSecondaryBatch({source}).empty() && secondaryTestExistsCalls == 0,
        "unchanged ordinary source frames also avoid native pin queries");
    ForgetSecondaryTileDelivery(galleryHostFrames.front().secondaryTileId);
    Check(PrepareSecondaryBatch(galleryHostFrames).size() == 5 && g_secondaryDelivered.count(source.secondaryTileId),
        "pin mutation forgets all gallery slot keys without clearing another source acknowledgement");
    auto incomplete = galleryHostFrames; incomplete.pop_back(); galleryHostFailures.clear();
    Check(PrepareSecondaryBatch(incomplete).empty() && !galleryHostFailures.empty(), "an incomplete window cannot deliver a reset-bearing partial queue");
    const auto other = GallerySyntheticWindow(bitmapPath, L"two");
    const std::vector<LiveTileUpdateAsset> asset{{L"LiveMediumTile", L"Assets\\LiveMediumTile.png", L"TileMedium", L"Assets\\synthetic.png", 150, 150}};
    for (const auto& frame : galleryHostFrames) RetainSecondaryTileAssets(frame, asset);
    for (const auto& frame : other) RetainSecondaryTileAssets(frame, asset);
    Check(g_secondaryTileAssets.size() == 10, "retention keeps all five asset sets for each independent gallery target");
    RetainSecondaryTileAssets(galleryHostFrames.front(), asset);
    Check(g_secondaryTileAssets.size() == 6 && g_secondaryTileAssets.count(SecondaryDeliveryKey(other.back())),
        "accepted reset retires only its target's old slot references");
    const auto primaryCount = g_contentQueueDelivery.CommittedCount();
    AcknowledgeContentQueueFrame(galleryHostFrames.front());
    Check(g_contentQueueDelivery.CommittedCount() == primaryCount, "gallery acknowledgements cannot mutate the primary native queue delta");
    galleryHostFailures.clear();
    { GalleryGenerationGuard failure(&galleryHostFrames.front(), false); }
    Check(galleryHostFailures.size() == 1, "generation early-return guard reports an undelivered gallery slot");
    try { GalleryGenerationGuard failure(&galleryHostFrames.front(), false); throw 1; } catch (int) {}
    Check(galleryHostFailures.size() == 2, "generation exception unwinding reports the gallery attempt failure");
    { GalleryGenerationGuard success(&galleryHostFrames.front(), false); success.delivered = true; }
    { GalleryGenerationGuard offline(&galleryHostFrames.front(), true); }
    Check(galleryHostFailures.size() == 2, "successful and offline-only renders do not publish false failures");
    { GalleryBatchFailureGuard skipped{galleryHostFrames}; skipped.next = 2; }
    Check(galleryHostFailures.size() == 5, "unprocessed batch slots receive failure when an earlier target stops generation");
    galleryHostValid = false;
    Check(!ContentGalleryFrameCurrent(galleryHostFrames.front()), "late target guard rejects a stale gallery receipt before transport");
    galleryHostValid = true;
    auto text = BuildInitialIniTemplate();
    const auto put = [&](const std::wstring& section, const wchar_t* key, const std::wstring& value) { return aip::WriteIniValueToText(text, section.c_str(), key, value); };
    put(L"Content", L"Enabled", L"1"); put(L"Content", L"Count", L"7"); put(L"Tiles", L"Enabled", L"1");
    for (unsigned slot = 1; slot <= 7; ++slot) {
        const auto section = L"Content." + std::to_wstring(slot);
        put(section, L"Enabled", L"1"); put(section, L"Background", L"Image"); put(section, L"ImagePath", bitmapPath); put(section, L"TextSources", L"CustomText");
    }
    g_secondaryDelivered.clear(); secondaryTestBatches.clear(); galleryHostAcks.clear();
    Check(WriteContentFixture(text) && ContentEngineTick(false) && !secondaryTestBatches.empty() && secondaryTestBatches.back().size() == 12,
        "one batch preserves primary and all six source frames while appending five gallery slots");
    Check(!secondaryTestBatches.empty() && secondaryTestBatches.back().front().secondaryTileId.empty() && galleryHostAcks.size() == 5,
        "mixed delivery retains the primary destination and routes all gallery successes to the gallery hook");
    {
        ScopedGdiPlus gdiplus;
        Check(gdiplus.ok(), "gallery pixel fixture initializes only in-memory GDI+");
        if (gdiplus.ok()) {
            // Saturated channels stay exact under the production bicubic
            // resampler; intermediate channels may overshoot by one level.
            Gdiplus::Bitmap pixels(40, 10, PixelFormat32bppARGB); { Gdiplus::Graphics fill(&pixels); fill.Clear(Gdiplus::Color(255, 255, 0, 0)); }
            auto frame = galleryHostFrames.front(); frame.galleryContain = true; g_disableFitting = true;
            ContentRenderScope render(&frame); FitMode mode = FitMode::Fill; std::unique_ptr<Gdiplus::Bitmap> prepared; Gdiplus::Bitmap* selected = nullptr;
            Check(PrepareWallpaperRenderSource(&pixels, mode, prepared, selected) && mode == FitMode::Fit && selected == &pixels && !prepared,
                "gallery contain ignores desktop fitting disable and never prepares desktop Tile/Span canvases");
            const auto containKey = BuildGenerationStateKey(bitmapPath);
            std::unique_ptr<Gdiplus::Bitmap> contained(ResizeBitmapToMode(selected, 20, 20, mode, ContentCanvasBackgroundColor()));
            Gdiplus::Color corner, center, sourceCenter;
            const auto sourceStatus = pixels.GetPixel(20, 5, &sourceCenter);
            const auto cornerStatus = contained ? contained->GetPixel(0, 0, &corner) : Gdiplus::InvalidParameter;
            const auto centerStatus = contained ? contained->GetPixel(10, 10, &center) : Gdiplus::InvalidParameter;
            std::cerr << "Gallery contain pixels: sourceARGB=" << sourceCenter.GetValue() << "; cornerARGB=" << corner.GetValue()
                << "; centerARGB=" << center.GetValue() << "; readStatus=" << sourceStatus << ',' << cornerStatus << ',' << centerStatus << '\n';
            Check(contained && sourceStatus == Gdiplus::Ok && cornerStatus == Gdiplus::Ok && centerStatus == Gdiplus::Ok &&
                sourceCenter.GetValue() == 0xFFFF0000u && corner.GetA() == 0 && center.GetValue() == 0xFFFF0000u,
                "contain preserves exact photo aspect with transparent letterbox pixels and opaque photo center");
            render.snapshot.galleryContain = false;
            Check(PrepareWallpaperRenderSource(&pixels, mode, prepared, selected) && mode == FitMode::Fill && BuildGenerationStateKey(bitmapPath) != containKey,
                "gallery fill has an independent render mode and generated asset cache key");
            render.snapshot.galleryFrame = false;
            std::unique_ptr<Gdiplus::Bitmap> primary(ResizeWithFit(&pixels, 20, 20, FitMode::Fit)); Gdiplus::Color opaque;
            if (primary) primary->GetPixel(0, 0, &opaque);
            Check(primary && opaque.GetA() == 255 && opaque.GetR() == 0, "ordinary image fit retains its existing opaque black canvas");
        }
    }
    galleryHostFrames.clear(); galleryHostAcks.clear(); galleryHostFailures.clear(); galleryHostValid = true;
    g_secondaryDelivered = savedDelivery; g_secondaryTileAssets = savedAssets;
    contentNativeQueueAvailable = savedNative; secondaryTestPinned = savedPinned; g_disableFitting = savedDisable; g_contentQueueRetryAfter = savedRetry;
    Check(WriteContentFixture(original), "gallery fixture restores the prior private configuration");
}

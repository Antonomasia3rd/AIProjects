// Prepared/submitted metadata is injected. No gallery scanner, real provider,
// Windows notification, pin, account, user photo or resident is called.
static bool galleryStatusWindowsOverridden = false;
static std::vector<GalleryStatusSnapshots> galleryStatusWindowsOverride;
static std::vector<GalleryStatusSnapshots> GalleryTestStatusWindows()
{
    if (galleryStatusWindowsOverridden) return galleryStatusWindowsOverride;
    // Preserve the existing status/dialog fixtures' synthetic prepared queues.
    std::map<std::wstring, std::size_t> indices; std::vector<GalleryStatusSnapshots> result;
    for (const auto& frame : CurrentSecondarySnapshots()) if (frame.active && frame.galleryFrame) {
        const auto inserted = indices.emplace(frame.secondaryTileId, result.size());
        if (inserted.second) result.push_back({{}, false});
        result[inserted.first->second].frames.push_back(frame);
    }
    return result;
}
static void TestGalleryStatusRetentionHost(const std::wstring& bitmapPath)
{
    const auto snapshots = CurrentSecondarySnapshots(); const auto pending = galleryHostFrames;
    const auto oldWindows = galleryStatusWindowsOverride; const bool oldOverride = galleryStatusWindowsOverridden;
    std::wstring original; Check(ReadIniFileForMutation(original), "status retention fixture reads only the private profile");
    auto configured = original;
    Check(WriteIniValueToText(configured, L"Strings", L"TileStatusSubmittedQueue", L"Submitted fixture queue (visible image unknown)") && WriteContentFixture(configured),
        "submitted queue wording uses the existing configurable status-label pattern");
    auto submitted = GallerySyntheticWindow(bitmapPath, L"retained");
    for (auto& frame : submitted) {
        frame.name = L"Retained gallery"; frame.secondaryFrameKey.clear(); frame.nativeQueueFrameKey.clear();
        frame.nativeQueue = false; frame.nativeQueueReset = false;
    }
    galleryStatusWindowsOverridden = true; galleryStatusWindowsOverride = {{submitted, true}}; galleryHostFrames.clear();
    aip::content::Snapshot primary; primary.active = true; primary.name = L"Primary fixture"; primary.text.primary = L"Primary text";
    PublishSecondarySnapshots({primary});
    auto pages = BuildCurrentTileStatusPages();
    Check(ContentGalleryFrames().empty() && pages.size() == 2 && pages.back().find(L"Retained gallery") != std::wstring::npos &&
        pages.back().find(L"Submitted fixture queue (visible image unknown)") != std::wstring::npos && pages.back().find(L"Slot 5 [g5]") != std::wstring::npos,
        "idle status includes all submitted gallery slots after its delivery pipeline is empty");
    Check(PrepareSecondaryBatch(submitted).empty(), "status-only snapshots have no delivery keys and cannot be resubmitted as pending work");
    PublishSecondarySnapshots({}); pages = BuildCurrentTileStatusPages();
    Check(pages.size() == 1 && pages.front().find(L"Retained gallery") != std::wstring::npos && pages.front().find(ReadTileStatusLabels().noSnapshot) == std::wstring::npos,
        "a gallery-only status does not acquire a misleading empty ordinary page");
    auto prepared = submitted;
    for (auto& frame : prepared) { frame.name = L"Prepared replacement"; ++frame.galleryTicket; }
    galleryStatusWindowsOverride = {{prepared, false}}; pages = BuildCurrentTileStatusPages();
    Check(pages.size() == 1 && pages.front().find(ReadTileStatusLabels().queue) != std::wstring::npos &&
        pages.front().find(L"Submitted fixture queue") == std::wstring::npos,
        "new prepared queues remain distinguishable from the last complete submitted queue");
    galleryStatusWindowsOverride = {{submitted, true}};
    PublishSecondarySnapshots(prepared); pages = BuildCurrentTileStatusPages();
    Check(pages.size() == 1 && pages.front().find(L"Retained gallery") != std::wstring::npos && pages.front().find(L"Prepared replacement") == std::wstring::npos,
        "a stale prepared snapshot cannot duplicate or override the authoritative gallery status window");
    galleryStatusWindowsOverride.clear(); pages = BuildCurrentTileStatusPages();
    Check(pages == std::vector<std::wstring>{ReadTileStatusLabels().noSnapshot},
        "invalidated or unpinned gallery status disappears even before old ordinary snapshots are replaced");
    galleryStatusWindowsOverridden = oldOverride; galleryStatusWindowsOverride = oldWindows; galleryHostFrames = pending;
    PublishSecondarySnapshots(snapshots); Check(WriteContentFixture(original), "status retention fixture restores its private configuration and snapshots");
}

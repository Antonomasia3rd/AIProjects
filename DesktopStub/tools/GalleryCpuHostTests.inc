// Private-profile, injected cleanup and synthetic metadata only. No provider,
// inventory, pin, package, notification or resident entry point is called.
static void TestGalleryCpuHost(const std::wstring& directory, const std::wstring& bitmapPath)
{
    const auto beforeCleanup = contentAssetCleanupDirectories.size();
    {
        VersionedLiveTileAssetCleanupBatch batch;
        for (unsigned slot = 0; slot < 80; ++slot) RequestVersionedLiveTileAssetCleanup(directory, {});
        Check(contentAssetCleanupDirectories.size() == beforeCleanup, "eighty successful slot cleanup requests stay deferred until the batch ends");
    }
    Check(contentAssetCleanupDirectories.size() == beforeCleanup + 1, "one eighty-slot batch performs one global asset cleanup");
    try {
        VersionedLiveTileAssetCleanupBatch batch;
        for (unsigned slot = 0; slot < 27; ++slot) RequestVersionedLiveTileAssetCleanup(directory, {});
        throw 1;
    } catch (int) {}
    Check(contentAssetCleanupDirectories.size() == beforeCleanup + 2, "partial batch failure still performs one deferred cleanup");
    {
        VersionedLiveTileAssetCleanupBatch empty;
    }
    Check(contentAssetCleanupDirectories.size() == beforeCleanup + 2, "a batch without published assets performs no cleanup");
    RequestVersionedLiveTileAssetCleanup(directory, {});
    Check(contentAssetCleanupDirectories.size() == beforeCleanup + 3, "a standalone generation retains immediate cleanup semantics");
    contentAssetCleanupDirectories.resize(beforeCleanup);

    const auto oldMode = g_liveTileUpdateMode.load(); const bool oldIcons = g_generateDesktopIconForDisabledEntries.load();
    g_liveTileUpdateMode = static_cast<int>(LiveTileUpdateMode::LiveTile); g_generateDesktopIconForDisabledEntries = true;
    const auto manifest = CurrentManifestDisplayInfo(); const std::vector<int> scales{100, 200};
    const auto root = directory + L"\\gallery-cpu-static";
    const auto primaryPath = ResolveManifestAssetPath(root, ManifestAssetPathForTile(manifest, g_tiles[1]));
    Check(EnsureParentDirectoryForFile(primaryPath) && WriteWholeFileBytes(primaryPath, {8, 7, 6, 5}), "static-scope fixture owns one synthetic primary asset sentinel");
    std::vector<LiveTileUpdateAsset> live;
    aip::content::Snapshot snapshot; snapshot.active = true; snapshot.backgroundPath = bitmapPath; snapshot.textMode = aip::content::TextMode::Off;
    std::wstring secondaryKey;
    {
        snapshot.secondaryTileId = L"AipGallery_fixture.one"; snapshot.galleryFrame = true;
        ContentRenderScope scope(&snapshot);
        const auto files = BuildGeneratedAssetCacheFileList(root, manifest, scales, true, L"0123456789abcdef", live);
        Check(files.size() == 3 && live.size() == 3 && std::all_of(files.begin(), files.end(), [](const auto& file) {
            return file.cacheRelativePath.compare(0, 11, L"Assets\\Live") == 0;
        }), "secondary gallery cache plans contain only three live images, with no primary icon or scale variants");
        secondaryKey = BuildGenerationStateKey(bitmapPath);
        scope.snapshot.secondaryTileId.clear();
        Check(BuildGenerationStateKey(bitmapPath) != secondaryKey, "live-only secondary assets have a separate cache namespace even with identical gallery pixels and metadata");
        scope.snapshot.secondaryTileId = snapshot.secondaryTileId;
        g_generateDesktopIconForDisabledEntries = false;
        DeleteDisabledAssetsForCurrentConfig(root, manifest, true);
        std::vector<BYTE> unchanged;
        Check(ReadWholeFileBytes(primaryPath, unchanged) && unchanged == std::vector<BYTE>({8, 7, 6, 5}), "a secondary delivery never removes an existing primary asset even when disabled assets normally get deleted");
        g_generateDesktopIconForDisabledEntries = true;
    }
    {
        snapshot.secondaryTileId.clear(); snapshot.galleryFrame = false;
        ContentRenderScope scope(&snapshot);
        const auto files = BuildGeneratedAssetCacheFileList(root, manifest, scales, true, L"0123456789abcdef", live);
        Check(files.size() > 3 && live.size() == 3, "primary generation keeps its static icon and scale asset plan");
        Check(BuildGenerationStateKey(bitmapPath) != secondaryKey, "secondary live-only cache entries cannot reuse older mixed primary asset entries");
    }
    DeleteFileW(primaryPath.c_str()); RemoveDirectoryW(ParentDirectoryOfPath(primaryPath).c_str()); RemoveDirectoryW(root.c_str());
    g_liveTileUpdateMode = oldMode; g_generateDesktopIconForDisabledEntries = oldIcons;

    std::wstring original; Check(ReadIniFileForMutation(original), "configuration-view fixture reads only its existing private profile");
    auto configured = original;
    Check(WriteIniValueToText(configured, L"Gallery", L"Enabled", L"0") &&
        WriteIniValueToText(configured, L"Gallery", L"Folder", L"view_A"), "prepare a valid typed gallery configuration without enabling inventory");
    Check(WriteContentFixture(configured), "write the private gallery view fixture");
    std::wstring error; const auto parsesBefore = galleryConfigurationParses;
    const auto first = CaptureGalleryConfigurationView(error, true);
    Check(first && galleryConfigurationParses == parsesBefore + 1, "first strict configuration capture creates one typed immutable view");
    const auto revision = g_iniCacheRevision;
    bool same = true;
    for (unsigned frame = 0; frame < 80; ++frame) same = CaptureGalleryConfigurationView(error, true) == first && same;
    Check(same && galleryConfigurationParses == parsesBefore + 1 && g_iniCacheRevision == revision,
        "eighty strict fresh-byte checks reuse one typed view and INI document without a TTL");
    FILETIME oldWrite{}; Check(GetFileWriteTime(g_iniPath, oldWrite), "capture only the private INI timestamp for the preserved-time regression");
    auto changed = configured;
    Check(WriteIniValueToText(changed, L"Gallery", L"Folder", L"view_B") && WriteUtf8BomTextFile(g_iniPath, changed), "edit private gallery bytes without refreshing the cache first");
    const auto timestampHandle = CreateFileW(g_iniPath.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(timestampHandle != INVALID_HANDLE_VALUE && SetFileTime(timestampHandle, nullptr, nullptr, &oldWrite), "restore the fixture's old timestamp to rule out timestamp-only freshness");
    if (timestampHandle != INVALID_HANDLE_VALUE) CloseHandle(timestampHandle);
    const auto newer = CaptureGalleryConfigurationView(error, true);
    Check(newer && newer != first && newer->revision != first->revision && newer->configuration.folder != first->configuration.folder,
        "strict publication validation sees changed bytes even when the INI timestamp and length are preserved");
    const auto journal = aip::gallery::transaction::Path(aip::MakeAbsolutePath(g_iniPath));
    Check(GetFileAttributesW(journal.c_str()) == INVALID_FILE_ATTRIBUTES && WriteWholeFileBytes(journal, {1}), "create only an owned transaction-fence sentinel beside the private profile");
    Check(!CaptureGalleryConfigurationView(error, true), "a newly created transaction fence rejects even an otherwise cached valid configuration");
    Check(DeleteFileW(journal.c_str()) && CaptureGalleryConfigurationView(error, true) == newer, "removing the private fence permits the unchanged exact view again");
    const auto activeProfile = g_iniPath;
    const auto sparseProfile = directory + L"\\gallery-cache-sparse.ini";
    const auto missingProfile = directory + L"\\gallery-cache-missing.ini";
    Check(GetFileAttributesW(sparseProfile.c_str()) == INVALID_FILE_ATTRIBUTES &&
        GetFileAttributesW(missingProfile.c_str()) == INVALID_FILE_ATTRIBUTES &&
        WriteUtf8BomTextFile(sparseProfile, L"[Content]\r\nEnabled=0\r\n"), "prepare an owned sparse profile without a Gallery section");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = sparseProfile; }
    Check(CaptureGalleryConfigurationView(error, false) != nullptr, "a readable sparse profile uses gallery defaults without mistaking a missing section for I/O failure");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = missingProfile; }
    Check(!CaptureGalleryConfigurationView(error, false) && !CaptureGalleryConfigurationView(error, false),
        "repeated failed profile refreshes never relabel a sparse cached gallery document as the missing profile");
    { std::lock_guard<std::mutex> lock(g_pathMutex); g_iniPath = activeProfile; }
    Check(DeleteFileW(sparseProfile.c_str()) != FALSE, "remove only the owned sparse gallery cache fixture");
    Check(WriteContentFixture(original), "restore the private profile after strong validation checks");
}

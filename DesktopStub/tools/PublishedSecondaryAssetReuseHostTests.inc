// Forty generated image identities, three synthetic PNG buffers, injected
// rendering and real private cache pruning. No user photo or native delivery.
static void TestPublishedSecondaryAssetReuseHost(const std::wstring& directory)
{
    std::wstring original; Check(ReadIniFileForMutation(original), "published reuse fixture reads its existing private profile");
    auto configured = original;
    Check(WriteIniValueToText(configured, L"Settings", L"GeneratedAssetCache", L"1") &&
        WriteIniValueToText(configured, L"Settings", L"GeneratedAssetCacheMaxEntries", L"32") && WriteContentFixture(configured),
        "published reuse fixture models the existing thirty-two-entry cache without changing live preferences");
    const auto oldMode = g_liveTileUpdateMode.load(); g_liveTileUpdateMode = static_cast<int>(LiveTileUpdateMode::LiveTile);
    const auto oldSecondary = g_secondaryTileAssets, oldNative = g_nativeQueueAssets; const auto oldPrimary = g_primaryTileAssets;
    const auto root = directory + L"\\published-secondary-reuse";
    const bool owned = CreateDirectoryW(root.c_str(), nullptr) != FALSE;
    Check(owned, "published reuse fixture creates a new owned root instead of reusing another directory");
    if (!owned) { g_liveTileUpdateMode = oldMode; Check(WriteContentFixture(original), "restore private preferences after unavailable fixture root"); return; }
    const auto manifest = CurrentManifestDisplayInfo(); const std::vector<int> scales{100};
    std::map<std::wstring, std::vector<BYTE>> png;
    {
        ScopedGdiPlus gdiplus; Check(gdiplus.ok(), "prepare only three offscreen synthetic PNG buffers");
        if (gdiplus.ok()) for (const auto& type : g_liveTileAssets) {
            const auto path = root + L"\\seed-" + type.name + L".png";
            Gdiplus::Bitmap image(type.w, type.h, PixelFormat32bppARGB);
            { Gdiplus::Graphics graphics(&image); graphics.Clear(Gdiplus::Color(255, 48, 96, 144)); }
            Check(SavePNG(&image, path.c_str()) && ReadWholeFileBytes(path, png[type.name]), "encode only generated in-memory pixels for injected renderer output");
        }
    }
    struct Counts { unsigned renders = 0, activeWrites = 0, cacheSaves = 0, cacheWrites = 0, restores = 0; } counts;
    struct Prepared { bool ok = false, reused = false; std::wstring key; std::vector<LiveTileUpdateAsset> assets; };
    const auto prepare = [&](const aip::content::Snapshot& snapshot) {
        ContentRenderScope scope(&snapshot); Prepared result; result.key = BuildGenerationStateKey(snapshot.backgroundPath);
        std::vector<GeneratedAssetCacheFile> renderedFiles;
        result.ok = PrepareGeneratedImageAssets(
            [&] { return TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, true, result.key, result.assets); },
            [&] {
                ++counts.restores;
                const bool restored = TryRestoreGeneratedAssetCache(root, manifest, scales, true, result.key, result.assets);
                if (restored) counts.activeWrites += static_cast<unsigned>(result.assets.size());
                return restored;
            },
            [&] {
                ++counts.renders;
                renderedFiles = BuildGeneratedAssetCacheFileList(root, manifest, scales, true, result.key, result.assets);
                bool ok = renderedFiles.size() == 3;
                for (const auto& file : renderedFiles) {
                    const auto type = std::find_if(result.assets.begin(), result.assets.end(), [&](const auto& asset) {
                        return ResolveManifestAssetPath(root, asset.file) == file.activePath;
                    });
                    if (type == result.assets.end() || !EnsureParentDirectoryForFile(file.activePath) ||
                        !WriteWholeFileBytes(file.activePath, png.at(type->name))) ok = false;
                    else ++counts.activeWrites;
                }
                return ok;
            }, result.reused);
        if (result.ok && !result.reused) {
            SaveGeneratedAssetCache(root, result.key, renderedFiles); ++counts.cacheSaves;
            counts.cacheWrites += static_cast<unsigned>(renderedFiles.size());
        }
        return result;
    };
    const auto primaryPath = ResolveManifestAssetPath(root, ManifestAssetPathForTile(manifest, g_tiles[1]));
    const std::vector<BYTE> primaryBytes{9, 8, 7, 6};
    Check(EnsureParentDirectoryForFile(primaryPath) && WriteWholeFileBytes(primaryPath, primaryBytes), "protect one fixture-owned primary/static asset sentinel");
    g_secondaryTileAssets.clear(); g_nativeQueueAssets.clear(); g_primaryTileAssets.clear();
    std::vector<aip::content::Snapshot> snapshots; std::vector<Prepared> first;
    bool cold = true;
    for (unsigned index = 0; index < 40; ++index) {
        const auto source = root + L"\\source-" + std::to_wstring(index) + L".bmp";
        BITMAPFILEHEADER file{}; file.bfType = 0x4d42; file.bfSize = 58; file.bfOffBits = 54;
        BITMAPINFOHEADER info{}; info.biSize = sizeof(info); info.biWidth = info.biHeight = 1; info.biPlanes = 1; info.biBitCount = 24; info.biSizeImage = 4;
        std::vector<BYTE> bytes(58); memcpy(bytes.data(), &file, sizeof(file)); memcpy(bytes.data() + 14, &info, sizeof(info)); bytes[54] = static_cast<BYTE>(index);
        Check(WriteWholeFileBytes(source, bytes), "write only a generated distinct one-pixel BMP identity");
        aip::content::Snapshot frame; frame.active = true; frame.backgroundId = aip::content::BackgroundId::Image; frame.backgroundPath = source;
        frame.textMode = aip::content::TextMode::Off; frame.galleryFrame = true; frame.galleryGroupId = L"fixture"; frame.galleryTileId = L"tile" + std::to_wstring(index / 5);
        frame.secondaryTileId = aip::gallery::NativeTileId(frame.galleryGroupId, frame.galleryTileId); frame.nativeQueue = true;
        frame.galleryTicket = 1; frame.gallerySlot = index % 5; frame.gallerySlotCount = 5;
        frame.nativeQueueTag = L"g" + std::to_wstring(index % 5 + 1); frame.nativeQueueReset = index % 5 == 0;
        frame.secondaryFrameKey = L"receipt1-" + std::to_wstring(index); frame.nativeQueueFrameKey = frame.secondaryFrameKey;
        snapshots.push_back(frame); first.push_back(prepare(frame)); cold = first.back().ok && !first.back().reused && cold;
        RetainPublishedLiveTileAssets(frame, first.back().assets);
        // Deterministic directory ordering verifies eviction without sleeping.
        const auto cache = GeneratedAssetCacheDir(root, first.back().key);
        const auto handle = CreateFileW(cache.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        ULARGE_INTEGER time{}; time.QuadPart = 132000000000000000ull + index * 10000000ull;
        FILETIME written{time.LowPart, time.HighPart};
        Check(handle != INVALID_HANDLE_VALUE && SetFileTime(handle, nullptr, nullptr, &written), "assign owned cache directory ordering for bounded eviction");
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    Check(cold && counts.renders == 40 && counts.activeWrites == 120 && counts.cacheSaves == 40,
        "forty cold synthetic images take the injected renderer and three-image publication path");
    DeleteOldVersionedLiveTileAssets(root, {});
    {
        ContentRenderScope scope(&snapshots.front());
        Check(!GeneratedAssetCacheComplete(root, manifest, scales, true, first.front().key), "the oldest image was actually evicted from the thirty-two-entry generated cache");
    }
    const auto warmBaseline = counts; bool warm = true;
    for (std::size_t index = 0; index < snapshots.size(); ++index) {
        auto frame = snapshots[index]; ++frame.galleryTicket;
        frame.secondaryFrameKey = L"new-receipt-" + std::to_wstring(index); frame.nativeQueueFrameKey = frame.secondaryFrameKey;
        const auto repeated = prepare(frame);
        warm = repeated.ok && repeated.reused && repeated.key == first[index].key && repeated.assets.size() == 3 && warm;
    }
    Check(warm && counts.renders == warmBaseline.renders && counts.activeWrites == warmBaseline.activeWrites &&
        counts.cacheSaves == warmBaseline.cacheSaves && counts.cacheWrites == warmBaseline.cacheWrites && counts.restores == warmBaseline.restores,
        "new receipts for forty retained images bypass rendering, PNG/cache writes and cache restoration after thirty-two-entry eviction");

    const auto frame = snapshots.front(); auto assets = first.front().assets;
    const auto path = [&](std::size_t index) { return ResolveManifestAssetPath(root, assets[index].file); };
    Check(DeleteFileW(path(0).c_str()) != FALSE, "remove only the first owned published PNG to test missing-file fallback");
    auto repaired = prepare(frame);
    Check(repaired.ok && !repaired.reused && counts.renders == warmBaseline.renders + 1 && repaired.assets.size() == 3,
        "one missing published PNG falls back to rendering when its generated cache entry was evicted");
    auto corrupt = png.at(g_liveTileAssets[1].name); corrupt.back() ^= 1;
    Check(WriteWholeFileBytes(path(1), corrupt), "corrupt only an owned PNG checksum");
    { ContentRenderScope scope(&frame); std::vector<LiveTileUpdateAsset> rejected = assets;
      Check(!TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, true, repaired.key, rejected) && rejected.empty(),
          "all published images require valid checksums and a partial asset set is never exposed"); }
    const auto beforeRestore = counts;
    const auto restored = prepare(frame);
    Check(restored.ok && restored.reused && counts.renders == beforeRestore.renders && counts.restores == beforeRestore.restores + 1 &&
        counts.activeWrites == beforeRestore.activeWrites + 3 && IsValidGeneratedPngWithDimensions(path(1), g_liveTileAssets[1].w, g_liveTileAssets[1].h),
        "corrupt published pixels are rejected even when a valid generated-cache copy can restore them without rendering");
    Check(DeleteDirectoryTree(GeneratedAssetCacheDir(root, restored.key)) && WriteWholeFileBytes(path(1), corrupt),
        "remove only the owned cache copy and reintroduce owned corruption to test render fallback");
    repaired = prepare(frame);
    Check(repaired.ok && !repaired.reused && counts.renders == warmBaseline.renders + 2, "corrupt published data takes the existing fallback path");
    Check(DeleteDirectoryTree(GeneratedAssetCacheDir(root, repaired.key)) && WriteWholeFileBytes(path(2), png.at(g_liveTileAssets[0].name)),
        "replace only an owned large PNG with a valid PNG of the wrong dimensions");
    repaired = prepare(frame);
    Check(repaired.ok && !repaired.reused && counts.renders == warmBaseline.renders + 3, "a valid PNG with wrong dimensions cannot enter the reuse path");
    {
        auto primary = frame; primary.secondaryTileId.clear(); primary.galleryFrame = false;
        ContentRenderScope scope(&primary); std::vector<LiveTileUpdateAsset> rejected = assets;
        Check(!TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, true, repaired.key, rejected) && rejected.empty(),
            "primary/static generation cannot reuse a secondary-only published asset set");
    }
    { ContentRenderScope scope(&frame); std::vector<LiveTileUpdateAsset> rejected = assets;
      Check(!TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, false, repaired.key, rejected) && rejected.empty(),
          "registration mode cannot enter the secondary live reuse path"); }
    auto disabled = configured;
    Check(WriteIniValueToText(disabled, L"Settings", L"GeneratedAssetCache", L"0") && WriteContentFixture(disabled), "disable cache reuse only inside the private fixture profile");
    { ContentRenderScope scope(&frame); std::vector<LiveTileUpdateAsset> rejected = assets;
      Check(!TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, true, repaired.key, rejected) && rejected.empty(),
          "the existing generated-cache master-off preference also disables published PNG reuse"); }
    auto zeroCapacity = configured;
    Check(WriteIniValueToText(zeroCapacity, L"Settings", L"GeneratedAssetCacheMaxEntries", L"0") && WriteContentFixture(zeroCapacity), "model a zero-copy cache capacity only in the private profile");
    { ContentRenderScope scope(&frame); std::vector<LiveTileUpdateAsset> existing;
      Check(TryReusePublishedSecondaryLiveTileAssets(root, manifest, scales, true, repaired.key, existing) && existing.size() == 3,
          "capacity zero does not invalidate already published images required by Windows when the cache master remains enabled"); }
    Check(WriteContentFixture(configured), "restore the fixture's thirty-two-entry setting before final layout checks");
    {
        auto prayer = frame; prayer.galleryFrame = false; prayer.secondaryTileId = L"AipSource_prayer"; prayer.textMode = aip::content::TextMode::Overlay;
        prayer.text = {L"Next prayer", L"10:00", L""};
        for (unsigned row = 0; row < 5; ++row) prayer.detailRows.push_back({L"Prayer " + std::to_wstring(row + 1), L"10:00", L"", L""});
        const auto generic = prepare(prayer); prayer.prayerSource = true;
        const auto structured = prepare(prayer);
        Check(generic.ok && structured.ok && !structured.reused && generic.key != structured.key,
            "prayer-specific compact and five-row bitmap layout cannot reuse generic detail-row pixels");
        prayer.detailRows.back().secondary = L"10:01";
        const auto changedRows = prepare(prayer);
        Check(changedRows.ok && !changedRows.reused && changedRows.key != structured.key,
            "changed baked prayer rows require new deterministic output assets");
    }
    {
        auto styled = frame; styled.textMode = aip::content::TextMode::Overlay; styled.text.primary = L"Presentation key fixture";
        ContentRenderScope scope(&styled);
        const auto defaultKey = BuildGenerationStateKey(styled.backgroundPath);
        scope.snapshot.presentation.primaryStyle = L"title";
        Check(BuildGenerationStateKey(styled.backgroundPath) == defaultKey,
            "Default text presentation contributes no key material even when inactive custom fields differ");
        scope.snapshot.presentation.preset = L"Custom";
        const auto customKey = BuildGenerationStateKey(styled.backgroundPath);
        scope.snapshot.presentation.primaryStyle = L"header";
        Check(customKey != defaultKey && BuildGenerationStateKey(styled.backgroundPath) != customKey,
            "an active baked text style requires its own deterministic bitmap key");
        const auto oldTarget = g_manifestCompatibilityTarget.load();
        g_manifestCompatibilityTarget = static_cast<int>(ManifestCompatibilityTarget::Windows10);
        scope.snapshot.textMode = aip::content::TextMode::Auto;
        const auto nativeKey = BuildGenerationStateKey(styled.backgroundPath);
        scope.snapshot.presentation.primaryStyle = L"caption";
        Check(BuildGenerationStateKey(styled.backgroundPath) == nativeKey,
            "native XML-only text style changes do not invalidate unchanged image pixels");
        scope.snapshot.textMode = aip::content::TextMode::Off;
        const auto hiddenKey = BuildGenerationStateKey(styled.backgroundPath);
        scope.snapshot.presentation.preset = L"Default";
        Check(BuildGenerationStateKey(styled.backgroundPath) == hiddenKey,
            "hidden text ignores presentation policy in image-cache keys");
        g_manifestCompatibilityTarget = oldTarget;
    }
    {
        const std::vector<BYTE> stableSentinel{1, 4, 7, 0};
        for (const auto& type : g_liveTileAssets) Check(WriteWholeFileBytes(ResolveManifestAssetPath(root, type.file), stableSentinel),
            "seed only owned primary stable-image sentinel bytes");
        auto secondary = frame; secondary.galleryFrame = false; secondary.secondaryTileId = L"AipSource_prayer";
        CopyPrimaryLiveTileAssetsToStablePaths(root, assets, secondary);
        bool preserved = true;
        for (const auto& type : g_liveTileAssets) {
            std::vector<BYTE> bytes; preserved = ReadWholeFileBytes(ResolveManifestAssetPath(root, type.file), bytes) && bytes == stableSentinel && preserved;
        }
        Check(preserved, "a non-gallery secondary source cannot overwrite the primary stable notification images");
        auto primary = secondary; primary.secondaryTileId.clear();
        CopyPrimaryLiveTileAssetsToStablePaths(root, assets, primary);
        bool copied = true;
        for (const auto& type : g_liveTileAssets) {
            std::vector<BYTE> bytes; copied = ReadWholeFileBytes(ResolveManifestAssetPath(root, type.file), bytes) && bytes == png.at(type.name) && copied;
        }
        Check(copied, "the same file helper still publishes stable-image copies for a primary generation");
    }
    std::vector<BYTE> unchanged;
    Check(ReadWholeFileBytes(primaryPath, unchanged) && unchanged == primaryBytes, "eviction and repeated secondary receipts preserve primary/static bytes");
    g_secondaryTileAssets = oldSecondary; g_nativeQueueAssets = oldNative; g_primaryTileAssets = oldPrimary; g_liveTileUpdateMode = oldMode;
    Check(DeleteDirectoryTree(root), "remove only the newly owned synthetic reuse directory and its files");
    Check(WriteContentFixture(original), "restore the fixture's original cache preference text");
}

// Real pruning on fixture-owned files only. No rendering, TileUpdater,
// registration, provider, user INI, pin, notification or live asset is touched.
static void TestPrimaryAssetRetentionHost(const std::wstring& directory)
{
    const auto previousPrimary = g_primaryTileAssets;
    const auto previousQueue = g_nativeQueueAssets;
    const auto previousSecondary = g_secondaryTileAssets;
    g_primaryTileAssets.clear(); g_nativeQueueAssets.clear(); g_secondaryTileAssets.clear();
    const auto root = directory + L"\\primary-retention";
    Check(EnsureDirectoryTreeExists(root + L"\\Assets"), "primary retention fixture owns an isolated asset directory");
    std::vector<std::wstring> created;
    unsigned sequence = 0;
    const auto makeAssets = [&](const wchar_t* token) {
        std::vector<LiveTileUpdateAsset> assets;
        for (const auto& type : g_liveTileAssets) {
            const auto relative = VersionedLiveTileAssetPath(type, token);
            Check(IsVersionedLiveTileAssetFileName(FileNameFromPath(relative), type.file), "fixture filenames enter the production versioned-image pruning path");
            const auto path = ResolveManifestAssetPath(root, relative);
            Check(WriteWholeFileBytes(path, {1, 2, 3}), "write a fixture-owned pruning candidate");
            // Deterministic timestamps avoid sort ties without sleeping.
            const auto handle = CreateFileW(path.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            ULARGE_INTEGER timestamp{}; timestamp.QuadPart = 132000000000000000ull + ++sequence * 10000000ull;
            FILETIME written{timestamp.LowPart, timestamp.HighPart};
            Check(handle != INVALID_HANDLE_VALUE && SetFileTime(handle, nullptr, nullptr, &written), "assign deterministic fixture asset ordering");
            if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
            created.push_back(path); assets.push_back({type.name, type.file, type.binding, relative, type.w, type.h});
        }
        return assets;
    };
    aip::content::Snapshot primary; primary.active = true;
    const auto first = makeAssets(L"0000000000000001"); RetainPublishedLiveTileAssets(primary, first);
    DeleteOldVersionedLiveTileAssets(root, first);
    for (unsigned index = 0; index < 5; ++index) {
        auto frame = primary;
        frame.secondaryTileId = index % 2 ? L"AipGallery_fixture.tile" + std::to_wstring(index) : L"AipSource_fixture" + std::to_wstring(index);
        frame.galleryFrame = index % 2 != 0; frame.nativeQueue = frame.galleryFrame; frame.nativeQueueTag = L"Slot1";
        frame.galleryEpoch = L"epoch"; frame.galleryTicket = index + 1;
        const auto assets = makeAssets((std::wstring(14, L'0') + std::to_wstring(index + 10)).c_str());
        RetainPublishedLiveTileAssets(frame, assets); DeleteOldVersionedLiveTileAssets(root, assets);
        for (const auto& asset : first)
            Check(GetFileAttributesW(ResolveManifestAssetPath(root, asset.file).c_str()) != INVALID_FILE_ATTRIBUTES,
                "unchanged primary notification images survive more than two secondary and gallery cleanup cycles");
    }
    const auto replacement = makeAssets(L"0000000000000020"); RetainPublishedLiveTileAssets(primary, replacement);
    DeleteOldVersionedLiveTileAssets(root, replacement);
    for (const auto& asset : first)
        Check(GetFileAttributesW(ResolveManifestAssetPath(root, asset.file).c_str()) == INVALID_FILE_ATTRIBUTES,
            "a superseded primary asset is reclaimed once no published notification needs it");
    for (const auto& asset : replacement)
        Check(GetFileAttributesW(ResolveManifestAssetPath(root, asset.file).c_str()) != INVALID_FILE_ATTRIBUTES,
            "the new primary keeps its own versioned images");
    for (const auto& path : created) DeleteFileW(path.c_str());
    RemoveDirectoryW((root + L"\\Assets").c_str()); RemoveDirectoryW(root.c_str());
    g_primaryTileAssets = previousPrimary; g_nativeQueueAssets = previousQueue; g_secondaryTileAssets = previousSecondary;
}

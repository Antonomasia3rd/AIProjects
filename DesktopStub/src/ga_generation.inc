// -----------------------------------------------------------------------------
// Tile generation
// -----------------------------------------------------------------------------
struct Tile { const wchar_t* name; const wchar_t* file; int w,h; };
static const Tile g_tiles[] = {
    {L"StoreLogo",       L"Assets\\StoreLogo.png", 50,50},
    {L"MediumTile",      L"Assets\\MediumTile.png",150,150},
    {L"Square44x44Logo", L"Assets\\Square44x44Logo.png",44,44},
    {L"Square30x30Logo", L"Assets\\Square30x30Logo.png",30,30},
    {L"SmallTile",       L"Assets\\SmallTile.png",71,71},
    {L"WideTile",        L"Assets\\WideTile.png",310,150},
    {L"LargeTile",       L"Assets\\LargeTile.png",310,310},
    {L"SplashScreen",    L"Assets\\SplashScreen.png",620,300},
};

struct LiveTileAsset { const wchar_t* name; const wchar_t* file; int w,h; const wchar_t* binding; };
static const LiveTileAsset g_liveTileAssets[] = {
    {L"LiveMediumTile", L"Assets\\LiveMediumTile.png",150,150,L"TileMedium"},
    {L"LiveWideTile",   L"Assets\\LiveWideTile.png",310,150,L"TileWide"},
    {L"LiveLargeTile",  L"Assets\\LiveLargeTile.png",310,310,L"TileLarge"},
};

struct LiveTileUpdateAsset
{
    const wchar_t* name;
    const wchar_t* stableFile;
    const wchar_t* binding;
    std::wstring file;
    int w;
    int h;
};

struct GeneratedAssetCacheFile
{
    std::wstring cacheRelativePath;
    std::wstring activePath;
    int expectedWidth;
    int expectedHeight;
};

struct GenerationCompletionRecord
{
    unsigned long long id = 0;
    bool success = false;
};

static std::atomic<unsigned long long> g_generationNextRequestId(1);
static std::mutex g_generationCompletionMutex;
static std::vector<GenerationCompletionRecord> g_generationCompletions;
static unsigned long long g_generationQueuedId = 0;
static std::mutex g_generatedAssetPrecacheMutex;
static std::condition_variable g_generatedAssetPrecacheCv;
static std::thread g_generatedAssetPrecacheThread;
static bool g_generatedAssetPrecacheWorkerStarted = false;
static bool g_generatedAssetPrecacheStop = false;
static bool g_generatedAssetPrecacheQueued = false;
static std::wstring g_generatedAssetPrecacheWallpaper;

static void WakePollThread();
static bool GenerationWorkPending();
static bool Appx_Update_LiveTile(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets,
    std::wstring& failureMessage);
static bool Appx_Update_Or_Request_LiveTile(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets,
    std::wstring& failureMessage);
static std::wstring ResolveManifestAssetPath(const std::wstring& exeDir, const std::wstring& assetPath);
static std::wstring ManifestAssetPathForTile(const ManifestDisplayInfo& info, const Tile& tile);
static bool IsValidGeneratedPng(const std::wstring& path);
static bool IsValidGeneratedPngWithDimensions(const std::wstring& path, int expectedWidth, int expectedHeight);

static std::wstring FileNameFromPath(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring InsertFileNameSuffix(const std::wstring& path, const std::wstring& suffix)
{
    size_t slash = path.find_last_of(L"\\/");
    size_t dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || (slash != std::wstring::npos && dot < slash))
        dot = path.size();

    return path.substr(0, dot) + suffix + path.substr(dot);
}

static bool StartsWithI(const std::wstring& text, const std::wstring& prefix)
{
    return text.size() >= prefix.size() && IEquals(text.substr(0, prefix.size()), prefix);
}

static bool EndsWithI(const std::wstring& text, const std::wstring& suffix)
{
    return text.size() >= suffix.size() && IEquals(text.substr(text.size() - suffix.size()), suffix);
}

static bool IsHexToken(const std::wstring& value)
{
    if (value.empty())
        return false;

    for (wchar_t ch : value)
    {
        if (!((ch >= L'0' && ch <= L'9') ||
            (ch >= L'a' && ch <= L'f') ||
            (ch >= L'A' && ch <= L'F')))
            return false;
    }
    return true;
}

static std::wstring NewLiveTileAssetToken()
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    wchar_t token[32];
    swprintf(token, _countof(token), L"%08lX%08lX", ft.dwHighDateTime, ft.dwLowDateTime);
    return token;
}

static std::wstring VersionedLiveTileAssetPath(const LiveTileAsset& asset, const std::wstring& token)
{
    return InsertFileNameSuffix(asset.file, L"." + token);
}

static bool IsVersionedLiveTileAssetFileName(const std::wstring& fileName, const wchar_t* stableAssetFile)
{
    std::wstring stableName = FileNameFromPath(stableAssetFile ? stableAssetFile : L"");
    size_t dot = stableName.find_last_of(L'.');
    if (dot == std::wstring::npos || dot == 0)
        return false;

    std::wstring stem = stableName.substr(0, dot);
    std::wstring ext = stableName.substr(dot);
    std::wstring prefix = stem + L".";
    if (!StartsWithI(fileName, prefix) || !EndsWithI(fileName, ext))
        return false;

    std::wstring token = fileName.substr(prefix.size(), fileName.size() - prefix.size() - ext.size());
    return token.size() == 16 && IsHexToken(token);
}

static bool IsKeptLiveTileAssetPath(
    const std::wstring& fullPath,
    const std::wstring& exeDir,
    const std::vector<LiveTileUpdateAsset>& keepAssets)
{
    for (const auto& asset : keepAssets)
    {
        if (IEquals(fullPath, ResolveManifestAssetPath(exeDir, asset.file)))
            return true;
    }
    return false;
}

static unsigned long long FileTimeToUInt64(const FILETIME& ft)
{
    ULARGE_INTEGER value{};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static void DeleteOldVersionedLiveTileAssets(
    const std::wstring& exeDir,
    const std::vector<LiveTileUpdateAsset>& keepAssets)
{
    struct VersionedLiveTileCandidate
    {
        std::wstring path;
        unsigned long long writeTime;
    };

    constexpr size_t retainedVersionedAssetsPerTile = 2;

    for (const auto& asset : g_liveTileAssets)
    {
        std::wstring stablePath = ResolveManifestAssetPath(exeDir, asset.file);
        std::wstring assetDir = ParentDirectoryOfPath(stablePath);
        if (assetDir.empty())
            continue;

        std::wstring stableName = FileNameFromPath(asset.file);
        size_t dot = stableName.find_last_of(L'.');
        if (dot == std::wstring::npos || dot == 0)
            continue;

        std::wstring pattern = assetDir + L"\\" + stableName.substr(0, dot) + L".*" + stableName.substr(dot);
        WIN32_FIND_DATAW data{};
        HANDLE find = FindFirstFileW(pattern.c_str(), &data);
        if (find == INVALID_HANDLE_VALUE)
            continue;

        std::vector<VersionedLiveTileCandidate> candidates;
        do
        {
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                continue;

            std::wstring fileName = data.cFileName;
            if (!IsVersionedLiveTileAssetFileName(fileName, asset.file))
                continue;

            std::wstring fullPath = assetDir + L"\\" + fileName;
            candidates.push_back({ fullPath, FileTimeToUInt64(data.ftLastWriteTime) });
        } while (FindNextFileW(find, &data));

        FindClose(find);

        std::sort(candidates.begin(), candidates.end(),
            [](const VersionedLiveTileCandidate& a, const VersionedLiveTileCandidate& b)
            {
                return a.writeTime > b.writeTime;
            });

        for (size_t i = 0; i < candidates.size(); ++i)
        {
            if (i < retainedVersionedAssetsPerTile ||
                IsKeptLiveTileAssetPath(candidates[i].path, exeDir, keepAssets))
                continue;

            DeleteFileW(candidates[i].path.c_str());
        }
    }
}

static void CopyLiveTileAssetsToStablePaths(
    const std::wstring& exeDir,
    const std::vector<LiveTileUpdateAsset>& liveTileAssets)
{
    for (const auto& asset : liveTileAssets)
    {
        if (!asset.stableFile || !*asset.stableFile)
            continue;

        std::wstring sourcePath = ResolveManifestAssetPath(exeDir, asset.file);
        std::wstring stablePath = ResolveManifestAssetPath(exeDir, asset.stableFile);
        if (IEquals(sourcePath, stablePath))
            continue;

        if (!CopyFileW(sourcePath.c_str(), stablePath.c_str(), FALSE))
        {
            Log(g_ui.failedSaveAsset.c_str(), stablePath.c_str());
            LogWin32Failure(L"CopyFileW");
        }
    }
}

static bool GeneratedAssetCacheEnabled()
{
    return IniReadI(L"Settings", L"GeneratedAssetCache", 1) != 0;
}

static int GeneratedAssetCacheMaxEntries()
{
    return IniReadClampedI(L"Settings", L"GeneratedAssetCacheMaxEntries", 64, 0, 4096);
}

static bool GeneratedAssetPrecacheEnabled()
{
    return GeneratedAssetCacheEnabled() &&
        IniReadI(L"Settings", L"GeneratedAssetPrecache", 1) != 0 &&
        GeneratedAssetCacheMaxEntries() > 0;
}

static int GeneratedAssetPrecacheMaxFiles()
{
    return IniReadClampedI(L"Settings", L"GeneratedAssetPrecacheMaxFiles", 16, 0, 256);
}

static std::wstring GeneratedAssetCacheRoot(const std::wstring& exeDir)
{
    return exeDir + L"\\AssetCache";
}

static std::wstring GeneratedAssetCacheDir(const std::wstring& exeDir, const std::wstring& stateKey)
{
    return GeneratedAssetCacheRoot(exeDir) + L"\\" + stateKey;
}

static std::wstring GeneratedAssetCacheFilePath(const std::wstring& cacheDir, const std::wstring& relativePath)
{
    return cacheDir + L"\\" + relativePath;
}

static std::wstring ManifestGeneratedAssetCacheName(const wchar_t* tileName)
{
    return L"Assets\\" + std::wstring(tileName ? tileName : L"Asset") + L".png";
}

static std::wstring LiveGeneratedAssetCacheName(const wchar_t* tileName)
{
    return L"Assets\\" + std::wstring(tileName ? tileName : L"Asset") + L".png";
}

static void AddGeneratedAssetCacheFile(
    std::vector<GeneratedAssetCacheFile>& files,
    const std::wstring& cacheRelativePath,
    const std::wstring& activePath,
    int expectedWidth,
    int expectedHeight)
{
    if (cacheRelativePath.empty() || activePath.empty() || expectedWidth <= 0 || expectedHeight <= 0)
        return;

    for (const auto& file : files)
    {
        if (IEquals(file.cacheRelativePath, cacheRelativePath) ||
            IEquals(file.activePath, activePath))
            return;
    }
    files.push_back({ cacheRelativePath, activePath, expectedWidth, expectedHeight });
}

static bool CopyPngFileAtomically(const std::wstring& sourcePath, const std::wstring& targetPath)
{
    std::vector<BYTE> bytes;
    if (!ReadWholeFileBytes(sourcePath, bytes))
        return false;

    DWORD dirError = ERROR_SUCCESS;
    if (!EnsureParentDirectoryForFile(targetPath, &dirError))
    {
        SetLastError(dirError);
        return false;
    }

    return WriteWholeFileBytes(targetPath, bytes);
}

static bool DeleteDirectoryTree(const std::wstring& dir)
{
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            std::wstring name = data.cFileName;
            if (name == L"." || name == L"..")
                continue;

            std::wstring child = dir + L"\\" + name;
            if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
                DeleteDirectoryTree(child);
            else
                DeleteFileW(child.c_str());
        } while (FindNextFileW(find, &data));

        FindClose(find);
    }

    return RemoveDirectoryW(dir.c_str()) || GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND;
}

static void PruneGeneratedAssetCache(const std::wstring& exeDir)
{
    int maxEntries = GeneratedAssetCacheMaxEntries();
    if (maxEntries <= 0)
        return;

    struct CacheDirCandidate
    {
        std::wstring path;
        unsigned long long writeTime;
    };

    std::wstring root = GeneratedAssetCacheRoot(exeDir);
    std::wstring pattern = root + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return;

    std::vector<CacheDirCandidate> candidates;
    do
    {
        std::wstring name = data.cFileName;
        if (name == L"." || name == L".." ||
            (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
            name.size() != 16 ||
            !IsHexToken(name))
            continue;

        candidates.push_back({ root + L"\\" + name, FileTimeToUInt64(data.ftLastWriteTime) });
    } while (FindNextFileW(find, &data));

    FindClose(find);

    if (candidates.size() <= static_cast<size_t>(maxEntries))
        return;

    std::sort(candidates.begin(), candidates.end(),
        [](const CacheDirCandidate& a, const CacheDirCandidate& b)
        {
            return a.writeTime > b.writeTime;
        });

    for (size_t i = static_cast<size_t>(maxEntries); i < candidates.size(); ++i)
        DeleteDirectoryTree(candidates[i].path);
}

static std::vector<GeneratedAssetCacheFile> BuildGeneratedAssetCacheFileList(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<int>& scales,
    bool liveTileUpdateMode,
    const std::wstring& liveTileToken,
    std::vector<LiveTileUpdateAsset>& liveTileUpdateAssets)
{
    std::vector<GeneratedAssetCacheFile> files;
    liveTileUpdateAssets.clear();

    for (const auto& t : g_tiles)
    {
        std::wstring manifestAssetPath = ManifestAssetPathForTile(manifestInfo, t);
        std::wstring outPath = ResolveManifestAssetPath(exeDir, manifestAssetPath);

        bool staticWallpaperAssetEnabled = !liveTileUpdateMode && IniReadI(L"Assets", t.name, 0) != 0;
        if (!staticWallpaperAssetEnabled && !g_generateDesktopIconForDisabledEntries)
            continue;

        std::wstring cacheName = ManifestGeneratedAssetCacheName(t.name);
        AddGeneratedAssetCacheFile(files, cacheName, outPath, t.w, t.h);

        for (int scale : scales)
            AddGeneratedAssetCacheFile(
                files,
                ScaleAssetPath(cacheName, scale),
                ScaleAssetPath(outPath, scale),
                ScalePixels(t.w, scale),
                ScalePixels(t.h, scale));
    }

    if (liveTileUpdateMode)
    {
        for (const auto& t : g_liveTileAssets)
        {
            std::wstring updateAssetPath = VersionedLiveTileAssetPath(t, liveTileToken);
            std::wstring outPath = ResolveManifestAssetPath(exeDir, updateAssetPath);
            liveTileUpdateAssets.push_back({ t.name, t.file, t.binding, updateAssetPath, t.w, t.h });
            AddGeneratedAssetCacheFile(files, LiveGeneratedAssetCacheName(t.name), outPath, t.w, t.h);
        }
    }

    return files;
}

static bool GeneratedAssetCacheFilesPresent(
    const std::wstring& cacheDir,
    const std::vector<GeneratedAssetCacheFile>& files)
{
    if (files.empty())
        return false;

    for (const auto& file : files)
    {
        if (!IsValidGeneratedPngWithDimensions(
                GeneratedAssetCacheFilePath(cacheDir, file.cacheRelativePath),
                file.expectedWidth,
                file.expectedHeight))
            return false;
    }

    return true;
}

static bool GeneratedAssetCacheComplete(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<int>& scales,
    bool liveTileUpdateMode,
    const std::wstring& stateKey)
{
    if (!GeneratedAssetCacheEnabled() || stateKey.empty())
        return false;

    std::vector<LiveTileUpdateAsset> unusedLiveTileAssets;
    std::vector<GeneratedAssetCacheFile> files = BuildGeneratedAssetCacheFileList(
        exeDir,
        manifestInfo,
        scales,
        liveTileUpdateMode,
        L"0000000000000000",
        unusedLiveTileAssets);

    return GeneratedAssetCacheFilesPresent(GeneratedAssetCacheDir(exeDir, stateKey), files);
}

static void DeleteDisabledAssetsForCurrentConfig(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    bool liveTileUpdateMode)
{
    for (const auto& t : g_tiles)
    {
        bool staticWallpaperAssetEnabled = !liveTileUpdateMode && IniReadI(L"Assets", t.name, 0) != 0;
        if (staticWallpaperAssetEnabled || g_generateDesktopIconForDisabledEntries)
            continue;

        if (!g_deleteDisabledAssets && !liveTileUpdateMode)
            continue;

        std::wstring manifestAssetPath = ManifestAssetPathForTile(manifestInfo, t);
        std::wstring outPath = ResolveManifestAssetPath(exeDir, manifestAssetPath);
        DeleteDisabledAssetFiles(outPath);
    }
}

static bool TryRestoreGeneratedAssetCache(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<int>& scales,
    bool liveTileUpdateMode,
    const std::wstring& stateKey,
    std::vector<LiveTileUpdateAsset>& liveTileUpdateAssets)
{
    liveTileUpdateAssets.clear();
    if (!GeneratedAssetCacheEnabled() || stateKey.empty())
        return false;

    std::wstring liveTileToken = NewLiveTileAssetToken();
    std::vector<GeneratedAssetCacheFile> files = BuildGeneratedAssetCacheFileList(
        exeDir,
        manifestInfo,
        scales,
        liveTileUpdateMode,
        liveTileToken,
        liveTileUpdateAssets);
    if (files.empty())
    {
        liveTileUpdateAssets.clear();
        return false;
    }

    std::wstring cacheDir = GeneratedAssetCacheDir(exeDir, stateKey);
    if (!GeneratedAssetCacheFilesPresent(cacheDir, files))
    {
        liveTileUpdateAssets.clear();
        return false;
    }

    DeleteDisabledAssetsForCurrentConfig(exeDir, manifestInfo, liveTileUpdateMode);

    for (const auto& file : files)
    {
        std::wstring sourcePath = GeneratedAssetCacheFilePath(cacheDir, file.cacheRelativePath);
        if (!CopyPngFileAtomically(sourcePath, file.activePath) ||
            !IsValidGeneratedPngWithDimensions(file.activePath, file.expectedWidth, file.expectedHeight))
        {
            liveTileUpdateAssets.clear();
            return false;
        }
    }

    Log(g_ui.generatedAssetCacheHit.c_str(), stateKey.c_str());
    return true;
}

static void SaveGeneratedAssetCache(
    const std::wstring& exeDir,
    const std::wstring& stateKey,
    const std::vector<GeneratedAssetCacheFile>& files)
{
    if (!GeneratedAssetCacheEnabled() || stateKey.empty() || files.empty())
        return;

    if (GeneratedAssetCacheMaxEntries() <= 0)
        return;

    std::wstring cacheDir = GeneratedAssetCacheDir(exeDir, stateKey);
    std::wstring stagingCacheDir = cacheDir + L".save." + NewLiveTileAssetToken();
    DeleteDirectoryTree(stagingCacheDir);

    bool savedAny = false;
    bool ok = true;
    for (const auto& file : files)
    {
        if (!IsValidGeneratedPngWithDimensions(file.activePath, file.expectedWidth, file.expectedHeight))
        {
            ok = false;
            continue;
        }

        std::wstring cachePath = GeneratedAssetCacheFilePath(stagingCacheDir, file.cacheRelativePath);
        if (CopyPngFileAtomically(file.activePath, cachePath))
        {
            savedAny = true;
        }
        else
        {
            ok = false;
        }
    }

    if (savedAny && ok && GeneratedAssetCacheFilesPresent(stagingCacheDir, files))
    {
        DeleteDirectoryTree(cacheDir);
        if (!MoveFileExW(stagingCacheDir.c_str(), cacheDir.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            LogWin32Failure(L"MoveFileExW(cache publish)");
            DeleteDirectoryTree(stagingCacheDir);
            return;
        }

        Log(g_ui.generatedAssetCacheSaved.c_str(), stateKey.c_str());
        PruneGeneratedAssetCache(exeDir);
        return;
    }

    DeleteDirectoryTree(stagingCacheDir);
}

static std::wstring ManifestAssetPathForTile(const ManifestDisplayInfo& info, const Tile& tile)
{
    std::wstring path;
    if (wcscmp(tile.name, L"StoreLogo") == 0)
        path = info.propertiesLogo;
    else if (wcscmp(tile.name, L"MediumTile") == 0)
        path = info.square150x150Logo;
    else if (wcscmp(tile.name, L"Square44x44Logo") == 0)
        path = info.square44x44Logo;
    else if (wcscmp(tile.name, L"Square30x30Logo") == 0)
        path = info.square30x30Logo;
    else if (wcscmp(tile.name, L"SmallTile") == 0)
        path = info.square71x71Logo;
    else if (wcscmp(tile.name, L"WideTile") == 0)
        path = info.wide310x150Logo;
    else if (wcscmp(tile.name, L"LargeTile") == 0)
        path = info.square310x310Logo;
    else if (wcscmp(tile.name, L"SplashScreen") == 0)
        path = info.splashScreenImage;

    return IsManifestRelativeAssetPath(path) ? path : tile.file;
}

static void RecordGenerationCompletion(unsigned long long id, bool success)
{
    if (id == 0)
        return;

    {
        std::lock_guard<std::mutex> lk(g_generationCompletionMutex);
        g_generationCompletions.push_back({ id, success });
        if (g_generationCompletions.size() > 64)
            g_generationCompletions.erase(g_generationCompletions.begin());
    }
    WakePollThread();
}

static bool TryGetGenerationCompletion(unsigned long long id, bool& success)
{
    std::lock_guard<std::mutex> lk(g_generationCompletionMutex);
    for (const auto& record : g_generationCompletions)
    {
        if (record.id == id)
        {
            success = record.success;
            return true;
        }
    }
    return false;
}

static uint32_t ReadPngBigEndian32(const BYTE* p)
{
    return (static_cast<uint32_t>(p[0]) << 24) |
        (static_cast<uint32_t>(p[1]) << 16) |
        (static_cast<uint32_t>(p[2]) << 8) |
        static_cast<uint32_t>(p[3]);
}

static uint32_t PngCrc32Update(uint32_t crc, const BYTE* data, size_t len)
{
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= static_cast<uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit)
        {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static bool PngChunkTypeEquals(const BYTE* type, const char (&name)[5])
{
    return memcmp(type, name, 4) == 0;
}

static bool ReadPngExact(HANDLE h, BYTE* buffer, DWORD len)
{
    DWORD total = 0;
    while (total < len)
    {
        DWORD read = 0;
        if (!ReadFile(h, buffer + total, len - total, &read, nullptr) || read == 0)
            return false;
        total += read;
    }
    return true;
}

struct GeneratedPngInfo
{
    uint32_t width = 0;
    uint32_t height = 0;
};

static bool TryReadGeneratedPngInfo(const std::wstring& path, GeneratedPngInfo& info)
{
    info = {};

    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        return false;
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        return false;

    ULARGE_INTEGER size{};
    size.LowPart = data.nFileSizeLow;
    size.HighPart = data.nFileSizeHigh;
    if (size.QuadPart < 33)
        return false;
    if (size.QuadPart > 64ull * 1024ull * 1024ull)
        return false;

    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;

    bool ok = false;
    bool parseOk = true;
    unsigned long long pos = 0;

    static const BYTE pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    BYTE signature[sizeof(pngSignature)] = {};
    if (!ReadPngExact(h, signature, static_cast<DWORD>(sizeof(signature))) ||
        memcmp(signature, pngSignature, sizeof(pngSignature)) != 0)
    {
        CloseHandle(h);
        return false;
    }

    pos = sizeof(pngSignature);
    bool sawIhdr = false;
    bool sawIdat = false;
    bool sawIend = false;
    while (pos + 12 <= size.QuadPart)
    {
        BYTE chunkHeader[8] = {};
        if (!ReadPngExact(h, chunkHeader, sizeof(chunkHeader)))
        {
            parseOk = false;
            break;
        }
        pos += sizeof(chunkHeader);

        uint32_t chunkLen = ReadPngBigEndian32(chunkHeader);
        BYTE chunkType[4] = {};
        memcpy(chunkType, chunkHeader + 4, sizeof(chunkType));

        if (chunkLen > size.QuadPart - pos - 4)
        {
            parseOk = false;
            break;
        }

        uint32_t crc = PngCrc32Update(0xFFFFFFFFu, chunkType, sizeof(chunkType));
        BYTE ihdr[13] = {};
        if (PngChunkTypeEquals(chunkType, "IHDR"))
        {
            if (chunkLen != sizeof(ihdr) || !ReadPngExact(h, ihdr, sizeof(ihdr)))
            {
                parseOk = false;
                break;
            }
            crc = PngCrc32Update(crc, ihdr, sizeof(ihdr));
            pos += chunkLen;
        }
        else
        {
            uint32_t remaining = chunkLen;
            BYTE chunkBuffer[4096] = {};
            while (remaining > 0)
            {
                DWORD request = remaining > sizeof(chunkBuffer) ? sizeof(chunkBuffer) : remaining;
                if (!ReadPngExact(h, chunkBuffer, request))
                {
                    parseOk = false;
                    break;
                }
                crc = PngCrc32Update(crc, chunkBuffer, request);
                remaining -= request;
                pos += request;
            }
            if (!parseOk)
                break;
        }

        BYTE crcBytes[4] = {};
        if (!ReadPngExact(h, crcBytes, sizeof(crcBytes)))
        {
            parseOk = false;
            break;
        }
        pos += sizeof(crcBytes);

        uint32_t expectedCrc = ReadPngBigEndian32(crcBytes);
        if (~crc != expectedCrc)
        {
            parseOk = false;
            break;
        }

        if (PngChunkTypeEquals(chunkType, "IHDR"))
        {
            if (sawIhdr || pos != 33)
            {
                parseOk = false;
                break;
            }
            uint32_t width = ReadPngBigEndian32(ihdr);
            uint32_t height = ReadPngBigEndian32(ihdr + 4);
            if (width == 0 || height == 0)
            {
                parseOk = false;
                break;
            }
            info.width = width;
            info.height = height;
            sawIhdr = true;
        }
        else if (!sawIhdr)
        {
            parseOk = false;
            break;
        }
        else if (PngChunkTypeEquals(chunkType, "IDAT"))
        {
            if (chunkLen == 0 || sawIend)
            {
                parseOk = false;
                break;
            }
            sawIdat = true;
        }
        else if (PngChunkTypeEquals(chunkType, "IEND"))
        {
            if (chunkLen != 0 || !sawIdat)
            {
                parseOk = false;
                break;
            }
            sawIend = true;
            break;
        }
    }

    ok = parseOk && sawIhdr && sawIdat && sawIend && pos == size.QuadPart;
    CloseHandle(h);
    return ok;
}

static bool IsValidGeneratedPng(const std::wstring& path)
{
    GeneratedPngInfo info{};
    return TryReadGeneratedPngInfo(path, info);
}

static bool IsValidGeneratedPngWithDimensions(const std::wstring& path, int expectedWidth, int expectedHeight)
{
    if (expectedWidth <= 0 || expectedHeight <= 0)
        return false;

    GeneratedPngInfo info{};
    if (!TryReadGeneratedPngInfo(path, info))
        return false;

    return info.width == static_cast<uint32_t>(expectedWidth) &&
        info.height == static_cast<uint32_t>(expectedHeight);
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
    AddUniqueManifestAsset(assets, info.square30x30Logo);
    AddUniqueManifestAsset(assets, info.square71x71Logo);
    AddUniqueManifestAsset(assets, info.wide310x150Logo);
    AddUniqueManifestAsset(assets, info.square310x310Logo);
    AddUniqueManifestAsset(assets, info.splashScreenImage);
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

static bool RegistrationUnresolvedRecorded()
{
    std::lock_guard<std::mutex> lk(g_registrationUnresolvedMutex);
    return g_registrationUnresolved;
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
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    std::vector<int> scales = GetConfiguredScales();

    if (UseExperimentalLiveTileUpdate())
    {
        for (const auto& t : g_tiles)
        {
            std::wstring manifestAssetPath = ManifestAssetPathForTile(manifestInfo, t);
            std::wstring basePath = ResolveManifestAssetPath(exeDir, manifestAssetPath);

            if (!g_generateDesktopIconForDisabledEntries)
            {
                if (PathFileExistsW(basePath.c_str()))
                    return false;
                for (int scale : SUPPORTED_ASSET_SCALES)
                {
                    if (PathFileExistsW(ScaleAssetPath(basePath, scale).c_str()))
                        return false;
                }
                continue;
            }

            if (!IsValidGeneratedPngWithDimensions(basePath, t.w, t.h))
                return false;

            for (int scale : scales)
            {
                std::wstring scaledPath = ScaleAssetPath(basePath, scale);
                if (!IsValidGeneratedPngWithDimensions(scaledPath, ScalePixels(t.w, scale), ScalePixels(t.h, scale)))
                    return false;
            }
        }

        for (const auto& asset : g_liveTileAssets)
        {
            std::wstring path = ResolveManifestAssetPath(exeDir, asset.file);
            if (!IsValidGeneratedPngWithDimensions(path, asset.w, asset.h))
                return false;
        }

        return true;
    }

    for (const auto& t : g_tiles)
    {
        std::wstring manifestAssetPath = ManifestAssetPathForTile(manifestInfo, t);
        std::wstring basePath = ResolveManifestAssetPath(exeDir, manifestAssetPath);

        bool generatedByCurrentConfig =
            IniReadI(L"Assets", t.name, 0) != 0 ||
            g_generateDesktopIconForDisabledEntries;
        if (!generatedByCurrentConfig)
            continue;

        if (!IsValidGeneratedPngWithDimensions(basePath, t.w, t.h))
            return false;

        for (int scale : scales)
        {
            std::wstring scaledPath = ScaleAssetPath(basePath, scale);
            if (!IsValidGeneratedPngWithDimensions(scaledPath, ScalePixels(t.w, scale), ScalePixels(t.h, scale)))
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
    state += L"v=4;";
    state += L"wallpaper=" + FileFingerprint(wallpaperPath) + L";";
    state += L"wallpaperDecodeLowMemory=" + IntToWString(IniReadI(L"Settings", L"WallpaperDecodeLowMemory", 1) ? 1 : 0) + L";";
    state += L"wallpaperDecodeMaxLongEdge=" + IntToWString(IniReadClampedI(L"Settings", L"WallpaperDecodeMaxLongEdge", 0, 0, 32768)) + L";";
    state += L"exe=" + FileFingerprint(g_exePath) + L";";
    state += L"manifest=" + FileFingerprint(GetExeDir() + L"\\AppxManifest.xml") + L";";
    state += L"method=" + std::wstring(WallpaperDetectionMethodName(CurrentWallpaperDetectionMethod())) + L";";
    state += L"methodFallback=" + IntToWString(g_wallpaperDetectionFallbackOnInvalid ? 1 : 0) + L";";
    state += L"usePowerShell=" + IntToWString(UsePowerShell() ? 1 : 0) + L";";
    state += L"liveTileUpdateMode=" + LiveTileUpdateModeIniValue(ConfiguredLiveTileUpdateMode()) + L";";
    state += L"effectiveLiveTileUpdate=" + IntToWString(UseExperimentalLiveTileUpdate() ? 1 : 0) + L";";
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

    state += L"tileTextEnabled=" + IntToWString(IniReadI(L"TileText", L"Enabled", 0) ? 1 : 0) + L";";
    state += L"tileText=" + IniReadS(L"TileText", L"Text", L"") + L";";
    state += L"tileTextSecondary=" + IniReadS(L"TileText", L"SecondaryText", L"") + L";";
    state += L"tileTextBadge=" + IniReadS(L"TileText", L"BadgeText", L"") + L";";
    state += L"tileTextFont=" + IniReadS(L"TileText", L"Font", L"Segoe UI") + L";";
    state += L"tileTextFontSize=" + IntToWString(IniReadClampedI(L"TileText", L"FontSize", 18, 6, 256)) + L";";
    state += L"tileTextSecondaryFontSize=" + IntToWString(IniReadClampedI(L"TileText", L"SecondaryFontSize", 12, 6, 256)) + L";";
    state += L"tileTextBadgeFontSize=" + IntToWString(IniReadClampedI(L"TileText", L"BadgeFontSize", 40, 6, 256)) + L";";
    state += L"tileTextBold=" + IntToWString(IniReadI(L"TileText", L"Bold", 0) ? 1 : 0) + L";";
    state += L"tileTextSecondaryBold=" + IntToWString(IniReadI(L"TileText", L"SecondaryBold", 0) ? 1 : 0) + L";";
    state += L"tileTextBadgeBold=" + IntToWString(IniReadI(L"TileText", L"BadgeBold", 0) ? 1 : 0) + L";";
    state += L"tileTextItalic=" + IntToWString(IniReadI(L"TileText", L"Italic", 0) ? 1 : 0) + L";";
    state += L"tileTextColor=" + IniReadS(L"TileText", L"Color", L"#FFFFFFFF") + L";";
    state += L"tileTextSecondaryColor=" + IniReadS(L"TileText", L"SecondaryColor", L"#CCFFFFFF") + L";";
    state += L"tileTextBadgeColor=" + IniReadS(L"TileText", L"BadgeColor", L"#FFFFFFFF") + L";";
    state += L"tileTextShadow=" + IntToWString(IniReadI(L"TileText", L"Shadow", 1) ? 1 : 0) + L";";
    state += L"tileTextShadowColor=" + IniReadS(L"TileText", L"ShadowColor", L"#AA000000") + L";";
    state += L"tileTextH=" + IniReadS(L"TileText", L"HorizontalAlign", L"Center") + L";";
    state += L"tileTextV=" + IniReadS(L"TileText", L"VerticalAlign", L"Top") + L";";
    state += L"tileTextBadgeH=" + IniReadS(L"TileText", L"BadgeHorizontalAlign", L"Right") + L";";
    state += L"tileTextBadgeV=" + IniReadS(L"TileText", L"BadgeVerticalAlign", L"Top") + L";";
    state += L"tileTextMarginX=" + IntToWString(IniReadClampedI(L"TileText", L"MarginX", 8, 0, 1000)) + L";";
    state += L"tileTextMarginY=" + IntToWString(IniReadClampedI(L"TileText", L"MarginY", 10, 0, 1000)) + L";";
    state += L"tileTextLineGap=" + IntToWString(IniReadClampedI(L"TileText", L"LineGap", 2, 0, 1000)) + L";";
    state += L"tileTextMaxSecondaryLines=" + IntToWString(IniReadClampedI(L"TileText", L"MaxSecondaryLines", 2, 1, 20)) + L";";
    state += L"tileTextApplyMedium=" + IntToWString(IniReadI(L"TileText", L"ApplyToMediumTile", 1) ? 1 : 0) + L";";
    state += L"tileTextApplyWide=" + IntToWString(IniReadI(L"TileText", L"ApplyToWideTile", 1) ? 1 : 0) + L";";
    state += L"tileTextApplyLarge=" + IntToWString(IniReadI(L"TileText", L"ApplyToLargeTile", 1) ? 1 : 0) + L";";
    state += L"tileTextApplySmall=" + IntToWString(IniReadI(L"TileText", L"ApplyToSmallTile", 0) ? 1 : 0) + L";";
    state += L"tileTextApplyLogos=" + IntToWString(IniReadI(L"TileText", L"ApplyToLogos", 0) ? 1 : 0) + L";";

    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, state);
    return Hex64(hash);
}

static std::wstring BuildLiveTileModeReregistrationStateKey()
{
    std::wstring state;
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();

    state += L"v=2;";
    state += L"mode=" + LiveTileUpdateModeIniValue(ConfiguredLiveTileUpdateMode()) + L";";
    state += L"target=" + ManifestCompatibilityTargetIniValue(ConfiguredManifestCompatibilityTarget()) + L";";
    state += L"identity=" + manifestInfo.identityName + L";";
    state += L"appId=" + manifestInfo.applicationId + L";";
    state += L"exe=" + manifestInfo.executable + L";";
    state += L"displayName=" + manifestInfo.displayName + L";";
    state += L"desktopIconForDisabled=" + IntToWString(g_generateDesktopIconForDisabledEntries ? 1 : 0) + L";";
    state += L"hideDisabled=" + IntToWString(g_hideDisabled ? 1 : 0) + L";";
    state += L"deleteDisabled=" + IntToWString(g_deleteDisabledAssets ? 1 : 0) + L";";

    for (const auto& t : g_tiles)
        state += std::wstring(t.name) + L"=" + IntToWString(IniReadI(L"Assets", t.name, 0) ? 1 : 0) + L";";

    uint64_t hash = 14695981039346656037ull;
    HashWide(hash, state);
    return Hex64(hash);
}

static bool LiveTileModeReregistrationStateIsCurrent()
{
    std::wstring last = IniReadS(L"State", L"LastLiveTileModeReregistrationKey", L"");
    return !last.empty() && IEquals(last, BuildLiveTileModeReregistrationStateKey());
}

static void StoreLiveTileModeReregistrationState()
{
    std::wstring key = BuildLiveTileModeReregistrationStateKey();
    if (!key.empty())
        IniWrite(L"State", L"LastLiveTileModeReregistrationKey", key.c_str());
}

static bool LiveTileModeReregistrationPending()
{
    return IniReadI(L"State", L"LiveTileModeReregistrationPending", 0) != 0;
}

static void SetLiveTileModeReregistrationPending(bool pending)
{
    IniWrite(L"State", L"LiveTileModeReregistrationPending", pending ? L"1" : L"0");
}

static bool LiveTileWasClearedOnShutdown()
{
    return IniReadI(L"State", L"LiveTileClearedOnShutdown", 0) != 0;
}

static void SetLiveTileClearedOnShutdown(bool cleared)
{
    IniWrite(L"State", L"LiveTileClearedOnShutdown", cleared ? L"1" : L"0");
}

static bool StartupLiveTileClearedStateNeedsGeneration()
{
    return UseExperimentalLiveTileUpdate() && LiveTileWasClearedOnShutdown();
}

static bool FixedPackagedLiveTileModeNeedsUnpackagedReregistration()
{
    bool shouldRunPackaged = false;
    if (!LiveTileModeHasFixedRuntime(
            ConfiguredLiveTileUpdateMode(),
            ConfiguredManifestCompatibilityTarget(),
            shouldRunPackaged))
        return false;

    if (!shouldRunPackaged)
        return false;

    return LiveTileModeReregistrationPending() || !LiveTileModeReregistrationStateIsCurrent();
}

static bool StartupGenerationCanSkip(const std::wstring& wallpaperPath)
{
    if (!g_generateOnStartupOnlyWhenChanged)
        return false;
    if (LiveTileModeReregistrationPending())
        return false;
    if (StartupLiveTileClearedStateNeedsGeneration())
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

static bool PrepareWallpaperRenderSource(
    Bitmap* src,
    FitMode& renderMode,
    std::unique_ptr<Bitmap>& preparedWallpaper,
    Bitmap*& renderSource)
{
    if (!src)
        return false;

    FitMode mode = g_disableFitting ? FitMode::Fill : GetWallpaperFit();
    renderSource = src;
    renderMode = mode;
    preparedWallpaper.reset();

    if (mode == FitMode::Tile || mode == FitMode::Span)
    {
        preparedWallpaper.reset((mode == FitMode::Tile)
            ? BuildTiledPrimaryBitmap(src)
            : BuildSpannedPrimaryBitmap(src));
        if (preparedWallpaper)
        {
            renderSource = preparedWallpaper.get();
            renderMode = FitMode::Fill;
        }
        else
        {
            renderMode = FitMode::Fill;
        }
    }

    return true;
}

static bool SaveGeneratedAssetCacheBitmap(Bitmap* bitmap, const std::wstring& path)
{
    DWORD dirError = ERROR_SUCCESS;
    if (!EnsureParentDirectoryForFile(path, &dirError))
    {
        LogWin32Failure(g_ui.createAssetsDirectoryOperation.c_str(), dirError);
        Log(g_ui.failedSaveAsset.c_str(), path.c_str());
        return false;
    }

    if (SavePNG(bitmap, path.c_str()))
        return true;

    Log(g_ui.failedSaveAsset.c_str(), path.c_str());
    return false;
}

static bool ShouldBakeTileTextIntoAssets(bool liveTileUpdateMode)
{
    if (!TileTextOverlayEnabled())
        return false;

    // Windows 10 adaptive Live Tiles can carry text in XML. Static/registration
    // tiles and older image-template Live Tiles need the text baked into PNGs.
    return !liveTileUpdateMode || ConfiguredManifestCompatibilityTarget() != ManifestCompatibilityTarget::Windows10;
}

static bool GenerationHasWallpaperBackedAssets(bool liveTileUpdateMode)
{
    if (liveTileUpdateMode)
        return true;

    for (const auto& t : g_tiles)
    {
        if (IniReadI(L"Assets", t.name, 0) != 0)
            return true;
    }
    return false;
}

static void AddLongEdgeCandidate(int& maxLongEdge, int width, int height)
{
    if (width > 0)
        maxLongEdge = std::max(maxLongEdge, width);
    if (height > 0)
        maxLongEdge = std::max(maxLongEdge, height);
}

static int MaxWallpaperBackedAssetLongEdge(const std::vector<int>& scales, bool liveTileUpdateMode)
{
    int maxLongEdge = 0;

    if (!liveTileUpdateMode)
    {
        for (const auto& t : g_tiles)
        {
            if (IniReadI(L"Assets", t.name, 0) == 0)
                continue;

            AddLongEdgeCandidate(maxLongEdge, t.w, t.h);
            for (int scale : scales)
                AddLongEdgeCandidate(maxLongEdge, ScalePixels(t.w, scale), ScalePixels(t.h, scale));
        }
    }

    if (liveTileUpdateMode)
    {
        for (const auto& t : g_liveTileAssets)
            AddLongEdgeCandidate(maxLongEdge, t.w, t.h);
    }

    return maxLongEdge;
}

static int MaxWallpaperDecodeLongEdgeForGeneration(const std::vector<int>& scales, bool liveTileUpdateMode)
{
    if (!GenerationHasWallpaperBackedAssets(liveTileUpdateMode))
        return 0;

    int maxLongEdge = MaxWallpaperBackedAssetLongEdge(scales, liveTileUpdateMode);

    // A wallpaper larger than the visible desktop cannot contribute extra detail
    // to the desktop-backed tile image, but it can cost hundreds of MB when GDI+
    // decodes it at native size. Use the monitor/virtual desktop as the automatic
    // decode ceiling, while still honoring unusually large generated assets.
    if (!g_disableFitting)
    {
        FitMode wallpaperFit = GetWallpaperFit();
        if ((wallpaperFit == FitMode::Center || wallpaperFit == FitMode::Tile) &&
            IniReadClampedI(L"Settings", L"WallpaperDecodeMaxLongEdge", 0, 0, 32768) <= 0)
        {
            // Center/Tile currently use the source bitmap's own pixel size as part
            // of their visual result. Keep the old full-size decode for those
            // modes unless the user explicitly sets WallpaperDecodeMaxLongEdge.
            return 0;
        }

        RECT primaryRect{};
        if (GetPrimaryMonitorRect(primaryRect))
            AddLongEdgeCandidate(maxLongEdge, primaryRect.right - primaryRect.left, primaryRect.bottom - primaryRect.top);

        if (wallpaperFit == FitMode::Span)
        {
            RECT virtualRect{};
            if (GetVirtualDesktopRect(virtualRect))
                AddLongEdgeCandidate(maxLongEdge, virtualRect.right - virtualRect.left, virtualRect.bottom - virtualRect.top);
        }
    }

    if (maxLongEdge <= 0)
        maxLongEdge = 2048;
    return ClampInt(maxLongEdge, 1, 32768);
}

static void LogWallpaperBitmapLoad(const WallpaperBitmapLoadInfo& info, Bitmap* bitmap)
{
    int bitmapWidth = 0;
    int bitmapHeight = 0;
    if (!BitmapDimensionsToInt(bitmap, bitmapWidth, bitmapHeight))
    {
        LogText(g_ui.failedLoadWallpaper);
        return;
    }

    if (info.usedWic && info.memoryConstrained)
    {
        Log(g_ui.wallpaperImageLoadedWicMemoryConstrained.c_str(),
            static_cast<unsigned long>(info.originalWidth),
            static_cast<unsigned long>(info.originalHeight),
            static_cast<unsigned long>(info.loadedWidth),
            static_cast<unsigned long>(info.loadedHeight),
            static_cast<unsigned long>(info.requestedWidth),
            static_cast<unsigned long>(info.requestedHeight),
            info.maxLongEdge);
        return;
    }

    if (info.usedWic && info.downscaled)
    {
        Log(g_ui.wallpaperImageLoadedWicDownscaled.c_str(),
            static_cast<unsigned long>(info.originalWidth),
            static_cast<unsigned long>(info.originalHeight),
            static_cast<unsigned long>(info.loadedWidth),
            static_cast<unsigned long>(info.loadedHeight),
            info.maxLongEdge);
        return;
    }

    if (info.usedWic)
    {
        Log(g_ui.wallpaperImageLoadedWic.c_str(),
            static_cast<unsigned long>(info.loadedWidth),
            static_cast<unsigned long>(info.loadedHeight),
            info.maxLongEdge);
        return;
    }

    if (info.attemptedWic)
    {
        Log(g_ui.wallpaperImageLoadedGdiWicFallback.c_str(), bitmapWidth, bitmapHeight, info.maxLongEdge);
        return;
    }

    Log(g_ui.wallpaperImageLoaded.c_str(), bitmapWidth, bitmapHeight);
}

static bool RenderWallpaperToGeneratedAssetCache(
    const std::wstring& exeDir,
    const ManifestDisplayInfo& manifestInfo,
    const std::vector<int>& scales,
    bool liveTileUpdateMode,
    const std::wstring& wallpaperPath)
{
    if (!GeneratedAssetPrecacheEnabled() || !IsUsableWallpaperPath(wallpaperPath))
        return false;

    std::wstring stateKey = BuildGenerationStateKey(wallpaperPath);
    if (stateKey.empty() ||
        GeneratedAssetCacheComplete(exeDir, manifestInfo, scales, liveTileUpdateMode, stateKey))
        return false;

    ScopedGdiPlus gdi;
    if (!gdi.ok())
        return false;

    std::wstring cacheDir = GeneratedAssetCacheDir(exeDir, stateKey);
    std::wstring stagingCacheDir = cacheDir + L".precache." + NewLiveTileAssetToken();
    DeleteDirectoryTree(stagingCacheDir);

    WallpaperBitmapLoadInfo wallpaperLoadInfo{};
    std::unique_ptr<Bitmap> src = LoadWallpaperBitmapForRender(
        wallpaperPath,
        MaxWallpaperDecodeLongEdgeForGeneration(scales, liveTileUpdateMode),
        &wallpaperLoadInfo);
    if (!src || src->GetLastStatus() != Ok)
        return false;

    FitMode renderMode = FitMode::Fill;
    std::unique_ptr<Bitmap> preparedWallpaper;
    Bitmap* renderSource = nullptr;
    if (!PrepareWallpaperRenderSource(src.get(), renderMode, preparedWallpaper, renderSource))
        return false;

    bool ok = true;
    bool savedAny = false;
    std::unique_ptr<Bitmap> icon;

    for (const auto& t : g_tiles)
    {
        bool staticWallpaperAssetEnabled = !liveTileUpdateMode && IniReadI(L"Assets", t.name, 0) != 0;
        if (!staticWallpaperAssetEnabled)
        {
            if (!g_generateDesktopIconForDisabledEntries)
                continue;

            if (!icon)
                icon.reset(LoadEmbeddedDesktopIcon());
            if (!icon)
            {
                ok = false;
                continue;
            }

            std::wstring outPath = GeneratedAssetCacheFilePath(stagingCacheDir, ManifestGeneratedAssetCacheName(t.name));
            std::unique_ptr<Bitmap> desktopAsset(RenderDesktopIconAsset(icon.get(), t.w, t.h, t.w, t.h));
            bool saved = desktopAsset && SaveGeneratedAssetCacheBitmap(desktopAsset.get(), outPath);
            ok = saved && ok;
            savedAny = saved || savedAny;

            for (int scale : scales)
            {
                int scaledW = ScalePixels(t.w, scale);
                int scaledH = ScalePixels(t.h, scale);
                std::wstring scaledPath = GeneratedAssetCacheFilePath(
                    stagingCacheDir,
                    ScaleAssetPath(ManifestGeneratedAssetCacheName(t.name), scale));
                std::unique_ptr<Bitmap> scaledDesktopAsset(RenderDesktopIconAsset(icon.get(), scaledW, scaledH, t.w, t.h));
                bool scaledSaved = scaledDesktopAsset && SaveGeneratedAssetCacheBitmap(scaledDesktopAsset.get(), scaledPath);
                ok = scaledSaved && ok;
                savedAny = scaledSaved || savedAny;
            }
            continue;
        }

        std::wstring outPath = GeneratedAssetCacheFilePath(stagingCacheDir, ManifestGeneratedAssetCacheName(t.name));
        std::unique_ptr<Bitmap> asset(ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0)));
        if (asset && ShouldBakeTileTextIntoAssets(liveTileUpdateMode))
            ok = ApplyTileTextOverlay(asset.get(), t.name, t.w, t.h) && ok;
        bool saved = asset && SaveGeneratedAssetCacheBitmap(asset.get(), outPath);
        ok = saved && ok;
        savedAny = saved || savedAny;

        for (int scale : scales)
        {
            int scaledW = ScalePixels(t.w, scale);
            int scaledH = ScalePixels(t.h, scale);
            std::wstring scaledPath = GeneratedAssetCacheFilePath(
                stagingCacheDir,
                ScaleAssetPath(ManifestGeneratedAssetCacheName(t.name), scale));
            std::unique_ptr<Bitmap> scaled(ResizeBitmapToMode(renderSource, scaledW, scaledH, renderMode, Color(255, 0, 0, 0)));
            if (scaled && ShouldBakeTileTextIntoAssets(liveTileUpdateMode))
                ok = ApplyTileTextOverlay(scaled.get(), t.name, t.w, t.h) && ok;
            bool scaledSaved = scaled && SaveGeneratedAssetCacheBitmap(scaled.get(), scaledPath);
            ok = scaledSaved && ok;
            savedAny = scaledSaved || savedAny;
        }
    }

    if (liveTileUpdateMode)
    {
        for (const auto& t : g_liveTileAssets)
        {
            std::wstring outPath = GeneratedAssetCacheFilePath(stagingCacheDir, LiveGeneratedAssetCacheName(t.name));
            std::unique_ptr<Bitmap> asset(ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0)));
            if (asset && ShouldBakeTileTextIntoAssets(liveTileUpdateMode))
                ok = ApplyTileTextOverlay(asset.get(), t.name, t.w, t.h) && ok;
            bool saved = asset && SaveGeneratedAssetCacheBitmap(asset.get(), outPath);
            ok = saved && ok;
            savedAny = saved || savedAny;
        }
    }

    if (!ok)
    {
        DeleteDirectoryTree(stagingCacheDir);
        return false;
    }

    if (savedAny)
    {
        std::vector<LiveTileUpdateAsset> unusedLiveTileAssets;
        std::vector<GeneratedAssetCacheFile> expectedFiles = BuildGeneratedAssetCacheFileList(
            exeDir,
            manifestInfo,
            scales,
            liveTileUpdateMode,
            L"0000000000000000",
            unusedLiveTileAssets);
        if (!GeneratedAssetCacheFilesPresent(stagingCacheDir, expectedFiles))
        {
            DeleteDirectoryTree(stagingCacheDir);
            return false;
        }

        if (GeneratedAssetCacheComplete(exeDir, manifestInfo, scales, liveTileUpdateMode, stateKey))
        {
            DeleteDirectoryTree(stagingCacheDir);
            return false;
        }

        DeleteDirectoryTree(cacheDir);
        if (!MoveFileExW(stagingCacheDir.c_str(), cacheDir.c_str(), MOVEFILE_WRITE_THROUGH))
        {
            LogWin32Failure(L"MoveFileExW(pre-cache publish)");
            DeleteDirectoryTree(stagingCacheDir);
            return false;
        }

        Log(g_ui.generatedAssetPrecacheSaved.c_str(), stateKey.c_str());
        PruneGeneratedAssetCache(exeDir);
    }

    return savedAny;
}

static bool IsSupportedWallpaperImageName(const std::wstring& fileName)
{
    size_t dot = fileName.find_last_of(L'.');
    if (dot == std::wstring::npos)
        return false;

    std::wstring ext = ToLowerCopy(fileName.substr(dot));
    return ext == L".jpg" ||
        ext == L".jpeg" ||
        ext == L".jpe" ||
        ext == L".jfif" ||
        ext == L".png" ||
        ext == L".bmp" ||
        ext == L".dib" ||
        ext == L".gif" ||
        ext == L".tif" ||
        ext == L".tiff";
}

static std::vector<std::wstring> EnumerateSiblingWallpaperCandidates(
    const std::wstring& currentWallpaper,
    int maxFiles)
{
    std::vector<std::wstring> result;
    if (maxFiles <= 0 || !IsUsableWallpaperPath(currentWallpaper))
        return result;

    std::wstring dir = ParentDirectoryOfPath(currentWallpaper);
    if (dir.empty())
        return result;

    struct WallpaperCandidate
    {
        std::wstring name;
        std::wstring path;
    };

    std::vector<WallpaperCandidate> candidates;
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
        return result;

    do
    {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            continue;

        std::wstring name = data.cFileName;
        if (!IsSupportedWallpaperImageName(name))
            continue;

        std::wstring path = dir + L"\\" + name;
        if (!IsUsableWallpaperPath(path))
            continue;

        candidates.push_back({ name, path });
    } while (FindNextFileW(find, &data));

    FindClose(find);

    if (candidates.size() <= 1)
        return result;

    std::sort(candidates.begin(), candidates.end(),
        [](const WallpaperCandidate& a, const WallpaperCandidate& b)
        {
            return StrCmpLogicalW(a.name.c_str(), b.name.c_str()) < 0;
        });

    size_t start = 0;
    bool foundCurrent = false;
    for (size_t i = 0; i < candidates.size(); ++i)
    {
        if (IEquals(candidates[i].path, currentWallpaper))
        {
            start = i;
            foundCurrent = true;
            break;
        }
    }

    size_t offsetStart = foundCurrent ? 1 : 0;
    for (size_t offset = offsetStart;
        offset < candidates.size() && result.size() < static_cast<size_t>(maxFiles);
        ++offset)
    {
        size_t index = foundCurrent
            ? ((start + offset) % candidates.size())
            : offset;
        if (!IEquals(candidates[index].path, currentWallpaper))
            result.push_back(candidates[index].path);
    }

    return result;
}

static void RunGeneratedAssetPrecache(const std::wstring& currentWallpaper)
{
    ReloadRuntimeSettingsFromIni();
    if (!GeneratedAssetPrecacheEnabled() || g_shutdownRequested)
        return;

    int maxFiles = GeneratedAssetPrecacheMaxFiles();
    if (maxFiles <= 0)
        return;

    std::vector<std::wstring> candidates = EnumerateSiblingWallpaperCandidates(currentWallpaper, maxFiles);
    if (candidates.empty())
        return;

    std::wstring exeDir = GetExeDir();
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    bool liveTileUpdateMode = UseExperimentalLiveTileUpdate();
    std::vector<int> scales = GetConfiguredScales();

    for (const auto& candidate : candidates)
    {
        if (g_shutdownRequested || GenerationWorkPending())
            return;

        RenderWallpaperToGeneratedAssetCache(exeDir, manifestInfo, scales, liveTileUpdateMode, candidate);
    }

    TrimIdleWorkingSetIfConfigured();
}

static void GeneratedAssetPrecacheThread()
{
    for (;;)
    {
        std::wstring wallpaper;
        {
            std::unique_lock<std::mutex> lk(g_generatedAssetPrecacheMutex);
            g_generatedAssetPrecacheCv.wait(lk, []
            {
                return g_generatedAssetPrecacheStop || g_generatedAssetPrecacheQueued;
            });

            if (!g_generatedAssetPrecacheQueued && g_generatedAssetPrecacheStop)
                break;

            wallpaper = std::move(g_generatedAssetPrecacheWallpaper);
            g_generatedAssetPrecacheQueued = false;
        }

        try
        {
            RunGeneratedAssetPrecache(wallpaper);
        }
        catch (...)
        {
            // Pre-caching is opportunistic; foreground generation remains authoritative.
        }
    }
}

static void QueueGeneratedAssetPrecache(const std::wstring& wallpaper)
{
    if (!GeneratedAssetPrecacheEnabled() || wallpaper.empty() || g_shutdownRequested)
        return;

    bool startWorker = false;
    {
        std::lock_guard<std::mutex> lk(g_generatedAssetPrecacheMutex);
        if (g_generatedAssetPrecacheStop)
            return;

        g_generatedAssetPrecacheWallpaper = wallpaper;
        g_generatedAssetPrecacheQueued = true;
        if (!g_generatedAssetPrecacheWorkerStarted)
        {
            g_generatedAssetPrecacheWorkerStarted = true;
            startWorker = true;
        }
    }

    if (startWorker)
    {
        try
        {
            g_generatedAssetPrecacheThread = std::thread(GeneratedAssetPrecacheThread);
        }
        catch (...)
        {
            std::lock_guard<std::mutex> lk(g_generatedAssetPrecacheMutex);
            g_generatedAssetPrecacheWorkerStarted = false;
            g_generatedAssetPrecacheQueued = false;
            g_generatedAssetPrecacheWallpaper.clear();
            return;
        }
    }

    g_generatedAssetPrecacheCv.notify_one();
}

static void JoinGeneratedAssetPrecache()
{
    {
        std::lock_guard<std::mutex> lk(g_generatedAssetPrecacheMutex);
        g_generatedAssetPrecacheStop = true;
    }
    g_generatedAssetPrecacheCv.notify_one();

    if (g_generatedAssetPrecacheThread.joinable())
        g_generatedAssetPrecacheThread.join();
}

static bool RegisterAppxManifest(const std::wstring& manifestPath, std::wstring& failureMessage)
{
    failureMessage.clear();

    auto setPowerShellFailureMessage = [&]()
    {
        std::lock_guard<std::mutex> lk(g_psMutex);
        failureMessage = g_psMsg.empty() ? g_ui.powerShellRegistrationFailed : g_psMsg;
    };

    std::wstring unresolvedReason;
    if (RegistrationBlockedByUnresolved(unresolvedReason))
    {
        Log(g_ui.registrationBlockedUnresolved.c_str(), unresolvedReason.c_str());
        failureMessage = unresolvedReason;
        return false;
    }

    if (!UsePowerShell())
    {
        AppxRegistrationResult comResult = Appx_Register_COM(manifestPath);
        if (comResult == AppxRegistrationResult::Success)
            return true;

        if (comResult == AppxRegistrationResult::TimeoutUncertain)
        {
            LogText(g_ui.comRegistrationFallbackSkippedAfterTimeout);
            if (!RegistrationUnresolvedRecorded())
                MarkRegistrationUnresolved(g_ui.comRegistrationFallbackSkippedAfterTimeout, nullptr, 0);
            failureMessage = g_ui.comRegistrationFallbackSkippedAfterTimeout;
            return false;
        }

        LogText(g_ui.comRegistrationFailed);
        std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
        bool ok = PS_Run(ps);
        if (!ok)
            setPowerShellFailureMessage();
        return ok;
    }

    std::wstring ps = L"Add-AppxPackage -Register \"" + manifestPath + L"\" -ForceUpdateFromAnyVersion";
    bool ok = PS_Run(ps);
    if (!ok)
        setPowerShellFailureMessage();
    return ok;
}


static std::wstring CleanGenerationReasonForSentence(const wchar_t* reason)
{
    std::wstring text = TrimCopy(reason ? reason : L"");
    if (text.empty())
        text = g_ui.unspecifiedReason;

    if (text.rfind(L"[i]", 0) == 0 || text.rfind(L"[!]", 0) == 0)
        text = TrimCopy(text.substr(3));

    while (!text.empty())
    {
        wchar_t ch = text.back();
        if (ch == L'.' || ch == L'!' || ch == L'?' || ch == L':' || iswspace(ch) != 0)
            text.pop_back();
        else
            break;
    }

    return text.empty() ? g_ui.unspecifiedReason : text;
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

    std::wstring why = CleanGenerationReasonForSentence(reason);
    Log(g_ui.startingWallpaperGeneration.c_str(), why.c_str());
    Log(g_ui.wallpaperSource.c_str(), wp);

    if (g_notifyOnStart)
        NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationStarted, NIIF_INFO);

    std::wstring exeDir = GetExeDir();
    ManifestDisplayInfo manifestInfo = CurrentManifestDisplayInfo();
    bool liveTileUpdateMode = UseExperimentalLiveTileUpdate();
    bool liveTileModeReregistrationPending = LiveTileModeReregistrationPending();
    bool useLiveTileUpdateForThisRun = liveTileUpdateMode && !liveTileModeReregistrationPending;
    bool assetsOk = true;
    bool restoredFromGeneratedAssetCache = false;
    std::vector<LiveTileUpdateAsset> liveTileUpdateAssets;
    std::vector<GeneratedAssetCacheFile> generatedAssetCacheFiles;
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
    std::vector<std::wstring> loggedAssetDirs;
    auto logAssetDirIfNeeded = [&](const std::wstring& assetDir)
    {
        bool directoryAlreadyLogged = false;
        for (const auto& loggedDir : loggedAssetDirs)
        {
            if (IEquals(loggedDir, assetDir))
            {
                directoryAlreadyLogged = true;
                break;
            }
        }
        if (!directoryAlreadyLogged)
        {
            loggedAssetDirs.push_back(assetDir);
            Log(g_ui.assetsDirectoryReady.c_str(), assetDir.c_str());
        }
    };

    std::wstring generationStateKey = BuildGenerationStateKey(wp);
    restoredFromGeneratedAssetCache = TryRestoreGeneratedAssetCache(
        exeDir,
        manifestInfo,
        scales,
        liveTileUpdateMode,
        generationStateKey,
        liveTileUpdateAssets);

    if (!restoredFromGeneratedAssetCache)
    {
        ScopedGdiPlus gdi;
        if (!gdi.ok())
            return false;

        WallpaperBitmapLoadInfo wallpaperLoadInfo{};
        std::unique_ptr<Bitmap> src = LoadWallpaperBitmapForRender(
            wp,
            MaxWallpaperDecodeLongEdgeForGeneration(scales, liveTileUpdateMode),
            &wallpaperLoadInfo);
        if (!src || src->GetLastStatus() != Ok)
        {
            LogText(g_ui.failedLoadWallpaper);
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.failedLoadWallpaper, NIIF_ERROR);
            return false;
        }
        LogWallpaperBitmapLoad(wallpaperLoadInfo, src.get());

        FitMode renderMode = FitMode::Fill;
        std::unique_ptr<Bitmap> preparedWallpaper;
        Bitmap* renderSource = nullptr;
        PrepareWallpaperRenderSource(src.get(), renderMode, preparedWallpaper, renderSource);
        Log(g_ui.renderModeSummary.c_str(), FitModeToString(renderMode));

        for (auto& t : g_tiles)
        {
        std::wstring manifestAssetPath = ManifestAssetPathForTile(manifestInfo, t);
        std::wstring outPath = ResolveManifestAssetPath(exeDir, manifestAssetPath);
        DWORD assetDirError = ERROR_SUCCESS;
        if (!EnsureParentDirectoryForFile(outPath, &assetDirError))
        {
            assetsOk = false;
            LogWin32Failure(g_ui.createAssetsDirectoryOperation.c_str(), assetDirError);
            Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
            continue;
        }
        logAssetDirIfNeeded(ParentDirectoryOfPath(outPath));

        bool staticWallpaperAssetEnabled = !liveTileUpdateMode && IniReadI(L"Assets", t.name, 0) != 0;
        if (!staticWallpaperAssetEnabled)
        {
            if (!g_generateDesktopIconForDisabledEntries)
            {
                Log((liveTileUpdateMode ? g_ui.skippingStaticAssetLiveTileMode : g_ui.skippingAssetDisabled).c_str(), t.name);
                if ((g_deleteDisabledAssets || liveTileUpdateMode) && !DeleteDisabledAssetFiles(outPath))
                    assetsOk = false;
                continue;
            }

            // Render the Desktop icon into full-size transparent asset
            // canvases using Windows 8-style tile logo sizing and placement.
            {
                std::unique_ptr<Bitmap> icon(LoadEmbeddedDesktopIcon());
                if (!icon)
                {
                    assetsOk = false;
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                    continue;
                }

                std::unique_ptr<Bitmap> desktopAsset(RenderDesktopIconAsset(icon.get(), t.w, t.h, t.w, t.h));
                bool ok = desktopAsset && SavePNG(desktopAsset.get(), outPath.c_str());
                if (ok)
                {
                    Log(g_ui.generatedDesktopIconAsset.c_str(), outPath.c_str());
                    AddGeneratedAssetCacheFile(generatedAssetCacheFiles, ManifestGeneratedAssetCacheName(t.name), outPath, t.w, t.h);
                }
                else
                {
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                }

                for (int scale : scales)
                {
                    int scaledW = ScalePixels(t.w, scale);
                    int scaledH = ScalePixels(t.h, scale);
                    std::wstring scaledPath = ScaleAssetPath(outPath, scale);
                    std::unique_ptr<Bitmap> scaledDesktopAsset(RenderDesktopIconAsset(icon.get(), scaledW, scaledH, t.w, t.h));
                    bool scaledOk = scaledDesktopAsset && SavePNG(scaledDesktopAsset.get(), scaledPath.c_str());
                    if (scaledOk)
                    {
                        Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                        AddGeneratedAssetCacheFile(
                            generatedAssetCacheFiles,
                            ScaleAssetPath(ManifestGeneratedAssetCacheName(t.name), scale),
                            scaledPath,
                            scaledW,
                            scaledH);
                    }
                    else
                    {
                        Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                    }
                    ok = ok && scaledOk;
                }

                if (!ok) assetsOk = false;
            }
            continue;
        }

        std::unique_ptr<Bitmap> o(ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0)));
        bool saved = false;
        if (o)
        {
            if (ShouldBakeTileTextIntoAssets(liveTileUpdateMode) && !ApplyTileTextOverlay(o.get(), t.name, t.w, t.h))
                assetsOk = false;
            saved = SavePNG(o.get(), outPath.c_str());
            if (saved)
            {
                Log(g_ui.savedAsset.c_str(), outPath.c_str());
                AddGeneratedAssetCacheFile(generatedAssetCacheFiles, ManifestGeneratedAssetCacheName(t.name), outPath, t.w, t.h);
            }
            else
            {
                Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
            }
        }

        for (int scale : scales)
        {
            int scaledW = ScalePixels(t.w, scale);
            int scaledH = ScalePixels(t.h, scale);
            std::unique_ptr<Bitmap> scaled(ResizeBitmapToMode(renderSource, scaledW, scaledH, renderMode, Color(255, 0, 0, 0)));
            std::wstring scaledPath = ScaleAssetPath(outPath, scale);
            if (scaled)
            {
                if (ShouldBakeTileTextIntoAssets(liveTileUpdateMode) && !ApplyTileTextOverlay(scaled.get(), t.name, t.w, t.h))
                    assetsOk = false;
                bool scaledSaved = SavePNG(scaled.get(), scaledPath.c_str());
                if (scaledSaved)
                {
                    Log(g_ui.savedScaledAsset.c_str(), scaledPath.c_str(), scale);
                    AddGeneratedAssetCacheFile(
                        generatedAssetCacheFiles,
                        ScaleAssetPath(ManifestGeneratedAssetCacheName(t.name), scale),
                        scaledPath,
                        scaledW,
                        scaledH);
                }
                else
                {
                    Log(g_ui.failedSaveScaledAsset.c_str(), scaledPath.c_str(), scale);
                }
                saved = scaledSaved && saved;
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

        if (liveTileUpdateMode)
        {
            std::wstring liveTileToken = NewLiveTileAssetToken();
            for (const auto& t : g_liveTileAssets)
            {
                std::wstring updateAssetPath = VersionedLiveTileAssetPath(t, liveTileToken);
                std::wstring outPath = ResolveManifestAssetPath(exeDir, updateAssetPath);
                DWORD assetDirError = ERROR_SUCCESS;
                if (!EnsureParentDirectoryForFile(outPath, &assetDirError))
                {
                    assetsOk = false;
                    LogWin32Failure(g_ui.createAssetsDirectoryOperation.c_str(), assetDirError);
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                    continue;
                }
                logAssetDirIfNeeded(ParentDirectoryOfPath(outPath));

                std::unique_ptr<Bitmap> o(ResizeBitmapToMode(renderSource, t.w, t.h, renderMode, Color(255, 0, 0, 0)));
                bool saved = false;
                if (o)
                {
                    if (ShouldBakeTileTextIntoAssets(liveTileUpdateMode) && !ApplyTileTextOverlay(o.get(), t.name, t.w, t.h))
                        assetsOk = false;
                    saved = SavePNG(o.get(), outPath.c_str());
                    if (saved)
                        Log(g_ui.savedAsset.c_str(), outPath.c_str());
                    else
                        Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                }
                else
                {
                    Log(g_ui.failedSaveAsset.c_str(), outPath.c_str());
                }

                if (!saved)
                    assetsOk = false;
                else
                {
                    liveTileUpdateAssets.push_back({ t.name, t.file, t.binding, updateAssetPath, t.w, t.h });
                    AddGeneratedAssetCacheFile(generatedAssetCacheFiles, LiveGeneratedAssetCacheName(t.name), outPath, t.w, t.h);
                }
            }
        }
    }

    auto assetsDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(), g_ui.timingAssetGeneration.c_str(), ElapsedMs(generationStart, assetsDone));

    if (!assetsOk)
    {
        auto generationDone = steady_clock::now();
        LogText(useLiveTileUpdateForThisRun
            ? g_ui.skippingLiveTileUpdateDueToAssetFailure
            : g_ui.skippingRegistrationDueToAssetFailure);
        Log(g_ui.timingLog.c_str(),
            useLiveTileUpdateForThisRun ? g_ui.timingLiveTileUpdate.c_str() : g_ui.timingAppRegistration.c_str(),
            0);
        Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, generationDone));
        LogText(g_ui.assetGenerationFailed);
        if (g_notifyOnFailure)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
        return false;
    }

    LogText(liveTileModeReregistrationPending
        ? g_ui.reRegisteringManifestForLiveTileModeChange
        : (useLiveTileUpdateForThisRun ? g_ui.updatingLiveTile : g_ui.reRegisteringManifest));

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

    bool appUpdateOk = false;
    std::wstring appUpdateFailureMessage;
    bool appUpdateFailureIsLiveTile = false;

    if (!manifestReady)
    {
        appUpdateOk = false;
        appUpdateFailureMessage = FormatWide(g_ui.appxManifestCreateFailed.c_str(), manifestPath.c_str());
    }
    else if (liveTileModeReregistrationPending)
    {
        appUpdateOk = RegisterAppxManifest(manifestPath, appUpdateFailureMessage);
        if (appUpdateOk)
        {
            SetLiveTileModeReregistrationPending(false);
            StoreLiveTileModeReregistrationState();
            LogText(g_ui.liveTileModeReregistrationComplete);

            if (liveTileUpdateMode)
            {
                bool deferLiveTileUpdateUntilPackagedRelaunch =
                    g_relaunchAfterExit.load() == 1 &&
                    !CurrentProcessHasPackageIdentity() &&
                    ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10;

                if (deferLiveTileUpdateUntilPackagedRelaunch)
                {
                    LogText(L"[i] Live Tile update deferred until the packaged relaunch starts with package identity.");
                }
                else
                {
                    appUpdateOk = Appx_Update_Or_Request_LiveTile(exeDir, manifestInfo, liveTileUpdateAssets, appUpdateFailureMessage);
                    if (!appUpdateOk)
                    {
                        appUpdateFailureIsLiveTile = true;
                        if (appUpdateFailureMessage.empty())
                            appUpdateFailureMessage = g_ui.liveTileUpdateFailed;
                    }
                }
            }
        }
    }
    else if (useLiveTileUpdateForThisRun)
    {
        appUpdateOk = Appx_Update_Or_Request_LiveTile(exeDir, manifestInfo, liveTileUpdateAssets, appUpdateFailureMessage);
        appUpdateFailureIsLiveTile = !appUpdateOk;
        if (!appUpdateOk && appUpdateFailureMessage.empty())
            appUpdateFailureMessage = g_ui.liveTileUpdateFailed;
    }
    else
    {
        appUpdateOk = RegisterAppxManifest(manifestPath, appUpdateFailureMessage);
        if (appUpdateOk)
            StoreLiveTileModeReregistrationState();
    }

    bool ok = assetsOk && appUpdateOk;
    auto registrationDone = steady_clock::now();
    Log(g_ui.timingLog.c_str(),
        useLiveTileUpdateForThisRun ? g_ui.timingLiveTileUpdate.c_str() : g_ui.timingAppRegistration.c_str(),
        ElapsedMs(assetsDone, registrationDone));
    Log(g_ui.timingLog.c_str(), g_ui.timingGenerationTotal.c_str(), ElapsedMs(generationStart, registrationDone));
    std::wstring missingManifestAssets;
    if (!appUpdateOk && !appUpdateFailureIsLiveTile)
    {
        missingManifestAssets = FindMissingManifestAssets(exeDir);
        if (!missingManifestAssets.empty())
            Log(g_ui.missingManifestAssets.c_str(), missingManifestAssets.c_str());
    }

    if (ok)
    {
        if (liveTileUpdateMode)
        {
            CopyLiveTileAssetsToStablePaths(exeDir, liveTileUpdateAssets);
            DeleteOldVersionedLiveTileAssets(exeDir, liveTileUpdateAssets);
        }
        if (!restoredFromGeneratedAssetCache)
            SaveGeneratedAssetCache(exeDir, BuildGenerationStateKey(wp), generatedAssetCacheFiles);
        StoreSuccessfulGenerationState(wp);
        SetLiveTileClearedOnShutdown(false);
        LogText(liveTileUpdateMode ? g_ui.assetGenerationLiveTileFinished : g_ui.assetGenerationFinished);
        if (g_notifyOnSuccess)
            NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationSuccess, NIIF_INFO);
    }
    else
    {
        if (!assetsOk)
            LogText(g_ui.assetGenerationFailed);
        if (!appUpdateOk)
            LogText(appUpdateFailureIsLiveTile ? g_ui.liveTileUpdateFailed : g_ui.appRegistrationFailed);
        if (g_notifyOnFailure)
        {
            std::wstring failMsg = appUpdateFailureMessage.empty()
                ? g_ui.notificationGenerationFailed
                : appUpdateFailureMessage;
            if (!missingManifestAssets.empty() &&
                (appUpdateFailureMessage.empty() ||
                    failMsg == g_ui.notificationGenerationFailed ||
                    failMsg == g_ui.appRegistrationFailed ||
                    failMsg == g_ui.powerShellRegistrationFailed ||
                    failMsg == g_ui.liveTileUpdateFailed))
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

static std::wstring ForceShutdownSkippedCleanupSummary()
{
    return L"generation/registration worker completion, asset pre-cache worker completion, poll-thread stop/join, normal GDI+ shutdown, and normal single-instance mutex release";
}

static std::wstring ForceShutdownCleanupDownsideSummary()
{
    return L"in-progress tile PNGs, AppxManifest.xml, or AppX registration may be incomplete; a temporary .tmp PNG can remain next to an asset; Windows will reclaim process handles/GDI+ memory, but normal cleanup logs may be missing";
}

static void RecordForceShutdownPendingCleanup()
{
    std::wstring reason = GenerationWorkPending() ? CurrentGenerationReason() : g_ui.unspecifiedReason;
    std::wstring skipped = ForceShutdownSkippedCleanupSummary();
    std::wstring downside = ForceShutdownCleanupDownsideSummary();

    IniWrite(L"State", L"ForceShutdownPendingCleanup", L"1");
    IniWrite(L"State", L"ForceShutdownReason", reason.c_str());
    IniWrite(L"State", L"ForceShutdownSkippedCleanup", skipped.c_str());
    IniWrite(L"State", L"ForceShutdownDownside", downside.c_str());
}

static bool ConsumePreviousForceShutdownCleanupWarning(std::wstring& warning)
{
    warning.clear();
    if (IniReadI(L"State", L"ForceShutdownPendingCleanup", 0) == 0)
        return false;

    std::wstring reason = IniReadS(L"State", L"ForceShutdownReason", L"");
    if (TrimCopy(reason).empty())
        reason = g_ui.unspecifiedReason;

    std::wstring skipped = IniReadS(L"State", L"ForceShutdownSkippedCleanup", L"");
    if (TrimCopy(skipped).empty())
        skipped = ForceShutdownSkippedCleanupSummary();

    std::wstring downside = IniReadS(L"State", L"ForceShutdownDownside", L"");
    if (TrimCopy(downside).empty())
        downside = ForceShutdownCleanupDownsideSummary();

    warning = FormatWide(g_ui.previousForceShutdownCleanupWarning.c_str(), reason.c_str(), skipped.c_str(), downside.c_str());

    IniWrite(L"State", L"ForceShutdownPendingCleanup", L"0");
    IniWrite(L"State", L"ForceShutdownReason", L"");
    IniWrite(L"State", L"ForceShutdownSkippedCleanup", L"");
    IniWrite(L"State", L"ForceShutdownDownside", L"");
    return true;
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
    RecordForceShutdownPendingCleanup();
    TrayRemove();
    ExitProcess(0);
}

static void GenerationWorkerThread()
{
    for (;;)
    {
        std::wstring wallpaper;
        std::wstring reason;
        unsigned long long requestId = 0;

        {
            std::unique_lock<std::mutex> lk(g_generationQueueMutex);
            g_generationQueueCv.wait(lk, [] { return g_generationWorkerStop || g_generationQueued; });

            if (!g_generationQueued && g_generationWorkerStop)
                break;

            wallpaper = std::move(g_generationQueuedWallpaper);
            reason = std::move(g_generationQueuedReason);
            requestId = g_generationQueuedId;
            MarkGenerationActive(true, reason);
            g_generationQueued = false;
            g_generationQueuedId = 0;
        }

        bool ok = false;
        try
        {
            ok = Generate(wallpaper.c_str(), reason.c_str());
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
        RecordGenerationCompletion(requestId, ok);
        MarkGenerationActive(false);
        if (ok && !GenerationWorkPending())
            QueueGeneratedAssetPrecache(wallpaper);
        if (!GenerationWorkPending())
            TrimIdleWorkingSetIfConfigured();
        MaybeCompleteRequestedShutdown();
    }
}

static unsigned long long QueueGenerate(std::wstring wallpaper, std::wstring reason)
{
    if (g_shutdownRequested)
        return 0;

    bool startWorker = false;
    unsigned long long requestId = g_generationNextRequestId.fetch_add(1, std::memory_order_relaxed);
    if (requestId == 0)
        requestId = g_generationNextRequestId.fetch_add(1, std::memory_order_relaxed);
    unsigned long long droppedRequestId = 0;
    {
        std::lock_guard<std::mutex> lk(g_generationQueueMutex);
        if (g_generationWorkerStop || g_shutdownRequested)
            return 0;

        if (g_generationQueued && g_generationQueuedId != 0)
            droppedRequestId = g_generationQueuedId;
        g_generationQueuedWallpaper = std::move(wallpaper);
        g_generationQueuedReason = std::move(reason);
        g_generationQueuedId = requestId;
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
                g_generationQueuedId = 0;
                g_generationQueuedWallpaper.clear();
                g_generationQueuedReason.clear();
            }
            RecordGenerationCompletion(requestId, false);
            Log(g_ui.generationThreadStartFailed.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
            MaybeCompleteRequestedShutdown();
            return 0;
        }
        catch (...)
        {
            std::wstring message = UnknownExceptionText();
            {
                std::lock_guard<std::mutex> lk(g_generationQueueMutex);
                g_generationWorkerStarted = false;
                g_generationQueued = false;
                g_generationQueuedId = 0;
                g_generationQueuedWallpaper.clear();
                g_generationQueuedReason.clear();
            }
            RecordGenerationCompletion(requestId, false);
            Log(g_ui.generationThreadStartFailed.c_str(), message.c_str());
            if (g_notifyOnFailure)
                NotifyTrayBalloon(g_ui.notificationTitle, g_ui.notificationGenerationFailed, NIIF_ERROR);
            MaybeCompleteRequestedShutdown();
            return 0;
        }
    }

    if (droppedRequestId != 0)
        RecordGenerationCompletion(droppedRequestId, false);
    g_generationQueueCv.notify_one();
    return requestId;
}

static bool QueueLiveTileModeReregistration()
{
    SetLiveTileModeReregistrationPending(true);

    std::wstring wp = GetWallpaper();
    if (wp.empty())
    {
        LogText(g_ui.liveTileModeReregistrationQueuedNoWallpaper);
        return false;
    }

    LogText(g_ui.liveTileModeReregistrationQueued);
    return QueueGenerate(wp, g_ui.liveTileModeChanged) != 0;
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
    bool pendingPollGeneration = false;
    unsigned long long pendingPollGenerationId = 0;
    std::wstring pendingWallpaper;
    FitMode pendingFit = FitMode::Fill;
    int pendingDpiScale = 100;

    while (g_running)
    {
        int poll = IniReadClampedI(L"Settings", L"PollIntervalMs", 2000, 250, 60000);
        int confirm = IniReadClampedI(L"Settings", L"ConfirmMs", 800, 0, 10000);
        int deb = IniReadClampedI(L"Settings", L"DebounceMinMs", 1200, 0, 60000);
        ReloadRuntimeSettingsFromIni();

        if (pendingPollGeneration)
        {
            bool generationSucceeded = false;
            if (TryGetGenerationCompletion(pendingPollGenerationId, generationSucceeded))
            {
                // Treat a completed poll-triggered generation attempt as the new
                // observation baseline even when the AppX/Live Tile side fails.
                // Otherwise a persistent post-generation failure (for example,
                // Live Tile mode selected while this instance is unpackaged) makes
                // the poller see the same wallpaper as "changed" forever and it
                // re-runs generation every debounce interval.
                last = pendingWallpaper;
                lastFit = pendingFit;
                lastDpiScale = pendingDpiScale;
                emptyWallpaperAlreadyReported = false;

                if (!generationSucceeded)
                    LogText(L"[i] Poll baseline updated after failed generation attempt to avoid retrying the same wallpaper change endlessly.");

                pendingPollGeneration = false;
                pendingPollGenerationId = 0;
                pendingWallpaper.clear();
            }
        }

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

        if (pendingPollGeneration)
        {
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

                    unsigned long long requestId = QueueGenerate(generationWallpaper, reason);
                    if (requestId != 0)
                    {
                        pendingPollGeneration = true;
                        pendingPollGenerationId = requestId;
                        pendingWallpaper = generationWallpaper;
                        pendingFit = fit2;
                        pendingDpiScale = dpiScale2;
                        emptyWallpaperAlreadyReported = false;
                    }
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


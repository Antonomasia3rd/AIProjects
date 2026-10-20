// Independent static packages contain a tiny transient launcher and rendered
// PNGs only. No resident host copy, Startup entry, automatic pin/unpin, or
// mutation of the main package is performed by this backend.
#include "../tile_registration.h"
#include <shlobj.h>
#ifndef DESKTOPSTUB_CONTENT_MESSAGEBOX
#define DESKTOPSTUB_CONTENT_MESSAGEBOX MessageBoxW
#endif
#ifdef DESKTOPSTUB_INERT_HARDWARE
static std::wstring g_testTileRegistrationUserScope, g_testTileRegistrationBase, g_testTileRegistrationLauncherSource;
static bool (*g_testTileRegistrationHostAumid)(std::wstring&,std::wstring&) = nullptr;
static bool (*g_testTileRegistrationRegister)(const std::wstring&,std::wstring&) = nullptr;
#endif
struct TileRegistrationReadyCacheEntry { ULONGLONG checked = 0; bool ready = false; };
static std::mutex g_tileRegistrationReadyMutex;
static std::wstring g_tileRegistrationReadyProfile;
static unsigned long long g_tileRegistrationReadyRevision = ~0ull;
static std::map<std::wstring,TileRegistrationReadyCacheEntry> g_tileRegistrationReadyCache;
static void InvalidateTileRegistrationReadyCache()
{
    std::lock_guard<std::mutex> lock(g_tileRegistrationReadyMutex); g_tileRegistrationReadyCache.clear();
}
static std::wstring CurrentTileRegistrationUserScope()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testTileRegistrationUserScope;
#else
    static std::mutex mutex; static std::wstring cached;
    std::lock_guard<std::mutex> lock(mutex);
    if (cached.empty()) { std::wstring sid; if (aip::named_objects::CurrentUserIdentity(sid)) aip::named_objects::StableIdentityHash(sid,cached); }
    return cached;
#endif
}
static std::wstring TileRegistrationBaseDirectory()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testTileRegistrationBase;
#else
    const auto local = LocalAppDataPath();
    return local.empty() ? std::wstring{} : local + L"\\DesktopStub\\RegistrationTiles";
#endif
}
static std::wstring TileRegistrationOwnerText(const aip::tile_registration::Target& target)
{
    std::wstring identity;
    if (!aip::named_objects::NormalizeFileIdentity(target.profile,identity)) return {};
    return L"AIP_TILE_REGISTRATION_V1\r\n" + target.userScope + L"\r\n" + target.profileScope + L"\r\n" +
        target.tileScope + L"\r\n" + identity + L"\r\n" + target.tileId + L"\r\n";
}
static bool TileRegistrationReadText(const std::wstring& path,std::wstring& output)
{
    std::vector<BYTE> bytes;
    if (!aip::ReadWholeFileBytes(path,bytes,131072)) return false;
    output = aip::Utf8ToWide(std::string(bytes.begin(),bytes.end())); return !output.empty();
}
static bool TileRegistrationWriteText(const std::wstring& path,const std::wstring& value)
{
    std::string bytes;
    return aip::TryWideToUtf8(value,bytes) && WriteWholeFileBytes(path,std::vector<BYTE>(bytes.begin(),bytes.end()));
}
static bool TileRegistrationTargetPathsValid(const aip::tile_registration::Target& target,std::wstring& error)
{
    const auto base = TileRegistrationBaseDirectory();
    const auto root = base + L"\\" + target.userScope + L"\\" + target.profileScope + L"\\" + target.tileScope;
    std::wstring identity, profileHash, tileHash;
    if (base.empty() || target.userScope != CurrentTileRegistrationUserScope() ||
        !aip::named_objects::NormalizeFileIdentity(target.profile,identity) || !aip::named_objects::StableIdentityHash(identity,profileHash) ||
        !aip::named_objects::StableIdentityHash(target.tileId,tileHash) || profileHash != target.profileScope || tileHash != target.tileScope ||
        !IEquals(aip::MakeAbsolutePath(target.root),aip::MakeAbsolutePath(root)) ||
        target.identityName != aip::tile_registration::IdentityName(target.userScope,target.profileScope,target.tileScope) ||
        !IEquals(target.manifestPath,target.root+L"\\AppxManifest.xml") || !IEquals(target.assetsDirectory,target.root+L"\\Assets") ||
        !IEquals(target.launcherPath,target.root+L"\\"+aip::tile_registration::LauncherFileName)) {
        error = L"The registration tile no longer belongs to this user, profile, or tile identity."; return false;
    }
    for (const auto& directory : {base,base+L"\\"+target.userScope,base+L"\\"+target.userScope+L"\\"+target.profileScope,root,target.assetsDirectory}) {
        const auto attributes = GetFileAttributesW(directory.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (!(attributes & FILE_ATTRIBUTE_DIRECTORY) || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))) {
            error = L"A registration tile cache path is not a plain owned directory."; return false;
        }
    }
    for (const auto* file : {L"registration.owner",L"registration.authorized",L"registration.revision",L"registration.published",L"AppxManifest.xml",aip::tile_registration::LauncherFileName}) {
        const auto attributes = GetFileAttributesW((target.root+L"\\"+file).c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
            error = L"A registration tile file is not an ordinary owned file."; return false;
        }
    }
    error.clear(); return true;
}
static bool BuildTileRegistrationTarget(const std::wstring& id,const std::wstring& name,const aip::content::TileAppearance& appearance,
    aip::tile_registration::Target& output,std::wstring& error)
{
    if (!aip::content::ValidTileInstanceId(id)) { error = L"The registration tile ID is invalid."; return false; }
    aip::tile_registration::Target target;
    { std::lock_guard<std::mutex> lock(g_pathMutex); target.profile = aip::MakeAbsolutePath(g_iniPath); target.hostPath = aip::MakeAbsolutePath(g_exePath); }
    std::wstring identity;
    target.userScope = CurrentTileRegistrationUserScope();
    if (target.userScope.empty() || !aip::named_objects::NormalizeFileIdentity(target.profile,identity) ||
        !aip::named_objects::StableIdentityHash(identity,target.profileScope) || !aip::named_objects::StableIdentityHash(id,target.tileScope)) {
        error = L"Could not determine the current user and profile identity for this registration tile."; return false;
    }
    const auto base = TileRegistrationBaseDirectory();
    if (base.empty()) { error = L"The registration tile cache location is unavailable."; return false; }
    target.tileId = id; target.displayName = name;
    target.identityName = aip::tile_registration::IdentityName(target.userScope,target.profileScope,target.tileScope);
    target.root = base+L"\\"+target.userScope+L"\\"+target.profileScope+L"\\"+target.tileScope;
    target.manifestPath = target.root+L"\\AppxManifest.xml"; target.assetsDirectory = target.root+L"\\Assets";
    target.launcherPath = target.root+L"\\"+aip::tile_registration::LauncherFileName;
    target.hostProduct = SanitizeManifestToken(ProductRuntimeBaseName(),L"DesktopStub");
    target.hostArguments = L"--ini "+aip::QuoteCommandLineArg(target.profile)+L" --tile-setup "+id;
    const auto main = CurrentManifestDisplayInfo(); // Existing on-disk identity wins over future defaults.
    target.publisher = main.publisher; target.publisherDisplayName = main.publisherDisplayName;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if (!g_testTileRegistrationHostAumid || !g_testTileRegistrationHostAumid(target.hostAumid,error)) return false;
#else
    std::wstring family, full;
    // The legacy CoreApplication broker cannot handle normal host arguments.
    // In that mode the launcher uses the verified unpackaged-host fallback.
    if (IEquals(FileNameFromPath(main.executable),FileNameFromPath(target.hostPath)) &&
        FindRegisteredPackageFamilyNameForManifest(main,family,full,error)) target.hostAumid = family+L"!"+main.applicationId;
    error.clear();
#endif
    target.branding = appearance.branding;
    target.backgroundColor = L"transparent";
    if (appearance.background != L"Transparent") {
        std::uint32_t fallback = 0x202020;
        aip::content::TileInstance tile;
        if (FindConfiguredSecondaryTileInstance(id,tile)) if (const auto* topic = aip::tile_visual::FindTopic(aip::content::TileInstanceArtworkTopic(tile))) fallback = topic->rgb;
        wchar_t color[16]{}; swprintf_s(color,L"#%06X",aip::content::TileAppearanceRgb(appearance,fallback)); target.backgroundColor = color;
    }
    std::wstring manifest;
    if (!TileRegistrationTargetPathsValid(target,error) || !aip::tile_registration::BuildManifest(target,manifest,error)) return false;
    output = std::move(target); return true;
}
static bool TileRegistrationOwnedSetup(const aip::tile_registration::Target& target,std::wstring& error)
{
    std::wstring owner;
    if (!TileRegistrationTargetPathsValid(target,error)) return false;
    if (!TileRegistrationReadText(target.root+L"\\registration.owner",owner) || owner != TileRegistrationOwnerText(target)) {
        error = L"Set up this registration tile explicitly before background updates can publish it."; return false;
    }
    error.clear(); return true;
}
static bool PrepareTileRegistrationPackage(const aip::tile_registration::Target& target,bool explicitSetup,std::wstring& error)
{
    if (!TileRegistrationTargetPathsValid(target,error)) return false;
    std::wstring owner;
    const auto ownerPath = target.root+L"\\registration.owner";
    if (TileRegistrationReadText(ownerPath,owner)) {
        if (owner != TileRegistrationOwnerText(target)) { error = L"This registration cache belongs to another identity. No files were changed."; return false; }
    } else {
        if (!explicitSetup) { error = L"Use this tile's registration setup action before enabling background registration updates."; return false; }
        const auto attributes = GetFileAttributesW(target.root.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES) {
            WIN32_FIND_DATAW data{}; auto find = FindFirstFileW((target.root+L"\\*").c_str(),&data); bool nonempty = false;
            if (find != INVALID_HANDLE_VALUE) { do { if (wcscmp(data.cFileName,L".") && wcscmp(data.cFileName,L"..")) { nonempty = true; break; } } while (FindNextFileW(find,&data)); FindClose(find); }
            if (nonempty) { error = L"An unowned registration cache already contains files. Existing files were kept."; return false; }
        }
        if (!EnsureDirectoryTreeExists(target.root) || !TileRegistrationTargetPathsValid(target,error) || !TileRegistrationWriteText(ownerPath,TileRegistrationOwnerText(target))) {
            error = L"Could not create this user's registration tile owner record."; return false;
        }
    }
    if (!EnsureDirectoryTreeExists(target.assetsDirectory)) { error = L"Could not prepare the registration tile image directory."; return false; }
    auto source = ParentDirectoryOfPath(target.hostPath)+L"\\"+aip::tile_registration::LauncherFileName;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if (!g_testTileRegistrationLauncherSource.empty()) source = g_testTileRegistrationLauncherSource;
#endif
    std::vector<BYTE> expected, existing;
    if (!aip::ReadWholeFileBytes(source,expected,1048576) || expected.empty()) { error = L"The small DesktopStub tile launcher is missing. Rebuild or reinstall DesktopStub before setting up Registration delivery."; return false; }
    if ((!aip::ReadWholeFileBytes(target.launcherPath,existing,1048576) || existing != expected) && !WriteWholeFileBytes(target.launcherPath,expected)) {
        error = L"Could not stage the small registration tile launcher."; return false;
    }
    if (explicitSetup && !TileRegistrationWriteText(target.root+L"\\registration.authorized",TileRegistrationOwnerText(target))) {
        error = L"Could not save this registration tile's explicit setup authorization."; return false;
    }
    if (explicitSetup) InvalidateTileRegistrationReadyCache();
    error.clear(); return true;
}
static bool WriteTileRegistrationManifest(aip::tile_registration::Target& target,std::wstring& error)
{
    if (!TileRegistrationOwnedSetup(target,error)) return false;
    aip::IniWriteMutexGuard guard(target.manifestPath,CurrentIniWriteLockWaitMs());
    if (!guard.IsLocked()) { error = L"This registration tile is already being updated."; return false; }
    const auto revisionPath = target.root+L"\\registration.revision";
    std::wstring previous; std::uint64_t revision = 0;
    if (GetFileAttributesW(revisionPath.c_str()) != INVALID_FILE_ATTRIBUTES &&
        (!TileRegistrationReadText(revisionPath,previous) || !aip::gallery::detail::UInt(previous,0xffffffffu,revision))) {
        error = L"The registration tile version record is invalid; it was kept for recovery."; return false;
    }
    if (revision == 0xffffffffu) { error = L"This registration tile exhausted its update version counter."; return false; }
    target.packageVersion = aip::tile_registration::RevisionVersion(static_cast<std::uint32_t>(++revision));
    std::wstring manifest;
    if (!aip::tile_registration::BuildManifest(target,manifest,error) || !TileRegistrationWriteText(revisionPath,std::to_wstring(revision)) ||
        !TileRegistrationWriteText(target.manifestPath,manifest)) { error = L"Could not write this registration tile's manifest or version record."; return false; }
    error.clear(); return true;
}
static bool RegisterTileRegistrationPackage(const aip::tile_registration::Target& target,bool explicitSetup,std::wstring& error)
{
    if (!TileRegistrationOwnedSetup(target,error)) return false;
    const auto receipt = target.root+L"\\registration.published";
    if (!explicitSetup && GetFileAttributesW(receipt.c_str()) == INVALID_FILE_ATTRIBUTES) {
        std::wstring authorization;
        if (!TileRegistrationReadText(target.root+L"\\registration.authorized",authorization) || authorization != TileRegistrationOwnerText(target)) {
            error = L"Finish this tile's explicit Registration setup before background registration updates are allowed."; return false;
        }
    }
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if (!g_testTileRegistrationRegister) { error = L"Native package registration is disabled in inert validation."; return false; }
    if (!g_testTileRegistrationRegister(target.manifestPath,error)) return false;
#else
    if (!RegisterAppxManifest(target.manifestPath,error)) return false;
#endif
    if (!TileRegistrationWriteText(receipt,target.packageVersion)) { error = L"Windows registered the tile, but its local update receipt could not be saved."; return false; }
    error.clear(); return true;
}
static bool TileRegistrationReady(const aip::tile_registration::Target& target,std::wstring& error)
{
    std::wstring authorization, published;
    if (!TileRegistrationOwnedSetup(target,error)) return false;
    if ((TileRegistrationReadText(target.root+L"\\registration.authorized",authorization) && authorization == TileRegistrationOwnerText(target)) ||
        (TileRegistrationReadText(target.root+L"\\registration.published",published) && aip::tile_registration::Version(published))) { error.clear(); return true; }
    error = L"Finish the explicit Registration setup before background updates are allowed."; return false;
}
static bool BackgroundTileRegistrationReady(const aip::tile_registration::Target& target)
{
    std::wstring error; return TileRegistrationReady(target,error);
}
static bool TileRegistrationReadyForId(const std::wstring& id)
{
    aip::tile_registration::Target target; std::wstring error;
    // Demand checks must not enumerate packages or parse every instance. The
    // owner marker depends only on user/profile/tile, not display or main AUMID.
    target.userScope = CurrentTileRegistrationUserScope(); target.tileId = id;
    { std::lock_guard<std::mutex> lock(g_pathMutex); target.profile = aip::MakeAbsolutePath(g_iniPath); }
    unsigned long long revision = 0; { std::lock_guard<std::mutex> lock(g_iniCacheMutex); revision = g_iniCacheRevision; }
    const auto cacheProfile = target.userScope+L":"+target.profile;
    const auto now = GetTickCount64();
    {
        std::lock_guard<std::mutex> lock(g_tileRegistrationReadyMutex);
        if (g_tileRegistrationReadyProfile != cacheProfile || g_tileRegistrationReadyRevision != revision) {
            g_tileRegistrationReadyProfile = cacheProfile; g_tileRegistrationReadyRevision = revision; g_tileRegistrationReadyCache.clear();
        }
        const auto cached = g_tileRegistrationReadyCache.find(id);
        if (cached != g_tileRegistrationReadyCache.end() && now-cached->second.checked < 10000) return cached->second.ready;
    }
    std::wstring identity;
    if (!aip::content::ValidTileInstanceId(id) || target.userScope.empty() ||
        !aip::named_objects::NormalizeFileIdentity(target.profile,identity) || !aip::named_objects::StableIdentityHash(identity,target.profileScope) ||
        !aip::named_objects::StableIdentityHash(id,target.tileScope)) return false;
    const auto base = TileRegistrationBaseDirectory(); if (base.empty()) return false;
    target.root = base+L"\\"+target.userScope+L"\\"+target.profileScope+L"\\"+target.tileScope;
    target.identityName = aip::tile_registration::IdentityName(target.userScope,target.profileScope,target.tileScope);
    target.manifestPath = target.root+L"\\AppxManifest.xml"; target.assetsDirectory = target.root+L"\\Assets";
    target.launcherPath = target.root+L"\\"+aip::tile_registration::LauncherFileName;
    const bool ready = BackgroundTileRegistrationReady(target);
    { std::lock_guard<std::mutex> lock(g_tileRegistrationReadyMutex);
        if (g_tileRegistrationReadyProfile == cacheProfile && g_tileRegistrationReadyRevision == revision)
            g_tileRegistrationReadyCache[id] = {now,ready}; }
    return ready;
}
static bool BackgroundTileRegistrationReady(const std::wstring& id) { return TileRegistrationReadyForId(id); }
static std::wstring TileRegistrationSetupStatus(const std::wstring& id)
{
    return BackgroundTileRegistrationReady(id) ?
        L"Registration setup is ready. Pin the separate app from Windows Start after its first successful update." :
        L"Registration setup is required. Select Registration in this tile's delivery settings to prepare its separate app.";
}
static bool SaveTileInstanceSettings(HWND,const std::vector<aip::IniSetting>&,const std::wstring&,const std::wstring&);
static bool SecondaryTilePinUiAvailable();
static bool OfferTileDeliveryMigration(HWND owner,const std::wstring& tileId,const std::wstring& requestedMode)
{
    auto mode = requestedMode; std::wstring error;
    if (!aip::content::NormalizeTilePolicySetting(L"Delivery.Mode",mode,error)) return false;
    aip::content::TileInstanceConfiguration cfg;
    if (!ReadCurrentTileInstances(cfg,error)) return false;
    const auto found = std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile) { return tile.id == tileId; });
    if (found == cfg.tiles.end()) return false;
    const auto tile = *found;
    const bool registration = mode == L"Registration" || (mode == L"Auto" && !CurrentProcessHasPackageIdentity());
    if (mode == tile.policy.Mode() && !registration) return true;
    if (registration && tile.policy.Read(L"Settings",L"AppxManifestTarget",L"Windows10") != L"Windows10") {
        const auto message = IniReadS(L"Strings",L"TileRegistrationRequiresWindows10",L"Separate Registration tiles require the Windows 10 manifest target. Select Windows 10 in this tile's delivery settings, then choose Registration again. No files or pins were changed.");
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),tile.name.c_str(),MB_OK|MB_ICONINFORMATION); return false;
    }
    const auto prompt = registration ?
        IniReadS(L"Strings",L"TileRegistrationMigrationPrompt",L"Registration uses a separate app identity and rendered tile images. Your existing Live Tile pin will be kept. After the first successful update, pin the new app from Start's All apps list; Windows cannot convert the existing pin in place. Prepare this separate Registration tile now?") :
        IniReadS(L"Strings",L"TileLiveMigrationPrompt",L"Live Tile delivery uses DesktopStub's native tile identity. Your separate Registration app and its pin will be kept. Windows may ask you to pin the Live Tile separately. Change this tile's delivery method now?");
    if (DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,prompt.c_str(),tile.name.c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONINFORMATION) != IDYES) return false;
    aip::tile_registration::Target target;
    if (registration && (!BuildTileRegistrationTarget(tile.id,tile.name,tile.appearance,target,error) ||
        !PrepareTileRegistrationPackage(target,true,error))) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Registration tile setup",MB_OK|MB_ICONWARNING); return false;
    }
    if (!SaveTileInstanceSettings(owner,{{aip::content::TileInstanceSection(tile.id),L"Delivery.Mode",mode}},tile.id,L"")) return false;
    ForgetSecondaryTileDelivery(aip::content::TileInstanceNativeId(tile));
    if (g_hwnd) PostMessageW(g_hwnd,WM_APP_CONTENT_FRAME_READY,0,0);
    if (!registration && !tile.primary && SecondaryTilePinUiAvailable() && !SecondaryTileExists(aip::content::TileInstanceNativeId(tile)))
        RequestPinSecondaryTile(owner,aip::content::TileInstanceNativeId(tile),tile.name);
    return true;
}
static bool ClearTileRegistrationImages(aip::tile_registration::Target& target,std::wstring& error)
{
    if (!TileRegistrationReady(target,error)) return false;
    std::wstring published;
    if (!TileRegistrationReadText(target.root+L"\\registration.published",published) || !aip::tile_registration::Version(published)) {
        error = L"This registration tile has not completed its first publication."; return false;
    }
    ScopedGdiPlus gdi;
    if (!gdi.ok()) { error = L"Could not initialize the registration tile image cleanup."; return false; }
    WIN32_FIND_DATAW data{}; auto find = FindFirstFileW((target.assetsDirectory+L"\\*.png").c_str(),&data);
    if (find == INVALID_HANDLE_VALUE) { error = L"No owned registration tile images are available to clear."; return false; }
    bool ok = true; unsigned cleared = 0;
    do {
        if (data.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)) continue;
        const std::wstring name = data.cFileName;
        bool generated = false;
        for (const auto& tile : g_tiles) {
            const auto base = FileNameFromPath(tile.file), stem = base.substr(0,base.size()-4);
            if (IEquals(name,base) || (aip::content::Lower(name).compare(0,stem.size()+7,aip::content::Lower(stem)+L".scale-")==0 &&
                name.size()>stem.size()+11 && IEquals(name.substr(name.size()-4),L".png") &&
                name.substr(stem.size()+7,name.size()-stem.size()-11).find_first_not_of(L"0123456789")==std::wstring::npos)) { generated = true; break; }
        }
        if (!generated) continue;
        const auto path = target.assetsDirectory+L"\\"+name;
        UINT width = 0,height = 0;
        { Gdiplus::Bitmap previous(path.c_str()); if (previous.GetLastStatus()==Gdiplus::Ok) {width=previous.GetWidth();height=previous.GetHeight();} }
        if (!width || !height || width>4096 || height>4096) { ok = false; break; }
        Gdiplus::Bitmap blank(width,height,PixelFormat32bppARGB);
        { Gdiplus::Graphics graphics(&blank); if (graphics.Clear(Gdiplus::Color(0,0,0,0))!=Gdiplus::Ok) {ok=false;break;} }
        if (!SavePNG(&blank,path.c_str())) {ok=false;break;} ++cleared;
    } while (FindNextFileW(find,&data));
    FindClose(find);
    if (!ok || !cleared) { error = L"Could not clear every owned registration tile image. Pin and ownership records were kept."; return false; }
    return WriteTileRegistrationManifest(target,error) && RegisterTileRegistrationPackage(target,false,error);
}
static void ClearOwnedRegistrationTilesOnShutdown()
{
    if (g_relaunchAfterExit.load()!=0) return;
    std::lock_guard<std::mutex> generationLock(g_generationMutex);
    const auto clear = [&](const std::wstring& id,const std::wstring& name,const aip::content::TileAppearance& appearance,const aip::content::TileDeliveryPolicy& policy) {
        if (!policy.included || policy.Read(L"Settings",L"ClearLiveTileOnShutdown",L"1")!=L"1") return;
        aip::content::TilePolicyReadScope scope(policy);
        if (UseExperimentalLiveTileUpdate() || !TileRegistrationReadyForId(id)) return;
        aip::tile_registration::Target target; std::wstring error;
        if (!BuildTileRegistrationTarget(id,name,appearance,target,error) || !ClearTileRegistrationImages(target,error))
            Log(L"[!] Could not clear owned Registration tile %ls on exit: %ls",id.c_str(),error.c_str());
    };
    aip::content::TileInstanceConfiguration instances; std::wstring error;
    if (ReadCurrentTileInstances(instances,error)) for (const auto& tile : instances.tiles) clear(tile.id,tile.name,tile.appearance,tile.policy);
    aip::gallery::Configuration gallery;
    if (ReadGalleryConfiguration(gallery,error)) for (const auto& tile : gallery.tiles) clear(tile.delivery.owner,tile.name,tile.appearance,tile.delivery);
}

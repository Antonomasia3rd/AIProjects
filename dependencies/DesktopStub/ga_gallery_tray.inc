// Gallery setup/configuration is separate from image inventory and notification
// delivery. Building this menu never scans photographs or initializes history.
static bool OpenGalleryFolder(const std::wstring& directory);
static bool GalleryDirectoryExists(const std::wstring& directory);
static bool OpenGalleryTilePolicySetup(HWND owner,const std::wstring& nativeId);
static bool GalleryTileUsesRegistration(const aip::gallery::TileConfiguration& tile);
static void PinGalleryGroup(HWND owner,const aip::gallery::Configuration& cfg,const std::vector<std::size_t>& indices);
static constexpr UINT ID_GALLERY_SETUP = 30000, ID_GALLERY_ENABLED = 30001,
    ID_GALLERY_FOLDER = 30002, ID_GALLERY_OPEN_FOLDER = 30003, ID_GALLERY_REFRESH = 30004,
    ID_GALLERY_CADENCE = 30005, ID_GALLERY_SEED = 30006, ID_GALLERY_COUNT = 30010, ID_GALLERY_QUEUE = 30020,
    ID_GALLERY_ORDER = 30030, ID_GALLERY_FIT = 30040, ID_GALLERY_PIN_ALL = 30042, ID_GALLERY_ADD = 30043,
    ID_GALLERY_REBALANCE = 30044, ID_GALLERY_RECOVER = 30045, ID_GALLERY_TILE = 30100, ID_GALLERY_GROUP = 52000;
static constexpr UINT GALLERY_TILE_COMMAND_STRIDE = 20;

static std::wstring GalleryProfilePath()
{
    std::lock_guard<std::mutex> lock(g_pathMutex); return aip::MakeAbsolutePath(g_iniPath);
}
static void ResolveGalleryConfigurationFolder(aip::gallery::Configuration& configuration, const std::wstring& profile)
{
    if (!configuration.folder.empty()) {
        if (PathIsRelativeW(configuration.folder.c_str())) configuration.folder = ParentDirectoryOfPath(profile) + L"\\" + configuration.folder;
        configuration.folder = aip::MakeAbsolutePath(configuration.folder);
    }
    for (auto& tile : configuration.tiles) if (!tile.folder.empty()) {
        if (PathIsRelativeW(tile.folder.c_str())) tile.folder = ParentDirectoryOfPath(profile) + L"\\" + tile.folder;
        tile.folder = aip::MakeAbsolutePath(tile.folder);
    }
}
static bool ReadGalleryMenuText(const std::wstring& text, aip::gallery::Configuration& configuration, std::wstring& error)
{
    const auto document = aip::ParseIniDocument(text);
    return aip::gallery::ReadConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value;
        return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    }, configuration, error);
}
static bool SameGalleryConfiguration(const aip::gallery::Configuration& a, const aip::gallery::Configuration& b)
{
    if (a.enabled != b.enabled || a.groupId != b.groupId || a.folder != b.folder || a.queueSize != b.queueSize ||
        a.refreshSeconds != b.refreshSeconds || a.order != b.order || a.fit != b.fit || a.seed != b.seed || a.tiles.size() != b.tiles.size()) return false;
    for (std::size_t i = 0; i < a.tiles.size(); ++i)
        if (a.tiles[i].id != b.tiles[i].id || a.tiles[i].name != b.tiles[i].name || a.tiles[i].enabled != b.tiles[i].enabled ||
            a.tiles[i].folder != b.tiles[i].folder || a.tiles[i].galleryName != b.tiles[i].galleryName ||
            a.tiles[i].appearance.background != b.tiles[i].appearance.background || a.tiles[i].appearance.branding != b.tiles[i].appearance.branding ||
            a.tiles[i].appearance.backgroundColor != b.tiles[i].appearance.backgroundColor||
            aip::content::TileDeliveryPolicyKey(a.tiles[i].delivery)!=aip::content::TileDeliveryPolicyKey(b.tiles[i].delivery)) return false;
    return true;
}
struct GalleryEditContext { std::wstring profile; aip::gallery::Configuration before; };
static GalleryEditContext GalleryEdit(const aip::gallery::Configuration& cfg)
{
    GalleryEditContext edit{GalleryProfilePath(),cfg};
    // A newly constructed draft has not captured creation defaults yet. Use
    // exactly the persistence serializer so its optimistic concurrency baseline
    // matches the subsequently parsed document, including every local policy.
    if(std::any_of(cfg.tiles.begin(),cfg.tiles.end(),[](const auto& tile){return !tile.delivery.included;})) {
        std::vector<aip::IniSetting> fields;std::wstring error;
        if(PrepareGalleryConfigurationSettings(cfg,fields,error)) {
            std::map<std::pair<std::wstring,std::wstring>,std::wstring> canonical;
            for(const auto& field:fields)canonical[{aip::content::TilePolicyLower(field.section),
                aip::content::TilePolicyLower(field.key)}]=field.value;
            aip::gallery::Configuration captured;
            if(aip::gallery::ReadConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
                const auto found=canonical.find({aip::content::TilePolicyLower(section),aip::content::TilePolicyLower(key)});
                return found==canonical.end()?std::wstring(fallback):found->second;
            },captured,error))edit.before=std::move(captured);
        }
    }
    ResolveGalleryConfigurationFolder(edit.before,edit.profile);
    return edit;
}
static bool MatchGalleryEdit(const std::wstring& text, const GalleryEditContext& edit, std::wstring& error)
{
    aip::gallery::Configuration current;
    if (GalleryProfilePath() != edit.profile) { error = L"The selected profile changed while the gallery dialog was open. Open the dialog again; no settings were overwritten."; return false; }
    if (!ReadGalleryMenuText(text, current, error)) return false;
    ResolveGalleryConfigurationFolder(current, edit.profile);
    if (!SameGalleryConfiguration(current, edit.before)) {
        error = L"Gallery settings changed while the dialog was open. Open it again; the newer settings and ownership were kept."; return false;
    }
    return true;
}
static bool GalleryConfigurationRecoveryBytes(const aip::gallery::Configuration& cfg, std::string& bytes, std::wstring& error)
{
    std::vector<aip::IniSetting> fields; std::wstring text;
    if (!PrepareGalleryConfigurationSettings(cfg, fields, error)) return false;
    for (const auto& field : fields) if (!WriteContentProfileSetting(text, field)) { error = L"Gallery settings cannot be encoded for recovery."; return false; }
    if (!aip::TryWideToUtf8(text, bytes) || bytes.size() > aip::gallery::transaction::MaxConfigurationBytes) {
        error = L"Gallery settings exceed the recovery record limit."; return false;
    }
    return true;
}
static bool StageGalleryMenuSettings(std::wstring& text, const std::vector<aip::IniSetting>& settings, std::wstring& error)
{
    auto candidate = text;
    for (auto setting : settings) {
        if (!NormalizeDesktopStubTypedSetting(setting.section, setting.key, setting.value, error) || !WriteContentProfileSetting(candidate, setting)) {
            if (error.empty()) error = L"A gallery setting cannot be saved in the single-line INI format.";
            return false;
        }
    }
    aip::gallery::Configuration checked;
    if (!ReadGalleryMenuText(candidate, checked, error) || !ValidateDesktopStubProfileDocument(candidate, error)) return false;
    text = std::move(candidate); error.clear(); return true;
}
// Activate galleries without implicitly opting into all six default sources.
// Missing source flags are disabled; explicit flags, existing content rows and
// account configuration are retained. Only enabled mappings require Count to
// grow and receive missing entry defaults.
static bool StageGalleryInitialLayout(std::wstring& text, std::wstring& error)
{
    auto candidate = text;
    auto document = aip::ParseIniDocument(candidate);
    if(aip::IniHasKeyInDoc(document,L"Tiles",L"Instances")){
        if(!ValidateDesktopStubProfileDocument(candidate,error))return false;
        error.clear();return true; // Pin demand replaces the retired numbered-content gates.
    }
    const auto missing = [&](const std::wstring& section, const wchar_t* key, const std::wstring& value) {
        std::wstring present;
        if (aip::ReadIniValueFromDoc(document, section.c_str(), key, present)) return true;
        if (!WriteContentProfileSetting(candidate, {section, key, value})) return false;
        document = aip::ParseIniDocument(candidate); return true;
    };
    const auto get = [&](const std::wstring& section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value;
        return aip::ReadIniValueFromDoc(document, section.c_str(), key, value) ? value : std::wstring(fallback);
    };
    int count = 1;
    if (!aip::content::ParseInt(get(L"Content", L"Count", L"1"), 1, aip::content::MaxEntries, count)) {
        error = L"Content.Count must be valid before setting up a gallery."; return false;
    }
    bool composed = false, legacyTextEnabled = false;
    if (!aip::content::ParseBool(get(L"Content", L"Enabled", L"0"), composed) ||
        !aip::content::ParseBool(get(L"TileText", L"Enabled", L"0"), legacyTextEnabled)) {
        error = L"The existing content/text enabled flags must be valid before gallery setup."; return false;
    }
    const bool legacyRss = !composed && ParseContentSourceValue(get(L"Settings", L"ContentSource", L"Wallpaper")) == ContentSource::RssFeed;
    const auto sources = legacyRss ? L"RssFeed" : !composed && legacyTextEnabled ? L"CustomText" : L"";
    if (!missing(L"Content.1", L"Enabled", L"1") || !missing(L"Content.1", L"Name", legacyRss ? L"RSS" : L"Wallpaper") ||
        !missing(L"Content.1", L"Background", L"Wallpaper") || !missing(L"Content.1", L"TextSources", sources) ||
        !missing(L"Content.1", L"Text", !composed ? get(L"TileText", L"Text", L"") : L"") ||
        !missing(L"Content.1", L"SecondaryText", !composed ? get(L"TileText", L"SecondaryText", L"") : L"") ||
        !missing(L"Content.1", L"BadgeText", !composed ? get(L"TileText", L"BadgeText", L"") : L"")) {
        error = L"Could not stage the primary tile defaults."; return false;
    }
    for (const auto& source : aip::content::SecondaryTiles) {
        const auto section = aip::content::SecondaryTileSection(source.id);
        if (!missing(section, L"Enabled", L"0") || !missing(section, L"Content", std::to_wstring(source.content))) {
            error = L"Could not stage the source tile flags."; return false;
        }
        bool enabled = false; int mapping = 1;
        if (!aip::content::ParseBool(get(section, L"Enabled", L"0"), enabled) ||
            !aip::content::ParseInt(get(section, L"Content", L"1"), 1, aip::content::MaxEntries, mapping)) {
            error = section + L" requires a valid Enabled flag and Content mapping."; return false;
        }
        if (!enabled) continue;
        count = (std::max)(count, mapping);
        const auto entry = L"Content." + std::to_wstring(mapping);
        if (!missing(entry, L"Enabled", L"1") || !missing(entry, L"Name", source.name) ||
            !missing(entry, L"Background", source.background) || !missing(entry, L"TextSources", source.source) ||
            !missing(entry, L"NotesResource", aip::content::NotesResources[source.notesResource].value)) {
            error = L"Could not stage an existing source tile's mapped content."; return false;
        }
    }
    if (!WriteContentProfileSetting(candidate, {L"Content", L"Count", std::to_wstring(count)}) ||
        !WriteContentProfileSetting(candidate, {L"Content", L"Enabled", L"1"}) ||
        !WriteContentProfileSetting(candidate, {L"Tiles", L"Enabled", L"1"}) ||
        !ValidateDesktopStubProfileDocument(candidate, error)) {
        if (error.empty()) error = L"The gallery tile mode could not be staged.";
        return false;
    }
    text = std::move(candidate); error.clear(); return true;
}
#include "ga_gallery_transaction.inc"
static bool SaveGalleryMenuSettings(HWND owner, const std::vector<aip::IniSetting>& settings, const GalleryEditContext* expected = nullptr)
{
    std::wstring error;
    const bool saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
        if (GalleryTransactionPending(GalleryProfilePath())) { error = L"Recover the interrupted gallery change before editing its settings."; return false; }
        if (expected && !MatchGalleryEdit(text, *expected, error)) return false;
        return StageGalleryMenuSettings(text, settings, error);
    }, CurrentIniWriteLockWaitMs());
    if (!saved) {
        if (error.empty()) error = L"The gallery configuration could not be saved; the previous configuration was kept.";
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING); return false;
    }
    const bool appearanceChanged = std::any_of(settings.begin(), settings.end(), [](const auto& setting) {
        const auto key = aip::content::Lower(setting.key);
        return aip::gallery::TileSectionNumber(setting.section) && (key == L"background" || key == L"branding" || key == L"backgroundcolor");
    });
    if (appearanceChanged) RequestRefreshExistingSecondaryTileLogos(g_hwnd, true);
    RequestGalleryRefresh(true); WakePollThread(); return true;
}
static bool OfferGalleryTileDeliveryMigration(HWND owner,const std::wstring& nativeId,const std::wstring& requestedMode)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner;(void)nativeId;(void)requestedMode;return false;
#else
    auto mode=requestedMode;std::wstring error;
    if(!aip::content::NormalizeTilePolicySetting(L"Delivery.Mode",mode,error))return false;
    aip::gallery::Configuration cfg;if(!ReadGalleryConfiguration(cfg,error))return false;
    const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& row){return aip::gallery::NativeTileId(cfg.groupId,row.id)==nativeId;});
    if(found==cfg.tiles.end())return false;
    const auto row=*found;const auto index=static_cast<std::size_t>(std::distance(cfg.tiles.begin(),found));
    bool registration=mode==L"Registration";
    if(mode==L"Auto"){
        auto policy=row.delivery;policy.values[L"settings.experimentallivetileupdate"]=L"Auto";
        aip::content::TilePolicyReadScope scope(policy);registration=!UseExperimentalLiveTileUpdate();
    }
    const auto prompt=registration?
        L"Registration uses a separate app identity and one rendered photo per update. Your existing Gallery Live Tile pin and photo ownership history will be kept. After the first successful update, pin the new app from Start's All apps list and unpin the old tile manually. Prepare this separate Registration tile now?":
        L"Live Tile delivery uses the existing Gallery tile identity and queued photos. The separate Registration app and its pin will be kept. Windows may ask you to pin the Live Tile separately. Change this gallery tile's delivery method now?";
    if(DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,prompt,row.name.c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONINFORMATION)!=IDYES)return false;
    if(registration){
        if(row.delivery.Read(L"Settings",L"AppxManifestTarget",L"Windows10")!=L"Windows10"){
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"Separate Registration tile requires Windows10 manifest target. Set this gallery tile's Delivery.ManifestTarget to Windows10 before preparing it.",row.name.c_str(),MB_OK|MB_ICONWARNING);return false;
        }
        aip::tile_registration::Target target;
        if(!BuildTileRegistrationTarget(aip::gallery::GalleryRegistrationId(cfg.groupId,row.id),row.name,row.appearance,target,error)||
            !PrepareTileRegistrationPackage(target,true,error)){
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Gallery Registration setup",MB_OK|MB_ICONWARNING);return false;
        }
    }
    const auto edit=GalleryEdit(cfg);const auto section=L"Gallery.Tile."+std::to_wstring(index+1);
    if(!SaveGalleryMenuSettings(owner,{{section,L"Delivery.Mode",mode}},&edit))return false;
    ForgetSecondaryTileDelivery(nativeId);
    if(!registration&&SecondaryTilePinUiAvailable()&&!SecondaryTileExists(nativeId))RequestPinSecondaryTile(owner,nativeId,row.name);
    return true;
#endif
}
// Only an entirely unused ledger from an interrupted first save can be reused.
// Missing/corrupt/populated history is never inferred from the current pins.
static bool GallerySetupHistoryAllowed(const aip::gallery::Configuration& current,
    aip::gallery::runtime::ReadResult status, const aip::gallery::Ledger& ledger,
    const GalleryPinHistory& pins, std::wstring& error)
{
    if (!pins.ready || pins.pending || !pins.error.empty()) { error = L"Windows has not successfully checked all existing gallery pins. No history was created."; return false; }
    if (!pins.ids.empty()) { error = L"Existing gallery tiles were found on Start. Keep their original INI and .gallery-ledger together; setup cannot create replacement ownership history while those pins remain."; return false; }
    if (!current.groupId.empty()) { error = L"This INI already identifies a gallery. Use its existing settings and ownership ledger; setup will not replace history."; return false; }
    if (status == aip::gallery::runtime::ReadResult::Error) { error = L"The existing gallery ledger is unavailable or invalid. Restore it instead of initializing replacement history."; return false; }
    if (status == aip::gallery::runtime::ReadResult::Ok &&
        (!aip::gallery::ValidateLedger(ledger, error) || ledger.revision != 0 || !ledger.images.empty() || !ledger.tiles.empty())) {
        error = L"A gallery ownership ledger already contains history. Restore its original configuration; setup will not reset it."; return false;
    }
    error.clear(); return true;
}
static bool PickGalleryFolder(HWND owner, std::wstring& result)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    (void)owner; (void)result; return false;
#else
    struct Apartment {
        HRESULT status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        ~Apartment() { if (SUCCEEDED(status)) CoUninitialize(); }
    } apartment;
    if (FAILED(apartment.status)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"The folder chooser needs a Windows UI apartment. No configuration was changed.", L"Photo gallery", MB_OK | MB_ICONWARNING); return false;
    }
    winrt::com_ptr<IFileOpenDialog> dialog;
    HRESULT status = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IFileOpenDialog), dialog.put_void());
    FILEOPENDIALOGOPTIONS options{};
    if (SUCCEEDED(status)) status = dialog->GetOptions(&options);
    if (SUCCEEDED(status)) status = dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    if (SUCCEEDED(status)) status = dialog->SetTitle(L"Choose the photo gallery folder");
    if (SUCCEEDED(status)) status = dialog->Show(owner);
    if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return false;
    winrt::com_ptr<IShellItem> selected;
    if (SUCCEEDED(status)) status = dialog->GetResult(selected.put());
    PWSTR path = nullptr;
    if (SUCCEEDED(status)) status = selected->GetDisplayName(SIGDN_FILESYSPATH, &path);
    if (FAILED(status) || !path) {
        CoTaskMemFree(path);
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Windows could not select a filesystem folder. No configuration was changed.", L"Photo gallery", MB_OK | MB_ICONWARNING); return false;
    }
    result = path; CoTaskMemFree(path); return true;
#endif
}
static std::wstring NewGalleryIdentity()
{
    GUID value{}; if (FAILED(CoCreateGuid(&value))) return {};
    const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
    std::wstring result = L"g";
    // 29 ASCII characters leave ample room for the per-tile ID and separator.
    for (std::size_t i = 0; i < 14; ++i) {
        result += L"0123456789abcdef"[bytes[i] >> 4]; result += L"0123456789abcdef"[bytes[i] & 15];
    }
    return result;
}
struct GallerySetupDraft {
    bool active = false;
    std::wstring profile;
    aip::gallery::Configuration before, requested;
};
static GallerySetupDraft g_gallerySetupDraft;
static void BeginGallerySetup(HWND owner)
{
    if (g_gallerySetupDraft.active || g_galleryPinHistory.pending) return;
    aip::gallery::Configuration current; std::wstring error;
    if (!ReadGalleryConfiguration(current, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING); return;
    }
    if (!current.groupId.empty()) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"This gallery is already configured. Change its folder, tile names, layout, or cadence in this menu. Existing image reservations stay assigned to their original tiles.", L"Photo gallery", MB_OK | MB_ICONINFORMATION); return;
    }
    const auto profile = GalleryProfilePath();
    auto requested = current;
    if (!PickGalleryFolder(owner, requested.folder)) return;
    auto countText = std::wstring(L"3");
    if (ShowTextInputDialog(owner, L"Photo gallery", IniReadS(L"Strings", L"GalleryCountPrompt", L"How many tiles? Enter 1 to 1024 (for example, 28). Each tile receives separate photos.").c_str(), countText, true) != IDOK) return;
    std::uint64_t count = 0;
    if (!aip::gallery::detail::UInt(aip::gallery::detail::Trim(countText), aip::gallery::MaxTiles, count) || !count) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Choose between 1 and 1024 gallery tiles.", L"Photo gallery", MB_OK | MB_ICONWARNING); return;
    }
    requested.tiles.clear(); requested.enabled = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto number = std::to_wstring(i + 1); auto name = L"Gallery " + number;
        // Large sets get useful numbered names; each remains editable later.
        if (count <= 8 && ShowTextInputDialog(owner, L"Photo gallery", L"Name for tile " + number, name, true) != IDOK) return;
        auto checked = name;
        if (!aip::gallery::NormalizeSetting(L"Gallery.Tile." + number, L"Name", checked, error)) {
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING); return;
        }
        requested.tiles.push_back({L"tile" + number, checked, true});
    }
    g_gallerySetupDraft = {true, profile, current, requested};
    if (!RequestGalleryPinHistory(g_hwnd)) {
        g_gallerySetupDraft = {};
        error = g_galleryPinHistory.error.empty() ? L"Gallery setup requires the registered packaged app and a successful Windows check for existing gallery tiles. No configuration was changed." : g_galleryPinHistory.error;
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING);
    }
}
static void CompleteGallerySetup(HWND owner)
{
    if (!g_gallerySetupDraft.active || g_galleryPinHistory.pending) return;
    auto draft = std::move(g_gallerySetupDraft); g_gallerySetupDraft = {};
    const auto pins = g_galleryPinHistory; g_galleryPinHistory = {};
    std::wstring error;
    if (GalleryProfilePath() != draft.profile) error = L"The selected INI changed during setup. No configuration was saved.";
    bool saved = false;
    if (error.empty()) saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
        aip::gallery::Configuration fresh;
        if (!ReadGalleryMenuText(text, fresh, error)) return false;
        ResolveGalleryConfigurationFolder(fresh, draft.profile);
        if (!SameGalleryConfiguration(fresh, draft.before)) { error = L"The gallery configuration changed during setup. Open setup again; the newer configuration was kept."; return false; }
        aip::gallery::Ledger ledger;
        const auto status = ReadGalleryLedgerNative(aip::gallery::runtime::LedgerPath(draft.profile), ledger, error);
        if (!GallerySetupHistoryAllowed(fresh, status, ledger, pins, error)) return false;
        if (status == aip::gallery::runtime::ReadResult::Ok) draft.requested.groupId = ledger.groupId;
        else draft.requested.groupId = NewGalleryIdentity();
        if (draft.requested.groupId.empty()) { error = L"Windows could not create a unique gallery identity."; return false; }
        std::vector<aip::IniSetting> settings;
        if (!PrepareGalleryConfigurationSettings(draft.requested, settings, error)) return false;
        auto candidate = text;
        if (!StageGalleryInitialLayout(candidate, error) || !StageGalleryMenuSettings(candidate, settings, error)) return false;
        if (status == aip::gallery::runtime::ReadResult::Missing) {
            const auto epoch = NewGalleryIdentity();
            if (epoch.empty() || !CreateGalleryLedgerForProfile(draft.profile, draft.requested, epoch, true, error)) return false;
        }
        // Ledger publication comes first. If the following INI commit fails,
        // retain the valid empty ledger and reuse it on the next setup attempt.
        text = std::move(candidate); return true;
    }, CurrentIniWriteLockWaitMs());
    if (!saved) {
        if (error.empty()) error = L"The gallery INI could not be saved. Any newly created empty ownership ledger was retained for a safe retry; no existing history was replaced.";
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING); return;
    }
    RequestGalleryRefresh(true); WakePollThread();
    aip::gallery::Configuration savedConfiguration;
    if(ReadGalleryConfiguration(savedConfiguration,error)){
        std::vector<std::size_t> indices;for(std::size_t i=0;i<savedConfiguration.tiles.size();++i)indices.push_back(i);
        PinGalleryGroup(owner,savedConfiguration,indices);
    }
}
struct GalleryMenuGroup { std::wstring folder, name; std::vector<std::size_t> indices; };
static std::vector<GalleryMenuGroup> GalleryGroups(const aip::gallery::Configuration& cfg) {
    std::vector<GalleryMenuGroup> groups;
    for (std::size_t i=0;i<cfg.tiles.size();++i) {
        const auto folder=aip::gallery::EffectiveFolder(cfg,cfg.tiles[i]);
        const auto caption=cfg.tiles[i].galleryName.empty() ? FileNameFromPath(folder) : cfg.tiles[i].galleryName;
        auto found=std::find_if(groups.begin(),groups.end(),[&](const auto& g){return IEquals(g.folder,folder)&&g.name==caption;});
        if(found==groups.end()) {groups.push_back({folder,caption.empty()?L"Gallery":caption,{}});found=groups.end()-1;}
        found->indices.push_back(i);
    }
    return groups;
}
static bool SaveGalleryConfiguration(HWND owner,const aip::gallery::Configuration& cfg,bool redistribute,
    const GalleryEditContext* expected = nullptr) {
    std::vector<aip::IniSetting> settings; std::wstring error;
    GalleryEditContext edit;
    if (expected) edit = *expected;
    else {
        aip::gallery::Configuration before;
        if (!ReadGalleryConfiguration(before, error)) { DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Photo gallery",MB_OK|MB_ICONWARNING); return false; }
        edit = GalleryEdit(before);
    }
    auto requested = cfg; ResolveGalleryConfigurationFolder(requested, edit.profile);
    if(!PrepareGalleryConfigurationSettings(requested,settings,error)) {DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Photo gallery",MB_OK|MB_ICONWARNING);return false;}
    if(!redistribute) return SaveGalleryMenuSettings(owner,settings,&edit);
    if(DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,IniReadS(L"Strings",L"GalleryRedistributePrompt",L"Redistribute photos across the configured tiles? Existing photo queues will briefly clear, then refill. Pinned tiles keep their positions and names.").c_str(),L"Photo gallery",MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES) return false;
    std::unique_lock<std::mutex> generation(g_generationMutex,std::try_to_lock);
    if(!generation.owns_lock()) {DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"A tile update is finishing. Try changing the gallery count again in a moment.",L"Photo gallery",MB_OK|MB_ICONINFORMATION);return false;}
    SuspendGalleryPublication();
    struct Resume {
        std::unique_lock<std::mutex>& generation;bool active=true;
        void Finish(){if(!active)return;active=false;if(generation.owns_lock())generation.unlock();ResumeGalleryPublication(true);WakePollThread();}
        ~Resume(){Finish();}
    } resume{generation};
    const bool saved = [&] {
        DesktopStubIniMutationGuard guard(CurrentIniWriteLockWaitMs()); std::wstring text;
        if (!guard.IsLocked() || !ReadIniFileForMutation(text)) { error = L"The gallery INI is unavailable; no queues or history were changed."; return false; }
        if (!MatchGalleryEdit(text, edit, error)) return false;
        if (GalleryTransactionPending(edit.profile)) { error = L"Recover the interrupted gallery change before starting another redistribution."; return false; }
        auto candidate = text;
        if (!StageGalleryMenuSettings(candidate, settings, error)) return false;
        const auto ledgerPath = aip::gallery::runtime::LedgerPath(edit.profile);
        aip::IniWriteMutexGuard ledgerGuard(ledgerPath, CurrentIniWriteLockWaitMs());
        if (!ledgerGuard.IsLocked()) { error = L"The gallery ledger is busy; no queues or history were changed."; return false; }
        aip::gallery::Ledger ledger;
        if (ReadGalleryLedgerNative(ledgerPath, ledger, error) != aip::gallery::runtime::ReadResult::Ok) return false;
        if (ledger.groupId != edit.before.groupId || requested.groupId != ledger.groupId) {
            error = L"The selected gallery settings and ledger have different group identities. Restore the matching pair before redistribution."; return false;
        }
        aip::gallery::transaction::Record record; record.epoch = NewGalleryIdentity();
        if (!GalleryConfigurationRecoveryBytes(edit.before, record.before, error) || !GalleryConfigurationRecoveryBytes(requested, record.after, error) ||
            !aip::gallery::SerializeLedger(ledger, record.ledger, error) || !CreateGalleryTransactionRecord(edit.profile, record, error)) return false;
        if (!GalleryTransactionEvent(L"after-journal")) { error = L"Gallery change interrupted after recovery record publication."; return false; }
        std::set<std::wstring> allHistory;
        for (const auto& tile : ledger.tiles) allHistory.insert(aip::gallery::NativeTileId(ledger.groupId, tile.first));
        for (const auto& tile : edit.before.tiles) allHistory.insert(aip::gallery::NativeTileId(ledger.groupId, tile.id));
        const std::vector<std::wstring> history(allHistory.begin(), allHistory.end());
        const auto restore = [&] {
            std::wstring actual, recoveryError;
            if (!ReadIniFileForMutation(actual) || !RecoverLockedGalleryTransaction(edit.profile, actual, recoveryError)) {
                error += L" Recovery is still pending: " + recoveryError;
            }
            return false;
        };
        if (!ClearGalleryQueuesForRebalance(history, error)) return restore();
        if (!GalleryTransactionEvent(L"before-reset") || !ResetGalleryLedgerForProfile(edit.profile, requested, ledger, record.epoch, true, error)) return restore();
        if (!GalleryTransactionEvent(L"after-reset")) { error = L"Gallery change interrupted after ledger reset; recovery is required."; return false; }
        if (!GalleryTransactionEvent(L"before-ini") || !WriteUtf8BomTextFile(edit.profile, candidate)) {
            error = L"The new gallery INI could not be saved."; return restore();
        }
        { std::lock_guard<std::mutex> lock(g_iniCacheMutex); SetIniCacheUnlocked(candidate); }
        if (!GalleryTransactionEvent(L"after-ini")) { error = L"Gallery change interrupted after INI publication; recovery is required."; return false; }
        return RecoverLockedGalleryTransaction(edit.profile, candidate, error);
    }();
    if(!saved) {resume.Finish();if(error.empty())error=L"The gallery change could not be saved. Check the recovery entry before retrying.";DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Photo gallery",MB_OK|MB_ICONWARNING);}
    return saved;
}
static bool AppendGalleryRows(aip::gallery::Configuration& cfg,const std::wstring& folder,const std::wstring& caption,std::size_t count) {
    if(count> aip::gallery::MaxTiles-cfg.tiles.size()) return false;
    for(std::size_t n=0;n<count;++n) {
        std::size_t serial=cfg.tiles.size()+1; std::wstring id;
        do {id=L"tile"+std::to_wstring(serial++);} while(std::any_of(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& row){return row.id==id;}));
        aip::gallery::TileConfiguration row;row.id=id;row.name=caption+L" "+std::to_wstring(n+1);row.folder=folder;row.galleryName=caption;cfg.tiles.push_back(std::move(row));
    }
    return true;
}
static void PinGalleryGroup(HWND owner,const aip::gallery::Configuration& cfg,const std::vector<std::size_t>& indices) {
    std::vector<std::pair<std::wstring,std::wstring>> pins;
    for(const auto i:indices) if(i<cfg.tiles.size()){
        const auto id=aip::gallery::NativeTileId(cfg.groupId,cfg.tiles[i].id);
        if(TileInstanceModeRequested()&&GalleryTileUsesRegistration(cfg.tiles[i]))OfferGalleryTileDeliveryMigration(owner,id,L"Registration");
        else pins.push_back({id,cfg.tiles[i].name});
    }
    RequestSecondaryTilePinBatch(owner,pins);
}
static void AddGalleryFolder(HWND owner,aip::gallery::Configuration cfg) {
    if(cfg.groupId.empty()){BeginGallerySetup(owner);return;}
    const auto edit = GalleryEdit(cfg);
    std::wstring folder; if(!PickGalleryFolder(owner,folder)) return;
    auto caption=FileNameFromPath(folder);if(caption.empty())caption=L"Gallery";
    if(ShowTextInputDialog(owner,L"Add gallery",L"Gallery name",caption,true)!=IDOK)return;
    auto countText=std::wstring(L"3");
    if(ShowTextInputDialog(owner,L"Add gallery",L"Number of tiles for this folder (1-1024 total across galleries)",countText,true)!=IDOK)return;
    std::uint64_t count=0;if(!aip::gallery::detail::UInt(aip::gallery::detail::Trim(countText),aip::gallery::MaxTiles,count)||!count||!AppendGalleryRows(cfg,folder,caption,static_cast<std::size_t>(count))){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"The combined gallery count must be between 1 and 1024.",L"Photo gallery",MB_OK|MB_ICONWARNING);return;}
    // A new folder normally brings new images. Explicit redistribution is also
    // available when that folder overlaps an existing pool.
    if(SaveGalleryConfiguration(owner,cfg,false,&edit)) {std::vector<std::size_t> added;for(std::size_t i=cfg.tiles.size()-static_cast<std::size_t>(count);i<cfg.tiles.size();++i)added.push_back(i);PinGalleryGroup(owner,cfg,added);}
}
static bool GalleryTileUsesRegistration(const aip::gallery::TileConfiguration& tile)
{
    if(!TileInstanceModeRequested())return false; // Preserve the legacy gallery pin workflow.
    if(tile.delivery.Mode()==L"Registration")return true;
    if(tile.delivery.Mode()==L"LiveTile")return false;
    aip::content::TilePolicyReadScope scope(tile.delivery);return !UseExperimentalLiveTileUpdate();
}
static void AppendGalleryTilesMenu(HMENU parent) {
    HMENU menu=aip::BeginTrayNestedMenu(parent);aip::gallery::Configuration cfg;std::wstring error;
    const bool valid=ReadGalleryConfiguration(cfg,error),configured=valid&&!cfg.groupId.empty();
    const bool pendingRecovery = GalleryTransactionPending(GalleryProfilePath());
    if (pendingRecovery) aip::AppendTrayMenuItem(menu, ID_GALLERY_RECOVER,
        IniReadS(L"Strings", L"GalleryRecover", L"Recover interrupted change..."), false, true);
    aip::AppendTrayMenuItem(menu,ID_GALLERY_ADD,configured?L"Add another gallery folder...":L"Set up gallery...",false,valid&&SecondaryTilePinUiAvailable());
    aip::AppendTrayMenuItem(menu,ID_GALLERY_PIN_ALL,L"Pin all gallery tiles...",false,configured&&SecondaryTilePinUiAvailable());
    aip::AppendTrayMenuItem(menu,ID_GALLERY_REBALANCE,L"Redistribute photos across tiles...",false,configured&&SecondaryTilePinUiAvailable());
    aip::AppendTrayMenuItem(menu,ID_GALLERY_REFRESH,L"Refresh gallery now",false,configured);
    HMENU options=aip::BeginTrayNestedMenu(menu);
    HMENU queue=aip::BeginTrayNestedMenu(options);for(UINT i=0;i<aip::gallery::MaxQueue;++i)aip::AppendTrayMenuItem(queue,ID_GALLERY_QUEUE+i,std::to_wstring(i+1),cfg.queueSize==i+1,configured);
    aip::EndTrayNestedMenu(options,queue,L"Photos queued per tile");
    aip::AppendTrayMenuItem(options,ID_GALLERY_CADENCE,L"Replace photo queues every (seconds): "+std::to_wstring(cfg.refreshSeconds)+L"...",false,configured);
    aip::AppendTrayMenuItem(options,ID_GALLERY_SEED,L"Shuffle seed...",false,configured);
    HMENU order=aip::BeginTrayNestedMenu(options);aip::AppendTrayMenuItem(order,ID_GALLERY_ORDER,L"Shuffle",cfg.order==aip::gallery::Order::Shuffle,configured);aip::AppendTrayMenuItem(order,ID_GALLERY_ORDER+1,L"Sequential",cfg.order==aip::gallery::Order::Sequential,configured);aip::EndTrayNestedMenu(options,order,L"Photo order");
    HMENU fit=aip::BeginTrayNestedMenu(options);aip::AppendTrayMenuItem(fit,ID_GALLERY_FIT,L"Fill tile (crop edges)",cfg.fit==aip::gallery::Fit::Fill,configured);aip::AppendTrayMenuItem(fit,ID_GALLERY_FIT+1,L"Fit whole photo",cfg.fit==aip::gallery::Fit::Fit,configured);aip::EndTrayNestedMenu(options,fit,L"Photo layout");
    aip::EndTrayNestedMenu(menu,options,L"Gallery settings");
    const auto groups=GalleryGroups(cfg);
    for(std::size_t groupIndex=0;valid&&groupIndex<groups.size();++groupIndex) {
        const auto& group=groups[groupIndex];HMENU groupMenu=aip::BeginTrayNestedMenu(menu);const auto groupBase=ID_GALLERY_GROUP+static_cast<UINT>(groupIndex)*4;
        aip::AppendTrayMenuItem(groupMenu,groupBase,L"Pin all tiles in this gallery...",false,configured&&SecondaryTilePinUiAvailable());
        aip::AppendTrayMenuItem(groupMenu,groupBase+1,L"Number of tiles: "+std::to_wstring(group.indices.size())+L"...",false,configured);
        aip::AppendTrayMenuItem(groupMenu,groupBase+2,L"Choose photo folder...",false,configured);
        aip::AppendTrayMenuItem(groupMenu,groupBase+3,L"Open photo folder",false,configured);
        for(std::size_t first=0;first<group.indices.size();first+=16) {
            HMENU page=group.indices.size()>16?aip::BeginTrayNestedMenu(groupMenu):groupMenu;
            for(std::size_t p=first;p<(std::min)(first+16,group.indices.size());++p) {
                const auto i=group.indices[p];const auto& tile=cfg.tiles[i];const auto base=ID_GALLERY_TILE+static_cast<UINT>(i)*GALLERY_TILE_COMMAND_STRIDE;
                HMENU item=aip::BeginTrayNestedMenu(page);
                aip::AppendTrayMenuItem(item,base+1,L"Rename...",false,configured);
                if(GalleryTileUsesRegistration(tile))aip::AppendTrayMenuItem(item,base+2,L"Registration setup and pin instructions...",false,configured);
                else AppendSecondaryTilePinCommands(item,aip::gallery::NativeTileId(cfg.groupId,tile.id),base+2,base+3,base+4,configured);
                aip::AppendTrayMenuItem(item,base+5,SecondaryTileUiString(L"SelectedTileContents",L"Tile contents..."),false,true);
                aip::AppendTrayMenuItem(item,base+6,L"Rendering, cache and delivery...",false,configured);
                AppendTileAppearanceMenu(item,L"Gallery.Tile."+std::to_wstring(i+1),base+10);
                aip::EndTrayNestedMenu(page,item,tile.name);
            }
            if(page!=groupMenu)aip::EndTrayNestedMenu(groupMenu,page,L"Tiles "+std::to_wstring(first+1)+L"-"+std::to_wstring((std::min)(first+16,group.indices.size())));
        }
        aip::EndTrayNestedMenu(menu,groupMenu,group.name);
    }
    aip::EndTrayNestedMenu(parent,menu,L"Photo galleries");
}
static bool DispatchGalleryTilesCommand(HWND owner,UINT command) {
    const bool global=command>=ID_GALLERY_SETUP&&command<=ID_GALLERY_RECOVER;
    const bool tileCommand=command>=ID_GALLERY_TILE&&command<ID_GALLERY_TILE+aip::gallery::MaxTiles*GALLERY_TILE_COMMAND_STRIDE;
    const bool groupCommand=command>=ID_GALLERY_GROUP&&command<ID_GALLERY_GROUP+aip::gallery::MaxTiles*4;
    if(!global&&!tileCommand&&!groupCommand)return false;
    if (command == ID_GALLERY_RECOVER) {
        std::wstring recoveryError;
        if (!RecoverPendingGalleryTransaction(recoveryError)) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, recoveryError.c_str(), L"Photo gallery recovery", MB_OK | MB_ICONWARNING);
        return true;
    }
    aip::gallery::Configuration cfg;std::wstring error;if(!ReadGalleryConfiguration(cfg,error)){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Photo gallery",MB_OK|MB_ICONWARNING);return true;}
    const auto edit = GalleryEdit(cfg);
    if(command==ID_GALLERY_ADD||command==ID_GALLERY_SETUP){AddGalleryFolder(owner,cfg);return true;}
    if(command==ID_GALLERY_PIN_ALL){std::vector<std::size_t> indices;for(std::size_t i=0;i<cfg.tiles.size();++i)indices.push_back(i);PinGalleryGroup(owner,cfg,indices);}
    else if(command==ID_GALLERY_REBALANCE)SaveGalleryConfiguration(owner,cfg,true,&edit);
    else if(command==ID_GALLERY_REFRESH){RequestGalleryRefresh(true);WakePollThread();}
    else if(command==ID_GALLERY_CADENCE||command==ID_GALLERY_SEED){auto value=command==ID_GALLERY_CADENCE?std::to_wstring(cfg.refreshSeconds):std::to_wstring(cfg.seed);if(ShowTextInputDialog(owner,L"Gallery settings",command==ID_GALLERY_CADENCE?L"Seconds between queue replacements (60-86400). Windows chooses animation timing.":L"Shuffle seed (nonnegative integer)",value,true)==IDOK)SaveGalleryMenuSettings(owner,{{L"Gallery",command==ID_GALLERY_CADENCE?L"RefreshSeconds":L"Seed",value}},&edit);}
    else if(command>=ID_GALLERY_QUEUE&&command<ID_GALLERY_QUEUE+aip::gallery::MaxQueue)SaveGalleryMenuSettings(owner,{{L"Gallery",L"QueueSize",std::to_wstring(command-ID_GALLERY_QUEUE+1)}},&edit);
    else if(command==ID_GALLERY_ORDER||command==ID_GALLERY_ORDER+1)SaveGalleryMenuSettings(owner,{{L"Gallery",L"Order",command==ID_GALLERY_ORDER?L"Shuffle":L"Sequential"}},&edit);
    else if(command==ID_GALLERY_FIT||command==ID_GALLERY_FIT+1)SaveGalleryMenuSettings(owner,{{L"Gallery",L"Fit",command==ID_GALLERY_FIT?L"Fill":L"Fit"}},&edit);
    else if(groupCommand) {
        const auto groups=GalleryGroups(cfg);const auto index=(command-ID_GALLERY_GROUP)/4,action=(command-ID_GALLERY_GROUP)%4;if(index>=groups.size())return true;const auto group=groups[index];
        if(action==0)PinGalleryGroup(owner,cfg,group.indices);
        else if(action==3)OpenGalleryFolder(group.folder);
        else if(action==2){auto folder=group.folder;if(PickGalleryFolder(owner,folder)){for(const auto i:group.indices)cfg.tiles[i].folder=folder;SaveGalleryConfiguration(owner,cfg,false,&edit);}}
        else {auto value=std::to_wstring(group.indices.size());if(ShowTextInputDialog(owner,L"Gallery size",L"Number of tiles for this folder (1-1024 total)",value,true)!=IDOK)return true;std::uint64_t count=0;if(!aip::gallery::detail::UInt(aip::gallery::detail::Trim(value),aip::gallery::MaxTiles,count)||!count||cfg.tiles.size()-group.indices.size()+count>aip::gallery::MaxTiles){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"Enter a valid count within the 1024-tile total limit.",L"Photo gallery",MB_OK|MB_ICONWARNING);return true;}if(count>group.indices.size())AppendGalleryRows(cfg,group.folder,group.name,static_cast<std::size_t>(count)-group.indices.size());else if(count<group.indices.size()){std::set<std::size_t> removed(group.indices.begin()+static_cast<std::size_t>(count),group.indices.end());std::vector<aip::gallery::TileConfiguration> kept;for(std::size_t i=0;i<cfg.tiles.size();++i)if(!removed.count(i))kept.push_back(cfg.tiles[i]);cfg.tiles=std::move(kept);}if(SaveGalleryConfiguration(owner,cfg,true,&edit)){const auto updated=GalleryGroups(cfg);for(const auto& g:updated)if(IEquals(g.folder,group.folder)&&g.name==group.name){PinGalleryGroup(owner,cfg,g.indices);break;}}}
    } else if(tileCommand) {
        const auto index=(command-ID_GALLERY_TILE)/GALLERY_TILE_COMMAND_STRIDE,action=(command-ID_GALLERY_TILE)%GALLERY_TILE_COMMAND_STRIDE;if(index>=cfg.tiles.size())return true;const auto& tile=cfg.tiles[index];const auto section=L"Gallery.Tile."+std::to_wstring(index+1),id=aip::gallery::NativeTileId(cfg.groupId,tile.id);
        if(action>=10&&action<=18){const auto a=action-10;if(a<2)SaveGalleryMenuSettings(owner,{{section,L"Background",a?L"Color":L"Transparent"}},&edit);else if(a<7)SaveGalleryMenuSettings(owner,{{section,L"Branding",aip::content::TileBrandingChoices[a-2]}},&edit);else if(a==7){auto color=tile.appearance.backgroundColor;if(ShowTextInputDialog(owner,L"Gallery background",L"Color as #RRGGBB, or Auto",color,true)==IDOK)SaveGalleryMenuSettings(owner,{{section,L"Background",L"Color"},{section,L"BackgroundColor",color}},&edit);}else SaveGalleryMenuSettings(owner,{{section,L"BackgroundColor",L"Auto"}},&edit);}
        else if(action==1){auto name=tile.name;if(ShowTextInputDialog(owner,L"Gallery tile",L"Tile name",name,true)==IDOK)SaveGalleryMenuSettings(owner,{{section,L"Name",name}},&edit);}
        else if(action==2){if(GalleryTileUsesRegistration(tile))OfferGalleryTileDeliveryMigration(owner,id,L"Registration");else RequestPinSecondaryTile(owner,id,tile.name);}
        else if(action==3){if(!GalleryTileUsesRegistration(tile))RequestUnpinSecondaryTile(owner,id);}
        else if(action==4)RequestSecondaryTileList(owner);
        else if(action==5)ShowSelectedTileStatus(owner,id,tile.name);
        else if(action==6)OpenGalleryTilePolicySetup(owner,id);
    }
    return true;
}

// Gallery setup/configuration is separate from image inventory and notification
// delivery. Building this menu never scans photographs or initializes history.
static bool OpenGalleryFolder(const std::wstring& directory);
static bool GalleryDirectoryExists(const std::wstring& directory);
static constexpr UINT ID_GALLERY_SETUP = 30000, ID_GALLERY_ENABLED = 30001,
    ID_GALLERY_FOLDER = 30002, ID_GALLERY_OPEN_FOLDER = 30003, ID_GALLERY_REFRESH = 30004,
    ID_GALLERY_CADENCE = 30005, ID_GALLERY_SEED = 30006, ID_GALLERY_COUNT = 30010, ID_GALLERY_QUEUE = 30020,
    ID_GALLERY_ORDER = 30030, ID_GALLERY_FIT = 30040, ID_GALLERY_TILE = 30100;
static constexpr UINT GALLERY_TILE_COMMAND_STRIDE = 20;

static std::wstring GalleryProfilePath()
{
    std::lock_guard<std::mutex> lock(g_pathMutex); return aip::MakeAbsolutePath(g_iniPath);
}
static void ResolveGalleryConfigurationFolder(aip::gallery::Configuration& configuration, const std::wstring& profile)
{
    if (configuration.folder.empty()) return;
    if (PathIsRelativeW(configuration.folder.c_str())) configuration.folder = ParentDirectoryOfPath(profile) + L"\\" + configuration.folder;
    configuration.folder = aip::MakeAbsolutePath(configuration.folder);
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
            a.tiles[i].appearance.background != b.tiles[i].appearance.background || a.tiles[i].appearance.branding != b.tiles[i].appearance.branding ||
            a.tiles[i].appearance.backgroundColor != b.tiles[i].appearance.backgroundColor) return false;
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
static bool SaveGalleryMenuSettings(HWND owner, const std::vector<aip::IniSetting>& settings)
{
    std::wstring error;
    const bool saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
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
    if (ShowTextInputDialog(owner, L"Photo gallery", L"How many separate gallery tiles? Enter 1 to 8. Photos stay permanently assigned to their first tile.", countText, true) != IDOK) return;
    std::uint64_t count = 0;
    if (!aip::gallery::detail::UInt(aip::gallery::detail::Trim(countText), aip::gallery::MaxTiles, count) || !count) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Choose between 1 and 8 gallery tiles.", L"Photo gallery", MB_OK | MB_ICONWARNING); return;
    }
    requested.tiles.clear(); requested.enabled = true;
    for (std::size_t i = 0; i < count; ++i) {
        const auto number = std::to_wstring(i + 1); auto name = L"Gallery " + number;
        if (ShowTextInputDialog(owner, L"Photo gallery", L"Name for tile " + number, name, true) != IDOK) return;
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
    DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Your gallery is ready. Open Tiles > Photo gallery, then choose Pin/update for each tile you want on Start. Windows chooses when queued photos rotate. Pausing or unpinning a tile keeps its image reservations.", L"Photo gallery", MB_OK | MB_ICONINFORMATION);
}
static void AppendGalleryTilesMenu(HMENU parent)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    aip::gallery::Configuration configuration; std::wstring error;
    const bool valid = ReadGalleryConfiguration(configuration, error);
    const bool configured = valid && !configuration.groupId.empty();
    aip::AppendTrayMenuItem(menu, ID_GALLERY_SETUP, configured ? L"Gallery is configured" : L"Set up gallery...", false,
        valid && !configured && SecondaryTilePinUiAvailable() && !g_gallerySetupDraft.active && !g_galleryPinHistory.pending);
    aip::AppendTrayMenuItem(menu, ID_GALLERY_ENABLED, L"Enable photo gallery", configuration.enabled, configured);
    aip::AppendTrayMenuItem(menu, ID_GALLERY_FOLDER, L"Choose photo folder...", false, configured);
    aip::AppendTrayMenuItem(menu, ID_GALLERY_OPEN_FOLDER, L"Open photo folder", false, configured && !configuration.folder.empty());
    aip::AppendTrayMenuItem(menu, ID_GALLERY_REFRESH, L"Refresh gallery now", false, configured);
    auto status = !valid ? error : configured ? GallerySourceStatus() : L"Set up a folder and tile names; pin tiles manually afterward";
    aip::AppendTrayMenuItem(menu, 0, status.empty() ? L"Waiting for gallery source" : status, false, false);
    aip::AppendTrayMenuItem(menu, 0, L"Permanent image pools: pausing/unpinning never reassigns photos", false, false);
    HMENU count = aip::BeginTrayNestedMenu(menu);
    for (UINT i = 0; i < aip::gallery::MaxTiles; ++i)
        aip::AppendTrayMenuItem(count, ID_GALLERY_COUNT + i, std::to_wstring(i + 1), configuration.tiles.size() == i + 1, configured);
    aip::EndTrayNestedMenu(menu, count, L"Number of tiles");
    HMENU queue = aip::BeginTrayNestedMenu(menu);
    for (UINT i = 0; i < aip::gallery::MaxQueue; ++i)
        aip::AppendTrayMenuItem(queue, ID_GALLERY_QUEUE + i, std::to_wstring(i + 1), configuration.queueSize == i + 1, configured);
    aip::EndTrayNestedMenu(menu, queue, L"Photos in each Windows queue");
    aip::AppendTrayMenuItem(menu, ID_GALLERY_CADENCE, L"Queue refresh interval (seconds): " + std::to_wstring(configuration.refreshSeconds) + L"...", false, configured);
    aip::AppendTrayMenuItem(menu, ID_GALLERY_SEED, L"Shuffle seed: " + std::to_wstring(configuration.seed) + L"...", false, configured);
    HMENU order = aip::BeginTrayNestedMenu(menu);
    aip::AppendTrayMenuItem(order, ID_GALLERY_ORDER, L"Shuffle", configuration.order == aip::gallery::Order::Shuffle, configured);
    aip::AppendTrayMenuItem(order, ID_GALLERY_ORDER + 1, L"Sequential", configuration.order == aip::gallery::Order::Sequential, configured);
    aip::EndTrayNestedMenu(menu, order, L"Photo order");
    HMENU fit = aip::BeginTrayNestedMenu(menu);
    aip::AppendTrayMenuItem(fit, ID_GALLERY_FIT, L"Fill tile (crop edges)", configuration.fit == aip::gallery::Fit::Fill, configured);
    aip::AppendTrayMenuItem(fit, ID_GALLERY_FIT + 1, L"Fit whole photo", configuration.fit == aip::gallery::Fit::Fit, configured);
    aip::EndTrayNestedMenu(menu, fit, L"Photo layout");
    const auto states = valid ? GalleryTileMenuStates() : std::vector<aip::gallery::runtime::MenuState>{};
    if (valid) for (std::size_t i = 0; i < configuration.tiles.size(); ++i) {
        const auto& tile = configuration.tiles[i]; const auto id = aip::gallery::NativeTileId(configuration.groupId, tile.id);
        HMENU item = aip::BeginTrayNestedMenu(menu); const auto base = ID_GALLERY_TILE + static_cast<UINT>(i) * GALLERY_TILE_COMMAND_STRIDE;
        aip::AppendTrayMenuItem(item, base, L"Enable this gallery tile", tile.enabled, configured);
        aip::AppendTrayMenuItem(item, base + 1, L"Rename...", false, configured);
        const auto state = std::find_if(states.begin(), states.end(), [&](const auto& value) { return value.nativeId == id; });
        const bool ledgerReady = state != states.end() && state->ledgerReady;
        aip::AppendTrayMenuItem(item, base + 2, L"Pin/update on Start...", false, configured && configuration.enabled && tile.enabled && SecondaryTilePinUiAvailable() && ledgerReady);
        aip::AppendTrayMenuItem(item, base + 3, L"Unpin from Start...", false, configured && SecondaryTilePinUiAvailable());
        if (state != states.end()) aip::AppendTrayMenuItem(item, 0, state->status, false, false);
        AppendTileAppearanceMenu(item, L"Gallery.Tile." + std::to_wstring(i + 1), base + 10);
        aip::EndTrayNestedMenu(menu, item, tile.name);
    }
    aip::EndTrayNestedMenu(parent, menu, L"Photo gallery");
}
static bool DispatchGalleryTilesCommand(HWND owner, UINT command)
{
    const bool global = command >= ID_GALLERY_SETUP && command <= ID_GALLERY_FIT + 1;
    const bool tileCommand = command >= ID_GALLERY_TILE && command < ID_GALLERY_TILE + aip::gallery::MaxTiles * GALLERY_TILE_COMMAND_STRIDE;
    if (!global && !tileCommand) return false;
    if (command == ID_GALLERY_SETUP) { BeginGallerySetup(owner); return true; }
    aip::gallery::Configuration configuration; std::wstring error;
    if (!ReadGalleryConfiguration(configuration, error)) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Photo gallery", MB_OK | MB_ICONWARNING); return true;
    }
    if (command == ID_GALLERY_ENABLED) SaveGalleryMenuSettings(owner, {{L"Gallery", L"Enabled", configuration.enabled ? L"0" : L"1"}});
    else if (command == ID_GALLERY_FOLDER) {
        auto folder = configuration.folder;
        if (PickGalleryFolder(owner, folder)) SaveGalleryMenuSettings(owner, {{L"Gallery", L"Folder", folder}});
    } else if (command == ID_GALLERY_OPEN_FOLDER) {
        if (!OpenGalleryFolder(configuration.folder)) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"The gallery folder is unavailable or Windows could not open it.", L"Photo gallery", MB_OK | MB_ICONWARNING);
    } else if (command == ID_GALLERY_REFRESH) { RequestGalleryRefresh(true); WakePollThread(); }
    else if (command == ID_GALLERY_CADENCE) {
        auto value = std::to_wstring(configuration.refreshSeconds);
        if (ShowTextInputDialog(owner, L"Gallery queue refresh", L"Seconds between queue replacements (60-86400). Windows independently chooses when each queued photo is displayed.", value, true) == IDOK)
            SaveGalleryMenuSettings(owner, {{L"Gallery", L"RefreshSeconds", value}});
    } else if (command == ID_GALLERY_SEED) {
        auto value = std::to_wstring(configuration.seed);
        if (ShowTextInputDialog(owner, L"Gallery shuffle seed", L"Nonnegative integer used to order this gallery's already reserved photos. Changing it never transfers ownership between tiles.", value, true) == IDOK)
            SaveGalleryMenuSettings(owner, {{L"Gallery", L"Seed", value}});
    } else if (command >= ID_GALLERY_COUNT && command < ID_GALLERY_COUNT + aip::gallery::MaxTiles) {
        const auto count = command - ID_GALLERY_COUNT + 1;
        std::vector<aip::IniSetting> settings{{L"Gallery", L"TileCount", std::to_wstring(count)}};
        for (std::size_t i = configuration.tiles.size(); i < count; ++i) {
            const auto number = std::to_wstring(i + 1), section = L"Gallery.Tile." + number;
            auto id = IniReadS(section.c_str(), L"Id", L""); if (id.empty()) id = L"tile" + number;
            settings.push_back({section, L"Id", id});
            settings.push_back({section, L"Name", IniReadS(section.c_str(), L"Name", (L"Gallery " + number).c_str())});
        }
        SaveGalleryMenuSettings(owner, settings); // Removed numbered rows/history are retained.
    } else if (command >= ID_GALLERY_QUEUE && command < ID_GALLERY_QUEUE + aip::gallery::MaxQueue)
        SaveGalleryMenuSettings(owner, {{L"Gallery", L"QueueSize", std::to_wstring(command - ID_GALLERY_QUEUE + 1)}});
    else if (command == ID_GALLERY_ORDER || command == ID_GALLERY_ORDER + 1)
        SaveGalleryMenuSettings(owner, {{L"Gallery", L"Order", command == ID_GALLERY_ORDER ? L"Shuffle" : L"Sequential"}});
    else if (command == ID_GALLERY_FIT || command == ID_GALLERY_FIT + 1)
        SaveGalleryMenuSettings(owner, {{L"Gallery", L"Fit", command == ID_GALLERY_FIT ? L"Fill" : L"Fit"}});
    else if (tileCommand) {
        const auto index = (command - ID_GALLERY_TILE) / GALLERY_TILE_COMMAND_STRIDE, action = (command - ID_GALLERY_TILE) % GALLERY_TILE_COMMAND_STRIDE;
        if (index >= configuration.tiles.size()) return true;
        const auto& tile = configuration.tiles[index]; const auto section = L"Gallery.Tile." + std::to_wstring(index + 1);
        const auto id = aip::gallery::NativeTileId(configuration.groupId, tile.id);
        if (action >= 10 && action <= 18) {
            const auto appearance = action - 10;
            if (appearance < 2) SaveGalleryMenuSettings(owner, {{section, L"Background", appearance ? L"Color" : L"Transparent"}});
            else if (appearance < 7) SaveGalleryMenuSettings(owner, {{section, L"Branding", aip::content::TileBrandingChoices[appearance - 2]}});
            else if (appearance == 7) {
                auto color = tile.appearance.backgroundColor;
                if (ShowTextInputDialog(owner, L"Gallery tile background", L"Color as #RRGGBB, or Auto for the gallery topic color", color, true) == IDOK)
                    SaveGalleryMenuSettings(owner, {{section, L"Background", L"Color"}, {section, L"BackgroundColor", color}});
            } else SaveGalleryMenuSettings(owner, {{section, L"BackgroundColor", L"Auto"}});
        } else if (action == 0) SaveGalleryMenuSettings(owner, {{section, L"Enabled", tile.enabled ? L"0" : L"1"}});
        else if (action == 1) {
            auto name = tile.name;
            if (ShowTextInputDialog(owner, L"Gallery tile", L"Tile name", name, true) == IDOK) SaveGalleryMenuSettings(owner, {{section, L"Name", name}});
        } else if (action == 2) RequestPinSecondaryTile(g_hwnd, id, tile.name);
        else if (action == 3) RequestUnpinSecondaryTile(g_hwnd, id);
    }
    return true;
}

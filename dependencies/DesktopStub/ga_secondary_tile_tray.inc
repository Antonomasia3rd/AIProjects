// Secondary tile management shares one INI and the resident content engine.
// Construction reads configuration/status only; pin and setup require a click.
#include "../secondary_tiles.h"

static constexpr UINT ID_SECONDARY_TILES_ENABLED = 28000;
static constexpr UINT ID_SECONDARY_TILES_INITIALIZE = 28001;
static constexpr UINT ID_SECONDARY_TILES_STATUS = 28002;
static constexpr UINT ID_SECONDARY_SOURCE_SIGN_IN = 28003;
static constexpr UINT ID_SECONDARY_SOURCE_ACCOUNT = 28004;
static constexpr UINT ID_SECONDARY_TILE_COMMANDS = 28100;
static constexpr UINT SECONDARY_TILE_COMMAND_STRIDE = 64;

static std::wstring SecondaryTileApiId(const std::wstring& key)
{
    const auto* descriptor = aip::content::FindSecondaryTileDescriptor(key);
    return descriptor ? L"AipSource_" + std::wstring(descriptor->id) : L"";
}

static bool ReadSecondaryTileMenuText(const std::wstring& text,
    aip::content::Configuration& content, aip::content::SecondaryTileConfiguration& tiles, std::wstring& error)
{
    const auto document = aip::ParseIniDocument(text);
    const auto read = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
        std::wstring value;
        return aip::ReadIniValueFromDoc(document, section, key, value) ? value : std::wstring(fallback);
    };
    return aip::content::ReadConfiguration(read, content, error, ContentAsusEventsFromDocument(document)) &&
        aip::content::ReadSecondaryTileConfiguration(read, content.entries.size(), tiles, error);
}

// Pure staging: add absent fields, grow Count only as required by saved mappings,
// and publish no partial text. Explicit activation fixes the primary at Content.1.
static bool StageSecondaryTileDefaultLayout(std::wstring& text, bool activate, std::wstring& error)
{
    auto candidate = text;
    auto document = aip::ParseIniDocument(candidate);
    const auto missing = [&](const std::wstring& section, const wchar_t* key, const std::wstring& value) {
        std::wstring present;
        if (aip::ReadIniValueFromDoc(document, section.c_str(), key, present)) return true;
        if (!WriteContentProfileSetting(candidate, {section, key, value})) return false;
        document = aip::ParseIniDocument(candidate); return true;
    };
    const auto put = [&](const std::wstring& section, const wchar_t* key, const std::wstring& value) {
        return WriteContentProfileSetting(candidate, {section, key, value});
    };
    std::wstring savedCount;
    int count = 1;
    if (aip::ReadIniValueFromDoc(document, L"Content", L"Count", savedCount) &&
        !aip::content::ParseInt(savedCount, 1, aip::content::MaxEntries, count)) {
        error = L"Content.Count must be valid before adding source tiles."; return false;
    }
    if (!missing(L"Tiles", L"Enabled", L"0") || !missing(L"Content", L"Enabled", L"0") ||
        !missing(L"Content", L"CycleEnabled", L"0") || !missing(L"Content.1", L"Enabled", L"1") ||
        !missing(L"Content.1", L"Name", L"Wallpaper") || !missing(L"Content.1", L"Background", L"Wallpaper") ||
        !missing(L"Content.1", L"TextSources", L"CustomText")) {
        error = L"Could not stage the primary content defaults."; return false;
    }
    for (const auto& descriptor : aip::content::SecondaryTiles) {
        const auto section = aip::content::SecondaryTileSection(descriptor.id);
        if (!missing(section, L"Enabled", L"1") || !missing(section, L"Name", descriptor.name) ||
            !missing(section, L"Content", std::to_wstring(descriptor.content))) {
            error = L"Could not stage a source tile mapping."; return false;
        }
        std::wstring mapping;
        int index = descriptor.content;
        if (!aip::ReadIniValueFromDoc(document, section.c_str(), L"Content", mapping) ||
            !aip::content::ParseInt(mapping, 1, aip::content::MaxEntries, index)) {
            error = L"A source tile has an invalid content mapping."; return false;
        }
        count = std::max(count, index);
        const auto entry = L"Content." + std::to_wstring(index);
        if (!missing(entry, L"Enabled", L"1") || !missing(entry, L"Name", descriptor.name) ||
            !missing(entry, L"Background", descriptor.background) || !missing(entry, L"ImagePath", L"") ||
            !missing(entry, L"TextSources", descriptor.source) || !missing(entry, L"Text", L"") ||
            !missing(entry, L"SecondaryText", L"") || !missing(entry, L"BadgeText", L"") ||
            !missing(entry, L"RssItem", L"1") || !missing(entry, L"NotesResource", aip::content::NotesResources[descriptor.notesResource].value) ||
            !missing(entry, L"NotesDetail", L"Summary") || (activate && !put(entry, L"Enabled", L"1"))) {
            error = L"Could not stage a source tile's content defaults."; return false;
        }
    }
    if (!put(L"Content", L"Count", std::to_wstring(count)) || (activate &&
        (!put(L"Content", L"Enabled", L"1") || !put(L"Tiles", L"Enabled", L"1") || !put(L"Content", L"CycleEnabled", L"0")))) {
        error = L"Could not stage the source tile mode."; return false;
    }
    aip::content::Configuration content;
    aip::content::SecondaryTileConfiguration tiles;
    if (!ReadSecondaryTileMenuText(candidate, content, tiles, error)) return false;
    text = std::move(candidate); error.clear(); return true;
}

static bool SaveSecondaryTileMenuSettings(HWND owner, const std::vector<aip::IniSetting>& settings,
    bool initialize = false, bool activate = false)
{
    std::wstring error;
    const bool saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
        auto candidate = text;
        if (initialize && !StageSecondaryTileDefaultLayout(candidate, activate, error)) return false;
        for (auto setting : settings) {
            const auto section = aip::content::Lower(setting.section);
            const bool tile = section == L"tiles" || section.compare(0, 5, L"tile.") == 0;
            if (!(tile ? aip::content::NormalizeSecondaryTileSetting(setting.section, setting.key, setting.value, error) :
                aip::content::NormalizeSetting(setting.section, setting.key, setting.value, error)) ||
                !WriteContentProfileSetting(candidate, setting)) {
                if (error.empty()) error = L"A tile or source setting cannot be stored in the single-line INI format.";
                return false;
            }
        }
        aip::content::Configuration content;
        aip::content::SecondaryTileConfiguration tiles;
        if (!ReadSecondaryTileMenuText(candidate, content, tiles, error)) return false;
        text = std::move(candidate); return true;
    }, CurrentIniWriteLockWaitMs());
    if (!saved) {
        if (error.empty()) error = L"The tile configuration could not be saved; no partial change was applied.";
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Tiles", MB_OK | MB_ICONWARNING); return false;
    }
    WakePollThread(); return true;
}

static bool SecondaryTilePinUiAvailable()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return false;
#else
    return CurrentProcessHasPackageIdentity();
#endif
}

static void AppendSecondaryTilesMenu(HMENU menu)
{
    const bool canPin = SecondaryTilePinUiAvailable();
    bool master = false; aip::content::ParseBool(IniReadS(L"Tiles", L"Enabled", L"0"), master);
    aip::AppendTrayMenuItem(menu, ID_SECONDARY_TILES_ENABLED, L"Update source tiles from this app", master, true);
    aip::AppendTrayMenuItem(menu, ID_SECONDARY_TILES_INITIALIZE, L"Set up and enable the six source tiles...", false, true);
    aip::AppendTrayMenuItem(menu, ID_SECONDARY_TILES_STATUS, L"Tile status...", false, true);
    if (!canPin) aip::AppendTrayMenuItem(menu, 0, L"Pinning requires the registered app; setup remains available", false, false);
    aip::AppendMenuSeparator(menu);
    const int count = IniReadClampedI(L"Content", L"Count", 1, 1, aip::content::MaxEntries);
    for (UINT index = 0; index < aip::content::SecondaryTileCount; ++index) {
        const auto& descriptor = aip::content::SecondaryTiles[index];
        const auto section = aip::content::SecondaryTileSection(descriptor.id);
        bool enabled = true; aip::content::ParseBool(IniReadS(section.c_str(), L"Enabled", L"1"), enabled);
        const auto name = IniReadS(section.c_str(), L"Name", descriptor.name);
        const bool pinned = canPin && SecondaryTileExists(SecondaryTileApiId(descriptor.id));
        const UINT base = ID_SECONDARY_TILE_COMMANDS + index * SECONDARY_TILE_COMMAND_STRIDE;
        HMENU child = aip::BeginTrayNestedMenu(menu);
        aip::AppendTrayMenuItem(child, 0, canPin ? (pinned ? L"Pinned to Start" : L"Not pinned to Start") : L"Pin status unavailable without app registration", false, false);
        aip::AppendTrayMenuItem(child, base, L"Update this tile", enabled, true);
        aip::AppendTrayMenuItem(child, base + 1, L"Pin/update tile...", false, canPin);
        aip::AppendTrayMenuItem(child, base + 2, L"Unpin from Start...", false, canPin && pinned);
        aip::AppendTrayMenuItem(child, base + 3, L"Status...", false, true);
        aip::AppendTrayMenuItem(child, base + 4, L"Source and sign-in settings...", false, true);
        aip::AppendTrayMenuItem(child, base + 5, L"Rename...", false, true);
        HMENU mappings = aip::BeginTrayNestedMenu(child);
        const int mapping = IniReadClampedI(section.c_str(), L"Content", descriptor.content, 1, aip::content::MaxEntries);
        for (int slot = 1; slot <= count; ++slot) {
            const auto entry = L"Content." + std::to_wstring(slot);
            aip::AppendTrayMenuItem(mappings, base + 16 + slot - 1,
                std::to_wstring(slot) + L". " + ContentMenuValue(IniReadS(entry.c_str(), L"Name", L"Content")), slot == mapping, true);
        }
        aip::EndTrayNestedMenu(child, mappings, L"Content shown by this tile");
        aip::EndTrayNestedMenu(menu, child, ContentMenuValue(name));
    }
}

static bool EditSecondaryNotesAccount(HWND owner, int resource)
{
    const auto section = L"Notes." + std::wstring(aip::content::NotesResources[resource].value);
    std::wstring uid = IniReadS(section.c_str(), L"UID", L""), token, user;
    if (ShowTextInputDialog(owner, L"Game account", L"Game UID (shown in the game profile)", uid, true) != IDOK ||
        ShowTextInputDialog(owner, L"Game account", L"HoYoLAB login token: both blank fields keep saved credentials; new values are stored in this INI", token, true, true) != IDOK ||
        ShowTextInputDialog(owner, L"Game account", L"HoYoLAB account identifier: provide both login fields when replacing credentials", user, true, true) != IDOK) return false;
    if (uid.empty() || token.empty() != user.empty()) {
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Enter a game UID and either both replacement login fields or leave both blank to keep saved credentials.", L"Game account", MB_OK | MB_ICONWARNING); return false;
    }
    std::vector<aip::IniSetting> changes{{section, L"UID", uid}};
    if (!token.empty()) {
        changes = aip::notes::ClearAccountSettings(resource);
        changes.push_back({section, L"UID", uid}); changes.push_back({section, L"LToken", token}); changes.push_back({section, L"LTUid", user});
    }
    if (!SaveSecondaryTileMenuSettings(owner, changes)) return false;
    RequestContentNotesRefresh(resource); return true;
}

// Explicit activation/setup route. No browser automation or cookie extraction.
static bool OpenSecondaryTileSetup(HWND owner, const std::wstring& key)
{
    const auto* descriptor = aip::content::FindSecondaryTileDescriptor(key);
    if (!descriptor) return false;
    HMENU menu = CreatePopupMenu(); if (!menu) return false;
    if (std::wstring(descriptor->source) == L"Notes") {
        aip::AppendTrayMenuItem(menu, ID_SECONDARY_SOURCE_SIGN_IN, L"Sign in on the official HoYoLAB website...", false, true);
        aip::AppendTrayMenuItem(menu, ID_SECONDARY_SOURCE_ACCOUNT, L"Set up this game account...", false, true);
        aip::AppendTrayMenuItem(menu, ID_NOTES_SETTINGS + 64 + descriptor->notesResource, L"Import saved login cookie JSON...", false, true);
        for (UINT field = 0; field < _countof(g_notesAccountKeys); ++field)
            aip::AppendTrayMenuItem(menu, ID_NOTES_SETTINGS + 16 + descriptor->notesResource * 8 + field, g_notesAccountKeys[field], false, true);
        aip::AppendTrayMenuItem(menu, ID_NOTES_SETTINGS + 16 + descriptor->notesResource * 8 + 6, L"Refresh this game", false, true);
        aip::AppendTrayMenuItem(menu, ID_NOTES_SETTINGS + 16 + descriptor->notesResource * 8 + 7, L"Clear this account", false, true);
    } else if (std::wstring(descriptor->id) == L"rss") {
        aip::AppendTrayMenuItem(menu, ID_RSS_FEED_URL, L"Set feed URL...", false, true);
        aip::AppendTrayMenuItem(menu, ID_RSS_FEED_UPDATE_INTERVAL, L"Refresh interval", false, true);
        aip::AppendTrayMenuItem(menu, ID_RSS_FEED_MAX_ITEMS, L"Number of headlines", false, true);
    } else if (std::wstring(descriptor->id) == L"discord") {
        for (UINT option = 0; option < _countof(g_discordOptions); ++option)
            aip::AppendTrayMenuItem(menu, ID_DISCORD_SETTINGS + option, g_discordOptions[option].label, false, true);
        aip::AppendTrayMenuItem(menu, ID_DISCORD_SETTINGS + 80, L"Create an advanced profile...", false, true);
        HMENU advanced = aip::BeginTrayNestedMenu(menu);
        const auto options = aip::discord::ProfileOptions();
        for (UINT option = 0; option < options.size(); ++option)
            aip::AppendTrayMenuItem(advanced, ID_DISCORD_PROFILE_OPTION + option, options[option].label, false, true);
        aip::EndTrayNestedMenu(menu, advanced, L"Advanced profile options");
    } else {
        aip::AppendTrayMenuItem(menu, 0, L"Now playing uses Windows' current media session", false, false);
        aip::AppendTrayMenuItem(menu, ID_SECONDARY_TILES_STATUS, L"Tile status and setup information...", false, true);
    }
    POINT point{}; GetCursorPos(&point); if (owner) SetForegroundWindow(owner);
    const UINT command = static_cast<UINT>(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, owner, nullptr));
    DestroyMenu(menu); if (owner) AfterTrayPopupClosed(owner);
    if (command == ID_SECONDARY_SOURCE_SIGN_IN) {
        const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(owner, L"open", L"https://www.hoyolab.com/", nullptr, nullptr, SW_SHOWNORMAL));
        if (result <= 32) DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Open https://www.hoyolab.com/ in your browser to sign in. Then return here to import or enter login details; no browser credentials are read automatically.", L"Game sign-in", MB_OK | MB_ICONINFORMATION);
    } else if (command == ID_SECONDARY_SOURCE_ACCOUNT) EditSecondaryNotesAccount(owner, descriptor->notesResource);
    else if (command) DispatchTrayCommand(owner, command);
    return true;
}

static bool DispatchSecondaryTilesCommand(HWND owner, UINT command)
{
    if (command == ID_SECONDARY_TILES_INITIALIZE) {
        if (DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, L"Enable the six source tiles and keep the primary tile on Content 1? Missing content fields are added; existing layout fields and saved login details are kept. Source updates are enabled and cycling is turned off.", L"Source tiles", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES)
            SaveSecondaryTileMenuSettings(owner, {}, true, true);
        return true;
    }
    if (command == ID_SECONDARY_TILES_ENABLED) {
        bool enabled = false; aip::content::ParseBool(IniReadS(L"Tiles", L"Enabled", L"0"), enabled);
        SaveSecondaryTileMenuSettings(owner, {{L"Tiles", L"Enabled", enabled ? L"0" : L"1"}}, !enabled, !enabled); return true;
    }
    if (command == ID_SECONDARY_TILES_STATUS) {
        std::wstring status = L"One resident process updates the primary tile and six named source tiles. Pinning still requires Windows confirmation. Missing source sign-in or a feed URL can be configured from each tile's Source and sign-in settings.\r\n";
        const bool available = SecondaryTilePinUiAvailable();
        const auto snapshots = CurrentSecondarySnapshots();
        for (const auto& tile : aip::content::SecondaryTiles) {
            status += std::wstring(tile.name) + L": " + (available ? (SecondaryTileExists(SecondaryTileApiId(tile.id)) ? L"pinned" : L"not pinned") : L"pin status unavailable") + L"\r\n";
            const auto frame = std::find_if(snapshots.begin(), snapshots.end(), [&](const auto& snapshot) { return snapshot.secondaryTileId == SecondaryTileApiId(tile.id); });
            status += frame == snapshots.end() ? L"No prepared source snapshot; enable updates and complete source setup.\r\n" :
                frame->text.primary + L"\r\n" + frame->text.secondary + L"\r\n";
        }
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, status.c_str(), L"Tiles", MB_OK | MB_ICONINFORMATION); return true;
    }
    if (command < ID_SECONDARY_TILE_COMMANDS || command >= ID_SECONDARY_TILE_COMMANDS + aip::content::SecondaryTileCount * SECONDARY_TILE_COMMAND_STRIDE) return false;
    const UINT index = (command - ID_SECONDARY_TILE_COMMANDS) / SECONDARY_TILE_COMMAND_STRIDE;
    const UINT action = (command - ID_SECONDARY_TILE_COMMANDS) % SECONDARY_TILE_COMMAND_STRIDE;
    const auto& descriptor = aip::content::SecondaryTiles[index];
    const auto section = aip::content::SecondaryTileSection(descriptor.id);
    if (action == 0) {
        bool enabled = true; aip::content::ParseBool(IniReadS(section.c_str(), L"Enabled", L"1"), enabled);
        SaveSecondaryTileMenuSettings(owner, {{section, L"Enabled", enabled ? L"0" : L"1"}}, !enabled); return true;
    }
    if (action == 1) {
        bool mode = false, cycling = false, enabled = true;
        aip::content::ParseBool(IniReadS(L"Tiles", L"Enabled", L"0"), mode);
        aip::content::ParseBool(IniReadS(L"Content", L"CycleEnabled", L"0"), cycling);
        aip::content::ParseBool(IniReadS(section.c_str(), L"Enabled", L"1"), enabled);
        if ((!mode || cycling || !enabled) && DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,
            L"Pin/update enables source tile updates and keeps the primary tile on Content 1. Cycling is turned off; existing layout fields and login details are kept. Continue?",
            L"Source tile mode", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) != IDYES) return true;
        if (SaveSecondaryTileMenuSettings(owner, {{section, L"Enabled", L"1"}}, true, true))
            RequestPinSecondaryTile(owner, SecondaryTileApiId(descriptor.id), IniReadS(section.c_str(), L"Name", descriptor.name));
        return true;
    }
    if (action == 2) { RequestUnpinSecondaryTile(owner, SecondaryTileApiId(descriptor.id)); return true; }
    if (action == 3) { return DispatchSecondaryTilesCommand(owner, ID_SECONDARY_TILES_STATUS); }
    if (action == 4) return OpenSecondaryTileSetup(owner, descriptor.id);
    if (action == 5) {
        auto name = IniReadS(section.c_str(), L"Name", descriptor.name);
        if (ShowTextInputDialog(owner, L"Tile name", L"Name shown for this source tile", name, true) == IDOK)
            SaveSecondaryTileMenuSettings(owner, {{section, L"Name", name}});
        return true;
    }
    if (action >= 16 && action < 16 + aip::content::MaxEntries) {
        const auto content = std::to_wstring(action - 16 + 1);
        SaveSecondaryTileMenuSettings(owner, {{section, L"Content", content}}); return true;
    }
    return false;
}

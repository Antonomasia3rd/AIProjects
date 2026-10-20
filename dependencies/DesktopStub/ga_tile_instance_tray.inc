// Tile-specific configuration and on-demand native menus. No provider fetch,
// photo scan or hardware discovery runs while these menus are constructed.
static constexpr UINT ID_TILE_INSTANCE_FIRST = 60000, ID_TILE_INSTANCE_LAST = 63999;
static constexpr std::size_t TileInstanceMenuPageSize = 16;
enum class TileInstanceMenuVerb { Set, Edit, Toggle, Pin, Unpin, Inventory, Status, Overview, Refresh, Remove, Add,
    Page, SignIn, ChooseAccount, AddGallery };
struct TileInstanceMenuCommand {
    TileInstanceMenuVerb verb;
    std::wstring id, key, value, prompt;
    bool secret = false;
};
static std::map<UINT, TileInstanceMenuCommand> g_tileInstanceMenuCommands;
static UINT g_nextTileInstanceMenuCommand = ID_TILE_INSTANCE_FIRST;
static std::size_t g_tileInstanceMenuPage = 0;
static bool DispatchTileInstanceCommand(HWND owner, UINT command);
static bool OpenTileInstanceManager(HWND owner);
static std::wstring TileInstanceUi(const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); }
static void ResetTileInstanceMenuCommands()
{
    g_tileInstanceMenuCommands.clear(); g_nextTileInstanceMenuCommand = ID_TILE_INSTANCE_FIRST;
}
static UINT RegisterTileInstanceCommand(TileInstanceMenuCommand item)
{
    if (g_nextTileInstanceMenuCommand > ID_TILE_INSTANCE_LAST) return 0;
    const auto id = g_nextTileInstanceMenuCommand++;
    g_tileInstanceMenuCommands.emplace(id, std::move(item)); return id;
}
static void AppendTileInstanceCommand(HMENU menu, const std::wstring& label, TileInstanceMenuCommand item,
    bool checked = false, bool enabled = true)
{
    const auto command = enabled ? RegisterTileInstanceCommand(std::move(item)) : 0;
    aip::AppendTrayMenuItem(menu, command, label, checked, enabled && command != 0);
}
static bool ReadTileInstanceMenuConfiguration(aip::content::TileInstanceConfiguration& cfg, std::wstring& error)
{
    if (ValidateTileInstanceHostConfiguration(IniReadS,cfg,error)) return true;
    // Preserve a settings route for invalid instances. The provider engine uses
    // only validated snapshots; this fallback is metadata for editing, never an
    // instruction to run the default type or perform a provider request.
    const auto validationError = error;
    std::vector<std::wstring> ids;
    if (!aip::content::ParseTileInstanceIds(IniReadS(L"Tiles",L"Instances",L""),ids,error)) return false;
    aip::content::TileInstanceConfiguration candidate; candidate.configured = true;
    for (const auto& id : ids) {
        aip::content::TileInstanceConfiguration single; std::wstring tileError;
        if (ValidateTileInstanceHostConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback) {
            if (IEquals(section,L"Tiles") && IEquals(key,L"Instances")) return id;
            return IniReadS(section,key,fallback);
        },single,tileError) && single.tiles.size() == 1) {
            candidate.tiles.push_back(std::move(single.tiles.front())); continue;
        }
        aip::content::TileInstance tile; tile.id = id;
        const auto section = aip::content::TileInstanceSection(id);
        const auto* descriptor = aip::content::FindTileInstanceType(IniReadS(section.c_str(),L"Type",L""));
        if (descriptor) tile.type = descriptor->type;
        tile.name = IniReadS(section.c_str(),L"Name",descriptor ? descriptor->name : id.c_str());
        tile.accountReference = IniReadS(section.c_str(),L"Account",L"");
        aip::content::ParseBool(IniReadS(section.c_str(),L"Primary",L"0"),tile.primary);
        tile.error = tileError.empty() ? validationError : tileError;
        tile.configuration.entries.emplace_back(); tile.configuration.entries.front().background = L"Transparent";
        candidate.tiles.push_back(std::move(tile));
    }
    cfg = std::move(candidate); error = validationError; return true;
}
static bool SaveTileInstanceSettings(HWND owner, const std::vector<aip::IniSetting>& settings,
    const std::wstring& expectedId = L"", const std::wstring& removedId = L"")
{
    std::wstring error;
    const bool saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
        auto candidate = text;
        const auto original = aip::ParseIniDocument(candidate);
        const auto readOriginal = [&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(original, section, key, value) ? value : std::wstring(fallback);
        };
        aip::content::TileInstanceConfiguration current;
        // Original provider values may be invalid: the purpose of this mutation
        // can be to repair one of them. Validate membership now and the complete
        // staged configuration after applying the proposed correction.
        std::vector<std::wstring> currentIds;
        if (!aip::content::ParseTileInstanceIds(readOriginal(L"Tiles",L"Instances",L""),currentIds,error)) return false;
        current.configured = true;
        for (const auto& id : currentIds) {
            aip::content::TileInstance tile; tile.id = id;
            const auto section = aip::content::TileInstanceSection(id);
            aip::content::ParseBool(readOriginal(section.c_str(),L"Primary",L"0"),tile.primary);
            current.tiles.push_back(std::move(tile));
        }
        if (!expectedId.empty() && std::none_of(current.tiles.begin(), current.tiles.end(), [&](const auto& tile) { return tile.id == expectedId; })) {
            error = L"This tile was removed while its menu was open. No setting was changed."; return false;
        }
        if (!removedId.empty()) {
            const auto tile = std::find_if(current.tiles.begin(), current.tiles.end(), [&](const auto& item) { return item.id == removedId; });
            if (tile == current.tiles.end()) { error = L"This tile no longer exists."; return false; }
            const auto nativeId = aip::content::TileInstanceNativeId(*tile);
            if (!nativeId.empty() && (!SecondaryTileInventoryReady() || SecondaryTileExists(nativeId))) {
                error = L"Unpin this tile from Start before removing its settings. Check pin status if it is unknown."; return false;
            }
            std::wstring ids;
            for (const auto& item : current.tiles) if (item.id != removedId) { if (!ids.empty()) ids += L","; ids += item.id; }
            if (!aip::WriteIniValueToText(candidate, L"Tiles", L"Instances", ids)) return false;
        }
        for (auto setting : settings) {
            if (aip::content::Lower(setting.section) == L"tiles" && aip::content::Lower(setting.key) == L"instances") {
                std::vector<std::wstring> proposed;
                if (!aip::content::ParseTileInstanceIds(setting.value,proposed,error)) return false;
                for (const auto& id : currentIds) if (std::find(proposed.begin(),proposed.end(),id) == proposed.end()) {
                    error = L"The tile list changed while this menu was open. Reopen Add tile and try again; no tile was removed."; return false;
                }
            }
            if (!aip::content::NormalizeTileInstanceSetting(setting.section, setting.key, setting.value, error) ||
                !WriteContentProfileSetting(candidate, setting)) {
                if (error.empty()) error = L"The tile setting cannot be stored in this profile.";
                return false;
            }
        }
        const auto staged = aip::ParseIniDocument(candidate);
        aip::content::TileInstanceConfiguration validated;
        if (!ValidateTileInstanceHostConfiguration([&](const wchar_t* section, const wchar_t* key, const wchar_t* fallback) {
            std::wstring value; return aip::ReadIniValueFromDoc(staged, section, key, value) ? value : std::wstring(fallback);
        }, validated, error)) return false;
        text = std::move(candidate); return true;
    }, CurrentIniWriteLockWaitMs());
    if (!saved) {
        if (error.empty()) error = L"The tile configuration could not be saved. The previous configuration was kept.";
        DESKTOPSTUB_CONTENT_MESSAGEBOX(owner, error.c_str(), L"Tile settings", MB_OK | MB_ICONWARNING); return false;
    }
    if (std::any_of(settings.begin(), settings.end(), [](const auto& setting) {
        const auto key = aip::content::Lower(setting.key);
        return key == L"name" || key == L"type" || key == L"background" || key == L"branding" || key == L"backgroundcolor";
    })) RequestRefreshExistingSecondaryTileLogos(g_hwnd, true);
    WakePollThread(); return true;
}
static void AppendTileInstanceEditable(HMENU menu, const aip::content::TileInstance& tile,
    const wchar_t* key, const wchar_t* label, const wchar_t* fallback, bool toggle = false, bool secret = false)
{
    const auto section = aip::content::TileInstanceSection(tile.id);
    const auto value = IniReadS(section.c_str(), key, fallback);
    bool checked = false; if (toggle) aip::content::ParseBool(value, checked);
    auto caption = TileInstanceUi((L"TileOption_" + std::wstring(key)).c_str(), label);
    if (!toggle && !secret) caption += L": " + ContentMenuValue(value);
    if (secret) caption += value.empty() ? L": not configured" : L": configured";
    AppendTileInstanceCommand(menu, caption, {toggle ? TileInstanceMenuVerb::Toggle : TileInstanceMenuVerb::Edit,
        tile.id,key,value,label,secret}, checked);
}
static void AppendTileInstanceChoices(HMENU parent, const aip::content::TileInstance& tile,
    const wchar_t* key, const wchar_t* label, const std::vector<std::pair<std::wstring,std::wstring>>& choices, const wchar_t* fallback)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    const auto selected = IniReadS(aip::content::TileInstanceSection(tile.id).c_str(), key, fallback);
    for (const auto& choice : choices) AppendTileInstanceCommand(menu, choice.second,
        {TileInstanceMenuVerb::Set,tile.id,key,choice.first,L""}, aip::content::Lower(selected) == aip::content::Lower(choice.first));
    aip::EndTrayNestedMenu(parent, menu, TileInstanceUi((L"TileOption_" + std::wstring(key)).c_str(),label));
}
static void AppendTileInstanceAppearance(HMENU parent, const aip::content::TileInstance& tile)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    AppendTileInstanceChoices(menu,tile,L"Background",L"Tile background",{{L"Transparent",L"Transparent"},{L"Color",L"Color"}},L"Transparent");
    AppendTileInstanceEditable(menu,tile,L"BackgroundColor",L"Background color (#RRGGBB or Auto)",L"Auto");
    std::vector<std::pair<std::wstring,std::wstring>> branding;
    for (const auto* item : aip::content::TileBrandingChoices) branding.emplace_back(item,item);
    AppendTileInstanceChoices(menu,tile,L"Branding",L"Name and corner icon",branding,L"Name");
    aip::EndTrayNestedMenu(parent,menu,TileInstanceUi(L"TileInstanceAppearance",L"Appearance"));
}
static void AppendTileInstanceText(HMENU parent, const aip::content::TileInstance& tile)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    std::vector<std::pair<std::wstring,std::wstring>> presets{{L"Default",L"Type default"},{L"Custom",L"Custom text layout"}};
    if (tile.type == aip::content::TileInstanceType::Rss) presets.push_back({L"RssHeadlines",L"Headlines with summaries"});
    AppendTileInstanceChoices(menu,tile,L"TextPreset",L"Layout",presets,L"Default");
    const wchar_t* styleKeys[] = {L"TextStyle",L"TextSecondaryStyle",L"TextBadgeStyle"};
    const wchar_t* styleLabels[] = {L"Primary text size and weight",L"Secondary text size and weight",L"Other text size and weight"};
    const wchar_t* styleDefaults[] = {L"caption",L"captionSubtle",L"captionSubtle"};
    std::vector<std::pair<std::wstring,std::wstring>> styles;
    for (const auto* style : aip::content::TextPresentationStyles) styles.emplace_back(style,std::wstring(style) + L" (" + std::to_wstring(aip::content::TextStylePixels(style)) + L")");
    for (std::size_t i = 0; i < _countof(styleKeys); ++i) AppendTileInstanceChoices(menu,tile,styleKeys[i],styleLabels[i],styles,styleDefaults[i]);
    AppendTileInstanceEditable(menu,tile,L"TextWrap",L"Wrap text onto multiple lines",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"TextMaxLines",L"Primary text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextSecondaryMaxLines",L"Secondary text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextBadgeMaxLines",L"Other text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextGapLines",L"Blank lines between text fields (0-2)",L"0");
    AppendTileInstanceChoices(menu,tile,L"TextAlign",L"Text alignment",{{L"left",L"Left"},{L"center",L"Center"},{L"right",L"Right"}},L"left");
    AppendTileInstanceChoices(menu,tile,L"TextStacking",L"Text vertical position",{{L"top",L"Top"},{L"center",L"Center"},{L"bottom",L"Bottom"}},L"top");
    std::vector<std::pair<std::wstring,std::wstring>> delivery;
    for (const auto& choice : aip::content::TextModes) delivery.emplace_back(choice.value,choice.label);
    AppendTileInstanceChoices(menu,tile,L"TextMode",L"Text delivery",delivery,L"Auto");
    aip::EndTrayNestedMenu(parent,menu,TileInstanceUi(L"TileInstanceTextLayout",L"Text size, wrapping and layout"));
}
static void AppendTileInstancePrayerSettings(HMENU menu,const aip::content::TileInstance& tile)
{
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.City",L"City",L"");
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.Country",L"Country",L"");
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.State",L"State or region (optional)",L"");
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.TimeZone",L"Time zone override (blank uses schedule)",L"");
    std::vector<std::pair<std::wstring,std::wstring>> methods{{L"Auto",L"Auto (regional method)"}};
    for (const auto& method : g_prayerMethods) methods.emplace_back(std::to_wstring(method.id),method.label);
    AppendTileInstanceChoices(menu,tile,L"PrayerTimes.Method",L"Calculation method",methods,L"Auto");
    AppendTileInstanceChoices(menu,tile,L"PrayerTimes.School",L"Asr calculation",{{L"0",L"Standard (Shafi / Maliki / Hanbali)"},{L"1",L"Hanafi"}},L"0");
    AppendTileInstanceChoices(menu,tile,L"PrayerTimes.LatitudeAdjustmentMethod",L"High-latitude adjustment",
        {{L"1",L"Middle of the night"},{L"2",L"One seventh of the night"},{L"3",L"Angle based"}},L"3");
    HMENU adjustments = aip::BeginTrayNestedMenu(menu);
    for (const auto* key : g_prayerAdjustmentKeys) AppendTileInstanceEditable(adjustments,tile,(L"PrayerTimes."+std::wstring(key)).c_str(),key,L"0");
    aip::EndTrayNestedMenu(menu,adjustments,TileInstanceUi(L"PrayerAdjustments",L"Local minute adjustments"));
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.TwentyFourHour",L"Use 24-hour times",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.ShowSunrise",L"Show sunrise information",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.ShowTimetable",L"Show timetable on large tiles",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.RefreshSeconds",L"Schedule refresh interval (seconds)",L"43200");
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.TimeoutSeconds",L"HTTP timeout (seconds)",L"10");
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.MaximumBytes",L"Response limit (bytes)",L"262144");
}
static void AppendTileInstanceSourceSettings(HMENU menu, const aip::content::TileInstance& tile)
{
    using namespace aip::content;
    switch (tile.type) {
    case TileInstanceType::Wallpaper: {
        std::vector<std::pair<std::wstring,std::wstring>> backgrounds;
        for (const auto& background : Backgrounds) backgrounds.emplace_back(background.value,background.label);
        AppendTileInstanceChoices(menu,tile,L"Content.Background",L"Image source",backgrounds,L"Wallpaper");
        AppendTileInstanceEditable(menu,tile,L"Content.ImagePath",L"Image path (for Custom image)",L"");
        AppendTileInstanceEditable(menu,tile,L"RefreshSeconds",L"Wallpaper check interval (seconds)",L"2");
        AppendTileInstanceEditable(menu,tile,L"Content.Text",L"Primary text (optional)",L"");
        AppendTileInstanceEditable(menu,tile,L"Content.SecondaryText",L"Secondary text (optional)",L"");
        AppendTileInstanceEditable(menu,tile,L"Content.BadgeText",L"Other text (optional)",L"");
        break;
    }
    case TileInstanceType::Rss:
        AppendTileInstanceEditable(menu,tile,L"RssFeed.FeedUrl",L"RSS or Atom feed URL",L"");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.UpdateIntervalSeconds",L"Fetch new headlines every (seconds, 60-86400)",L"900");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.MaxItems",L"Headlines to keep (1-20)",L"5");
        AppendTileInstanceEditable(menu,tile,L"Content.RssItem",L"Featured headline number (1-20)",L"1");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.HttpTimeoutSeconds",L"HTTP timeout (seconds, 5-120)",L"20");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.MaxFeedBytes",L"Response limit (bytes)",L"2097152");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.UserAgent",L"HTTP user agent",L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)");
        break;
    case TileInstanceType::NowPlaying:
        aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TileInstanceSmtcExplanation",L"Uses Windows' current media session"),false,false);
        AppendTileInstanceChoices(menu,tile,L"SMTC.Indicator",L"Playback indicator",{{L"Status",L"Playback status"},{L"AppIcon",L"Source app icon"},{L"None",L"Hidden"}},L"Status");
        AppendTileInstanceEditable(menu,tile,L"SMTC.ShowRepeatShuffle",L"Show repeat and shuffle status",L"0",true);
        break;
    case TileInstanceType::PrayerTimes: AppendTileInstancePrayerSettings(menu,tile); break;
    case TileInstanceType::Genshin: case TileInstanceType::Hsr: case TileInstanceType::Zzz: {
        AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceSignIn",L"Sign in to HoYoLAB inside this app..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceChooseAccount",L"Choose saved account..."),{TileInstanceMenuVerb::ChooseAccount,tile.id,L"",L"",L""});
        AppendTileInstanceEditable(menu,tile,L"Account",L"Account reference (Notes.<resource> or Account.<id>)",L"");
        AppendTileInstanceEditable(menu,tile,L"Notes.UID",L"Game UID override (blank uses saved account)",L"");
        AppendTileInstanceEditable(menu,tile,L"Notes.Server",L"Game server override (blank auto-detects)",L"");
        AppendTileInstanceEditable(menu,tile,L"Notes.RefreshSeconds",L"Fetch game notes every (seconds)",L"300");
        AppendTileInstanceEditable(menu,tile,L"Notes.HttpTimeoutSeconds",L"HTTP timeout (seconds)",L"15");
        AppendTileInstanceEditable(menu,tile,L"Notes.MaximumBytes",L"Response limit (bytes)",L"1048576");
        std::vector<std::pair<std::wstring,std::wstring>> details;
        const auto resource = DescribeTileInstanceType(tile.type).notesResource;
        for (const auto& detail : aip::notes::Details) if (aip::notes::DetailAvailable(resource,detail.id)) details.emplace_back(detail.value,detail.label);
        AppendTileInstanceChoices(menu,tile,L"Content.NotesDetail",L"Featured game information",details,L"Summary");
        break;
    }
    case TileInstanceType::Discord:
        for (const auto& option : g_discordOptions) AppendTileInstanceEditable(menu,tile,(L"Discord."+std::wstring(option.key)).c_str(),option.label,option.fallback,option.toggle);
        break;
    case TileInstanceType::QuickAction: {
        std::vector<std::pair<std::wstring,std::wstring>> actions;
        for (const auto& action : TileActions) actions.emplace_back(action.id,action.label);
        AppendTileInstanceChoices(menu,tile,L"Action",L"Action",actions,L"toggle");
        AppendTileInstanceEditable(menu,tile,L"VolumeStepPercent",L"Volume step (percentage points, 1-100)",L"5");
        break;
    }
    case TileInstanceType::Gallery: break; // Existing ownership-aware gallery editor is used instead.
    }
}
static void AppendTileInstanceDetails(HMENU menu,const aip::content::TileInstance& tile)
{
    using namespace aip::content;
    const auto nativeId = TileInstanceNativeId(tile);
    if (!tile.primary) {
        const auto state = TilePinState(SecondaryTilePinUiAvailable(),SecondaryTileInventoryReady(),SecondaryTileExists(nativeId));
        if (state == TilePinMenuState::Pinned) AppendTileInstanceCommand(menu,TileInstanceUi(L"UnpinTile",L"Unpin from Start..."),{TileInstanceMenuVerb::Unpin,tile.id,L"",L"",L""});
        else if (state == TilePinMenuState::Unpinned) AppendTileInstanceCommand(menu,TileInstanceUi(L"PinTile",L"Pin tile..."),{TileInstanceMenuVerb::Pin,tile.id,L"",L"",L""});
        else if (state == TilePinMenuState::Unknown) AppendTileInstanceCommand(menu,TileInstanceUi(L"CheckTilePinStatus",L"Check pin status..."),{TileInstanceMenuVerb::Inventory,tile.id,L"",L"",L""});
        else aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TilePinStatusUnavailable",L"Pin status requires the registered app"),false,false);
    }
    AppendTileInstanceCommand(menu,TileInstanceUi(L"SelectedTileContents",L"Tile contents..."),{TileInstanceMenuVerb::Status,tile.id,L"",L"",L""});
    AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceRefresh",L"Refresh this tile now"),{TileInstanceMenuVerb::Refresh,tile.id,L"",L"",L""});
    AppendTileInstanceEditable(menu,tile,L"Name",L"Tile name",tile.name.c_str());
    HMENU source = aip::BeginTrayNestedMenu(menu); AppendTileInstanceSourceSettings(source,tile);
    aip::EndTrayNestedMenu(menu,source,TileInstanceUi(L"TileInstanceSourceSettings",L"Source settings"));
    if (!tile.error.empty()) {
        aip::AppendTrayMenuItem(menu,0,ContentMenuValue(tile.error),false,false);
        std::vector<std::pair<std::wstring,std::wstring>> types;
        for (const auto& descriptor : TileInstanceTypes) if (descriptor.type != TileInstanceType::Gallery) types.emplace_back(descriptor.value,descriptor.name);
        AppendTileInstanceChoices(menu,tile,L"Type",L"Repair tile type",types,DescribeTileInstanceType(tile.type).value);
    }
    AppendTileInstanceAppearance(menu,tile); AppendTileInstanceText(menu,tile);
    if (tile.type != TileInstanceType::QuickAction) {
        std::vector<std::pair<std::wstring,std::wstring>> actions{{L"setup",L"Open this tile's settings"}};
        if (tile.type == TileInstanceType::Rss) actions.push_back({L"source",L"Open this tile's featured article"});
        for (const auto& action : TileActions) actions.emplace_back(action.id,action.label);
        AppendTileInstanceChoices(menu,tile,L"ClickAction",L"Click action",actions,tile.type == TileInstanceType::NowPlaying ? L"toggle" : tile.type == TileInstanceType::Rss ? L"source" : L"setup");
    }
    const bool removable = tile.primary || (SecondaryTileInventoryReady() && !SecondaryTileExists(nativeId));
    AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceRemove",L"Remove tile settings (unpin first)"),{TileInstanceMenuVerb::Remove,tile.id,L"",L"",L""},false,removable);
}
static void AppendTileInstancesMenu(HMENU menu)
{
    using namespace aip::content;
    ResetTileInstanceMenuCommands();
    HMENU add = aip::BeginTrayNestedMenu(menu);
    for (const auto& descriptor : TileInstanceTypes) AppendTileInstanceCommand(add,descriptor.name,
        {descriptor.type == TileInstanceType::Gallery ? TileInstanceMenuVerb::AddGallery : TileInstanceMenuVerb::Add,L"",L"",descriptor.value,L""});
    aip::EndTrayNestedMenu(menu,add,TileInstanceUi(L"TileInstanceAdd",L"Add tile..."));
    AppendTileInstanceCommand(menu,TileInstanceUi(L"AllTileContents",L"All tile contents..."),{TileInstanceMenuVerb::Overview,L"",L"",L"",L""});
    TileInstanceConfiguration cfg; std::wstring error;
    if (!ReadTileInstanceMenuConfiguration(cfg,error)) { aip::AppendTrayMenuItem(menu,0,ContentMenuValue(error),false,false); return; }
    const auto pages = (std::max)(std::size_t(1),(cfg.tiles.size()+TileInstanceMenuPageSize-1)/TileInstanceMenuPageSize);
    if (g_tileInstanceMenuPage >= pages) g_tileInstanceMenuPage = pages-1;
    if (pages > 1) {
        aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TileInstancePage",L"Tiles page")+L" "+std::to_wstring(g_tileInstanceMenuPage+1)+L" / "+std::to_wstring(pages),false,false);
        if (g_tileInstanceMenuPage) AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstancePreviousPage",L"Previous tiles..."),
            {TileInstanceMenuVerb::Page,L"",L"",std::to_wstring(g_tileInstanceMenuPage-1),L""});
        if (g_tileInstanceMenuPage+1 < pages) AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceNextPage",L"Next tiles..."),
            {TileInstanceMenuVerb::Page,L"",L"",std::to_wstring(g_tileInstanceMenuPage+1),L""});
    }
    const auto end = (std::min)(cfg.tiles.size(),(g_tileInstanceMenuPage+1)*TileInstanceMenuPageSize);
    for (std::size_t index = g_tileInstanceMenuPage*TileInstanceMenuPageSize; index < end; ++index) {
        const auto& tile = cfg.tiles[index]; HMENU child = aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceDetails(child,tile); aip::EndTrayNestedMenu(menu,child,ContentMenuValue(tile.name));
    }
}
static void AppendTileInstancePluginsMenu(HMENU menu)
{
    const auto append = [](HMENU target,UINT id,const wchar_t* section,const ContentTrayOption& option) {
        const auto value = IniReadS(section,option.key,option.fallback); bool checked = false;
        aip::content::ParseBool(value,checked);
        auto label = std::wstring(option.label); if (!option.toggle) label += L": "+ContentMenuValue(value);
        aip::AppendTrayMenuItem(target,id,label,option.toggle&&checked,true);
    };
    HMENU caps = aip::BeginTrayNestedMenu(menu);
    for (UINT option = 0; option < _countof(g_capsOptions); ++option) append(caps,ID_CAPS_SETTINGS+option,L"CapsBlink",g_capsOptions[option]);
    aip::AppendTrayMenuItem(caps,ID_CAPS_SETTINGS+30,L"Restart indicator pattern",false,true);
    aip::AppendTrayMenuItem(caps,ID_CAPS_SETTINGS+31,L"Import CapsBlink INI...",false,true);
    aip::EndTrayNestedMenu(menu,caps,TileInstanceUi(L"TileInstanceCapsPlugin",L"CapsBlink"));
    HMENU asus = aip::BeginTrayNestedMenu(menu);
    for (UINT option = 0; option < _countof(g_asusOptions); ++option) append(asus,ID_ASUS_SETTINGS+option,L"AsusBlink",g_asusOptions[option]);
    aip::AppendTrayMenuItem(asus,ID_ASUS_SETTINGS+30,L"Restart indicator patterns",false,true);
    aip::AppendTrayMenuItem(asus,ID_ASUS_SETTINGS+31,L"Import ASUS indicator INI...",false,true);
    aip::AppendTrayMenuItem(asus,ID_ASUS_SETTINGS+32,L"Edit numbered event setting...",false,true);
    aip::EndTrayNestedMenu(menu,asus,TileInstanceUi(L"TileInstanceAsusPlugin",L"ASUS indicators"));
}
// Windows delivery is an application-wide transport choice. Instance source,
// update and appearance settings remain on the individual tile menus.
static void AppendTileInstanceDeliveryMenu(HMENU parent)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    HMENU mode = aip::BeginTrayNestedMenu(menu);
    const auto selectedMode = ConfiguredLiveTileUpdateMode();
    aip::AppendTrayMenuItem(mode,ID_LIVE_TILE_MODE_AUTO,g_ui.liveTileModeAuto,selectedMode == LiveTileUpdateMode::Auto,true);
    aip::AppendTrayMenuItem(mode,ID_LIVE_TILE_MODE_LIVE_TILE,g_ui.liveTileModeEnabled,selectedMode == LiveTileUpdateMode::LiveTile,true);
    aip::AppendTrayMenuItem(mode,ID_LIVE_TILE_MODE_REGISTRATION,g_ui.liveTileModeDisabled,selectedMode == LiveTileUpdateMode::Registration,true);
    aip::EndTrayNestedMenu(menu,mode,g_ui.liveTileModeTitle);
    HMENU templates = aip::BeginTrayNestedMenu(menu);
    const auto selectedTemplate = ConfiguredLiveTileTemplateStyle();
    const bool templatesAvailable = ConfiguredManifestCompatibilityTarget() == ManifestCompatibilityTarget::Windows10;
    aip::AppendTrayMenuItem(templates,ID_LIVE_TILE_TEMPLATE_ADAPTIVE,g_ui.liveTileTemplateAdaptive,selectedTemplate == LiveTileTemplateStyle::Adaptive,templatesAvailable);
    aip::AppendTrayMenuItem(templates,ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET,g_ui.liveTileTemplateWindows81Preset,selectedTemplate == LiveTileTemplateStyle::Windows81Preset,templatesAvailable);
    aip::EndTrayNestedMenu(menu,templates,g_ui.liveTileTemplateTitle);
    aip::AppendTrayMenuItem(menu,ID_CLEAR_LIVE_TILE_ON_SHUTDOWN,g_ui.clearLiveTileOnShutdown,g_clearLiveTileOnShutdown,true);
    aip::AppendTrayMenuItem(menu,ID_LIVE_TILE_RELAUNCH_ON_SWITCH,g_ui.liveTileRelaunchOnSwitch,g_liveTileRelaunchOnSwitch,true);
    aip::EndTrayNestedMenu(parent,menu,TileInstanceUi(L"TileInstanceDelivery",L"Windows tile delivery"));
}
static bool AddIndependentTile(HWND owner,const std::wstring& type)
{
    using namespace aip::content;
    const auto* descriptor = FindTileInstanceType(type); if (!descriptor || descriptor->type == TileInstanceType::Gallery) return false;
    TileInstanceConfiguration cfg; std::wstring error;
    if (!ReadTileInstanceMenuConfiguration(cfg,error)) return false;
    const auto fresh = NewGalleryIdentity();
    if (fresh.empty()) return false;
    const auto id = Lower(descriptor->value)+L"-"+fresh.substr(1,16), section = TileInstanceSection(id);
    std::wstring name = descriptor->name;
    if (ShowTextInputDialog(owner,TileInstanceUi(L"TileInstanceAdd",L"Add tile..."),TileInstanceUi(L"TileInstanceNamePrompt",L"Name for this tile"),name,true) != IDOK) return false;
    std::wstring ids; for (const auto& tile : cfg.tiles) { if (!ids.empty()) ids += L","; ids += tile.id; }
    if (!ids.empty()) ids += L","; ids += id;
    const bool primary = descriptor->type == TileInstanceType::Wallpaper && std::none_of(cfg.tiles.begin(),cfg.tiles.end(),[](const auto& tile) { return tile.primary; });
    std::vector<aip::IniSetting> settings{{section,L"Type",descriptor->value},{section,L"Name",name},{section,L"Primary",primary?L"1":L"0"},{L"Tiles",L"Instances",ids}};
    if (!SaveTileInstanceSettings(owner,settings)) return false;
    // Configuration stays useful before pinning. Native pin confirmations are
    // always initiated explicitly by the user from the new tile's menu.
    return true;
}
static bool ChooseTileInstanceAccount(HWND owner,const aip::content::TileInstance& tile)
{
    const auto resource = aip::content::DescribeTileInstanceType(tile.type).notesResource; if (resource < 0) return false;
    HMENU menu = CreatePopupMenu(); if (!menu) return false;
    std::vector<std::wstring> references{L"Notes."+std::wstring(aip::content::NotesResources[resource].value)};
    std::vector<aip::IniSectionData> document; aip::LoadIniDocument(g_iniPath,document);
    for (const auto& section : document) if (aip::content::Lower(section.name).compare(0,8,L"account.") == 0 &&
        aip::content::ValidTileAccountReference(section.name)) references.push_back(section.name);
    for (std::size_t index = 0; index < references.size(); ++index) aip::AppendTrayMenuItem(menu,static_cast<UINT>(index+1),references[index],references[index] == tile.accountReference,true);
    POINT point{}; GetCursorPos(&point); if (owner) SetForegroundWindow(owner);
    const auto selected = static_cast<std::size_t>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,nullptr));
    DestroyMenu(menu); if (owner) AfterTrayPopupClosed(owner);
    if (!selected || selected > references.size()) return false;
    return SaveTileInstanceSettings(owner,{{aip::content::TileInstanceSection(tile.id),L"Account",references[selected-1]}},tile.id);
}
static bool OpenTileInstanceSetup(HWND owner,const std::wstring& id)
{
    aip::content::TileInstanceConfiguration cfg; std::wstring error;
    if (!ReadTileInstanceMenuConfiguration(cfg,error)) return false;
    const auto found = std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile) { return tile.id == id; });
    if (found == cfg.tiles.end()) return false;
    HMENU menu = CreatePopupMenu(); if (!menu) return false;
    ResetTileInstanceMenuCommands(); AppendTileInstanceDetails(menu,*found);
    POINT point{}; GetCursorPos(&point); if (owner) SetForegroundWindow(owner);
    const auto command = static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,nullptr));
    DestroyMenu(menu); if (owner) AfterTrayPopupClosed(owner);
    if (command) DispatchTileInstanceCommand(owner,command); return true;
}
static bool OpenTileInstanceManager(HWND owner)
{
    for (;;) {
        HMENU menu = CreatePopupMenu(); if (!menu) return false;
        AppendTileInstancesMenu(menu);
        POINT point{}; GetCursorPos(&point); if (owner) SetForegroundWindow(owner);
        const auto command = static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,nullptr));
        DestroyMenu(menu); if (owner) AfterTrayPopupClosed(owner);
        const auto found = g_tileInstanceMenuCommands.find(command);
        if (found == g_tileInstanceMenuCommands.end()) return true;
        if (found->second.verb == TileInstanceMenuVerb::Page) { g_tileInstanceMenuPage = static_cast<std::size_t>(_wtoi(found->second.value.c_str())); continue; }
        DispatchTileInstanceCommand(owner,command); return true;
    }
}
static bool DispatchTileInstanceCommand(HWND owner,UINT command)
{
    const auto found = g_tileInstanceMenuCommands.find(command); if (found == g_tileInstanceMenuCommands.end()) return false;
    const auto item = found->second; // Popup construction may replace the registry.
    using namespace aip::content;
    if (item.verb == TileInstanceMenuVerb::Page) { g_tileInstanceMenuPage = static_cast<std::size_t>(_wtoi(item.value.c_str())); OpenTileInstanceManager(owner); return true; }
    if (item.verb == TileInstanceMenuVerb::Add) { AddIndependentTile(owner,item.value); OpenTileInstanceManager(owner); return true; }
    if (item.verb == TileInstanceMenuVerb::AddGallery) {
        aip::gallery::Configuration gallery; std::wstring error;
        if (ReadGalleryConfiguration(gallery,error)) { if (gallery.groupId.empty()) BeginGallerySetup(owner); else AddGalleryFolder(owner,gallery); }
        return true;
    }
    if (item.verb == TileInstanceMenuVerb::Overview) { ShowPagedTextDialog(owner,L"All tile contents",BuildCurrentTileStatusPages()); return true; }
    if (item.verb == TileInstanceMenuVerb::Inventory) { RequestSecondaryTileList(owner); return true; }
    TileInstanceConfiguration cfg; std::wstring error;
    if (!ReadTileInstanceMenuConfiguration(cfg,error)) return true;
    const auto selected = std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& tile) { return tile.id == item.id; });
    if (selected == cfg.tiles.end()) return true;
    const auto tile = *selected;
    const auto section = TileInstanceSection(tile.id), nativeId = TileInstanceNativeId(tile);
    if (item.verb == TileInstanceMenuVerb::Pin) { RequestPinSecondaryTile(owner,nativeId,tile.name); return true; }
    if (item.verb == TileInstanceMenuVerb::Unpin) { RequestUnpinSecondaryTile(owner,nativeId); return true; }
    if (item.verb == TileInstanceMenuVerb::Status) { ShowSelectedTileStatus(owner,nativeId,tile.name); return true; }
    if (item.verb == TileInstanceMenuVerb::Refresh) { RefreshTileInstanceNow(tile.id); return true; }
    if (item.verb == TileInstanceMenuVerb::ChooseAccount) { ChooseTileInstanceAccount(owner,tile); return true; }
    if (item.verb == TileInstanceMenuVerb::SignIn) {
        const auto resource = DescribeTileInstanceType(tile.type).notesResource;
        if (resource >= 0) OpenEmbeddedNotesSignInForTile(owner,resource,tile.id);
        return true;
    }
    if (item.verb == TileInstanceMenuVerb::Remove) { SaveTileInstanceSettings(owner,{},tile.id,tile.id); return true; }
    auto value = item.value;
    if (item.verb == TileInstanceMenuVerb::Edit) {
        value = IniReadS(section.c_str(),item.key.c_str(),item.value.c_str());
        if (ShowTextInputDialog(owner,tile.name,item.prompt,value,true,item.secret) != IDOK) return true;
    } else if (item.verb == TileInstanceMenuVerb::Toggle) {
        bool enabled = false; ParseBool(IniReadS(section.c_str(),item.key.c_str(),item.value.c_str()),enabled); value = enabled ? L"0" : L"1";
    }
    std::vector<aip::IniSetting> settings{{section,item.key,value}};
    if (Lower(item.key) == L"textpreset" && value == L"RssHeadlines") {
        // Materialize the named layout so stale per-field custom settings from
        // the previous layout cannot make selecting it appear to do nothing.
        settings.push_back({section,L"TextMode",L"Auto"});
        settings.push_back({section,L"TextStyle",L"subtitle"});
        settings.push_back({section,L"TextSecondaryStyle",L"captionSubtle"});
        settings.push_back({section,L"TextBadgeStyle",L"captionSubtle"});
        settings.push_back({section,L"TextWrap",L"1"});
        settings.push_back({section,L"TextMaxLines",L"3"});
        settings.push_back({section,L"TextSecondaryMaxLines",L"2"});
        settings.push_back({section,L"TextBadgeMaxLines",L"1"});
        settings.push_back({section,L"TextGapLines",L"1"});
        settings.push_back({section,L"TextAlign",L"left"});
        settings.push_back({section,L"TextStacking",L"top"});
    }
    if (IsTextPresentationKey(item.key) && Lower(item.key) != L"textpreset")
        settings.push_back({section,L"TextPreset",tile.presentation.preset == L"RssHeadlines" ? L"RssHeadlines" : L"Custom"});
    SaveTileInstanceSettings(owner,settings,tile.id); return true;
}

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
    std::wstring sectionOverride;
};
static std::wstring g_tilePolicyMenuSection;
static std::map<UINT, TileInstanceMenuCommand> g_tileInstanceMenuCommands;
static UINT g_nextTileInstanceMenuCommand = ID_TILE_INSTANCE_FIRST;
static std::size_t g_tileInstanceMenuPage = 0;
static bool DispatchTileInstanceCommand(HWND owner, UINT command);
static bool OpenTileInstanceManager(HWND owner);
static bool OfferTileDeliveryMigration(HWND owner,const std::wstring& tileId,const std::wstring& newMode);
static bool OpenGoogleCalendarSetup(HWND owner,const std::wstring& tileId);
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testTileDeliveryMigration)(HWND,const std::wstring&,const std::wstring&)=nullptr;
#endif
static std::wstring TileInstanceUi(const wchar_t* key, const wchar_t* fallback) { return IniReadS(L"Strings", key, fallback); }
static void ResetTileInstanceMenuCommands()
{
    g_tileInstanceMenuCommands.clear(); g_nextTileInstanceMenuCommand = ID_TILE_INSTANCE_FIRST;
}
static UINT RegisterTileInstanceCommand(TileInstanceMenuCommand item)
{
    if (g_nextTileInstanceMenuCommand > ID_TILE_INSTANCE_LAST) return 0;
    const auto id = g_nextTileInstanceMenuCommand++;
    if(!g_tilePolicyMenuSection.empty())item.sectionOverride=g_tilePolicyMenuSection;
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
static bool TileInstanceUsesRegistration(const aip::content::TileInstance& tile)
{
    if(tile.policy.Mode()==L"Registration")return true;
    if(tile.policy.Mode()==L"LiveTile")return false;
    aip::content::TilePolicyReadScope scope(tile.policy);return !UseExperimentalLiveTileUpdate();
}
static bool SaveTileInstanceSettingsWithRemovalConfirmation(HWND owner,const std::vector<aip::IniSetting>& settings,
    const std::wstring& expectedId,const std::wstring& removedId,bool staticRemovalConfirmed,
    const std::function<bool(const std::wstring&,std::wstring&)>& confirmedGuard={})
{
    std::wstring error;
    const bool saved = MutateDesktopStubIniWithWait([&](std::wstring& text) {
        if(confirmedGuard&&!confirmedGuard(text,error))return false;
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
            aip::content::TileDeliveryPolicy removalPolicy;
            if(!aip::content::ReadTileDeliveryPolicy(readOriginal,aip::content::TileInstanceSection(tile->id),tile->id,removalPolicy,error))return false;
            tile->policy=std::move(removalPolicy);
            const bool registration=TileInstanceUsesRegistration(*tile);
            if(registration&&!staticRemovalConfirmed){error=L"Unpin the Registration tile manually before removing its settings. Its registered identity is kept.";return false;}
            if (!registration&&!nativeId.empty() && (!SecondaryTileInventoryReady() || SecondaryTileExists(nativeId))) {
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
static bool SaveConfirmedTileInstanceSettings(HWND owner,const std::vector<aip::IniSetting>& settings,const std::wstring& expectedId,
    const std::function<bool(const std::wstring&,std::wstring&)>& confirmedGuard)
{
    return SaveTileInstanceSettingsWithRemovalConfirmation(owner,settings,expectedId,L"",false,confirmedGuard);
}
static bool SaveTileInstanceSettings(HWND owner,const std::vector<aip::IniSetting>& settings,
    const std::wstring& expectedId=L"",const std::wstring& removedId=L"")
{
    return SaveTileInstanceSettingsWithRemovalConfirmation(owner,settings,expectedId,removedId,false);
}
static std::wstring TilePolicyMenuSection(const aip::content::TileInstance& tile)
{
    return g_tilePolicyMenuSection.empty()?aip::content::TileInstanceSection(tile.id):g_tilePolicyMenuSection;
}
static void AppendTileInstanceEditable(HMENU menu, const aip::content::TileInstance& tile,
    const wchar_t* key, const wchar_t* label, const wchar_t* fallback, bool toggle = false, bool secret = false)
{
    const auto section = TilePolicyMenuSection(tile);
    const auto value = IniReadS(section.c_str(), key, fallback);
    bool checked = false; if (toggle) aip::content::ParseBool(value, checked);
    auto caption = TileInstanceUi((L"TileOption_" + std::wstring(key)).c_str(), label);
    if (!toggle && !secret) caption += L": " + ContentMenuValue(value);
    if (secret) {
        const bool protectedToken=aip::content::Lower(key)==L"kuro.gametoken"&&!IniReadS(section.c_str(),L"Kuro.GameTokenProtected",L"").empty();
        caption += value.empty()&&!protectedToken ? L": not configured" : L": configured";
    }
    AppendTileInstanceCommand(menu, caption, {toggle ? TileInstanceMenuVerb::Toggle : TileInstanceMenuVerb::Edit,
        tile.id,key,value,label,secret}, checked);
}
static void AppendTileInstanceChoices(HMENU parent, const aip::content::TileInstance& tile,
    const wchar_t* key, const wchar_t* label, const std::vector<std::pair<std::wstring,std::wstring>>& choices, const wchar_t* fallback)
{
    HMENU menu = aip::BeginTrayNestedMenu(parent);
    auto selected = IniReadS(TilePolicyMenuSection(tile).c_str(), key, fallback);
    if(aip::content::FindTilePolicyField(key)){std::wstring ignored;aip::content::NormalizeTilePolicySetting(key,selected,ignored);}
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
    std::vector<std::pair<std::wstring,std::wstring>> presets{{L"Default",tile.type==aip::content::TileInstanceType::Rss?L"Readable headlines (recommended)":L"Type default"},{L"Custom",L"Custom text layout"}};
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
static void AppendTileInstancePolicyMenu(HMENU parent,const aip::content::TileInstance& tile)
{
    using namespace aip::content;
    const auto appendGroup=[&](const wchar_t* prefix,const wchar_t* title){
        HMENU menu=aip::BeginTrayNestedMenu(parent),advanced=nullptr;
        for(const auto& field:TilePolicyFields){
            if(std::wstring(field.key).compare(0,wcslen(prefix),prefix)!=0)continue;
            if(std::wstring(field.key)==L"Cache.Precache"||std::wstring(field.key)==L"Cache.PrecacheMaxFiles")continue;
            if(tile.type==TileInstanceType::Gallery&&std::wstring(prefix)==L"Wallpaper."&&
                std::wstring(field.key)!=L"Wallpaper.Fit"&&
                std::wstring(field.key)!=L"Wallpaper.DecodeLowMemory"&&std::wstring(field.key)!=L"Wallpaper.DecodeMaxLongEdge")continue;
            HMENU target=menu;
            const auto key=std::wstring(field.key);
            if(std::wstring(prefix)==L"Delivery."&&key!=L"Delivery.Mode"&&key!=L"Delivery.Template"&&key!=L"Delivery.ClearOnShutdown"&&key!=L"Delivery.ManifestTarget"){
                if(!advanced)advanced=aip::BeginTrayNestedMenu(menu);target=advanced;
            }
            if(field.kind==TilePolicyValueKind::Choice){
                std::vector<std::pair<std::wstring,std::wstring>> choices;
                for(const auto& choice:TilePolicyChoices(field)){
                    if(tile.type==TileInstanceType::Gallery&&std::wstring(field.key)==L"Wallpaper.Fit"&&choice!=L"Auto"&&choice!=L"Fill"&&choice!=L"Fit")continue;
                    choices.emplace_back(choice,choice);
                }
                AppendTileInstanceChoices(target,tile,field.key,field.label,choices,field.fallback);
            }else AppendTileInstanceEditable(target,tile,field.key,field.label,field.fallback,field.kind==TilePolicyValueKind::Boolean);
        }
        if(advanced)aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceRegistrationBackend",L"Registration backend"));
        aip::EndTrayNestedMenu(parent,menu,TileInstanceUi((L"TileInstancePolicy_"+std::wstring(prefix)).c_str(),title));
    };
    appendGroup(L"Delivery.",L"Delivery and exit behavior");
    appendGroup(L"Cache.",L"Image cache");
    appendGroup(L"Assets.",L"Generated assets and scales");
    appendGroup(L"Text.",L"Text on tile sizes");
    if(tile.type==TileInstanceType::Wallpaper||(!tile.configuration.entries.empty()&&
        BackgroundIdFor(tile.configuration.entries.front().background)!=BackgroundId::Transparent&&
        BackgroundIdFor(tile.configuration.entries.front().background)!=BackgroundId::None))appendGroup(L"Wallpaper.",L"Image fitting, decode and capture");
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
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.TwentyFourHour",L"Use 24-hour times",L"0",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.ShowSunrise",L"Show sunrise in details and timetable",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.NextIncludesSunrise",L"Include sunrise in next event countdown",L"0",true);
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.ShowTimetable",L"Show timetable on large tiles",L"1",true);
    HMENU advanced=aip::BeginTrayNestedMenu(menu);
    AppendTileInstanceEditable(advanced,tile,L"PrayerTimes.RefreshSeconds",L"Schedule refresh interval (seconds)",L"43200");
    AppendTileInstanceEditable(advanced,tile,L"PrayerTimes.TimeoutSeconds",L"HTTP timeout (seconds)",L"10");
    AppendTileInstanceEditable(advanced,tile,L"PrayerTimes.MaximumBytes",L"Response limit (bytes)",L"262144");
    aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
}
static void AppendTileInstanceAlerts(HMENU parent,const aip::content::TileInstance& tile)
{
    HMENU menu=aip::BeginTrayNestedMenu(parent);
    AppendTileInstanceEditable(menu,tile,L"Alerts.Enabled",L"Enable notifications for this tile",L"0",true);
    AppendTileInstanceChoices(menu,tile,L"Alerts.Type",L"Notification type",{{L"General",L"General"},{L"Reminder",L"Reminder"},{L"Alarm",L"Alarm"}},L"General");
    AppendTileInstanceChoices(menu,tile,L"Alerts.Sound",L"Notification sound",{{L"Silent",L"Silent"},{L"Default",L"Default"},{L"Alarm",L"Alarm sound"}},L"Silent");
    if(tile.type==aip::content::TileInstanceType::PrayerTimes){
        HMENU upcoming=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(upcoming,tile,L"Alerts.UpcomingEnabled",L"Notify before prayer",L"1",true);
        AppendTileInstanceEditable(upcoming,tile,L"Alerts.LeadMinutes",L"Minutes before prayer",L"20");
        AppendTileInstanceEditable(upcoming,tile,L"Alerts.UpcomingTitle",L"Title (%event%, %minutes%, %time%, %city%, %tile%)",L"%event% in %minutes% minutes");
        AppendTileInstanceEditable(upcoming,tile,L"Alerts.UpcomingBody",L"Message (%time%, %event%, %city%, %tile%)",L"%time%");
        aip::EndTrayNestedMenu(menu,upcoming,TileInstanceUi(L"PrayerUpcomingNotification",L"Before prayer"));
        HMENU atTime=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(atTime,tile,L"Alerts.AtTimeEnabled",L"Notify at prayer time",L"1",true);
        AppendTileInstanceEditable(atTime,tile,L"Alerts.AtTimeTitle",L"Title (%event%, %time%, %city%, %tile%)",L"It's time for %event%");
        AppendTileInstanceEditable(atTime,tile,L"Alerts.AtTimeBody",L"Message (%time%, %event%, %city%, %tile%)",L"%time%");
        aip::EndTrayNestedMenu(menu,atTime,TileInstanceUi(L"PrayerAtTimeNotification",L"At prayer time"));
        HMENU current=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(current,tile,L"Alerts.CurrentEnabled",L"Show silently in Notification Center (Toast API)",L"0",true);
        AppendTileInstanceEditable(current,tile,L"Alerts.CurrentTitle",L"Title (%event%, %time%, %city%, %tile%)",L"%event%, %time%");
        AppendTileInstanceEditable(current,tile,L"Alerts.CurrentBody",L"Message (%time%, %event%, %city%, %tile%)",L"at %city%");
        aip::AppendTrayMenuItem(current,0,TileInstanceUi(L"PrayerCurrentDismissible",L"Windows allows dismissal; expires at the next prayer"),false,false);
        aip::EndTrayNestedMenu(menu,current,TileInstanceUi(L"PrayerCurrentNotification",L"Current prayer status"));
        AppendTileInstanceChoices(menu,tile,L"Alerts.TimeFormat",L"Notification time format",{{L"Inherit",L"Same as tile"},{L"12Hour",L"12-hour (AM/PM)"},{L"24Hour",L"24-hour"},{L"System",L"Windows setting"}},L"Inherit");
        AppendTileInstanceEditable(menu,tile,L"Alerts.Events",L"Events (Fajr,Dhuhr,Asr,Maghrib,Isha,Sunrise)",L"Fajr,Dhuhr,Asr,Maghrib,Isha");
    }else{
        AppendTileInstanceEditable(menu,tile,L"Alerts.MinimumNewItems",L"New items required before notifying",L"1");
        AppendTileInstanceEditable(menu,tile,L"Alerts.CooldownSeconds",L"Minimum time between notifications (seconds)",L"60");
        AppendTileInstanceEditable(menu,tile,L"Alerts.Title",L"Notification title (%count%, %tile%; blank uses default)",L"");
        AppendTileInstanceEditable(menu,tile,L"Alerts.Body",L"Notification message (%count%, %tile%; blank uses default)",L"");
    }
    aip::EndTrayNestedMenu(parent,menu,TileInstanceUi(L"TileSourceNotifications",L"Notifications for this tile"));
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
    case TileInstanceType::Rss: {
        AppendTileInstanceEditable(menu,tile,L"RssFeed.FeedUrl",L"RSS or Atom feed URL",L"");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.UpdateIntervalSeconds",L"Fetch new headlines every (seconds, 60-86400)",L"900");
        AppendTileInstanceEditable(menu,tile,L"RssFeed.MaxItems",L"Headlines to keep (1-20)",L"5");
        AppendTileInstanceEditable(menu,tile,L"Content.RssItem",L"Featured headline number (1-20)",L"1");
        HMENU advanced=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(advanced,tile,L"RssFeed.HttpTimeoutSeconds",L"HTTP timeout (seconds, 5-120)",L"20");
        AppendTileInstanceEditable(advanced,tile,L"RssFeed.MaxFeedBytes",L"Response limit (bytes)",L"2097152");
        AppendTileInstanceEditable(advanced,tile,L"RssFeed.UserAgent",L"HTTP user agent",L"Mozilla/5.0 (Windows NT; DesktopStub RSS Live Tile)");
        aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
        break;
    }
    case TileInstanceType::NowPlaying:
        aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TileInstanceSmtcExplanation",L"Uses Windows' current media session"),false,false);
        AppendTileInstanceChoices(menu,tile,L"SMTC.Indicator",L"Playback indicator",{{L"Status",L"Playback status"},{L"AppIcon",L"Source app icon"},{L"None",L"Hidden"}},L"Status");
        AppendTileInstanceEditable(menu,tile,L"SMTC.ShowRepeatShuffle",L"Show repeat and shuffle status",L"0",true);
        break;
    case TileInstanceType::PrayerTimes: AppendTileInstancePrayerSettings(menu,tile); break;
    case TileInstanceType::Clock: case TileInstanceType::Calendar: {
        const bool clock=tile.type==TileInstanceType::Clock;const auto section=clock?L"Clock.":L"Calendar.";
        AppendTileInstanceEditable(menu,tile,(std::wstring(section)+L"TimeZone").c_str(),L"Time zone (Local or Windows/IANA zone ID)",L"Local");
        AppendTileInstanceChoices(menu,tile,(std::wstring(section)+L"TimeFormat").c_str(),L"Time format",{{L"12Hour",L"12-hour (AM/PM)"},{L"24Hour",L"24-hour"},{L"System",L"Windows setting"}},clock?L"System":L"12Hour");
        if(clock){
            AppendTileInstanceEditable(menu,tile,L"Clock.Location",L"Location label (optional)",L"");
            AppendTileInstanceEditable(menu,tile,L"Clock.ShowDate",L"Show date",L"1",true);
        }else{
            AppendTileInstanceChoices(menu,tile,L"Calendar.Source",L"Calendar source",{{L"Local",L"Local calendar file"},{L"Google",L"Google Calendar"}},L"Local");
            if(tile.calendar.source==L"Google") {
                AppendTileInstanceCommand(menu,TileInstanceUi(L"GoogleCalendarSetup",L"Google account and calendars..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
            }else AppendTileInstanceEditable(menu,tile,L"Calendar.FilePath",L"Local calendar file (.ics, optional)",L"");
            AppendTileInstanceEditable(menu,tile,L"Calendar.DaysAhead",tile.calendar.source==L"Google"?L"Look ahead (1-31 days)":L"Look ahead (days)",L"7");
            AppendTileInstanceEditable(menu,tile,L"Calendar.MaxEvents",L"Upcoming events to keep (1-10)",L"3");
            AppendTileInstanceEditable(menu,tile,L"Calendar.ShowLocation",L"Show event location",L"1",true);
            HMENU advanced=aip::BeginTrayNestedMenu(menu);
            if(tile.calendar.source==L"Google"){
                AppendTileInstanceEditable(advanced,tile,L"GoogleCalendar.RefreshSeconds",L"Fetch events every (seconds)",L"300");
                AppendTileInstanceEditable(advanced,tile,L"GoogleCalendar.TimeoutSeconds",L"HTTP timeout (seconds)",L"15");
                AppendTileInstanceEditable(advanced,tile,L"GoogleCalendar.MaximumBytes",L"Response limit (bytes)",L"2097152");
            }else AppendTileInstanceEditable(advanced,tile,L"Calendar.RefreshSeconds",L"Check calendar file every (seconds)",L"60");
            aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
        }break;
    }
    case TileInstanceType::Wuwa: {
        AppendTileInstanceCommand(menu,TileInstanceUi(L"KuroSignIn",L"Sign in to Kuro and choose character..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
        AppendTileInstanceEditable(menu,tile,L"Kuro.PlayerId",L"WuWa player ID (blank discovers a single linked role)",L"");
        AppendTileInstanceEditable(menu,tile,L"Kuro.Region",L"WuWa region (blank discovers a single linked role)",L"");
        AppendTileInstanceEditable(menu,tile,L"Kuro.RefreshSeconds",L"Fetch game notes every (seconds)",L"300");
        HMENU advanced=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(advanced,tile,L"Kuro.GameToken",L"Import game access token (protected with Windows DPAPI)",L"",false,true);
        AppendTileInstanceEditable(advanced,tile,L"Kuro.DeviceId",L"Device ID (optional)",L"");
        AppendTileInstanceEditable(advanced,tile,L"Kuro.TimeoutSeconds",L"HTTP timeout (seconds)",L"15");
        AppendTileInstanceEditable(advanced,tile,L"Kuro.MaximumBytes",L"Response limit (bytes)",L"1048576");
        aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
        break;
    }
    case TileInstanceType::Genshin: case TileInstanceType::Hsr: case TileInstanceType::Zzz: {
        AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceSignIn",L"Sign in to HoYoLAB inside this app..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceChooseAccount",L"Choose saved account..."),{TileInstanceMenuVerb::ChooseAccount,tile.id,L"",L"",L""});
        AppendTileInstanceEditable(menu,tile,L"Notes.UID",L"Game UID override (blank uses saved account)",L"");
        AppendTileInstanceEditable(menu,tile,L"Notes.Server",L"Game server override (blank auto-detects)",L"");
        AppendTileInstanceEditable(menu,tile,L"Notes.RefreshSeconds",L"Fetch game notes every (seconds)",L"300");
        HMENU advanced=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(advanced,tile,L"Notes.HttpTimeoutSeconds",L"HTTP timeout (seconds)",L"15");
        AppendTileInstanceEditable(advanced,tile,L"Notes.MaximumBytes",L"Response limit (bytes)",L"1048576");
        AppendTileInstanceEditable(advanced,tile,L"Account",L"Account reference (Notes.<resource> or Account.<id>)",L"");
        aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
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
    const bool registration=TileInstanceUsesRegistration(tile);
    if (!tile.primary) {
        const auto state = TilePinState(SecondaryTilePinUiAvailable(),SecondaryTileInventoryReady(),SecondaryTileExists(nativeId));
        if(registration)AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceRegistrationPin",L"Registration setup and pin instructions..."),{TileInstanceMenuVerb::Pin,tile.id,L"",L"",L""});
        else{
        if (state == TilePinMenuState::Pinned) AppendTileInstanceCommand(menu,TileInstanceUi(L"UnpinTile",L"Unpin from Start..."),{TileInstanceMenuVerb::Unpin,tile.id,L"",L"",L""});
        else if (state == TilePinMenuState::Unpinned) AppendTileInstanceCommand(menu,TileInstanceUi(L"PinTile",L"Pin tile..."),{TileInstanceMenuVerb::Pin,tile.id,L"",L"",L""});
        else if (state == TilePinMenuState::Unknown) AppendTileInstanceCommand(menu,TileInstanceUi(L"CheckTilePinStatus",L"Check pin status..."),{TileInstanceMenuVerb::Inventory,tile.id,L"",L"",L""});
        else aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TilePinStatusUnavailable",L"Pin status requires the registered app"),false,false);
        }
    }
    AppendTileInstanceCommand(menu,TileInstanceUi(L"TileInstanceRefresh",L"Refresh this tile now"),{TileInstanceMenuVerb::Refresh,tile.id,L"",L"",L""});
    if(tile.type==TileInstanceType::Rss||tile.type==TileInstanceType::PrayerTimes)AppendTileInstanceAlerts(menu,tile);
    AppendTileInstanceEditable(menu,tile,L"Name",L"Tile name",tile.name.c_str());
    HMENU source = aip::BeginTrayNestedMenu(menu); AppendTileInstanceSourceSettings(source,tile);
    aip::EndTrayNestedMenu(menu,source,TileInstanceUi(L"TileInstanceSourceSettings",L"Source settings"));
    if (!tile.error.empty()) {
        aip::AppendTrayMenuItem(menu,0,ContentMenuValue(tile.error),false,false);
        std::vector<std::pair<std::wstring,std::wstring>> types;
        for (const auto& descriptor : TileInstanceTypes) if (descriptor.type != TileInstanceType::Gallery) types.emplace_back(descriptor.value,descriptor.name);
        AppendTileInstanceChoices(menu,tile,L"Type",L"Repair tile type",types,DescribeTileInstanceType(tile.type).value);
    }
    AppendTileInstanceAppearance(menu,tile); AppendTileInstanceText(menu,tile); AppendTileInstancePolicyMenu(menu,tile);
    if (tile.type != TileInstanceType::QuickAction) {
        std::vector<std::pair<std::wstring,std::wstring>> actions{{L"setup",L"Open this tile's settings"}};
        if (tile.type == TileInstanceType::Rss) actions.push_back({L"source",L"Open this tile's featured article"});
        for (const auto& action : TileActions) actions.emplace_back(action.id,action.label);
        AppendTileInstanceChoices(menu,tile,L"ClickAction",L"Click action",actions,tile.type == TileInstanceType::NowPlaying ? L"toggle" : tile.type == TileInstanceType::Rss ? L"source" : L"setup");
    }
    const bool removable = registration||tile.primary || (SecondaryTileInventoryReady() && !SecondaryTileExists(nativeId));
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
    if(descriptor->type==TileInstanceType::Discord){
        // Freeze factory templates once for this new tile. Existing explicit
        // inline fields and imported advanced profiles are never rewritten.
        settings.push_back({section,L"Discord.Name",aip::discord::DefaultActivityName});
        settings.push_back({section,L"Discord.Details",aip::discord::DefaultDetailsTemplate});
        settings.push_back({section,L"Discord.State",aip::discord::DefaultStateTemplate});
    }
    for(const auto& field:TilePolicyFields){
        auto value=IniReadS(field.section,field.setting,field.fallback);
        if(!NormalizeTilePolicySetting(field.key,value,error))return false;
        settings.push_back({section,field.key,value});
    }
    if (!SaveTileInstanceSettings(owner,settings)) return false;
    NotifyUserOperation(L"tile-added-"+id,TileInstanceUi(L"NotificationTileAdded",L"Tile created"),
        NotificationSubjectText(L"NotificationTileAddedMessage",L"%s was created. Open its settings to configure and pin it.",name));
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
static void AppendGalleryTilePolicySettings(HMENU menu,const aip::gallery::Configuration& cfg,std::size_t index)
{
    if(index>=cfg.tiles.size())return;
    const auto& row=cfg.tiles[index];
    aip::content::TileInstance adapter;adapter.id=aip::gallery::NativeTileId(cfg.groupId,row.id);adapter.name=row.name;adapter.type=aip::content::TileInstanceType::Gallery;
    adapter.configuration.entries.emplace_back();adapter.configuration.entries.front().background=L"Image";
    const auto previous=g_tilePolicyMenuSection;g_tilePolicyMenuSection=L"Gallery.Tile."+std::to_wstring(index+1);
    AppendTileInstancePolicyMenu(menu,adapter);g_tilePolicyMenuSection=previous;
}
static bool OpenGalleryTilePolicySetup(HWND owner,const std::wstring& nativeId)
{
    aip::gallery::Configuration cfg;std::wstring error;
    if(!ReadGalleryConfiguration(cfg,error))return false;
    const auto found=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[&](const auto& row){return aip::gallery::NativeTileId(cfg.groupId,row.id)==nativeId;});
    if(found==cfg.tiles.end())return false;
    const auto index=static_cast<std::size_t>(std::distance(cfg.tiles.begin(),found));
    HMENU menu=CreatePopupMenu();if(!menu)return false;
    ResetTileInstanceMenuCommands();
    AppendGalleryTilePolicySettings(menu,cfg,index);
    POINT point{};GetCursorPos(&point);if(owner)SetForegroundWindow(owner);
    const auto command=static_cast<UINT>(TrackPopupMenu(menu,TPM_RETURNCMD|TPM_RIGHTBUTTON,point.x,point.y,0,owner,nullptr));
    DestroyMenu(menu);if(owner)AfterTrayPopupClosed(owner);
    if(command)DispatchTileInstanceCommand(owner,command);return true;
}
static bool DispatchGalleryTilePolicyCommand(HWND owner,const TileInstanceMenuCommand& item)
{
    aip::gallery::Configuration cfg;std::wstring error;
    if(!ReadGalleryConfiguration(cfg,error))return true;
    const auto index=aip::gallery::TileSectionNumber(item.sectionOverride);
    if(index<1||static_cast<std::size_t>(index)>cfg.tiles.size()||aip::gallery::NativeTileId(cfg.groupId,cfg.tiles[static_cast<std::size_t>(index-1)].id)!=item.id)return true;
    const auto& row=cfg.tiles[static_cast<std::size_t>(index-1)];auto value=item.value;
    if(item.verb==TileInstanceMenuVerb::Edit){
        value=IniReadS(item.sectionOverride.c_str(),item.key.c_str(),item.value.c_str());
        if(ShowTextInputDialog(owner,row.name,item.prompt,value,true)!=IDOK)return true;
    }else if(item.verb==TileInstanceMenuVerb::Toggle){bool enabled=false;aip::content::ParseBool(IniReadS(item.sectionOverride.c_str(),item.key.c_str(),item.value.c_str()),enabled);value=enabled?L"0":L"1";}
    if(aip::content::Lower(item.key)==L"delivery.mode"){
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(g_testTileDeliveryMigration)g_testTileDeliveryMigration(owner,item.id,value);
#else
        OfferGalleryTileDeliveryMigration(owner,item.id,value);
#endif
        return true;
    }
    const auto edit=GalleryEdit(cfg);SaveGalleryMenuSettings(owner,{{item.sectionOverride,item.key,value}},&edit);return true;
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
static bool OpenKuroSignInForTile(HWND owner,const std::wstring& tileId);
static bool DispatchTileInstanceCommand(HWND owner,UINT command)
{
    const auto found = g_tileInstanceMenuCommands.find(command); if (found == g_tileInstanceMenuCommands.end()) return false;
    const auto item = found->second; // Popup construction may replace the registry.
    if(!item.sectionOverride.empty())return DispatchGalleryTilePolicyCommand(owner,item);
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
    if (item.verb == TileInstanceMenuVerb::Pin) {
        if(TileInstanceUsesRegistration(tile)){
#ifdef DESKTOPSTUB_INERT_HARDWARE
            if(g_testTileDeliveryMigration)g_testTileDeliveryMigration(owner,tile.id,L"Registration");
#else
            OfferTileDeliveryMigration(owner,tile.id,L"Registration");
#endif
        }else RequestPinSecondaryTile(owner,nativeId,tile.name);
        return true;
    }
    if (item.verb == TileInstanceMenuVerb::Unpin) { RequestUnpinSecondaryTile(owner,nativeId); return true; }
    if (item.verb == TileInstanceMenuVerb::Status) { ShowSelectedTileStatus(owner,nativeId,tile.name); return true; }
    if (item.verb == TileInstanceMenuVerb::Refresh) { RefreshTileInstanceNow(tile.id); return true; }
    if (item.verb == TileInstanceMenuVerb::ChooseAccount) { ChooseTileInstanceAccount(owner,tile); return true; }
    if (item.verb == TileInstanceMenuVerb::SignIn) {
        if(tile.type==TileInstanceType::Calendar){OpenGoogleCalendarSetup(owner,tile.id);return true;}
        if(tile.type==TileInstanceType::Wuwa){OpenKuroSignInForTile(owner,tile.id);return true;}
        const auto resource = DescribeTileInstanceType(tile.type).notesResource;
        if (resource >= 0) OpenEmbeddedNotesSignInForTile(owner,resource,tile.id);
        return true;
    }
    if (item.verb == TileInstanceMenuVerb::Remove) {
        const bool registration=TileInstanceUsesRegistration(tile);
        if(registration&&DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"Unpin this Registration tile from Start manually before removing its settings. The registered entry and files will be kept so existing pins remain recoverable. Remove these tile settings now?",tile.name.c_str(),MB_YESNO|MB_DEFBUTTON2|MB_ICONQUESTION)!=IDYES)return true;
        SaveTileInstanceSettingsWithRemovalConfirmation(owner,{},tile.id,tile.id,registration);return true;
    }
    auto value = item.value;
    if (item.verb == TileInstanceMenuVerb::Edit) {
        value = IniReadS(section.c_str(),item.key.c_str(),item.value.c_str());
        if (ShowTextInputDialog(owner,tile.name,item.prompt,value,true,item.secret) != IDOK) return true;
    } else if (item.verb == TileInstanceMenuVerb::Toggle) {
        bool enabled = false; ParseBool(IniReadS(section.c_str(),item.key.c_str(),item.value.c_str()),enabled); value = enabled ? L"0" : L"1";
    }
    if(Lower(item.key)==L"delivery.mode"){
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(g_testTileDeliveryMigration)g_testTileDeliveryMigration(owner,tile.id,value);
#else
        OfferTileDeliveryMigration(owner,tile.id,value);return true;
#endif
        return true;
    }
    std::vector<aip::IniSetting> settings{{section,item.key,value}};
    if(Lower(item.key)==L"kuro.gametoken"){
        std::wstring tokenError; if(!aip::kuro::NormalizeSetting(L"GameToken",value,tokenError)){
            DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,tokenError.c_str(),tile.name.c_str(),MB_OK|MB_ICONERROR);return true;
        }
        std::wstring protectedValue;
        try {if(!value.empty())protectedValue=aip::ProtectSecretForCurrentUser(value);}
        catch(...){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"Windows could not protect this token. It has not been saved.",tile.name.c_str(),MB_OK|MB_ICONERROR);return true;}
        settings={{section,L"Kuro.GameToken",L""},{section,L"Kuro.GameTokenProtected",protectedValue}};
    }
    if(Lower(item.key)==L"content.background"&&value==L"LiveWallpaper")settings.push_back({section,L"Wallpaper.Capture",L"1"});
    if (Lower(item.key) == L"textpreset" && value == L"RssHeadlines") {
        // Materialize the named layout so stale per-field custom settings from
        // the previous layout cannot make selecting it appear to do nothing.
        settings.push_back({section,L"TextMode",L"Auto"});
        settings.push_back({section,L"TextStyle",L"base"});
        settings.push_back({section,L"TextSecondaryStyle",L"captionSubtle"});
        settings.push_back({section,L"TextBadgeStyle",L"captionSubtle"});
        settings.push_back({section,L"TextWrap",L"1"});
        settings.push_back({section,L"TextMaxLines",L"3"});
        settings.push_back({section,L"TextSecondaryMaxLines",L"2"});
        settings.push_back({section,L"TextBadgeMaxLines",L"1"});
        settings.push_back({section,L"TextGapLines",L"0"});
        settings.push_back({section,L"TextAlign",L"left"});
        settings.push_back({section,L"TextStacking",L"top"});
    }
    if (IsTextPresentationKey(item.key) && Lower(item.key) != L"textpreset"){
        if((tile.type==TileInstanceType::PrayerTimes||tile.type==TileInstanceType::Clock||tile.type==TileInstanceType::Calendar)&&!tile.presentation.Active()) {
            const std::pair<const wchar_t*,const wchar_t*> defaults[]={{L"TextStyle",L"title"},{L"TextSecondaryStyle",L"captionSubtle"},
                {L"TextBadgeStyle",L"headerNumeral"},{L"TextMaxLines",L"1"},{L"TextSecondaryMaxLines",L"2"},{L"TextBadgeMaxLines",L"1"}};
            for(const auto& field:defaults)if(Lower(field.first)!=Lower(item.key))settings.push_back({section,field.first,field.second});
            if(tile.type==TileInstanceType::Calendar){
                if(Lower(item.key)!=L"textstyle")settings.push_back({section,L"TextStyle",L"headerNumeral"});
                if(Lower(item.key)!=L"textbadgestyle")settings.push_back({section,L"TextBadgeStyle",L"base"});
            }
        }
        const bool richRss=tile.type==TileInstanceType::Rss&&(tile.presentation.preset==L"Default"||tile.presentation.preset==L"RssHeadlines");
        if(richRss&&tile.presentation.preset==L"Default"){
            const std::pair<const wchar_t*,const wchar_t*> defaults[]={
                {L"TextStyle",L"base"},{L"TextSecondaryStyle",L"captionSubtle"},{L"TextBadgeStyle",L"captionSubtle"},
                {L"TextWrap",L"1"},{L"TextMaxLines",L"3"},{L"TextSecondaryMaxLines",L"2"},{L"TextBadgeMaxLines",L"1"},
                {L"TextGapLines",L"0"},{L"TextAlign",L"left"},{L"TextStacking",L"top"}};
            for(const auto& field:defaults)if(Lower(field.first)!=Lower(item.key))settings.push_back({section,field.first,field.second});
        }
        settings.push_back({section,L"TextPreset",richRss?L"RssHeadlines":L"Custom"});
    }
    SaveTileInstanceSettings(owner,settings,tile.id); return true;
}

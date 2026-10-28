// Tile-specific configuration and on-demand native menus. No provider fetch,
// photo scan or hardware discovery runs while these menus are constructed.
static constexpr UINT ID_TILE_INSTANCE_FIRST = 60000, ID_TILE_INSTANCE_LAST = 63999;
static constexpr std::size_t TileInstanceMenuPageSize = 16;
static bool RequestDesktopHostSetup(HWND);
enum class TileInstanceMenuVerb { Set, Edit, Toggle, Pin, Unpin, Inventory, Status, Overview, Refresh, Remove, Add,
    Page, SignIn, ChooseAccount, AddGallery, BrowseLauncherTarget, BrowseLauncherIcon, NowPlayingLayout, SelectSmtcSource,
    DiscordConnect,DiscordCredential,DiscordCompose,DiscordDisconnect,DiscordPolicy };
static bool ConfigureDiscordReader(HWND,const std::wstring&,bool);
static bool ActivateDiscordReader(HWND,const std::wstring&,bool forceCompose=false);
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
static bool OpenGmailSetup(HWND owner,const std::wstring& tileId);
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
        return key == L"name" || key == L"type" || key == L"background" || key == L"branding" || key == L"backgroundcolor" ||
            key==L"launcher.target"||key==L"launcher.iconmode"||key==L"launcher.iconpath"||key==L"launcher.iconindex"||key==L"launcher.iconsizepercent";
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
    AppendTileInstanceChoices(menu,tile,L"Branding",L"Name and corner icon",branding,tile.appearance.branding.c_str());
    if(tile.type!=aip::content::TileInstanceType::AppLauncher)
        AppendTileInstanceChoices(menu,tile,L"Artwork.IconFont",L"Icon font",{{L"Auto",L"Auto (Windows 10 MDL2 / Windows 11 Fluent)"},{L"MDL2",L"Segoe MDL2 Assets"},{L"Fluent",L"Segoe Fluent Icons"},{L"Vector",L"Drawn fallback icons"}},L"Auto");
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
    for (std::size_t i = 0; i < _countof(styleKeys); ++i) AppendTileInstanceChoices(menu,tile,styleKeys[i],styleLabels[i],styles,
        i==0&&!tile.presentation.Active()&&(aip::content::DescribeTileInstanceType(tile.type).notesResource>=0||tile.type==aip::content::TileInstanceType::Wuwa)?L"base":styleDefaults[i]);
    AppendTileInstanceEditable(menu,tile,L"TextWrap",L"Wrap text onto multiple lines",L"1",true);
    AppendTileInstanceEditable(menu,tile,L"TextMaxLines",L"Primary text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextSecondaryMaxLines",L"Secondary text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextBadgeMaxLines",L"Other text maximum lines (1-8)",L"2");
    AppendTileInstanceEditable(menu,tile,L"TextGapLines",L"Blank lines between text fields (0-2)",L"0");
    if(aip::content::DescribeTileInstanceType(tile.type).notesResource>=0||tile.type==aip::content::TileInstanceType::Wuwa){
        AppendTileInstanceChoices(menu,tile,L"TextIconPlacement",L"Row icon placement",{{L"Right",L"Right"},{L"Left",L"Left"},{L"None",L"No icons"}},L"Right");
        AppendTileInstanceEditable(menu,tile,L"TextIconScalePercent",L"Row icon scale (%)",L"100");
        AppendTileInstanceEditable(menu,tile,L"TextRowSpacing",L"Space between rows (pixels)",L"6");
    }
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
            if(key==L"Delivery.PrimaryRegistration"&&!tile.primary)continue;
            const bool registration=tile.policy.Mode()==L"Registration";
            const bool permission=key==L"Delivery.DisabledWarning"||key==L"Delivery.DisabledCheckSeconds";
            const bool backend=std::wstring(prefix)==L"Delivery."&&!permission&&key!=L"Delivery.Mode"&&key!=L"Delivery.Template"&&key!=L"Delivery.ClearOnShutdown"&&key!=L"Delivery.ManifestTarget";
            if((backend&&!registration)||(permission&&registration))continue;
            if(backend){
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
    if(tile.type==TileInstanceType::Gallery)appendGroup(L"Artwork.",L"Fallback icon rendering");
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
    AppendTileInstanceEditable(menu,tile,L"PrayerTimes.TimetableGapLines",L"Blank lines before timetable (0–3)",L"1");
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
static void AppendTileSourceHealthSettings(HMENU parent,const aip::content::TileInstance& tile)
{
    using namespace aip::source_health;
    HMENU menu=aip::BeginTrayNestedMenu(parent),tileMenu=aip::BeginTrayNestedMenu(menu),popupMenu=aip::BeginTrayNestedMenu(menu),textMenu=aip::BeginTrayNestedMenu(menu);
    for(const auto& field:Settings){const std::wstring key=field.key,full=L"SourceHealth."+key;HMENU target=menu;
        if(key.compare(0,4,L"Tile")==0)target=tileMenu;if(key.compare(0,5,L"Popup")==0)target=popupMenu;
        if(field.type==FieldType::Template)target=textMenu;
        std::wstring label;for(std::size_t i=0;i<key.size();++i){if(i&&key[i]>=L'A'&&key[i]<=L'Z'&&key[i-1]>=L'a'&&key[i-1]<=L'z')label+=L' ';label+=key[i];}
        if(key==L"AgeDisplay")label=L"Show last successful refresh age";
        else if(key==L"RetainLastGood")label=L"Keep the last successful resource values during failures";
        else if(key==L"AuthenticationImmediate")label=L"Show rejected-account/setup failures immediately";
        else if(key==L"TileFailureAttempts"||key==L"PopupFailureAttempts")label=L"Consecutive failed refreshes before showing an error (0 disables)";
        else if(key==L"TileFailureSeconds"||key==L"PopupFailureSeconds")label=L"Seconds since first consecutive failure (0 disables)";
        else if(key==L"TileThreshold"||key==L"PopupThreshold")label=L"Require either or both enabled failure thresholds";
        else if(key==L"AgeResolutionSeconds")label=L"Refresh-age display granularity (seconds)";
        if(field.type==FieldType::Choice){std::vector<std::pair<std::wstring,std::wstring>> choices;const std::wstring values=field.choices;std::size_t begin=0;
            for(;;){const auto end=values.find(L'|',begin);const auto value=values.substr(begin,end==std::wstring::npos?end:end-begin);choices.emplace_back(value,value);if(end==std::wstring::npos)break;begin=end+1;}
            AppendTileInstanceChoices(target,tile,full.c_str(),label.c_str(),choices,field.fallback);
        }else AppendTileInstanceEditable(target,tile,full.c_str(),label.c_str(),field.fallback,field.type==FieldType::Boolean);
    }
    aip::AppendTrayMenuItem(tileMenu,0,L"Counts completed source refreshes, not idle polling ticks or inner HTTP retries.",false,false);
    aip::EndTrayNestedMenu(menu,tileMenu,L"Failure text on the tile");aip::EndTrayNestedMenu(menu,popupMenu,L"Failure and recovery popup notices");
    aip::AppendTrayMenuItem(textMenu,0,L"Tokens: %age%, %ageSeconds%, %ageMinutes%, %failures%, %elapsedSeconds%, %error%, %tile%; %% writes percent.",false,false);
    aip::EndTrayNestedMenu(menu,textMenu,L"Text templates (blank popup text uses application wording)");
    aip::EndTrayNestedMenu(parent,menu,L"Data freshness and failures");
}
static void AppendBatteryTileSettings(HMENU menu,const aip::content::TileInstance& tile)
{
    using namespace aip::battery;
    HMENU critical=aip::BeginTrayNestedMenu(menu),text=aip::BeginTrayNestedMenu(menu),advanced=aip::BeginTrayNestedMenu(menu);
    for(const auto& field:Settings){
        const std::wstring key=field.key,full=L"Battery."+key;
        HMENU target=menu;
        if(key.compare(0,8,L"Critical")==0)target=critical;
        else if(field.type==FieldType::Template||field.type==FieldType::Text)target=text;
        if(key==L"RefreshSeconds"||key==L"DeviceDetails"||key==L"CollectionTimeoutMs"||key==L"DeviceTimeoutMs")target=advanced;
        std::wstring label;
        for(std::size_t i=0;i<key.size();++i){
            if(i&&key[i]>=L'A'&&key[i]<=L'Z'&&key[i-1]>=L'a'&&key[i-1]<=L'z')label+=L' ';
            label+=key[i];
        }
        if(key==L"CriticalVisibility")label=L"When to show critical-action view";
        if(key==L"CriticalThresholdPercent")label=L"Custom activation threshold (%)";
        if(key==L"CriticalMarginPercent")label=L"Custom margin above Windows critical level (%)";
        if(key==L"DeviceDetails")label=L"Read battery capacity and wear from devices";
        if(key==L"CollectionTimeoutMs")label=L"Sampling budget (milliseconds)";
        if(key==L"DeviceTimeoutMs")label=L"Each device I/O timeout (milliseconds)";
        if(key==L"CornerMetric")label=L"Ordinary corner metric";
        if(field.type==FieldType::Choice){
            std::vector<std::pair<std::wstring,std::wstring>> choices;
            const std::wstring values=field.choices;std::size_t start=0;
            for(;;){const auto end=values.find(L'|',start);const auto value=values.substr(start,end==std::wstring::npos?end:end-start);
                choices.emplace_back(value,value);if(end==std::wstring::npos)break;start=end+1;}
            AppendTileInstanceChoices(target,tile,full.c_str(),label.c_str(),choices,field.fallback);
        }else AppendTileInstanceEditable(target,tile,full.c_str(),label.c_str(),field.fallback,field.type==FieldType::Boolean);
    }
    aip::AppendTrayMenuItem(critical,0,L"Windows power policy is read only; this tile never performs a power action.",false,false);
    aip::EndTrayNestedMenu(menu,critical,TileInstanceUi(L"BatteryCriticalView",L"Critical-action view"));
    aip::EndTrayNestedMenu(menu,text,TileInstanceUi(L"BatteryTextLabels",L"Text templates and labels"));
    aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvancedSource",L"Advanced source settings"));
}
static void AppendCalendarReminderMenu(HMENU parent,const aip::content::TileInstance& tile)
{
    HMENU menu=aip::BeginTrayNestedMenu(parent);
    AppendTileInstanceEditable(menu,tile,L"CalendarReminders.Enabled",L"Enable Calendar reminders",L"0",true);
    AppendTileInstanceEditable(menu,tile,L"CalendarReminders.LeadMinutes",L"Remind before event (minutes, 0 = at start)",L"15");
    AppendTileInstanceChoices(menu,tile,L"CalendarReminders.Type",L"Notification type",{{L"General",L"General"},{L"Reminder",L"Reminder"},{L"Alarm",L"Alarm"}},L"Reminder");
    AppendTileInstanceChoices(menu,tile,L"CalendarReminders.Sound",L"Notification sound",{{L"Silent",L"Silent"},{L"Default",L"Default"},{L"Alarm",L"Alarm sound"}},L"Default");
    AppendTileInstanceEditable(menu,tile,L"CalendarReminders.IncludeAllDay",L"Include all-day events",L"0",true);
    HMENU advanced=aip::BeginTrayNestedMenu(menu);
    AppendTileInstanceEditable(advanced,tile,L"CalendarReminders.SnoozeMinutes",L"Snooze choices (minutes, up to 5)",L"5,10,15,30,60");
    AppendTileInstanceEditable(advanced,tile,L"CalendarReminders.DefaultSnoozeMinutes",L"Default snooze (minutes)",L"15");
    AppendTileInstanceEditable(advanced,tile,L"CalendarReminders.Title",L"Reminder title (%event%, %tile%, %minutes%)",L"%event%");
    AppendTileInstanceEditable(advanced,tile,L"CalendarReminders.Body",L"Reminder body (%details%, %time%, %location%)",L"%details%");
    aip::AppendTrayMenuItem(advanced,0,TileInstanceUi(L"CalendarReminderSourcePolicy",L"Uses this tile's reminder time, not imported event alarms"),false,false);
    aip::AppendTrayMenuItem(advanced,0,TileInstanceUi(L"CalendarReminderBalloonLimit",L"Snooze and Dismiss buttons require the Toast notification API"),false,false);
    aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));
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
    case TileInstanceType::NowPlaying: {
        aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"TileInstanceSmtcExplanation",L"Uses Windows' current media session"),false,false);
        HMENU players=aip::BeginTrayNestedMenu(menu);const auto& selection=tile.configuration.smtc.selection;
        AppendTileInstanceCommand(players,TileInstanceUi(L"SmtcWindowsCurrent",L"Windows current session"),{TileInstanceMenuVerb::SelectSmtcSource,tile.id,L"",L"",L""},selection.mode==L"Current");
        const auto applications=CurrentSmtcApplications();
        for(const auto& app:applications)AppendTileInstanceCommand(players,ContentMenuValue(app),{TileInstanceMenuVerb::SelectSmtcSource,tile.id,L"",app,L""},selection.mode==L"Application"&&selection.appId==app);
        if(applications.empty())aip::AppendTrayMenuItem(players,0,L"Applications appear after a pinned Now Playing tile refreshes.",false,false);
        aip::EndTrayNestedMenu(menu,players,TileInstanceUi(L"SmtcSourceSelection",L"Media application (last refresh)"));
        AppendTileInstanceChoices(menu,tile,L"SMTC.SessionMode",L"Session selection",{{L"Current",L"Windows current session"},{L"Application",L"Chosen application"}},L"Current");
        AppendTileInstanceEditable(menu,tile,L"SMTC.SessionAppId",L"Application ID (optional in current-session mode)",L"");
        AppendTileInstanceEditable(menu,tile,L"SMTC.SessionInstance",L"Session within application (0 = current or first, 1-64 = occurrence)",L"0");
        HMENU layouts=aip::BeginTrayNestedMenu(menu);
        const bool artwork=!tile.configuration.entries.empty()&&tile.configuration.entries.front().background==L"SMTCArtwork"&&tile.configuration.textMode==TextMode::Off;
        AppendTileInstanceCommand(layouts,TileInstanceUi(L"NowPlayingInfoPreset",L"Information"),{TileInstanceMenuVerb::NowPlayingLayout,tile.id,L"",L"Info",L""},!artwork);
        AppendTileInstanceCommand(layouts,TileInstanceUi(L"NowPlayingArtworkPreset",L"Album art only"),{TileInstanceMenuVerb::NowPlayingLayout,tile.id,L"",L"AlbumArt",L""},artwork);
        aip::EndTrayNestedMenu(menu,layouts,TileInstanceUi(L"NowPlayingPresentationPresets",L"Presentation presets"));
        AppendTileInstanceChoices(menu,tile,L"Content.Background",L"Image source",{{L"Transparent",L"Nothing / transparent"},{L"SMTCArtwork",L"Album art from the current media session"}},L"Transparent");
        AppendTileInstanceChoices(menu,tile,L"SMTC.Indicator",L"Playback indicator",{{L"Status",L"Playback status"},{L"AppIcon",L"Source app icon"},{L"None",L"Hidden"}},L"Status");
        AppendTileInstanceEditable(menu,tile,L"SMTC.ShowRepeatShuffle",L"Show repeat and shuffle status",L"0",true);
        break;
    }
    case TileInstanceType::PrayerTimes: AppendTileInstancePrayerSettings(menu,tile); break;
    case TileInstanceType::Battery: AppendBatteryTileSettings(menu,tile); break;
    case TileInstanceType::Clock: case TileInstanceType::Calendar: {
        const bool clock=tile.type==TileInstanceType::Clock;const auto section=clock?L"Clock.":L"Calendar.";
        AppendTileInstanceEditable(menu,tile,(std::wstring(section)+L"TimeZone").c_str(),L"Time zone (Local or Windows/IANA zone ID)",L"Local");
        AppendTileInstanceChoices(menu,tile,(std::wstring(section)+L"TimeFormat").c_str(),L"Time format",{{L"12Hour",L"12-hour (AM/PM)"},{L"24Hour",L"24-hour"},{L"System",L"Windows setting"}},clock?L"System":L"12Hour");
        if(clock){
            AppendTileInstanceChoices(menu,tile,L"Clock.IconStyle",L"Static icon",{{L"Auto",L"Automatic (local or world clock)"},{L"Clock",L"Clock"},{L"WorldClock",L"World clock"}},L"Auto");
            AppendTileInstanceEditable(menu,tile,L"Clock.Location",L"Location label (optional)",L"");
            AppendTileInstanceEditable(menu,tile,L"Clock.ShowDate",L"Show date",L"1",true);
        }else{
            AppendTileInstanceChoices(menu,tile,L"Calendar.Source",L"Calendar source",{{L"Local",L"Local calendar file"},{L"Google",L"Google Calendar"}},L"Local");
            if(tile.calendar.source==L"Google") {
                AppendTileInstanceCommand(menu,TileInstanceUi(L"GoogleCalendarSetup",L"Google account and calendars..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
            }else AppendTileInstanceEditable(menu,tile,L"Calendar.FilePath",L"Local calendar file (.ics, optional)",L"");
            AppendTileInstanceChoices(menu,tile,L"Calendar.EventWindow",L"Events displayed",{{L"TodayAndTomorrow",L"Today and tomorrow"},{L"DaysAhead",L"Custom days ahead"}},L"DaysAhead");
            if(tile.calendar.eventWindow==L"DaysAhead")AppendTileInstanceEditable(menu,tile,L"Calendar.DaysAhead",tile.calendar.source==L"Google"?L"Look ahead (1-31 days)":L"Look ahead (days)",L"7");
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
        AppendTileSourceHealthSettings(menu,tile);
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
    case TileInstanceType::Discord: {
        AppendTileInstanceChoices(menu,tile,L"DiscordReader.View",L"Discord tile view",{{L"Presence",L"Rich presence"},{L"Channel",L"Channel messages and new items"},{L"Profile",L"Account profile and visible friends"}},L"Presence");
        AppendTileInstanceChoices(menu,tile,L"DiscordReader.Connection",L"Account reader connection",{{L"IPC",L"Presence IPC (no account reader)"},{L"OAuth",L"OAuth identity / granted data"},{L"Bot",L"Bot channel access"},{L"UserTokenExperimental",L"Personal user token (experimental; Discord prohibits this)"}},L"IPC");
        aip::AppendTrayMenuItem(menu,0,TileInstanceUi(L"DiscordReaderPolicyNotice",L"Personal-account automation violates Discord's rules and may terminate the account."),false,false);
        AppendTileInstanceCommand(menu,TileInstanceUi(L"DiscordReaderConfigureCredential",L"Set protected reader credential and review account..."),{TileInstanceMenuVerb::DiscordCredential,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"DiscordReaderConnect",L"Identify, review and connect the configured account..."),{TileInstanceMenuVerb::DiscordConnect,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"DiscordReaderDisconnect",L"Disconnect account reader (keep settings)"),{TileInstanceMenuVerb::DiscordDisconnect,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"DiscordReaderCompose",L"Compose a message for review..."),{TileInstanceMenuVerb::DiscordCompose,tile.id,L"",L"",L""});
        AppendTileInstanceCommand(menu,TileInstanceUi(L"DiscordReaderPolicy",L"Read Discord's personal-account automation policy"),{TileInstanceMenuVerb::DiscordPolicy,tile.id,L"",L"",L""});
        HMENU display=aip::BeginTrayNestedMenu(menu),click=aip::BeginTrayNestedMenu(menu),technical=aip::BeginTrayNestedMenu(menu),advanced=aip::BeginTrayNestedMenu(menu);
        const auto caption=[](const wchar_t* key){std::wstring out;for(std::size_t i=0;key[i];++i){const auto c=key[i];if(i&&c>=L'A'&&c<=L'Z')out+=L' ';out+=i&&c>=L'A'&&c<=L'Z'?static_cast<wchar_t>(c-L'A'+L'a'):c;}return out;};
        for(const auto& field:aip::discord_tiles::Fields){const std::wstring name=field.key;if(name==L"View"||name==L"Connection"||name==L"ExperimentalAcknowledged"||name==L"AcknowledgedProfileKey"||name==L"AcknowledgedAccountId")continue;
            const bool send=name.compare(0,4,L"Send")==0||name==L"ClickAction"||name==L"ConfiguredText"||name==L"DirectSendEnabled"||name==L"AllowReviewedSend";
            const bool network=name==L"RefreshMs"||name==L"OperationTimeoutMs"||name==L"ReconnectMinimumMs"||name==L"ReconnectMaximumMs"||name==L"MaximumBytes"||name==L"UseGateway";
            const bool low=name==L"MessageLimit"||name==L"MaximumMessageCharacters"||name==L"FriendsCacheLimit"||name==L"EventHistoryLimit"||name==L"WarnEveryConnection";
            HMENU target=send?click:network?technical:low?advanced:display;const auto key=L"DiscordReader."+name;const auto label=caption(field.key);
            if(field.type==aip::discord_tiles::FieldType::Choice){std::vector<std::pair<std::wstring,std::wstring>> values;std::wstring choices=field.choices;std::size_t start=0;for(;;){const auto end=choices.find(L'|',start);auto value=choices.substr(start,end==std::wstring::npos?end:end-start);values.emplace_back(value,value);if(end==std::wstring::npos)break;start=end+1;}AppendTileInstanceChoices(target,tile,key.c_str(),label.c_str(),values,field.fallback);}
            else AppendTileInstanceEditable(target,tile,key.c_str(),label.c_str(),field.fallback,field.type==aip::discord_tiles::FieldType::Boolean);
        }
        aip::EndTrayNestedMenu(menu,display,TileInstanceUi(L"DiscordReaderDisplay",L"Account, channel and display"));
        aip::EndTrayNestedMenu(menu,click,TileInstanceUi(L"DiscordReaderSending",L"Click action and reviewed sending"));
        aip::EndTrayNestedMenu(menu,technical,TileInstanceUi(L"DiscordReaderNetwork",L"Network and reconnect timing"));
        aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"DiscordReaderAdvanced",L"Advanced cache and warning policy"));
        HMENU presence=aip::BeginTrayNestedMenu(menu);for (const auto& option : g_discordOptions) AppendTileInstanceEditable(presence,tile,(L"Discord."+std::wstring(option.key)).c_str(),option.label,option.fallback,option.toggle);
        aip::EndTrayNestedMenu(menu,presence,TileInstanceUi(L"DiscordReaderPresence",L"Independent rich presence settings"));
        break;
    }
    case TileInstanceType::QuickAction: {
        std::vector<std::pair<std::wstring,std::wstring>> actions;
        for (const auto& action : TileActions) actions.emplace_back(action.id,action.label);
        AppendTileInstanceChoices(menu,tile,L"Action",L"Action",actions,L"toggle");
        AppendTileInstanceEditable(menu,tile,L"VolumeStepPercent",L"Volume step (percentage points, 1-100)",L"5");
        break;
    }
    case TileInstanceType::AppLauncher: {
        AppendTileInstanceCommand(menu,TileInstanceUi(L"AppLauncherBrowseTarget",L"Choose program or document..."),{TileInstanceMenuVerb::BrowseLauncherTarget,tile.id,L"",L"",L""});
        AppendTileInstanceEditable(menu,tile,L"Launcher.Target",L"Program, document, folder or URL",L"");
        AppendTileInstanceEditable(menu,tile,L"Launcher.Arguments",L"Program arguments",L"");
        AppendTileInstanceEditable(menu,tile,L"Launcher.WorkingDirectory",L"Working directory (optional)",L"");
        AppendTileInstanceChoices(menu,tile,L"Launcher.IconMode",L"Icon source",{{L"Auto",L"Use the program/file icon"},{L"Custom",L"Use a custom icon or image"},{L"None",L"No icon"}},L"Auto");
        AppendTileInstanceCommand(menu,TileInstanceUi(L"AppLauncherBrowseIcon",L"Choose custom icon or image..."),{TileInstanceMenuVerb::BrowseLauncherIcon,tile.id,L"",L"",L""});
        AppendTileInstanceEditable(menu,tile,L"Launcher.IconPath",L"Custom icon/image file",L"");
        AppendTileInstanceEditable(menu,tile,L"Launcher.IconIndex",L"Icon resource index",L"0");
        AppendTileInstanceEditable(menu,tile,L"Launcher.IconSizePercent",L"Icon size (% of shorter tile side)",L"64");
        break;
    }
    case TileInstanceType::Mail: {
        AppendTileInstanceCommand(menu,TileInstanceUi(L"GmailSetup",L"Google account for this Mail tile..."),{TileInstanceMenuVerb::SignIn,tile.id,L"",L"",L""});
        AppendTileInstanceChoices(menu,tile,L"Mail.Preview",L"Read access",{{L"Headers",L"Sender and subject only"},{L"Snippet",L"Also read message previews (reconnect required)"}},L"Headers");
        AppendTileInstanceEditable(menu,tile,L"Mail.UnreadOnly",L"Show only unread Inbox messages",L"1",true);
        AppendTileInstanceEditable(menu,tile,L"Mail.BadgeEnabled",L"Show unread count badge",L"1",true);
        AppendTileInstanceEditable(menu,tile,L"Mail.MaxMessages",L"Messages to rotate (1-5)",L"3");
        AppendTileInstanceChoices(menu,tile,L"Mail.PrimaryField",L"Primary message text",{{L"Sender",L"Sender"},{L"Subject",L"Subject"}},L"Sender");
        AppendTileInstanceChoices(menu,tile,L"Mail.SecondaryField",L"Secondary message text",{{L"SubjectAndSnippet",L"Subject and permitted preview"},{L"Subject",L"Subject"},{L"Snippet",L"Permitted preview only"}},L"SubjectAndSnippet");
        HMENU advanced=aip::BeginTrayNestedMenu(menu);
        AppendTileInstanceEditable(advanced,tile,L"Mail.RefreshSeconds",L"Fetch Inbox every (seconds)",L"120");
        AppendTileInstanceEditable(advanced,tile,L"Mail.TimeoutSeconds",L"HTTP timeout (seconds)",L"15");
        AppendTileInstanceEditable(advanced,tile,L"Mail.MaximumBytes",L"Aggregate response limit (bytes)",L"2097152");
        AppendTileInstanceEditable(advanced,tile,L"Mail.ClientId",L"Google Desktop OAuth client ID",L"");
        AppendTileInstanceEditable(advanced,tile,L"Mail.ClientSecret",L"Google Desktop OAuth client secret",L"",false,true);
        aip::EndTrayNestedMenu(menu,advanced,TileInstanceUi(L"TileInstanceAdvanced",L"Advanced"));break;
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
    if(tile.type==TileInstanceType::Calendar)AppendCalendarReminderMenu(menu,tile);
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
        if(tile.type==TileInstanceType::Mail)actions.push_back({L"source",TileInstanceUi(L"GmailOpenInbox",L"Open the Gmail inbox")});
        if(tile.type==TileInstanceType::AppLauncher)actions.push_back({L"launch",TileInstanceUi(L"AppLauncherOpenTarget",L"Open this tile's program or target")});
        if(tile.type==TileInstanceType::Discord)actions.push_back({L"discord",TileInstanceUi(L"DiscordReaderTileClick",L"Use this Discord view's click action")});
        for (const auto& action : TileActions) actions.emplace_back(action.id,action.label);
        AppendTileInstanceChoices(menu,tile,L"ClickAction",L"Click action",actions,tile.clickAction.c_str());
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
    if(!cfg.configured){
        aip::content::TileInstanceMigrationPlan plan;
        if(!aip::content::PlanLegacyTileInstanceMigration(IniReadS,plan,error)||plan.legacyModeRequired){
            NotifyUserOperation(L"legacy-layout-kept",L"Legacy layout preserved",L"This profile uses a legacy composition that cannot be converted without changing its layout. Keep using it, or create a separate profile before adding independent tiles.",NIIF_WARNING);
            return false;
        }
    }
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
        if(primary&&std::wstring(field.key)==L"Delivery.PrimaryRegistration")value=L"Main";
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
    adapter.policy=row.delivery;
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
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testBrowseAppLauncher)(HWND,bool,std::wstring&)=nullptr;
#endif
static bool BrowseAppLauncherFile(HWND owner,bool icon,std::wstring& path)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testBrowseAppLauncher&&g_testBrowseAppLauncher(owner,icon,path);
#else
    auto filter=TileInstanceUi(icon?L"AppLauncherIconFilter":L"AppLauncherTargetFilter",icon?
        L"Icons and images|*.ico;*.exe;*.dll;*.png;*.jpg;*.jpeg;*.bmp;*.gif|All files|*.*|":
        L"Programs and shortcuts|*.exe;*.com;*.lnk|All files|*.*|");
    std::replace(filter.begin(),filter.end(),L'|',L'\0');filter.push_back(L'\0');
    const auto title=TileInstanceUi(icon?L"AppLauncherBrowseIcon":L"AppLauncherBrowseTarget",icon?L"Choose custom icon or image...":L"Choose program or document...");
    std::vector<wchar_t> file(32768,L'\0');OPENFILENAMEW picker{};picker.lStructSize=sizeof(picker);picker.hwndOwner=owner;
    picker.lpstrTitle=title.c_str();picker.lpstrFilter=filter.c_str();picker.lpstrFile=file.data();picker.nMaxFile=static_cast<DWORD>(file.size());
    picker.Flags=OFN_EXPLORER|OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_DONTADDTORECENT;
    if(!GetOpenFileNameW(&picker))return false;path=file.data();return true;
#endif
}
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
    if(item.verb==TileInstanceMenuVerb::DiscordCredential||item.verb==TileInstanceMenuVerb::DiscordConnect){if(tile.type==TileInstanceType::Discord)ConfigureDiscordReader(owner,tile.id,item.verb==TileInstanceMenuVerb::DiscordCredential);return true;}
    if(item.verb==TileInstanceMenuVerb::DiscordCompose){if(tile.type==TileInstanceType::Discord)ActivateDiscordReader(owner,tile.id,true);return true;}
    if(item.verb==TileInstanceMenuVerb::DiscordDisconnect){if(tile.type==TileInstanceType::Discord&&SaveTileInstanceSettings(owner,{{section,L"DiscordReader.Enabled",L"0"}},tile.id))ForgetDiscordReaderSessionCredential(DiscordReaderProfileKey(tile.id));return true;}
    if(item.verb==TileInstanceMenuVerb::DiscordPolicy){ShellExecuteW(owner,L"open",L"https://support.discord.com/hc/en-us/articles/115002192352-Automated-User-Accounts-Self-Bots",nullptr,nullptr,SW_SHOWNORMAL);return true;}
    if(item.verb==TileInstanceMenuVerb::SelectSmtcSource){
        if(tile.type!=TileInstanceType::NowPlaying)return true;
        auto app=item.value;if(!NormalizeSmtcSelectionSetting(L"sessionappid",app,error))return true;
        SaveConfirmedTileInstanceSettings(owner,{{section,L"SMTC.SessionMode",app.empty()?L"Current":L"Application"},{section,L"SMTC.SessionAppId",app},{section,L"SMTC.SessionInstance",L"0"}},tile.id,
            [section](const std::wstring& text,std::wstring& issue){
                std::wstring type;const auto document=aip::ParseIniDocument(text);
                if(aip::ReadIniValueFromDoc(document,section.c_str(),L"Type",type)&&Lower(type)==L"nowplaying")return true;
                issue=L"This tile changed. Reopen its settings before choosing a media source.";return false;
            });return true;
    }
    if(item.verb==TileInstanceMenuVerb::NowPlayingLayout){
        if(tile.type!=TileInstanceType::NowPlaying||(item.value!=L"Info"&&item.value!=L"AlbumArt"))return true;
        const bool artwork=item.value==L"AlbumArt";
        SaveConfirmedTileInstanceSettings(owner,{{section,L"Content.Background",artwork?L"SMTCArtwork":L"Transparent"},
            {section,L"TextMode",artwork?L"Off":L"Auto"},{section,L"Branding",artwork?L"Name":L"NameAndLogo"}},tile.id,
            [section](const std::wstring& text,std::wstring& issue){
                std::wstring type;const auto document=aip::ParseIniDocument(text);
                if(aip::ReadIniValueFromDoc(document,section.c_str(),L"Type",type)&&Lower(type)==L"nowplaying")return true;
                issue=L"The tile type changed. Reopen its settings before applying a Now Playing preset.";return false;
            });
        return true;
    }
    if(item.verb==TileInstanceMenuVerb::BrowseLauncherTarget||item.verb==TileInstanceMenuVerb::BrowseLauncherIcon){
        if(tile.type!=TileInstanceType::AppLauncher)return true;const bool icon=item.verb==TileInstanceMenuVerb::BrowseLauncherIcon;
        std::wstring path;if(!BrowseAppLauncherFile(owner,icon,path))return true;
        std::vector<aip::IniSetting> settings{{section,icon?L"Launcher.IconPath":L"Launcher.Target",path}};
        if(icon){settings.push_back({section,L"Launcher.IconMode",L"Custom"});settings.push_back({section,L"Launcher.IconIndex",L"0"});}
        SaveConfirmedTileInstanceSettings(owner,settings,tile.id,[section](const std::wstring& text,std::wstring& issue){
            const auto document=aip::ParseIniDocument(text);std::wstring type;
            if(!aip::ReadIniValueFromDoc(document,section.c_str(),L"Type",type)||Lower(type)!=L"applauncher"){
                issue=L"This tile changed while the file picker was open. Reopen its current settings before choosing a target or icon.";return false;}
            return true;
        });return true;
    }
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
        if(tile.type==TileInstanceType::Mail){OpenGmailSetup(owner,tile.id);return true;}
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
        if(tile.primary&&tile.policy.Read(L"Settings",L"PrimaryRegistration",L"Separate")==L"Main"){
            if(!NormalizeTilePolicySetting(item.key,value,error))return true;
            if(!SaveTileInstanceSettings(owner,{{section,item.key,value}},tile.id))return true;
            if(value==L"Registration"&&tile.policy.Mode()==L"LiveTile")SetLiveTileModeReregistrationPending(true);
            if(value==L"LiveTile"&&!CurrentProcessHasPackageIdentity())RequestDesktopHostSetup(owner);
            else RefreshTileInstanceNow(tile.id);
            return true;
        }
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
    if (IsTextPresentationKey(item.key) && Lower(item.key) != L"textpreset"&&!IsGameTextLayoutKey(item.key)){
        if((DescribeTileInstanceType(tile.type).notesResource>=0||tile.type==TileInstanceType::Wuwa)&&!tile.presentation.Active()){
            const std::pair<const wchar_t*,const wchar_t*> defaults[]={{L"TextStyle",L"base"},{L"TextSecondaryStyle",L"captionSubtle"},{L"TextBadgeStyle",L"captionSubtle"},
                {L"TextWrap",L"1"},{L"TextMaxLines",L"2"},{L"TextSecondaryMaxLines",L"2"},{L"TextGapLines",L"0"},{L"TextAlign",L"left"},{L"TextStacking",L"top"}};
            for(const auto& field:defaults)if(Lower(field.first)!=Lower(item.key))settings.push_back({section,field.first,field.second});
        }
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

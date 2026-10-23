// Per-tile source notifications reuse the content poll and the already fetched
// snapshot. No additional timer, worker, network request or location lookup.
#include "../tile_source_alerts.h"
struct TileSourceAlertRuntime {
    aip::notifications::AlertState state;
    std::wstring owner,path,lastSaved;
    bool persistenceWarning=false;
    ULONGLONG nextSaveAttempt=0;
    std::wstring currentTagKey,currentGroup,currentDeliveryKey;
    ULONGLONG nextCurrentAttempt=0;
};
static std::map<std::wstring,TileSourceAlertRuntime> g_tileSourceAlerts;
static bool SaveTileSourceAlertState(TileSourceAlertRuntime& runtime);
static void RemovePrayerCurrentStatus(const std::wstring& tileId)
{
    const auto found=g_tileSourceAlerts.find(tileId);if(found==g_tileSourceAlerts.end())return;
    auto& runtime=found->second;
    if(!runtime.currentTagKey.empty())RemoveQuietUserStatus(runtime.currentTagKey,runtime.currentGroup);
    // This is an explicit application removal (disable, source change or
    // invalid configuration), rather than a user dismissal in Action Center.
    // Allow one fresh quiet entry if that setting/source becomes active again.
    bool changed=false;
    if(!runtime.currentDeliveryKey.empty()) {
        const auto delivered=std::find(runtime.state.delivered.begin(),runtime.state.delivered.end(),runtime.currentDeliveryKey);
        if(delivered!=runtime.state.delivered.end()){runtime.state.delivered.erase(delivered);changed=true;}
    }
    runtime.currentTagKey.clear();runtime.currentGroup.clear();runtime.currentDeliveryKey.clear();runtime.nextCurrentAttempt=0;
    if(changed)SaveTileSourceAlertState(runtime);
}
static void ClearPrayerCurrentStatuses()
{
    for(const auto& entry:g_tileSourceAlerts)RemovePrayerCurrentStatus(entry.first);
}
static void PrunePrayerCurrentStatuses(const std::set<std::wstring>& used)
{
    for(const auto& entry:g_tileSourceAlerts)if(!used.count(entry.first))RemovePrayerCurrentStatus(entry.first);
}
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testLoadSourceAlerts)(const std::wstring&,const std::wstring&,aip::notifications::AlertState&)=nullptr;
static bool (*g_testSaveSourceAlerts)(const std::wstring&,const std::wstring&)=nullptr;
static void (*g_testSourceAlertEvent)(const std::wstring&,const std::wstring&,const std::wstring&,
    aip::notifications::EventType,aip::notifications::EventSound)=nullptr;
#endif
static void EmitTileSourceAlert(const std::wstring& key,const std::wstring& title,const std::wstring& message,
    aip::notifications::EventType type,aip::notifications::EventSound sound)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testSourceAlertEvent){g_testSourceAlertEvent(key,title,message,type,sound);return;}
#endif
    NotifyScheduledUserEvent(key,title,message,type,sound);
}
static bool LoadTileSourceAlertState(const std::wstring& path,const std::wstring& scope,aip::notifications::AlertState& state)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testLoadSourceAlerts&&g_testLoadSourceAlerts(path,scope,state);
#else
    aip::IniWriteMutexGuard guard(path,250);if(!guard.IsLocked())return false;
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    struct Close {HANDLE file;~Close(){CloseHandle(file);}} close{file};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
    if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)||
        !GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>150000)return false;
    std::vector<BYTE> bytes(static_cast<size_t>(length.QuadPart));DWORD read=0;
    if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())return false;
    std::wstring text;
    return DecodeTextBytes(bytes,text)&&aip::notifications::ParseAlertState(text,scope,state);
#endif
}
static bool SaveTileSourceAlertState(TileSourceAlertRuntime& runtime)
{
    const auto text=aip::notifications::SerializeAlertState(runtime.state);
    if(text.empty()||text==runtime.lastSaved)return !text.empty();
    if(GetTickCount64()<runtime.nextSaveAttempt)return false;
    bool saved=false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    saved=g_testSaveSourceAlerts&&g_testSaveSourceAlerts(runtime.path,text);
#else
    const auto attributes=GetFileAttributesW(runtime.path.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))) {
        aip::IniWriteMutexGuard guard(runtime.path,250);
        saved=guard.IsLocked()&&WriteUtf8BomTextFile(runtime.path,text);
    }
#endif
    if(saved){runtime.lastSaved=text;runtime.nextSaveAttempt=0;}
    else {
        runtime.nextSaveAttempt=GetTickCount64()+30000;
        if(!runtime.persistenceWarning){runtime.persistenceWarning=true;LogText(L"[!] A tile notification baseline could not be saved; duplicates remain suppressed for this run.");}
    }
    return saved;
}
static bool ReadTileSourceAlerts(const aip::content::TileInstance& tile,aip::notifications::AlertSettings& settings)
{
    bool enabled=false;
    if(!aip::content::ParseBool(aip::content::TileInstanceRead(tile,IniReadS,L"Alerts",L"Enabled",L"0"),enabled)||!enabled)return false;
    std::wstring ignored;
    return aip::notifications::ReadAlertSettings([&](const wchar_t* key,const wchar_t* fallback){
        return aip::content::TileInstanceRead(tile,IniReadS,L"Alerts",key,fallback);
    },settings,ignored)&&settings.enabled&&!g_commandLine.renderOnly&&!g_commandLine.configureOnly&&!g_shutdownState.IsRequested();
}
static std::wstring SourceAlertReplace(std::wstring text,const std::wstring& token,const std::wstring& value)
{
    size_t at=0;while((at=text.find(token,at))!=std::wstring::npos){text.replace(at,token.size(),value);at+=value.size();}return text;
}
static TileSourceAlertRuntime* TileSourceAlertState(const aip::content::TileInstance& tile,const std::wstring& source)
{
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    const auto user=CurrentTileRegistrationUserScope();if(user.empty()||profile.empty()){RemovePrayerCurrentStatus(tile.id);return nullptr;}
    const auto profilePath=profile;
    profile=ToLowerCopy(profile);
    const auto owner=aip::notifications::StableId(user+L"|"+profile+L"|"+tile.id);
    const auto scope=aip::notifications::StableId(owner+L"|"+source);
    auto& runtime=g_tileSourceAlerts[tile.id];
    if(runtime.owner!=owner||runtime.state.scope!=scope) {
        RemovePrayerCurrentStatus(tile.id);
        runtime={};runtime.owner=owner;runtime.path=profilePath+L".alerts."+owner;
        aip::notifications::SetAlertScope(runtime.state,scope);
        LoadTileSourceAlertState(runtime.path,scope,runtime.state);
        runtime.lastSaved=aip::notifications::SerializeAlertState(runtime.state);
    }
    return &runtime;
}
static void ObserveRssTileAlerts(const aip::content::TileInstance& tile,const RssFeedSnapshot& feed)
{
    RemovePrayerCurrentStatus(tile.id); // A reused tile ID must not retain its former prayer card.
    aip::notifications::AlertSettings settings;
    if(!feed.ok||feed.items.empty()||!ReadTileSourceAlerts(tile,settings))return;
    const auto source=aip::content::TileInstanceRead(tile,IniReadS,L"RssFeed",L"FeedUrl",L"");
    auto* runtime=TileSourceAlertState(tile,L"rss|"+source);if(!runtime)return;
    std::vector<std::wstring> identities;
    for(const auto& item:feed.items)identities.push_back(aip::notifications::StableId(
        !item.identity.empty()?L"id:"+item.identity:!item.link.empty()?L"url:"+item.link:L"text:"+item.title+L"|"+item.date));
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto count=aip::notifications::ObserveRss(runtime->state,identities,now,settings,NotificationAllowed(false));
    SaveTileSourceAlertState(*runtime);
    if(!count)return;
    auto title=settings.title.empty()?IniReadS(L"Strings",L"NotificationRssTitle",L"Feed updated"):settings.title;
    auto message=settings.body.empty()?IniReadS(L"Strings",L"NotificationRssNewItems",L"%count% new items in %tile%."):settings.body;
    const auto format=[&](std::wstring text){text=SourceAlertReplace(text,L"%count%",std::to_wstring(count));return SourceAlertReplace(text,L"%tile%",tile.name);};
    EmitTileSourceAlert(L"rss-new-"+runtime->owner,format(title),format(message),settings.type,settings.sound);
}
static std::wstring PrayerNotificationTime(aip::prayer::UTC utc,int offset,const aip::prayer::Configuration& cfg,
    const aip::notifications::AlertSettings& settings)
{
    if(settings.timeFormat==L"System") {
        const auto local=aip::prayer::DateAtUTC(utc,offset);
        const auto seconds=utc+static_cast<aip::prayer::UTC>(offset)*60;
        const auto within=seconds-aip::prayer::FloorDay(seconds)*86400;
        SYSTEMTIME system{};system.wYear=static_cast<WORD>(local.year);system.wMonth=static_cast<WORD>(local.month);system.wDay=static_cast<WORD>(local.day);
        system.wHour=static_cast<WORD>(within/3600);system.wMinute=static_cast<WORD>(within%3600/60);
        wchar_t text[128]{};if(GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&system,nullptr,text,static_cast<int>(std::size(text))))return text;
    }
    const bool twentyFour=settings.timeFormat==L"24Hour"||(settings.timeFormat==L"Inherit"&&cfg.twentyFourHour);
    return aip::prayer::TimeText(utc,offset,twentyFour);
}
static std::wstring PrayerNotificationText(std::wstring text,const aip::content::TileInstance& tile,
    aip::prayer::PrayerId event,aip::prayer::UTC utc,int offset,aip::prayer::UTC now,const aip::notifications::AlertSettings& settings)
{
    const auto& cfg=tile.configuration.prayer;
    const auto minutes=(std::max)(aip::prayer::UTC(0),(utc-now+59)/60);
    for(const auto& entry:std::vector<std::pair<std::wstring,std::wstring>>{{L"%event%",aip::prayer::PrayerName(event)},
        {L"%minutes%",std::to_wstring(minutes)},{L"%time%",PrayerNotificationTime(utc,offset,cfg,settings)},
        {L"%city%",cfg.city},{L"%country%",cfg.country},{L"%tile%",tile.name}})text=SourceAlertReplace(text,entry.first,entry.second);
    return text;
}
static void ObservePrayerTileAlerts(const aip::content::TileInstance& tile,const aip::prayer::Data& data,aip::prayer::UTC now)
{
    aip::notifications::AlertSettings settings;
    if(!ReadTileSourceAlerts(tile,settings)||!NotificationAllowed(false)){RemovePrayerCurrentStatus(tile.id);return;}
    const auto& cfg=tile.configuration.prayer;
    if(cfg.city.empty()||cfg.country.empty()||data.configKey!=aip::prayer::ConfigurationKey(cfg)){RemovePrayerCurrentStatus(tile.id);return;}
    auto* runtime=TileSourceAlertState(tile,L"prayer|"+aip::prayer::ConfigurationKey(cfg));if(!runtime)return;
    const auto events=aip::notifications::DuePrayers(cfg,data,now,settings,runtime->state);
    SaveTileSourceAlertState(*runtime);
    for(const auto& event:events) {
        const bool upcoming=event.phase==aip::notifications::PrayerAlertPhase::Upcoming;
        EmitTileSourceAlert(L"prayer-"+runtime->owner+L"-"+event.key,
            PrayerNotificationText(upcoming?settings.upcomingTitle:settings.atTimeTitle,tile,event.event,event.eventUtc,event.offsetMinutes,now,settings),
            PrayerNotificationText(upcoming?settings.upcomingBody:settings.atTimeBody,tile,event.event,event.eventUtc,event.offsetMinutes,now,settings),settings.type,settings.sound);
    }
    const auto current=aip::notifications::CurrentPrayer(cfg,data,now,settings);
    if(aip::notifications::ParseApi(IniReadS(L"Settings",L"NotificationApi",L"Auto"))==aip::notifications::Api::Balloon){
        RemovePrayerCurrentStatus(tile.id);SaveTileSourceAlertState(*runtime);return;
    }
    if(!current.available){RemovePrayerCurrentStatus(tile.id);SaveTileSourceAlertState(*runtime);return;}
    const auto title=PrayerNotificationText(settings.currentTitle,tile,current.event,current.eventUtc,current.offsetMinutes,now,settings);
    const auto body=PrayerNotificationText(settings.currentBody,tile,current.event,current.eventUtc,current.offsetMinutes,now,settings);
    const auto delivery=aip::notifications::StableId(current.key+L"|"+std::to_wstring(current.endUtc)+L"|"+title+L"|"+body);
    if(runtime->currentDeliveryKey!=delivery) {
        runtime->currentTagKey=L"prayer-current-"+runtime->owner;
        runtime->currentGroup=NotificationGroup();runtime->currentDeliveryKey=delivery;runtime->nextCurrentAttempt=0;
    }
    // A dismissed history entry stays dismissed, including across a restart.
    // Its native expiration removes it at the next actual prayer even if the
    // resident exits. Content changes or the next prayer get a new delivery ID.
    if(std::find(runtime->state.delivered.begin(),runtime->state.delivered.end(),delivery)==runtime->state.delivered.end()&&
        GetTickCount64()>=runtime->nextCurrentAttempt) {
        if(NotifyQuietUserStatus(runtime->currentTagKey,title,body,current.endUtc,runtime->currentGroup))
            aip::notifications::Remember(runtime->state.delivered,delivery,128);
        else runtime->nextCurrentAttempt=GetTickCount64()+30000;
    }
    SaveTileSourceAlertState(*runtime);
}

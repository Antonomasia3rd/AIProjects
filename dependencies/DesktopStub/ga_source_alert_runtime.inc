// Per-tile source notifications reuse the content poll and the already fetched
// snapshot. No additional timer, worker, network request or location lookup.
#include "../tile_source_alerts.h"
struct TileSourceAlertRuntime {
    aip::notifications::AlertState state;
    std::wstring owner,path,lastSaved;
    bool persistenceWarning=false;
    ULONGLONG nextSaveAttempt=0;
};
static std::map<std::wstring,TileSourceAlertRuntime> g_tileSourceAlerts;
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
    const auto user=CurrentTileRegistrationUserScope();if(user.empty()||profile.empty())return nullptr;
    const auto profilePath=profile;
    profile=ToLowerCopy(profile);
    const auto owner=aip::notifications::StableId(user+L"|"+profile+L"|"+tile.id);
    const auto scope=aip::notifications::StableId(owner+L"|"+source);
    auto& runtime=g_tileSourceAlerts[tile.id];
    if(runtime.owner!=owner||runtime.state.scope!=scope) {
        runtime={};runtime.owner=owner;runtime.path=profilePath+L".alerts."+owner;
        aip::notifications::SetAlertScope(runtime.state,scope);
        LoadTileSourceAlertState(runtime.path,scope,runtime.state);
        runtime.lastSaved=aip::notifications::SerializeAlertState(runtime.state);
    }
    return &runtime;
}
static void ObserveRssTileAlerts(const aip::content::TileInstance& tile,const RssFeedSnapshot& feed)
{
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
    auto message=IniReadS(L"Strings",L"NotificationRssNewItems",L"%count% new items in %tile%.");
    message=SourceAlertReplace(message,L"%count%",std::to_wstring(count));message=SourceAlertReplace(message,L"%tile%",tile.name);
    EmitTileSourceAlert(L"rss-new-"+runtime->owner,IniReadS(L"Strings",L"NotificationRssTitle",L"Feed updated"),message,settings.type,settings.sound);
}
static void ObservePrayerTileAlerts(const aip::content::TileInstance& tile,const aip::prayer::Data& data,aip::prayer::UTC now)
{
    aip::notifications::AlertSettings settings;if(!ReadTileSourceAlerts(tile,settings)||!NotificationAllowed(false))return;
    const auto& cfg=tile.configuration.prayer;
    if(cfg.city.empty()||cfg.country.empty()||data.configKey!=aip::prayer::ConfigurationKey(cfg))return;
    auto* runtime=TileSourceAlertState(tile,L"prayer|"+aip::prayer::ConfigurationKey(cfg));if(!runtime)return;
    const auto events=aip::notifications::DuePrayers(cfg,data,now,settings,runtime->state);
    if(events.empty())return;
    SaveTileSourceAlertState(*runtime);
    for(const auto& event:events) {
        const auto minutes=(std::max)(aip::prayer::UTC(0),(event.eventUtc-now+59)/60);
        auto message=IniReadS(L"Strings",minutes?L"NotificationPrayerBefore":L"NotificationPrayerNow",
            minutes?L"%event% in %minutes% minutes at %city% (%time%).":L"It is time for %event% at %city% (%time%).");
        message=SourceAlertReplace(message,L"%event%",aip::prayer::PrayerName(event.event));
        message=SourceAlertReplace(message,L"%minutes%",std::to_wstring(minutes));message=SourceAlertReplace(message,L"%city%",cfg.city);
        message=SourceAlertReplace(message,L"%time%",aip::prayer::TimeText(event.eventUtc,event.offsetMinutes,cfg.twentyFourHour));
        EmitTileSourceAlert(L"prayer-"+runtime->owner+L"-"+event.key,
            IniReadS(L"Strings",L"NotificationPrayerTitle",L"Prayer times"),message,settings.type,settings.sound);
    }
}

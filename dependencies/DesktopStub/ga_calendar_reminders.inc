// Existing Calendar reads and poll wakeups own reminder demand. No extra worker
// or timer is installed; Windows itself owns the Snooze/Dismiss button behavior.
#include "../calendar_reminders.h"
struct CalendarReminderRuntime {
    aip::calendar_reminders::State state;
    std::wstring owner,path,group,lastSaved,settingsKey;
    ULONGLONG retrySaveAfter=0;
    ULONGLONG retryLoadAfter=0;
    bool warned=false,historyBlocked=false;
};
enum class CalendarReminderLoad {Missing,Loaded,Failed};
static std::map<std::wstring,CalendarReminderRuntime> g_calendarReminders;
#ifdef DESKTOPSTUB_INERT_HARDWARE
static CalendarReminderLoad (*g_testLoadCalendarReminders)(const std::wstring&,const std::wstring&,aip::calendar_reminders::State&)=nullptr;
static bool (*g_testSaveCalendarReminders)(const std::wstring&,const std::wstring&)=nullptr;
static bool (*g_testEmitCalendarReminder)(const std::wstring&,const std::wstring&,const std::wstring&,
    const std::wstring&,const aip::notifications::NativeNoticeOptions&)=nullptr;
static bool (*g_testRemoveCalendarReminder)(const std::wstring&,const std::wstring&)=nullptr;
#endif
static bool ReadCalendarReminderSettings(const aip::content::TileInstance& tile,aip::calendar_reminders::Settings& settings)
{
    if(tile.type!=aip::content::TileInstanceType::Calendar)return false;
    std::wstring error;return aip::calendar_reminders::ReadSettings([&](const wchar_t* key,const wchar_t* fallback){
        return aip::content::TileInstanceRead(tile,IniReadS,L"CalendarReminders",key,fallback);
    },settings,error);
}
static bool CalendarRemindersDemanded(const aip::content::TileInstance& tile)
{
    aip::calendar_reminders::Settings settings;return ReadCalendarReminderSettings(tile,settings)&&settings.enabled&&
        !g_commandLine.renderOnly&&!g_commandLine.configureOnly&&!g_shutdownState.IsRequested();
}
static CalendarReminderLoad LoadCalendarReminderState(CalendarReminderRuntime& runtime)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testLoadCalendarReminders?g_testLoadCalendarReminders(runtime.path,runtime.state.scope,runtime.state):CalendarReminderLoad::Failed;
#else
    aip::IniWriteMutexGuard guard(runtime.path,250);if(!guard.IsLocked())return CalendarReminderLoad::Failed;
    HANDLE file=CreateFileW(runtime.path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){const auto error=GetLastError();return error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?CalendarReminderLoad::Missing:CalendarReminderLoad::Failed;}
    struct Close{HANDLE value;~Close(){CloseHandle(value);}}close{file};
    BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
    if(!GetFileInformationByHandle(file,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        !GetFileSizeEx(file,&length)||length.QuadPart<=0||length.QuadPart>200000)return CalendarReminderLoad::Failed;
    std::vector<BYTE> bytes(static_cast<std::size_t>(length.QuadPart));DWORD read=0;std::wstring text;
    return ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)&&read==bytes.size()&&
        DecodeTextBytes(bytes,text)&&aip::calendar_reminders::Parse(text,runtime.state.scope,runtime.state)?CalendarReminderLoad::Loaded:CalendarReminderLoad::Failed;
#endif
}
static bool SaveCalendarReminderState(CalendarReminderRuntime& runtime)
{
    const auto text=aip::calendar_reminders::Serialize(runtime.state);
    if(text.empty())return false;if(text==runtime.lastSaved)return true;
    if(GetTickCount64()<runtime.retrySaveAfter)return false;
    bool saved=false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    saved=g_testSaveCalendarReminders&&g_testSaveCalendarReminders(runtime.path,text);
#else
    aip::IniWriteMutexGuard guard(runtime.path,250);const auto attributes=GetFileAttributesW(runtime.path.c_str());
    const auto error=attributes==INVALID_FILE_ATTRIBUTES?GetLastError():ERROR_SUCCESS;
    if(guard.IsLocked()&&((attributes==INVALID_FILE_ATTRIBUTES&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND))||
        (attributes!=INVALID_FILE_ATTRIBUTES&&!(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))))
        saved=WriteUtf8BomTextFile(runtime.path,text);
#endif
    if(saved){runtime.lastSaved=text;runtime.retrySaveAfter=0;}
    else{runtime.retrySaveAfter=GetTickCount64()+30000;if(!runtime.warned){runtime.warned=true;
        LogText(L"[!] Calendar reminder history could not be saved; reminder delivery is paused to avoid restart duplicates.");}}
    return saved;
}
static std::wstring CalendarReminderTag(const CalendarReminderRuntime& runtime,const std::wstring& key)
{
    return L"calendar-reminder-"+runtime.owner+L"-"+key;
}
static bool RemoveCalendarReminderToast(const std::wstring& key,const std::wstring& group)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testRemoveCalendarReminder&&g_testRemoveCalendarReminder(key,group);
#else
    if(!NotificationExistingIdentity())return true;
    const bool history=RemoveQuietUserStatus(key,group);
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);
    struct Apartment{HRESULT result;~Apartment(){if(SUCCEEDED(result))RoUninitialize();}}apartment{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return false;
    try{
        using namespace winrt::Windows::UI::Notifications;
        const auto manager=aip::AcquireUncachedActivationFactory<ToastNotificationManager,IToastNotificationManagerStatics>();
        UINT32 length=0;if(GetCurrentApplicationUserModelId(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||length<2||length>1024)return false;
        std::vector<wchar_t> aumid(length);if(GetCurrentApplicationUserModelId(&length,aumid.data())!=ERROR_SUCCESS)return false;
        const auto notifier=manager.CreateToastNotifier(aumid.data());const auto tag=aip::notifications::StableId(key);
        // System-snoozed entries may appear in the platform's pending list.
        // Match both owned fields; never clear another tile/profile's queue.
        for(const auto& item:notifier.GetScheduledToastNotifications())if(item.Tag()==tag&&item.Group()==group)notifier.RemoveFromSchedule(item);
        return history;
    }catch(...){return false;}
#endif
}
static bool EmitCalendarReminder(const CalendarReminderRuntime& runtime,const aip::calendar_reminders::Occurrence& event,
    const aip::calendar_reminders::Settings& settings,const std::wstring& tileName,aip::calendar_reminders::UTC now)
{
    using namespace aip::calendar_reminders;
    if(!NotificationAllowed(false))return false;
    const auto title=Format(settings.title,event,tileName,now),body=Format(settings.body,event,tileName,now);
    const auto key=CalendarReminderTag(runtime,OccurrenceKey(event));
    const auto xml=Xml(title,body,settings,IniReadS(L"Strings",L"CalendarReminderMinutes",L"%minutes% minutes"));
    const aip::notifications::NativeNoticeOptions options{false,Expiry(event),runtime.group};
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testEmitCalendarReminder)return g_testEmitCalendarReminder(key,title,body,xml,options);
#endif
    if(ConfiguredNotificationApi()==aip::notifications::Api::Toast){
        if(NotificationToast(key,title,body,nullptr,xml,&options))return true;
        g_notificationToastRetryAfter=GetTickCount64()+30000;
    }
    return NotificationBalloon(title,body,NIIF_INFO|(settings.sound==aip::notifications::EventSound::Silent?NIIF_NOSOUND:0),false);
}
static void DeactivateCalendarReminders(CalendarReminderRuntime& runtime)
{
    bool changed=false;
    for(auto& record:runtime.state.delivered)if(record.active&&RemoveCalendarReminderToast(CalendarReminderTag(runtime,record.key),runtime.group)){
        record.active=false;changed=true;}
    if(changed)SaveCalendarReminderState(runtime);
}
static void ClearCalendarReminders()
{
    for(auto& entry:g_calendarReminders)DeactivateCalendarReminders(entry.second);
}
static void PruneCalendarReminders(const std::set<std::wstring>& used)
{
    for(auto& entry:g_calendarReminders)if(!used.count(entry.first))DeactivateCalendarReminders(entry.second);
}
static void ObserveCalendarReminders(const aip::content::TileInstance& tile,
    const std::vector<aip::calendar_reminders::Occurrence>& occurrences,aip::calendar_reminders::UTC now,bool authoritative=true)
{
    using namespace aip::calendar_reminders;
    Settings settings;
    const bool enabled=ReadCalendarReminderSettings(tile,settings)&&settings.enabled&&NotificationAllowed(false)&&
        !g_shutdownState.IsRequested();
    const auto existing=g_calendarReminders.find(tile.id);
    if(!enabled){if(existing!=g_calendarReminders.end())DeactivateCalendarReminders(existing->second);return;}
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    const auto user=CurrentTileRegistrationUserScope();if(profile.empty()||user.empty())return;
    const auto owner=aip::notifications::StableId(user+L"|"+ToLowerCopy(profile)+L"|"+tile.id);
    const auto source=tile.calendar.source==L"Google"?L"google|"+tile.googleCalendar.ownerKey+L"|"+tile.googleCalendar.clientId+
        L"|"+tile.googleCalendar.refreshTokenProtected+L"|"+(tile.googleCalendar.allCalendars?L"all":tile.googleCalendar.calendarId):
        L"local|"+ToLowerCopy(tile.calendar.filePath);
    const auto scope=aip::notifications::StableId(owner+L"|"+source);
    auto& runtime=g_calendarReminders[tile.id];
    if(runtime.owner!=owner||runtime.state.scope!=scope){
        DeactivateCalendarReminders(runtime);runtime={};runtime.owner=owner;runtime.group=NotificationGroup();
        runtime.path=profile+L".calendar-reminders."+owner+L"."+scope;runtime.state.scope=scope;
        runtime.historyBlocked=LoadCalendarReminderState(runtime)==CalendarReminderLoad::Failed;
        if(!runtime.historyBlocked)runtime.lastSaved=Serialize(runtime.state);
    }
    if(runtime.historyBlocked){
        if(GetTickCount64()<runtime.retryLoadAfter)return;
        runtime.historyBlocked=LoadCalendarReminderState(runtime)==CalendarReminderLoad::Failed;
        if(runtime.historyBlocked){runtime.retryLoadAfter=GetTickCount64()+30000;if(!runtime.warned){runtime.warned=true;
            LogText(L"[!] Calendar reminder history could not be read; delivery is paused to avoid replaying earlier reminders.");}return;}
        runtime.retryLoadAfter=0;runtime.lastSaved=Serialize(runtime.state);
    }
    const auto configuration=SettingsKey(settings);
    if(!runtime.settingsKey.empty()&&runtime.settingsKey!=configuration)DeactivateCalendarReminders(runtime);
    runtime.settingsKey=configuration;
    for(const auto& key:Removals(runtime.state,occurrences,now,authoritative,true))if(RemoveCalendarReminderToast(CalendarReminderTag(runtime,key),runtime.group)){
        Deactivate(runtime.state,key);SaveCalendarReminderState(runtime);}
    if(!authoritative||occurrences.size()>2000)return;
    for(const auto& event:Due(runtime.state,occurrences,now,settings)){
        const auto previous=runtime.state;
        if(!Claim(runtime.state,event,now))break;
        // A disk failure fails closed: do not announce an event that would be
        // replayed by the next process after its delivery marker was lost.
        if(!SaveCalendarReminderState(runtime)){runtime.state=previous;break;}
        if(!EmitCalendarReminder(runtime,event,settings,tile.name,now)){
            runtime.state=previous;SaveCalendarReminderState(runtime);break;
        }
    }
}

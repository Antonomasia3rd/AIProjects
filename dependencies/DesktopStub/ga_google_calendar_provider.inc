// Google accounts belong to one Windows user, profile and tile. Calendar
// refresh never starts authorization; only the explicit setup window can do so.
#include "../content_sources/google_calendar.inc"
static aip::google_calendar::Configuration GoogleCalendarConfigurationForTile(const aip::content::TileInstance& tile)
{
    auto cfg=tile.googleCalendar;std::wstring profile;
    {std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    const auto user=CurrentTileRegistrationUserScope();
    cfg.ownerKey=user.empty()||profile.empty()?L"":aip::notifications::StableId(user+L"|"+ToLowerCopy(profile)+L"|"+tile.id);
    return cfg;
}
class ContentGoogleCalendarProvider {
    using Result=std::shared_ptr<const ContentCalendarResult>;
    aip::content::AsyncSource<Result> source;
    std::shared_ptr<aip::google_calendar::Session> session=std::make_shared<aip::google_calendar::Session>();
    ContentDateTimeProvider header{WakePollThread};
    std::wstring key;
public:
#ifdef DESKTOPSTUB_INERT_HARDWARE
    aip::google_calendar::Hooks testHooks;
#endif
    ~ContentGoogleCalendarProvider(){Stop();}
    ContentDateTimeView Calendar(const aip::datetime::CalendarConfiguration& display,const aip::google_calendar::Configuration& cfg,
        aip::datetime::UTC now,int milliseconds=0,bool force=false,bool synchronous=false)
    {
        using namespace aip::datetime;
        auto headerCfg=display;headerCfg.filePath.clear();auto view=header.Calendar(headerCfg,now,milliseconds,false,false);
        if(!view.ok)return view;
        std::wstring error;
        if(!aip::google_calendar::RuntimeConfigValidate(cfg,error,true)) {
            Cancel();view.configured=false;view.status=cfg.clientId.empty()?L"Set up Google Calendar in this tile's source settings.":L"Connect Google Calendar in this tile's source settings.";
            return view;
        }
        if(display.daysAhead<1||display.daysAhead>31||display.maxEvents<1||display.maxEvents>10){Cancel();view.ok=false;view.status=L"Google Calendar supports a look-ahead of 1 to 31 days and 1 to 10 visible events.";return view;}
        const auto requested=aip::google_calendar::ConfigurationKey(cfg)+L"|"+std::to_wstring(now/86400)+L"|"+std::to_wstring(display.daysAhead);
        if(key!=requested){source.CancelPending();session->Clear();key=requested;}
        aip::google_calendar::Hooks hooks;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        hooks=testHooks;hooks.inert=true;
#endif
        const auto sharedSession=session;
        source.Request(requested,std::chrono::milliseconds(static_cast<long long>(cfg.refreshSeconds)*1000),
            [cfg,now,display,hooks,sharedSession](const aip::content::AsyncSource<Result>::Cancel& canceled)->Result {
                auto result=std::make_shared<ContentCalendarResult>();std::vector<aip::google_calendar::Event> events;
                const auto cancel=[&]{return canceled()||!g_running.load();};
                const auto day=now/86400*86400;
                if(!aip::google_calendar::FetchEvents(cfg,day,day+static_cast<UTC>(display.daysAhead+1)*86400,
                    events,result->error,cancel,hooks,sharedSession.get()))return result;
                const auto resolve=[](const Civil& civil,const std::wstring& iana,UTC& utc){
                    std::wstring windows;
                    return (aip::google_calendar::ResolveIanaWindowsTimeZone(iana,windows)&&ContentTimeZoneLocalToUtc(civil,windows,utc))||
                        ContentTimeZoneLocalToUtc(civil,iana,utc);
                };
                for(const auto& event:events){
                    if(cancel())return result;Occurrence occurrence;
                    if(!aip::google_calendar::ToOccurrence(event,resolve,occurrence)){
                        result->error=L"A Google Calendar event has an unsupported or ambiguous time zone.";return result;
                    }
                    result->occurrences.push_back(std::move(occurrence));
                }
                std::stable_sort(result->occurrences.begin(),result->occurrences.end(),[](const auto& a,const auto& b){return a.start<b.start;});
                result->ok=true;return result;
            },WakePollThread,force);
        if(synchronous&&!source.Wait(requested,std::chrono::seconds((std::min)(45,cfg.timeoutSeconds+2)))){
            source.CancelPending();view.ok=false;view.status=L"Google Calendar refresh timed out. DesktopStub will retry.";return view;
        }
        Result result;std::string privateError;
        if(!source.Read(requested,result,privateError)||!result){
            view.pending=privateError.empty();view.ok=privateError.empty();view.status=view.pending?L"Fetching Google Calendar events...":L"Google Calendar could not refresh. Reconnect in source settings if needed.";return view;
        }
        if(!result->ok){view.ok=false;view.status=result->error.empty()?L"Google Calendar could not refresh.":result->error;return view;}
        DYNAMIC_TIME_ZONE_INFORMATION zone{};ResolveContentTimeZone(display.timeZone,zone);
        for(const auto& event:result->occurrences){
            if(view.events.size()>=static_cast<size_t>(display.maxEvents))break;
            if(event.start>=now+static_cast<UTC>(display.daysAhead)*86400||!(event.end>now||(event.start==event.end&&event.start>=now)))continue;
            Civil local;if(!ContentTimeZoneUtcToLocal(event.start,zone,local))continue;auto system=DateTimeSystemTime(local);wchar_t date[128]{};
            GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&system,nullptr,date,static_cast<int>(std::size(date)),nullptr);
            view.events.push_back({event.summary,std::wstring(date)+L" · "+(event.allDay?L"All day":ContentDateTimeFormat(local,display.timeFormat)),display.showLocation?event.location:L"",event.allDay});
        }
        if(view.events.empty())view.status=L"No upcoming events.";return view;
    }
    void Cancel(){source.CancelPending();session->Clear();key.clear();header.Cancel();}
    void Stop(){source.CancelPending();source.Stop();session->Clear();key.clear();header.Stop();}
};

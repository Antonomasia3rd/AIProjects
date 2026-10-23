// Demand-created, offline clock and calendar providers. Calendar files are
// explicitly selected local files; no account store or network is inspected.
#include "../content_sources/clock_calendar_model.h"
#include "../content_source_host.h"
struct ContentDateTimeEvent { std::wstring title,when,location; bool allDay=false; };
struct ContentDateTimeView {
    bool ok=false,configured=true,pending=false;
    std::wstring time,weekday,dayNumber,date,location,status;
    std::vector<ContentDateTimeEvent> events;
    std::uint64_t nextDelayMs=60000;
    std::wstring shortWeekday;
    bool rightToLeft=false;
};
static SYSTEMTIME DateTimeSystemTime(const aip::datetime::Civil& c) {
    SYSTEMTIME s{};s.wYear=static_cast<WORD>(c.year);s.wMonth=static_cast<WORD>(c.month);s.wDay=static_cast<WORD>(c.day);
    s.wHour=static_cast<WORD>(c.hour);s.wMinute=static_cast<WORD>(c.minute);s.wSecond=static_cast<WORD>(c.second);return s;
}
static aip::datetime::Civil DateTimeCivil(const SYSTEMTIME& s) {return {s.wYear,s.wMonth,s.wDay,s.wHour,s.wMinute,s.wSecond};}
static bool ResolveContentTimeZone(std::wstring name,DYNAMIC_TIME_ZONE_INFORMATION& result) {
    if(name.empty()||aip::datetime::Lower(name)==L"local")return GetDynamicTimeZoneInformation(&result)!=TIME_ZONE_ID_INVALID;
    // Common exporter IANA names. Other zones are accepted by exact Windows
    // time-zone ID; an unknown custom VTIMEZONE is rejected, never guessed.
    const std::pair<const wchar_t*,const wchar_t*> aliases[]={
        {L"Etc/UTC",L"UTC"},{L"Etc/GMT",L"UTC"},{L"GMT",L"UTC"},{L"Asia/Jakarta",L"SE Asia Standard Time"},
        {L"Asia/Bangkok",L"SE Asia Standard Time"},{L"Asia/Singapore",L"Singapore Standard Time"},{L"Asia/Makassar",L"Singapore Standard Time"},
        {L"Asia/Jayapura",L"Tokyo Standard Time"},{L"Asia/Tokyo",L"Tokyo Standard Time"},{L"Asia/Seoul",L"Korea Standard Time"},
        {L"Asia/Shanghai",L"China Standard Time"},{L"Asia/Kolkata",L"India Standard Time"},{L"Asia/Dubai",L"Arabian Standard Time"},
        {L"Europe/London",L"GMT Standard Time"},{L"Europe/Paris",L"Romance Standard Time"},{L"Europe/Berlin",L"W. Europe Standard Time"},
        {L"America/New_York",L"Eastern Standard Time"},{L"America/Chicago",L"Central Standard Time"},{L"America/Denver",L"Mountain Standard Time"},
        {L"America/Los_Angeles",L"Pacific Standard Time"},{L"America/Phoenix",L"US Mountain Standard Time"},
        {L"Australia/Sydney",L"AUS Eastern Standard Time"},{L"Pacific/Auckland",L"New Zealand Standard Time"}};
    for(const auto& alias:aliases)if(aip::datetime::Lower(name)==aip::datetime::Lower(alias.first)){name=alias.second;break;}
    static std::once_flag loaded;static std::map<std::wstring,DYNAMIC_TIME_ZONE_INFORMATION> zones;
    std::call_once(loaded,[]{for(DWORD index=0;;++index){DYNAMIC_TIME_ZONE_INFORMATION zone{};const auto status=EnumDynamicTimeZoneInformation(index,&zone);if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)break;zones.emplace(aip::datetime::Lower(zone.TimeZoneKeyName),zone);}});
    const auto found=zones.find(aip::datetime::Lower(name));if(found==zones.end())return false;result=found->second;return true;
}
static bool ContentTimeZoneUtcToLocal(aip::datetime::UTC utc,const DYNAMIC_TIME_ZONE_INFORMATION& zone,aip::datetime::Civil& local) {
    const auto source=DateTimeSystemTime(aip::datetime::FromSeconds(utc));SYSTEMTIME target{};
    if(!SystemTimeToTzSpecificLocalTimeEx(&zone,&source,&target))return false;local=DateTimeCivil(target);return true;
}
static bool ContentTimeZoneLocalToUtc(const aip::datetime::Civil& local,const std::wstring& name,aip::datetime::UTC& utc) {
    using namespace aip::datetime;DYNAMIC_TIME_ZONE_INFORMATION zone{};if(!Valid(local)||!ResolveContentTimeZone(name,zone))return false;
    TIME_ZONE_INFORMATION year{};if(!GetTimeZoneInformationForYear(static_cast<USHORT>(local.year),&zone,&year))return false;
    std::set<UTC> matches;
    for(const auto bias:{year.StandardBias,year.DaylightBias}){
        if(zone.DynamicDaylightTimeDisabled&&bias!=year.StandardBias)continue;
        const auto candidate=Seconds(local)+static_cast<UTC>(year.Bias+bias)*60;Civil roundtrip;
        if(ContentTimeZoneUtcToLocal(candidate,zone,roundtrip)&&Seconds(roundtrip)==Seconds(local))matches.insert(candidate);
    }
    // A missing spring-forward time has no match; repeated autumn times have
    // two. Both are actionable errors instead of silently choosing an hour.
    if(matches.size()!=1)return false;utc=*matches.begin();return true;
}
static std::wstring ContentDateTimeFormat(const aip::datetime::Civil& civil,const std::wstring& format) {
    if(format!=L"System")return aip::datetime::Time(civil,format);
    auto system=DateTimeSystemTime(civil);wchar_t text[128]{};
    return GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT,TIME_NOSECONDS,&system,nullptr,text,static_cast<int>(std::size(text)))?text:aip::datetime::Time(civil,L"12Hour");
}
static bool ContentDateTimeRightToLeft(const wchar_t* localeName) {
    wchar_t readingLayout[8]{};
    // LOCALE_IREADINGLAYOUT: 0 left-to-right, 1 right-to-left. Vertical
    // layouts do not request the native Clock's right-to-left time mark.
    return GetLocaleInfoEx(localeName,LOCALE_IREADINGLAYOUT,readingLayout,static_cast<int>(std::size(readingLayout)))&&readingLayout[0]==L'1';
}
static bool ContentDateTimeUserInterfaceRightToLeft() {
    wchar_t localeName[LOCALE_NAME_MAX_LENGTH]{};
    return LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(),SORT_DEFAULT),localeName,static_cast<int>(std::size(localeName)),0)&&
        ContentDateTimeRightToLeft(localeName);
}
static ContentDateTimeView ContentDateTimeHeader(aip::datetime::UTC now,int milliseconds,const std::wstring& zoneName,const std::wstring& format) {
    ContentDateTimeView view;DYNAMIC_TIME_ZONE_INFORMATION zone{};aip::datetime::Civil local;
    if(!ResolveContentTimeZone(zoneName,zone)||!ContentTimeZoneUtcToLocal(now,zone,local)){
        view.status=L"Choose a Windows time-zone ID such as SE Asia Standard Time, or Local.";return view;
    }
    view.ok=true;view.time=ContentDateTimeFormat(local,format);view.dayNumber=std::to_wstring(local.day);auto system=DateTimeSystemTime(local);wchar_t text[256]{};
    // This header is cached per minute and configuration by the provider.
    // Keep raw time untouched; only the native tile XML adds its LRM/RLM.
    view.rightToLeft=ContentDateTimeUserInterfaceRightToLeft();
    if(GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&system,L"dddd",text,static_cast<int>(std::size(text)),nullptr))view.weekday=text;
    if(GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,0,&system,L"ddd",text,static_cast<int>(std::size(text)),nullptr))view.shortWeekday=text;
    if(GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_LONGDATE,&system,nullptr,text,static_cast<int>(std::size(text)),nullptr))view.date=text;
    view.nextDelayMs=aip::datetime::MillisecondsToNextMinute(now,milliseconds);return view;
}
static bool CalendarFileAttributesAllowed(DWORD attributes,bool directory,std::wstring& error) {
    // The recall constants are literal for older SDK headers. Reject cloud
    // placeholders before an open/read can hydrate them through the network.
    constexpr DWORD recallOnOpen=0x00040000,recallOnData=0x00400000;
    if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE|recallOnOpen|recallOnData))||
        (!directory&&(attributes&FILE_ATTRIBUTE_DIRECTORY))) {
        error=L"Choose an available local .ics copy outside symlinks and cloud placeholders. Remote, offline and recall-on-access files are not read.";return false;
    }
    return true;
}
static bool CalendarLocalPathAllowed(const std::wstring& path,std::wstring& error) {
    std::wstring key=L"FilePath",normalized=path;
    if(!aip::datetime::NormalizeSetting(L"Calendar",key,normalized,error)||normalized.empty())return false;
    const auto drive=GetDriveTypeW((normalized.substr(0,3)).c_str());
    if(drive!=DRIVE_FIXED&&drive!=DRIVE_REMOVABLE&&drive!=DRIVE_RAMDISK){error=L"Choose an .ics file on an available local drive. Mapped network drives are not read.";return false;}
    for(std::size_t p=3;;++p){
        if(p<normalized.size()&&normalized[p]!=L'\\'&&normalized[p]!=L'/')continue;
        if(!CalendarFileAttributesAllowed(GetFileAttributesW(normalized.substr(0,p).c_str()),p<normalized.size(),error))return false;
        if(p==normalized.size())break;
    }
    return true;
}
static bool ReadContentCalendarFile(const std::wstring& path,std::wstring& text,std::wstring& error) {
    if(!CalendarLocalPathAllowed(path,error))return false;
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){error=L"Could not read the selected .ics file. Check that it exists and is available locally.";return false;}
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(file,&info)||!CalendarFileAttributesAllowed(info.dwFileAttributes,false,error)){CloseHandle(file);return false;}
    wchar_t finalPath[32768]{};const auto finalLength=GetFinalPathNameByHandleW(file,finalPath,static_cast<DWORD>(std::size(finalPath)),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const std::wstring resolved=finalLength&&finalLength<std::size(finalPath)?std::wstring(finalPath,finalLength):std::wstring{};
    if(resolved.size()<7||resolved.compare(0,4,L"\\\\?\\")!=0||resolved[5]!=L':'||resolved[6]!=L'\\'||
        (GetDriveTypeW(resolved.substr(4,3).c_str())!=DRIVE_FIXED&&GetDriveTypeW(resolved.substr(4,3).c_str())!=DRIVE_REMOVABLE&&GetDriveTypeW(resolved.substr(4,3).c_str())!=DRIVE_RAMDISK)) {
        CloseHandle(file);error=L"The selected calendar resolves outside an available local drive. Save a local .ics copy instead.";return false;
    }
    LARGE_INTEGER size{};const bool bounded=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=2097152;
    if(!bounded){CloseHandle(file);error=L"The selected calendar must be at most 2 MiB.";return false;}
    std::string bytes(static_cast<std::size_t>(size.QuadPart),'\0');DWORD count=0;
    const bool read=bytes.empty()||ReadFile(file,&bytes[0],static_cast<DWORD>(bytes.size()),&count,nullptr)!=FALSE;CloseHandle(file);
    if(!read||count!=bytes.size()){error=L"The calendar changed while it was being read. DesktopStub will retry.";return false;}
    if(bytes.size()>=2&&static_cast<unsigned char>(bytes[0])==0xff&&static_cast<unsigned char>(bytes[1])==0xfe){
        if(bytes.size()%2){error=L"Calendar has incomplete UTF-16 text.";return false;}text.clear();text.reserve((bytes.size()-2)/2);
        for(std::size_t p=2;p<bytes.size();p+=2)text+=static_cast<wchar_t>(static_cast<unsigned char>(bytes[p])|(static_cast<unsigned char>(bytes[p+1])<<8));
    }else{const int skip=bytes.size()>=3&&bytes.compare(0,3,"\xEF\xBB\xBF")==0?3:0;const int length=static_cast<int>(bytes.size())-skip;
        const int needed=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+skip,length,nullptr,0);
        if(needed<=0){error=L"Calendar must contain UTF-8 or UTF-16LE text.";return false;}text.resize(static_cast<std::size_t>(needed));
        if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,bytes.data()+skip,length,&text[0],needed)){error=L"Calendar text could not be decoded.";return false;}
    }
    if(text.find(L'\0')!=std::wstring::npos){error=L"Calendar contains invalid zero characters.";return false;}return true;
}
struct ContentCalendarWorkerCache {
    std::wstring calendarKey,fileError;
    ULONGLONG nextFileRead=0;
    FILETIME fileModified{};
    std::uint64_t fileBytes=~0ull;
    aip::datetime::Calendar calendar;
    aip::datetime::UTC expandedMinute=-1;
    std::wstring expandedZone;
    int expandedDays=0;
    std::vector<aip::datetime::Occurrence> expanded;
    std::vector<std::wstring> expansionWarnings;
    bool loaded=false;
    int refreshSeconds=0;
};
struct ContentCalendarResult {
    bool ok=false;
    std::wstring error;
    std::vector<aip::datetime::Occurrence> occurrences;
    std::vector<std::wstring> warnings;
};
class ContentDateTimeProvider {
    using Result=std::shared_ptr<const ContentCalendarResult>;
    using CancelCheck=aip::content::AsyncSource<Result>::Cancel;
    using FileReader=std::function<bool(const std::wstring&,std::wstring&,std::wstring&)>;
    std::shared_ptr<ContentCalendarWorkerCache> workerCache=std::make_shared<ContentCalendarWorkerCache>();
    aip::content::AsyncSource<Result> calendarSource;
    std::function<void()> wake;
    std::wstring requestKey,visibleKey;
    bool waitExpired=false;
    std::vector<ContentDateTimeEvent> visibleEvents;
    aip::datetime::UTC headerMinute=-1;
    std::wstring headerKey;
    std::wstring regionalKey;
    ContentDateTimeView header;
    ContentDateTimeView Header(aip::datetime::UTC now,int milliseconds,const std::wstring& zone,const std::wstring& format) {
        const auto key=zone+L"\n"+format;
        if(headerMinute!=now/60||headerKey!=key){
            headerMinute=now/60;headerKey=key;header=ContentDateTimeHeader(now,milliseconds,zone,format);
            wchar_t pattern[256]{};regionalKey.clear();
            if(GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_SSHORTDATE,pattern,static_cast<int>(std::size(pattern))))regionalKey=pattern;
            if(format==L"System"&&GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT,LOCALE_STIMEFORMAT,pattern,static_cast<int>(std::size(pattern))))regionalKey+=L"|"+std::wstring(pattern);
        }
        auto result=header;result.nextDelayMs=aip::datetime::MillisecondsToNextMinute(now,milliseconds);return result;
    }
public:
    explicit ContentDateTimeProvider(std::function<void()> onWake={}):wake(std::move(onWake)){}
    ~ContentDateTimeProvider(){Stop();}
#if defined(DESKTOPSTUB_INERT_HARDWARE)||defined(DESKTOPSTUB_CLOCK_CALENDAR_TESTING)
    FileReader testReader;
#endif
    ContentDateTimeView Clock(const aip::datetime::ClockConfiguration& cfg,aip::datetime::UTC now,int milliseconds=0) {
        auto view=Header(now,milliseconds,cfg.timeZone,cfg.timeFormat);
        view.location=cfg.location.empty()?(aip::datetime::Lower(cfg.timeZone)==L"local"?L"Local time":cfg.timeZone):cfg.location;
        if(!cfg.showDate)view.date.clear();return view;
    }
    ContentDateTimeView Calendar(const aip::datetime::CalendarConfiguration& cfg,aip::datetime::UTC now,int milliseconds=0,bool force=false,bool synchronous=true) {
        using namespace aip::datetime;auto view=Header(now,milliseconds,cfg.timeZone,cfg.timeFormat);if(!view.ok)return view;
        if(cfg.filePath.empty()){if(!requestKey.empty())Cancel();return view;}
        if(cfg.refreshSeconds<30||cfg.refreshSeconds>86400||cfg.daysAhead<1||cfg.daysAhead>366||cfg.maxEvents<1||cfg.maxEvents>10){view.ok=false;view.status=L"Calendar settings are outside their supported ranges.";return view;}
        DYNAMIC_TIME_ZONE_INFORMATION zone{};ResolveContentTimeZone(cfg.timeZone,zone);
        const auto zoneScope=cfg.timeZone+L"|"+zone.TimeZoneKeyName+L"|"+std::to_wstring(zone.Bias)+L":"+std::to_wstring(zone.DynamicDaylightTimeDisabled);
        const auto key=cfg.filePath+L"\n"+zoneScope+L"\n"+std::to_wstring(cfg.daysAhead)+L":"+std::to_wstring(cfg.refreshSeconds)+L":"+std::to_wstring(now/86400);
        if(requestKey!=key){calendarSource.CancelPending();requestKey=key;waitExpired=false;}
        waitExpired=false;
        FileReader reader;
#if defined(DESKTOPSTUB_INERT_HARDWARE)||defined(DESKTOPSTUB_CLOCK_CALENDAR_TESTING)
        reader=testReader;
#endif
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(!reader)reader=[](const auto&,auto&,std::wstring& error){error=L"Calendar file reading is unavailable in inert validation.";return false;};
#endif
        const auto cache=workerCache;
        calendarSource.Request(key,std::chrono::milliseconds(static_cast<long long>(cfg.refreshSeconds)*1000),
            [cache,cfg,now,force,reader,zoneScope](const CancelCheck& canceled)->Result {
                auto result=std::make_shared<ContentCalendarResult>();
                if(canceled())return result;
                const auto ticks=GetTickCount64();
                if(force||!cache->loaded||cache->calendarKey!=cfg.filePath||cache->refreshSeconds!=cfg.refreshSeconds||ticks>=cache->nextFileRead){
                    cache->refreshSeconds=cfg.refreshSeconds;
                    cache->nextFileRead=ticks+static_cast<ULONGLONG>(cfg.refreshSeconds)*1000;
                    WIN32_FILE_ATTRIBUTE_DATA attributes{};
                    // Native path checks run solely on this demand-owned worker.
                    std::wstring refusal;
                    const bool local=reader||CalendarLocalPathAllowed(cfg.filePath,refusal);
                    const bool exists=reader||(local&&GetFileAttributesExW(cfg.filePath.c_str(),GetFileExInfoStandard,&attributes)!=FALSE);
                    const auto bytes=static_cast<std::uint64_t>(attributes.nFileSizeHigh)*4294967296ULL+attributes.nFileSizeLow;
                    if(force||reader||!cache->loaded||cache->calendarKey!=cfg.filePath||!exists||!cache->fileError.empty()||bytes!=cache->fileBytes||CompareFileTime(&cache->fileModified,&attributes.ftLastWriteTime)!=0){
                        cache->calendarKey=cfg.filePath;cache->fileModified=attributes.ftLastWriteTime;cache->fileBytes=bytes;
                        std::wstring text,error;cache->calendar={};cache->fileError.clear();cache->expandedMinute=-1;cache->loaded=true;
                        bool read=false;
                        if(!local)error=refusal;
                        else if(!canceled())read=reader?reader(cfg.filePath,text,error):ReadContentCalendarFile(cfg.filePath,text,error);
                        if(canceled()){cache->loaded=false;return result;}
                        if(!read||!ParseCalendar(text,cache->calendar,error))cache->fileError=error.empty()?L"Calendar reading was canceled or failed.":error;
                    }
                }
                if(canceled())return result;
                if(!cache->fileError.empty()){result->error=cache->fileError;return result;}
                if(cache->expandedMinute!=now/86400||cache->expandedZone!=zoneScope||cache->expandedDays!=cfg.daysAhead){
                    cache->expandedMinute=now/86400;cache->expandedZone=zoneScope;cache->expandedDays=cfg.daysAhead;cache->expansionWarnings=cache->calendar.warnings;
                    const auto day=now/86400*86400;
                    const auto resolve=[&](const Civil& civil,const std::wstring& zone,UTC& utc){return !canceled()&&ContentTimeZoneLocalToUtc(civil,zone,utc);};
                    cache->expanded=Expand(cache->calendar,day,day+static_cast<UTC>(cfg.daysAhead+1)*86400,cfg.timeZone,resolve,cache->expansionWarnings);
                }
                if(canceled()){cache->expandedMinute=-1;return result;}
                result->ok=true;result->occurrences=cache->expanded;result->warnings=cache->expansionWarnings;return result;
            },wake,force);
        if(synchronous){waitExpired=!calendarSource.Wait(key,std::chrono::seconds(5));if(waitExpired)calendarSource.CancelPending();}
        Result captured;std::string error;
        if(!calendarSource.Read(key,captured,error)||!captured){
            if(waitExpired||!error.empty()){view.ok=false;view.status=waitExpired?L"Calendar loading exceeded the one-shot deadline.":L"The calendar worker could not complete.";}
            else {view.status=L"Loading calendar events...";view.pending=true;}
            return view;
        }
        waitExpired=false;
        if(!captured->ok){view.ok=false;view.status=captured->error;return view;}
        std::vector<const Occurrence*> selected;std::wstring newVisibleKey=zoneScope+L"\n"+cfg.timeFormat+L":"+std::to_wstring(cfg.showLocation)+L"|"+regionalKey;
        for(const auto& event:captured->occurrences){if(selected.size()>=static_cast<std::size_t>(cfg.maxEvents))break;
            if(event.start>=now+static_cast<UTC>(cfg.daysAhead)*86400||!(event.end>now||(event.end==event.start&&event.start>=now)))continue;
            selected.push_back(&event);
            newVisibleKey+=L"|"+std::to_wstring(event.start)+L":"+std::to_wstring(event.end)+L":"+std::to_wstring(event.allDay)+
                L":"+std::to_wstring(event.summary.size())+L":"+event.summary+L":"+std::to_wstring(event.location.size())+L":"+event.location;
        }
        if(newVisibleKey!=visibleKey){
            visibleKey=std::move(newVisibleKey);visibleEvents.clear();
            for(const auto* event:selected){Civil local;if(!ContentTimeZoneUtcToLocal(event->start,zone,local))continue;auto system=DateTimeSystemTime(local);wchar_t date[128]{};
                GetDateFormatEx(LOCALE_NAME_USER_DEFAULT,DATE_SHORTDATE,&system,nullptr,date,static_cast<int>(std::size(date)),nullptr);
                visibleEvents.push_back({event->summary,std::wstring(date)+L" · "+(event->allDay?L"All day":ContentDateTimeFormat(local,cfg.timeFormat)),cfg.showLocation?event->location:L"",event->allDay});}
        }
        view.events=visibleEvents;
        if(!captured->warnings.empty())view.status=captured->warnings.front();else if(view.events.empty())view.status=L"No upcoming events.";
        return view;
    }
    void Cancel(){calendarSource.CancelPending();requestKey.clear();waitExpired=false;visibleKey.clear();visibleEvents.clear();}
    void Stop(){Cancel();calendarSource.Stop();}
};

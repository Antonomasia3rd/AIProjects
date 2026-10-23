#define NOMINMAX
#include <windows.h>
#include <mutex>
#include <iostream>
#include <stdexcept>
#define DESKTOPSTUB_CLOCK_CALENDAR_TESTING 1
#include "../dependencies/content_sources/clock_calendar_model.h"
#include "../dependencies/DesktopStub/ga_clock_calendar.inc"
using namespace aip::datetime;
namespace {
int checks=0;
void Check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
UTC At(int y,int m,int d,int h=0,int min=0){return Seconds({y,m,d,h,min,0});}
bool Fixed(const Civil& civil,const std::wstring& zone,UTC& utc){if(zone!=L"Local"&&zone!=L"UTC"&&zone!=L"Test/Zone")return false;utc=Seconds(civil)-(zone==L"Test/Zone"?(civil.month>=4?2:1)*3600:0);return true;}
Calendar Parse(const std::wstring& events){Calendar result;std::wstring error;Check(ParseCalendar(L"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n"+events+L"END:VCALENDAR\r\n",result,error),"parse calendar");return result;}
void CivilTests(){for(int y:{1601,1900,1970,2000,2026,2100,9999})for(int m=1;m<=12;++m)for(int d:{1,MonthDays(y,m)}){Civil c{y,m,d,23,59,59};auto round=FromSeconds(Seconds(c));Check(Valid(c)&&Seconds(round)==Seconds(c)&&round.year==y&&round.month==m&&round.day==d,"Gregorian round trip");}Check(!Valid({1900,2,29})&&Valid({2000,2,29}),"leap century");Check(Weekday({2026,10,23})==5,"weekday Friday");Check(Time({2026,1,1,0,3},L"12Hour")==L"12:03 AM"&&Time({2026,1,1,12,3},L"12Hour")==L"12:03 PM"&&Time({2026,1,1,23,3},L"24Hour")==L"23:03","AMPM and 24 hour");Check(MillisecondsToNextMinute(At(2026,1,1,1,1)+59,999)==1&&MillisecondsToNextMinute(At(2026,1,1,1,1))==60000,"minute boundary no polling cadence drift");}
void SettingsTests(){std::wstring error,k=L"TimeFormat",v=L"12hour";Check(NormalizeSetting(L"Clock",k,v,error)&&v==L"12Hour","format canonical");for(const auto* path:{L"https://example.test/calendar.ics",L"\\\\server\\calendar.ics",L"relative.ics",L"C:\\test.txt"}){k=L"FilePath";v=path;Check(!NormalizeSetting(L"Calendar",k,v,error),"calendar no URL or implicit network/local source");}k=L"FilePath";v=L"C:\\test.ics";Check(NormalizeSetting(L"Calendar",k,v,error),"explicit local file");k=L"DaysAhead";v=L"367";Check(!NormalizeSetting(L"Calendar",k,v,error),"bounded horizon");ClockConfiguration clock;Check(ReadClock([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},clock,error)&&clock.timeFormat==L"12Hour"&&clock.timeZone==L"Local","default clock offline local and AMPM");}
void IcsTests(){
    auto calendar=Parse(L"BEGIN:VEVENT\r\nUID:x\r\nDTSTART:20261023T120000Z\r\nDTEND:20261023T130000Z\r\nSUMMARY:Lunch\\, team\r\n continued\r\nLOCATION:Office\\nRoom 3\r\nBEGIN:VALARM\r\nDTSTART:19990101T000000Z\r\nSUMMARY:Do not replace event\r\nACTION:DISPLAY\r\nEND:VALARM\r\nEND:VEVENT\r\n");
    std::vector<std::wstring> warnings;auto events=Expand(calendar,At(2026,10,23,12,30),At(2026,10,24),L"Local",Fixed,warnings);Check(events.size()==1&&events[0].summary==L"Lunch, teamcontinued"&&events[0].location==L"Office\nRoom 3","unfold escape and ignore embedded alarms");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261023\nDTEND;VALUE=DATE:20261025\nSUMMARY:All day\nEND:VEVENT\n");events=Expand(calendar,At(2026,10,24,12),At(2026,10,26),L"Local",Fixed,warnings);Check(events.size()==1&&events[0].allDay&&events[0].end==At(2026,10,25),"all day spans exclusive end date");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART:20261023T120000Z\nRRULE:FREQ=DAILY;COUNT=3\nEXDATE:20261024T120000Z\nSUMMARY:Daily\nEND:VEVENT\n");events=Expand(calendar,At(2026,10,23),At(2026,10,30),L"Local",Fixed,warnings);Check(events.size()==2&&events.back().start==At(2026,10,25,12),"daily count includes excluded occurrence");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART;VALUE=DATE:20261019\nRRULE:FREQ=WEEKLY;BYDAY=MO,WE,FR;COUNT=6\nSUMMARY:Weekly\nEND:VEVENT\n");events=Expand(calendar,At(2026,10,23),At(2026,11,1),L"Local",Fixed,warnings);Check(events.size()==4&&events.front().start==At(2026,10,23)&&events.back().start==At(2026,10,30),"weekly BYDAY count and seek");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART;TZID=Test/Zone:20260331T100000\nRRULE:FREQ=DAILY;COUNT=2\nSUMMARY:DST\nEND:VEVENT\n");events=Expand(calendar,At(2026,3,30),At(2026,4,3),L"Local",Fixed,warnings);Check(events.size()==2&&events[1].start-events[0].start==23*3600,"recurrence re-resolves each local wall date across DST");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART:19900101T100000Z\nRRULE:FREQ=DAILY\nSUMMARY:Old recurring\nEND:VEVENT\n");events=Expand(calendar,At(2026,10,23),At(2026,10,24),L"Local",Fixed,warnings);Check(events.size()==1&&events[0].start==At(2026,10,23,10),"old unbounded recurrence seeks near now");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART:20261023T100000Z\nRRULE:FREQ=MONTHLY;BYDAY=2FR\nSUMMARY:Unsupported\nEND:VEVENT\nBEGIN:VEVENT\nDTSTART:20261024T100000Z\nSUMMARY:Supported sibling\nEND:VEVENT\n");Check(calendar.events.size()==1&&!calendar.warnings.empty(),"unsupported recurrence explicit warning and sibling preservation");
    calendar=Parse(L"BEGIN:VEVENT\nUID:series\nDTSTART:20261023T100000Z\nRRULE:FREQ=DAILY;COUNT=3\nEND:VEVENT\nBEGIN:VEVENT\nUID:series\nRECURRENCE-ID:20261024T100000Z\nDTSTART:20261024T120000Z\nSUMMARY:Override\nEND:VEVENT\n");Check(calendar.events.empty()&&!calendar.warnings.empty(),"unsupported series override suppresses original series rather than display wrong appointments");
    calendar=Parse(L"BEGIN:VEVENT\nDTSTART;TZID=Test/Zone:20260307T023000\nRRULE:FREQ=DAILY;COUNT=3\nSUMMARY:DST count\nEND:VEVENT\n");events=Expand(calendar,At(2026,3,9),At(2026,3,12),L"Local",[](const Civil& c,const std::wstring&,UTC& utc){if(c.month==3&&c.day==8)return false;utc=Seconds(c);return true;},warnings);Check(events.size()==2&&events.back().start==At(2026,3,10,2,30),"nonexistent recurrence local time is skipped without consuming COUNT even before seek window");
    for(const auto* stamp:{L"20260229",L"20261023T250000",L"20261023T010000+0100",L"20261023Z"}){Timestamp t;Check(!ParseTimestamp(stamp,t),"reject bad date and non-iCalendar offset");}
    Calendar bad;std::wstring error;Check(!ParseCalendar(L"BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:20261023\nEND:VCALENDAR",bad,error),"reject truncated event");
    Rule r;for(const auto* rule:{L"FREQ=DAILY;COUNT=1;UNTIL=20261024",L"FREQ=YEARLY",L"FREQ=WEEKLY;BYDAY=2MO",L"FREQ=DAILY;INTERVAL=0",L"FREQ=DAILY;BYSECOND=0"})Check(!ParseRule(rule,r),"reject unsupported recurrence rather than guessing");
    // Arithmetic seek count must equal the day-by-day definition for varied
    // weekly intervals and weekdays, including a DTSTART outside BYDAY.
    Event event;event.start.civil={2026,10,19};event.rule.frequency=Rule::Frequency::Weekly;
    for(int interval=1;interval<=5;++interval)for(int mask=0;mask<128;++mask){event.rule.interval=interval;event.rule.weekdays.clear();for(int day=0;day<7;++day)if(mask&(1<<day))event.rule.weekdays.insert(day);int count=0;for(int delta=0;delta<100;++delta){Check(OccurrencesBefore(event,delta)==count,"weekly recurrence arithmetic seek agrees with expansion");const auto day=FromDays(Days(event.start.civil)+delta);if(delta==0||(delta/7%interval==0&&(event.rule.weekdays.empty()?Weekday(day)==1:event.rule.weekdays.count(Weekday(day))!=0)))++count;}}
}
void WindowsTests(){DYNAMIC_TIME_ZONE_INFORMATION zone{};Check(ResolveContentTimeZone(L"SE Asia Standard Time",zone),"installed Windows zone");Civil local;Check(ContentTimeZoneUtcToLocal(At(2026,10,23,5),zone,local)&&local.hour==12,"UTC to selected world clock");UTC utc=0;Check(ContentTimeZoneLocalToUtc({2026,10,23,12},L"Asia/Jakarta",utc)&&utc==At(2026,10,23,5),"recognized exporter IANA zone");Check(!ContentTimeZoneLocalToUtc({2026,10,23,12},L"Unrecognized/Zone",utc),"unknown zone actionable");Check(!ContentTimeZoneLocalToUtc({2026,3,8,2,30},L"Eastern Standard Time",utc),"DST nonexistent local time rejected");Check(!ContentTimeZoneLocalToUtc({2026,11,1,1,30},L"Eastern Standard Time",utc),"DST repeated local time rejected");Check(ContentTimeZoneLocalToUtc({2026,7,1,12},L"Eastern Standard Time",utc)&&utc==At(2026,7,1,16),"summer dynamic DST");Check(ContentTimeZoneLocalToUtc({2026,1,1,12},L"Eastern Standard Time",utc)&&utc==At(2026,1,1,17),"winter dynamic DST");ContentDateTimeProvider provider;ClockConfiguration clock;clock.timeZone=L"SE Asia Standard Time";clock.location=L"Surabaya";const auto view=provider.Clock(clock,At(2026,10,23,5));Check(view.ok&&view.time==L"12:00 PM"&&view.location==L"Surabaya"&&!view.weekday.empty()&&view.dayNumber==L"23","host clock semantics");CalendarConfiguration calendar;calendar.timeZone=L"SE Asia Standard Time";const auto date=provider.Calendar(calendar,At(2026,10,23,5));Check(date.ok&&date.events.empty()&&date.dayNumber==L"23","date only calendar needs no file or account");}
void WorkerTests(){
    std::mutex mutex;std::condition_variable changed;bool entered=false,release=false;int reads=0;std::atomic<DWORD> workerThread{0};
    const DWORD caller=GetCurrentThreadId();std::atomic<int> wakes{0};ContentDateTimeProvider provider([&]{++wakes;});
    provider.testReader=[&](const std::wstring& path,std::wstring& text,std::wstring&){
        workerThread=GetCurrentThreadId();
        {std::unique_lock<std::mutex> lock(mutex);++reads;if(reads==1){entered=true;changed.notify_all();changed.wait_for(lock,std::chrono::seconds(3),[&]{return release;});}}
        text=L"BEGIN:VCALENDAR\nBEGIN:VEVENT\nDTSTART:20261023T120000Z\nDTEND:20261023T130000Z\nSUMMARY:"+
            std::wstring(path==L"C:\\old.ics"?L"Old calendar":L"New calendar")+L"\nEND:VEVENT\nEND:VCALENDAR\n";return true;
    };
    CalendarConfiguration cfg;cfg.timeZone=L"UTC";cfg.filePath=L"C:\\old.ics";
    auto view=provider.Calendar(cfg,At(2026,10,23,11),0,false,false);
    Check(view.ok&&view.pending&&view.events.empty()&&view.status==L"Loading calendar events...","ordinary calendar call returns header while file worker is pending");
    {std::unique_lock<std::mutex> lock(mutex);Check(changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"calendar worker starts");}
    Check(workerThread!=caller,"calendar reading and parsing are off the shared caller thread");
    auto newer=cfg;newer.filePath=L"C:\\new.ics";
    view=provider.Calendar(newer,At(2026,10,23,11),0,false,false);
    Check(view.events.empty(),"changed source never shows the old calendar result");
    {std::lock_guard<std::mutex> lock(mutex);release=true;}changed.notify_all();
    view=provider.Calendar(newer,At(2026,10,23,11),0,false,true);
    Check(view.ok&&view.events.size()==1&&view.events[0].title==L"New calendar","latest queued calendar replaces canceled source work");
    const auto previousReads=reads;
    for(int i=0;i<100;++i)view=provider.Calendar(newer,At(2026,10,23,11)+i,0,false,false);
    Check(view.ok&&view.events.size()==1&&reads==previousReads,"repeated calendar polls reuse immutable result without extra file reads");
    view=provider.Calendar(newer,At(2026,10,23,13),0,false,false);
    Check(view.events.empty(),"expired event disappears from cached occurrences without reparsing the file");
    view=provider.Calendar(newer,At(2026,10,23,11),0,true,true);
    Check(view.ok&&reads==previousReads+1&&view.events.size()==1,"explicit refresh rereads on worker and restores correct filtered view");
    provider.Cancel();view=provider.Calendar(CalendarConfiguration{},At(2026,10,23,11));
    Check(view.ok&&view.events.empty()&&reads==previousReads+1,"cancel plus date-only mode creates no further file work");
    provider.Stop();Check(wakes>0,"calendar worker wakes existing poll rather than creating a timer");
    ContentDateTimeProvider empty;int emptyReads=0;
    empty.testReader=[&](const auto&,auto&,auto&){++emptyReads;return false;};
    for(int i=0;i<100;++i)empty.Calendar(CalendarConfiguration{},At(2026,10,23,11)+i,0,false,false);
    Check(emptyReads==0,"unconfigured calendar never starts a reader");
    std::wstring error;
    for(DWORD flags:{static_cast<DWORD>(FILE_ATTRIBUTE_REPARSE_POINT),static_cast<DWORD>(FILE_ATTRIBUTE_OFFLINE),DWORD(0x00040000),DWORD(0x00400000),static_cast<DWORD>(FILE_ATTRIBUTE_DIRECTORY)})
        Check(!CalendarFileAttributesAllowed(flags,false,error),"reject reparse, offline, cloud recall, and directory inputs before read");
    Check(CalendarFileAttributesAllowed(FILE_ATTRIBUTE_NORMAL,false,error),"ordinary local file attributes remain supported");
    Check(!CalendarLocalPathAllowed(L"\\\\server\\share\\calendar.ics",error),"network path refused before file access");
}
}
int wmain(){try{CivilTests();SettingsTests();IcsTests();WindowsTests();WorkerTests();std::cout<<"Clock/calendar checks: "<<checks<<" passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<" after "<<checks<<" checks\n";return 1;}}

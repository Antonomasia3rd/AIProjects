#pragma once
// Offline clock/calendar data and bounded iCalendar reader. No filesystem,
// account, network, notification or operating-system access.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace aip { namespace datetime {
using UTC = std::int64_t;
struct Civil { int year=1970,month=1,day=1,hour=0,minute=0,second=0; };
inline bool Leap(int y) { return y%4==0&&(y%100!=0||y%400==0); }
inline int MonthDays(int y,int m) { constexpr int days[]={31,28,31,30,31,30,31,31,30,31,30,31};return m>=1&&m<=12?days[m-1]+(m==2&&Leap(y)?1:0):0; }
inline bool Valid(const Civil& d) {return d.year>=1601&&d.year<=9999&&d.day>=1&&d.day<=MonthDays(d.year,d.month)&&d.hour>=0&&d.hour<24&&d.minute>=0&&d.minute<60&&d.second>=0&&d.second<60;}
// Gregorian civil date algorithms; UTC seconds use the Unix epoch.
inline std::int64_t Days(Civil d) { d.year-=d.month<=2;const int era=(d.year>=0?d.year:d.year-399)/400;const unsigned y=static_cast<unsigned>(d.year-era*400);const unsigned doy=(153*static_cast<unsigned>(d.month+(d.month>2?-3:9))+2)/5+static_cast<unsigned>(d.day)-1;return static_cast<std::int64_t>(era)*146097+y*365+y/4-y/100+doy-719468; }
inline Civil FromDays(std::int64_t z) { z+=719468;const auto era=(z>=0?z:z-146096)/146097;const auto doe=static_cast<unsigned>(z-era*146097);const auto yoe=(doe-doe/1460+doe/36524-doe/146096)/365;int y=static_cast<int>(yoe)+static_cast<int>(era)*400;const auto doy=doe-(365*yoe+yoe/4-yoe/100);const auto mp=(5*doy+2)/153;const int day=static_cast<int>(doy-(153*mp+2)/5+1),m=static_cast<int>(mp)+(mp<10?3:-9);y+=m<=2;return {y,m,day}; }
inline UTC Seconds(const Civil& d) {return Days(d)*86400+d.hour*3600+d.minute*60+d.second;}
inline Civil FromSeconds(UTC seconds) {auto day=seconds/86400,rem=seconds%86400;if(rem<0){rem+=86400;--day;}auto d=FromDays(day);d.hour=static_cast<int>(rem/3600);d.minute=static_cast<int>(rem%3600/60);d.second=static_cast<int>(rem%60);return d;}
inline int Weekday(const Civil& d) {return static_cast<int>((Days(d)+4)%7+7)%7;} // Sunday=0.
inline std::wstring Lower(std::wstring v){for(auto& c:v)c=static_cast<wchar_t>(std::towlower(c));return v;}
inline std::wstring Trim(const std::wstring& v){const auto a=v.find_first_not_of(L" \r\n\t");return a==std::wstring::npos?L"":v.substr(a,v.find_last_not_of(L" \r\n\t")-a+1);}
inline bool Number(const std::wstring& v,int& n){if(v.empty()||v.size()>8)return false;n=0;for(auto c:v){if(c<L'0'||c>L'9')return false;n=n*10+c-L'0';}return true;}
inline bool SafeText(const std::wstring& v,std::size_t max){return v.size()<=max&&v.find_first_of(std::wstring(L"\r\n\0",3))==std::wstring::npos;}
struct Setting {const wchar_t* key;const wchar_t* fallback;};
inline constexpr Setting ClockSettings[]={{L"TimeZone",L"Local"},{L"TimeFormat",L"System"},{L"Location",L""},{L"ShowDate",L"1"},{L"IconStyle",L"Auto"}};
inline constexpr Setting CalendarSettings[]={{L"TimeZone",L"Local"},{L"FilePath",L""},{L"DaysAhead",L"7"},{L"MaxEvents",L"3"},{L"RefreshSeconds",L"60"},{L"ShowLocation",L"1"},{L"TimeFormat",L"12Hour"},{L"Source",L"Local"},{L"EventWindow",L"DaysAhead"}};
inline bool NormalizeSetting(const std::wstring& section,std::wstring& key,std::wstring& value,std::wstring& error){
    const bool clock=Lower(section)==L"clock";if(!clock&&Lower(section)!=L"calendar"){error=L"Unknown date/time source.";return false;}
    const Setting* field=nullptr;const auto k=Lower(key);if(clock){for(const auto& f:ClockSettings)if(Lower(f.key)==k)field=&f;}else for(const auto& f:CalendarSettings)if(Lower(f.key)==k)field=&f;
    if(!field){error=L"Unknown "+section+L" setting.";return false;}key=field->key;value=Trim(value);
    const auto fail=[&](const wchar_t* why){error=section+L"."+key+L": "+why;return false;};
    if(k==L"source"){const auto low=Lower(value);if(low==L"local")value=L"Local";else if(low==L"google")value=L"Google";else return fail(L"expected Local or Google.");}
    else if(k==L"iconstyle"){const auto low=Lower(value);if(low==L"auto")value=L"Auto";else if(low==L"clock")value=L"Clock";else if(low==L"worldclock")value=L"WorldClock";else return fail(L"expected Auto, Clock or WorldClock.");}
    else if(k==L"eventwindow"){const auto low=Lower(value);if(low==L"daysahead")value=L"DaysAhead";else if(low==L"todayandtomorrow")value=L"TodayAndTomorrow";else return fail(L"expected DaysAhead or TodayAndTomorrow.");}
    else if(k==L"timezone"){if(value.empty())value=L"Local";if(!SafeText(value,128))return fail(L"expected a Windows time-zone ID or Local.");}
    else if(k==L"timeformat"){const auto low=Lower(value);if(low==L"12hour")value=L"12Hour";else if(low==L"24hour")value=L"24Hour";else if(low==L"system")value=L"System";else return fail(L"expected 12Hour, 24Hour or System.");}
    else if(k==L"showdate"||k==L"showlocation"){const auto low=Lower(value);if(low==L"1"||low==L"true"||low==L"on"||low==L"yes")value=L"1";else if(low==L"0"||low==L"false"||low==L"off"||low==L"no")value=L"0";else return fail(L"expected a boolean.");}
    else if(k==L"filepath"){if(!SafeText(value,32760))return fail(L"expected a local .ics file path.");if(!value.empty()&&(value.size()<7||value[1]!=L':'||(value[2]!=L'\\'&&value[2]!=L'/')||Lower(value.substr(value.size()-4))!=L".ics"))return fail(L"choose an absolute local drive path ending in .ics; URLs and network paths are not read.");}
    else if(k==L"location"){if(!SafeText(value,256))return fail(L"expected a short single-line location.");}
    else {int n=0;if(!Number(value,n)||(k==L"daysahead"?(n<1||n>366):k==L"maxevents"?(n<1||n>10):(n<30||n>86400)))return fail(L"number is outside its supported range.");value=std::to_wstring(n);}
    error.clear();return true;
}
struct ClockConfiguration {std::wstring timeZone=L"Local",timeFormat=L"System",location,iconStyle=L"Auto";bool showDate=true;};
struct CalendarConfiguration {std::wstring timeZone=L"Local",filePath,timeFormat=L"12Hour";int daysAhead=7,maxEvents=3,refreshSeconds=60;bool showLocation=true;std::wstring source=L"Local",eventWindow=L"DaysAhead";};
template<class Reader> bool ReadClock(Reader get,ClockConfiguration& output,std::wstring& error){ClockConfiguration c;for(const auto& f:ClockSettings){std::wstring k=f.key,v=get(f.key,f.fallback);if(!NormalizeSetting(L"Clock",k,v,error))return false;if(k==L"TimeZone")c.timeZone=v;else if(k==L"TimeFormat")c.timeFormat=v;else if(k==L"Location")c.location=v;else if(k==L"IconStyle")c.iconStyle=v;else c.showDate=v==L"1";}output=std::move(c);return true;}
template<class Reader> bool ReadCalendar(Reader get,CalendarConfiguration& output,std::wstring& error){CalendarConfiguration c;for(const auto& f:CalendarSettings){std::wstring k=f.key,v=get(f.key,f.fallback);if(!NormalizeSetting(L"Calendar",k,v,error))return false;if(k==L"Source")c.source=v;else if(k==L"EventWindow")c.eventWindow=v;else if(k==L"TimeZone")c.timeZone=v;else if(k==L"FilePath")c.filePath=v;else if(k==L"TimeFormat")c.timeFormat=v;else if(k==L"ShowLocation")c.showLocation=v==L"1";else {int n=0;Number(v,n);if(k==L"DaysAhead")c.daysAhead=n;else if(k==L"MaxEvents")c.maxEvents=n;else c.refreshSeconds=n;}}output=std::move(c);return true;}
inline std::wstring Time(const Civil& d,const std::wstring& format){const auto two=[](int n){return (n<10?L"0":L"")+std::to_wstring(n);};return format==L"24Hour"?two(d.hour)+L":"+two(d.minute):std::to_wstring(d.hour%12?d.hour%12:12)+L":"+two(d.minute)+(d.hour<12?L" AM":L" PM");}
inline std::uint64_t MillisecondsToNextMinute(UTC seconds,int milliseconds=0){return static_cast<std::uint64_t>(60-(seconds%60+60)%60)*1000-static_cast<unsigned>(milliseconds>=0&&milliseconds<1000?milliseconds:0);}
struct Timestamp {Civil civil;std::wstring zone;bool dateOnly=false,utc=false;};
inline bool ParseTimestamp(std::wstring v,Timestamp& out){Timestamp t;if(!v.empty()&&v.back()==L'Z'){t.utc=true;v.pop_back();}if(v.size()!=8&&v.size()!=15)return false;t.dateOnly=v.size()==8;if(t.dateOnly&&t.utc)return false;if(!t.dateOnly&&v[8]!=L'T')return false;const auto n=[&](std::size_t p,std::size_t count,int& number){return Number(v.substr(p,count),number);};if(!n(0,4,t.civil.year)||!n(4,2,t.civil.month)||!n(6,2,t.civil.day)||(!t.dateOnly&&(!n(9,2,t.civil.hour)||!n(11,2,t.civil.minute)||!n(13,2,t.civil.second)))||!Valid(t.civil))return false;out=t;return true;}
inline std::wstring Unescape(const std::wstring& v){std::wstring out;for(std::size_t i=0;i<v.size();++i){if(v[i]==L'\\'&&i+1<v.size()){const auto c=v[++i];out+=c==L'n'||c==L'N'?L'\n':c;}else out+=v[i];}return out.substr(0,2048);}
struct Rule {enum class Frequency{None,Daily,Weekly};Frequency frequency=Frequency::None;int interval=1,count=0;bool hasUntil=false;Timestamp until;std::set<int> weekdays;};
inline bool ParseRule(const std::wstring& text,Rule& output){Rule r;std::set<std::wstring> keys;std::size_t pos=0;while(pos<text.size()){const auto end=text.find(L';',pos),eq=text.find(L'=',pos);if(eq==std::wstring::npos||(end!=std::wstring::npos&&eq>end))return false;const auto key=Lower(text.substr(pos,eq-pos)),v=text.substr(eq+1,end==std::wstring::npos?end:end-eq-1);if(!keys.insert(key).second)return false;if(key==L"freq"){if(v==L"DAILY")r.frequency=Rule::Frequency::Daily;else if(v==L"WEEKLY")r.frequency=Rule::Frequency::Weekly;else return false;}else if(key==L"interval"){if(!Number(v,r.interval)||r.interval<1||r.interval>366)return false;}else if(key==L"count"){if(!Number(v,r.count)||r.count<1||r.count>50000)return false;}else if(key==L"until"){if(!ParseTimestamp(v,r.until))return false;r.hasUntil=true;}else if(key==L"byday"){std::size_t p=0;for(;;){const auto e=v.find(L',',p);const auto day=v.substr(p,e==std::wstring::npos?e:e-p);const wchar_t* names[]={L"SU",L"MO",L"TU",L"WE",L"TH",L"FR",L"SA"};int index=0;for(;index<7&&day!=names[index];++index){}if(index==7)return false;r.weekdays.insert(index);if(e==std::wstring::npos)break;p=e+1;}}else if(key==L"wkst"){if(v!=L"MO")return false;}else return false;if(end==std::wstring::npos)break;pos=end+1;}if(r.frequency==Rule::Frequency::None||(r.count&&r.hasUntil)||(!r.weekdays.empty()&&r.frequency!=Rule::Frequency::Weekly))return false;output=r;return true;}
struct Event {std::wstring uid,summary,location;Timestamp start,end;bool hasEnd=false;Rule rule;std::vector<Timestamp> excluded;};
struct Calendar {std::vector<Event> events;std::vector<std::wstring> warnings;};
inline bool ParseCalendar(const std::wstring& text,Calendar& output,std::wstring& error){
    if(text.size()>2097152){error=L"Calendar exceeds the 2 MiB text limit.";return false;}std::vector<std::wstring> lines;std::size_t pos=0;
    while(pos<text.size()){const auto end=text.find(L'\n',pos);auto line=text.substr(pos,end==std::wstring::npos?end:end-pos);if(!line.empty()&&line.back()==L'\r')line.pop_back();if(!line.empty()&&(line.front()==L' '||line.front()==L'\t')&&!lines.empty())lines.back()+=line.substr(1);else lines.push_back(std::move(line));if(end==std::wstring::npos)break;pos=end+1;}
    Calendar result;bool inCalendar=false,inEvent=false,sawCalendar=false,haveStart=false,invalid=false,seriesException=false;int ignoredComponent=0;Event event;std::set<std::wstring> eventKeys,unsupportedSeries;
    const auto warn=[&](){if(result.warnings.size()<8)result.warnings.push_back(L"Skipped an event with unsupported or invalid dates, recurrence, timezone or exception fields. Supported recurrence: DAILY/WEEKLY, INTERVAL, COUNT/UNTIL, plain BYDAY and EXDATE. Export expanded events for other recurrence.");};
    for(const auto& line:lines){if(line==L"BEGIN:VCALENDAR"){if(inCalendar){error=L"Nested VCALENDAR is invalid.";return false;}inCalendar=true;sawCalendar=true;continue;}if(line==L"END:VCALENDAR"){if(inEvent){error=L"Unclosed VEVENT.";return false;}inCalendar=false;continue;}if(!inCalendar)continue;if(line==L"BEGIN:VEVENT"){if(inEvent){error=L"Nested VEVENT is invalid.";return false;}inEvent=true;event={};haveStart=false;invalid=false;seriesException=false;ignoredComponent=0;eventKeys.clear();continue;}if(line==L"END:VEVENT"){if(!inEvent)continue;inEvent=false;if(ignoredComponent)invalid=true;if(event.rule.hasUntil&&(event.rule.until.dateOnly!=event.start.dateOnly||(!event.start.dateOnly&&(event.start.utc||!event.start.zone.empty())&&!event.rule.until.utc)))invalid=true;if(seriesException&&!event.uid.empty())unsupportedSeries.insert(event.uid);if(!haveStart||invalid){warn();continue;}if(result.events.size()>=5000){error=L"Calendar exceeds 5000 supported events.";return false;}result.events.push_back(std::move(event));continue;}if(!inEvent)continue;
        const auto colon=line.find(L':');if(colon==std::wstring::npos){invalid=true;continue;}auto head=line.substr(0,colon),value=line.substr(colon+1);const auto semi=head.find(L';');const auto key=Lower(head.substr(0,semi));if(key==L"begin"){++ignoredComponent;continue;}if(key==L"end"){if(ignoredComponent)--ignoredComponent;continue;}if(ignoredComponent)continue;
        if(key==L"recurrence-id"||key==L"rdate"||key==L"duration"){invalid=true;seriesException=seriesException||key==L"recurrence-id";continue;}
        if(key==L"status"&&value==L"CANCELLED"){invalid=true;seriesException=true;continue;}
        if(key==L"summary")event.summary=Unescape(value);else if(key==L"location")event.location=Unescape(value);else if(key==L"uid")event.uid=value.substr(0,512);else if(key==L"rrule"){if(!eventKeys.insert(key).second||!ParseRule(value,event.rule))invalid=true;}
        else if(key==L"dtstart"||key==L"dtend"||key==L"exdate"){
            std::wstring zone;bool dateParam=false;std::size_t p=semi;while(p!=std::wstring::npos){const auto next=head.find(L';',p+1),eq=head.find(L'=',p+1);if(eq==std::wstring::npos||(next!=std::wstring::npos&&eq>next)){invalid=true;break;}const auto param=Lower(head.substr(p+1,eq-p-1)),v=head.substr(eq+1,next==std::wstring::npos?next:next-eq-1);if(param==L"tzid"){zone=v;if(zone.size()>1&&zone.front()==L'"'&&zone.back()==L'"')zone=zone.substr(1,zone.size()-2);}else if(param==L"value"&&v==L"DATE")dateParam=true;else if(!(param==L"value"&&v==L"DATE-TIME"))invalid=true;p=next;}
            std::size_t begin=0;for(;;){const auto comma=value.find(L',',begin);Timestamp t;if(!ParseTimestamp(value.substr(begin,comma==std::wstring::npos?comma:comma-begin),t)||(!zone.empty()&&(t.utc||t.dateOnly))||(dateParam&&!t.dateOnly)){invalid=true;break;}t.zone=zone;if(key==L"exdate")event.excluded.push_back(t);else{if(!eventKeys.insert(key).second)invalid=true;if(key==L"dtstart"){event.start=t;haveStart=true;}else{event.end=t;event.hasEnd=true;}}if(comma==std::wstring::npos)break;if(key!=L"exdate"){invalid=true;break;}begin=comma+1;}
        }
    }
    if(!sawCalendar||inCalendar||inEvent){error=L"Choose a complete VCALENDAR .ics file.";return false;}result.events.erase(std::remove_if(result.events.begin(),result.events.end(),[&](const Event& e){return unsupportedSeries.count(e.uid)!=0;}),result.events.end());output=std::move(result);error.clear();return true;
}
struct Occurrence {UTC start=0,end=0;bool allDay=false;std::wstring summary,location,uid;};
inline int OccurrencesBefore(const Event& event,int delta) {
    if(delta<=0)return 0;
    if(event.rule.frequency==Rule::Frequency::Daily)return (delta-1)/event.rule.interval+1;
    if(event.rule.frequency==Rule::Frequency::None)return 1;
    const auto firstWeekday=Weekday(event.start.civil);const auto monday=(firstWeekday+6)%7;int count=0;
    for(int offset=0;offset<7&&offset<delta;++offset){const auto day=(firstWeekday+offset)%7;
        if(event.rule.weekdays.empty()?day!=firstWeekday:event.rule.weekdays.count(day)==0)continue;
        const int weekOffset=(offset+monday)/7,firstK=(event.rule.interval-weekOffset%event.rule.interval)%event.rule.interval,maxK=(delta-1-offset)/7;
        if(maxK>=firstK)count+=(maxK-firstK)/event.rule.interval+1;
    }
    if(!event.rule.weekdays.empty()&&!event.rule.weekdays.count(firstWeekday))++count; // DTSTART always belongs to the set.
    return count;
}
// Resolver receives the original event timezone (or Calendar.TimeZone for
// floating/all-day dates) for EVERY occurrence, preserving wall time across DST.
template<class Resolver> std::vector<Occurrence> Expand(const Calendar& calendar,UTC now,UTC horizon,const std::wstring& floatingZone,Resolver resolve,std::vector<std::wstring>& warnings){
    std::vector<Occurrence> result;const auto finish=[&](){std::stable_sort(result.begin(),result.end(),[](const Occurrence& a,const Occurrence& b){return a.start<b.start;});return result;};std::size_t resolutionBudget=50000;for(const auto& event:calendar.events){if(result.size()>=2000)break;const auto convert=[&](Timestamp t,UTC& out){if(t.utc){out=Seconds(t.civil);return true;}if(!resolutionBudget)return false;--resolutionBudget;return resolve(t.civil,t.zone.empty()?floatingZone:t.zone,out);};UTC first=0,last=0;if(!convert(event.start,first)){if(warnings.size()<8)warnings.push_back(L"An event timezone is unknown, ambiguous or invalid; use a Windows timezone ID, supported IANA ID or UTC.");continue;}if(event.hasEnd&&(!convert(event.end,last)||last<first||event.end.dateOnly!=event.start.dateOnly)){if(warnings.size()<8)warnings.push_back(L"An event end time is invalid.");continue;}const auto duration=event.hasEnd?last-first:event.start.dateOnly?86400:0;
        if(duration>366LL*86400){if(warnings.size()<8)warnings.push_back(L"An event longer than 366 days was skipped; export shorter occurrences.");continue;}
        UTC until=0;if(event.rule.hasUntil&&!convert(event.rule.until,until)){warnings.push_back(L"An event recurrence end is invalid.");continue;}
        const auto startDay=Days(event.start.civil);
        // Seek past old occurrences arithmetically. A calendar exported years
        // ago must not require replaying every elapsed day on each refresh.
        const int firstDelta=event.rule.frequency==Rule::Frequency::None?0:static_cast<int>((std::max)(UTC(0),(now-duration-first)/86400-2));
        const bool countLocal=event.rule.count&&!event.start.utc;
        int count=countLocal?0:OccurrencesBefore(event,firstDelta);const int maxDay=event.rule.frequency==Rule::Frequency::None?0:firstDelta+static_cast<int>((std::max)(UTC(0),(horizon-now+duration)/86400))+5;bool limited=true;
        for(int delta=countLocal?0:firstDelta;delta<=maxDay;++delta){auto start=event.start;const auto d=FromDays(startDay+delta);start.civil.year=d.year;start.civil.month=d.month;start.civil.day=d.day;bool selected=delta==0;
            if(event.rule.frequency==Rule::Frequency::Daily)selected=delta%event.rule.interval==0;
            else if(event.rule.frequency==Rule::Frequency::Weekly){const auto monday=(Weekday(event.start.civil)+6)%7;selected=((delta+monday)/7)%event.rule.interval==0&&(event.rule.weekdays.empty()?Weekday(d)==Weekday(event.start.civil):event.rule.weekdays.count(Weekday(d))!=0);}
            if(delta==0)selected=true;if(!selected)continue;UTC utc=0;if(!convert(start,utc)){if(!resolutionBudget){if(warnings.size()<8)warnings.push_back(L"Calendar recurrence exceeds the bounded timezone-resolution budget; export expanded occurrences.");return finish();}if(warnings.size()<8)warnings.push_back(L"An occurrence crosses an invalid or ambiguous local time and was skipped.");continue;}if(event.rule.count&&++count>event.rule.count){limited=false;break;}if((event.rule.hasUntil&&utc>until)||utc>=horizon){limited=false;break;}
            bool excluded=false;for(const auto& ex:event.excluded){UTC value=0;if(convert(ex,value)&&value==utc){excluded=true;break;}}
            // All-day end is exclusive in iCalendar; use civil days rather
            // than a fixed 24 hours so DST changes retain the intended date.
            UTC end=utc+duration;if(event.start.dateOnly){auto endDate=start;const auto days=event.hasEnd?Days(event.end.civil)-startDay:1;endDate.civil=FromDays(startDay+delta+days);if(!convert(endDate,end))continue;}
            if(!excluded&&(end>now||(end==utc&&utc>=now)))result.push_back({utc,end,event.start.dateOnly,event.summary.empty()?L"Untitled event":event.summary,event.location,event.uid});
            if(result.size()>=2000){if(warnings.size()<8)warnings.push_back(L"Calendar preview is limited to 2000 occurrences.");limited=false;break;}
        }
        if(limited&&maxDay>0&&warnings.size()<8)warnings.push_back(L"An old recurrence exceeds the bounded expansion window; export recent expanded occurrences.");
    }return finish();
}
} }

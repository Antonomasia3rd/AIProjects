#include "../dependencies/content_sources/prayer_times_model.h"
#include "../dependencies/prayer_tile_layout.h"
#include <iostream>
#include <map>
#include <stdexcept>
using namespace aip::prayer;
static int checks=0;
static void Check(bool value,const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
static Configuration Config() { Configuration cfg; cfg.city=L"Fixture City"; cfg.country=L"ID"; cfg.method=20; cfg.timeZone=L"Asia/Jakarta"; cfg.twentyFourHour=true; return cfg; }
static Day Fixture(Date date,int offset=420) {
    Day day; day.date=date; day.timeZone=L"Asia/Jakarta"; day.hijriDate=L"07-05-1448"; day.offsetMinutes=offset;
    day.fetchedAtUtc=CivilDay(date)*86400-offset*60;
    const int hours[]={5,12,15,18,19};
    for(size_t i=0;i<5;++i) day.prayers[i]={static_cast<PrayerId>(i),CivilDay(date)*86400+hours[i]*3600-offset*60,offset};
    day.sunrise={PrayerId::Sunrise,CivilDay(date)*86400+6*3600-offset*60,offset};day.hasSunrise=true;return day;
}
int main() { try {
    Configuration empty; std::wstring error;
    Check(!empty.twentyFourHour&&TimeText(12*3600+34*60,0,empty.twentyFourHour)==L"12:34 PM"&&TimeText(34*60,0,empty.twentyFourHour)==L"12:34 AM",
        "factory clock format includes AM/PM with correct noon and midnight labels");
    Check(empty.city.empty()&&empty.country.empty()&&Validate(empty,error)&&!Validate(empty,error,true),"location remains unset and requires explicit city/country before fetch");
    for(int method=-1;method<=24;++method) {
        std::wstring key=L"method",value=std::to_wstring(method);
        Check(NormalizeSetting(key,value,error)==(method>=-1&&method<=23&&method!=6),"method range matches supported API IDs");
    }
    for(const auto& test:std::vector<std::pair<std::wstring,std::wstring>>{{L"Method",L"99"},{L"School",L"2"},{L"LatitudeAdjustmentMethod",L"0"},
        {L"FajrAdjustmentMinutes",L"-181"},{L"IshaAdjustmentMinutes",L"181"},{L"RefreshSeconds",L"299"},{L"TimeoutSeconds",L"61"},
        {L"MaximumBytes",L"1048577"},{L"ShowSunrise",L"sometimes"},{L"TimetableGapLines",L"-1"},{L"TimetableGapLines",L"4"},{L"City",L"A\nB"},{L"Unknown",L"0"}}) {
        auto key=test.first,value=test.second;Check(!NormalizeSetting(key,value,error),"invalid typed configuration rejected");
    }
    std::map<std::wstring,std::wstring> values{{L"City",L" Fixture City "},{L"Country",L"ID"},{L"Method",L"auto"},{L"ShowSunrise",L"off"}};
    Configuration read;
    Check(ReadConfiguration([&](const wchar_t* key,const wchar_t* fallback){auto at=values.find(key);return at==values.end()?std::wstring(fallback):at->second;},read,error)&&
        read.city==L"Fixture City"&&read.method==-1&&!read.showSunrise,"reader normalizes canonical configuration without host dependencies");
    Check(read.timetableGapLines==1,"Prayer timetable defaults to one structural blank line");
    values[L"TimetableGapLines"]=L"0";Check(ReadConfiguration([&](const wchar_t* key,const wchar_t* fallback){auto at=values.find(key);return at==values.end()?std::wstring(fallback):at->second;},read,error)&&read.timetableGapLines==0,
        "an explicit zero preserves a compact timetable preference");
    Check(ValidDate({2024,2,29})&&!ValidDate({2025,2,29})&&AddDays({2024,12,31},1)==Date{2025,1,1},"civil calendar handles leap years/year rollover");
    Check(CivilDay({1970,1,1})==0&&FromCivilDay(-1)==Date{1969,12,31},"civil epoch works before and after Unix epoch");
    for(int year=1970;year<=2100;++year) Check(FromCivilDay(CivilDay({year,3,1}))==Date{year,3,1},"civil conversion roundtrip");
    IsoTime parsed;
    Check(ParseIso8601(L"2026-10-18T04:30:00+07:00",parsed)&&parsed.utcSeconds==CivilDay({2026,10,17})*86400+21*3600+30*60,"ISO positive offset rolls UTC into previous date");
    Check(ParseIso8601(L"2026-10-18T23:30:00-05:30",parsed)&&parsed.utcSeconds==CivilDay({2026,10,19})*86400+5*3600,"ISO negative fractional offset rolls UTC into next date");
    for(const auto* invalid:{L"04:30",L"2026-10-18T04:30:00",L"2026-10-18T24:00:00Z",L"2026-02-29T04:00:00Z",L"2026-10-18T+1:00:00Z",L"2026-10-18T04:30:00+14:01"})
        Check(!ParseIso8601(invalid,parsed),"ambiguous or invalid ISO timing rejected");
    Configuration cfg=Config(); Data data;data.configKey=ConfigurationKey(cfg);
    data.schedule.days={Fixture({2026,10,18}),Fixture({2026,10,19})};
    data.lastSuccessUtc=data.schedule.days[0].fetchedAtUtc;data.nextRefreshUtc=data.lastSuccessUtc+43200;
    const UTC fajr=data.schedule.days[0].prayers[0].utcSeconds;
    auto view=Compose(cfg,data,fajr-61);
    Check(view.clock.Available()&&view.clock.time==L"05:00"&&view.clock.prayer==L"Fajr"&&view.clock.location==cfg.city&&
        view.clock.minutes==L"2"&&view.clock.countdown==L"in 2 minutes","clock layout carries independent semantic time, prayer, city and rounded countdown");
    Check(Compose(cfg,data,fajr-60).clock.countdown==L"in 1 minute"&&Compose(cfg,data,fajr).clock.minutes==L"0",
        "clock countdown handles singular and due-now boundaries without parsing summary text");
    Check(view.available&&view.detailRows.size()==6&&view.detailRows[1].id==PrayerId::Sunrise&&view.detailRows[2].id==PrayerId::Dhuhr&&view.secondary.find(L"2 min")!=std::wstring::npos&&view.nextUTCDeadline==fajr-60,"countdown schedules exact minute transition and chronological timetable includes distinct Sunrise/Shuruq");
    Check(view.badge.find(L"Sunrise 06:00")!=std::wstring::npos&&view.timeZone==L"Asia/Jakarta"&&!view.hijriDate.empty(),"optional sunrise/timezone/Hijri metadata remains distinct from five required rows");
    Check(Compose(cfg,data,fajr).secondary.find(L"0 min")!=std::wstring::npos&&NextUTCDeadline(cfg,data,fajr)==fajr+1,"exact prayer instant is due now then advances");
    Check(Compose(cfg,data,fajr+1).secondary.find(L"Dhuhr")!=std::wstring::npos,"after prayer instant next mandatory prayer is selected");
    cfg.nextIncludesSunrise=true;
    Check(Compose(cfg,data,fajr+1).clock.prayer==L"Sunrise"&&Compose(cfg,data,data.schedule.days[0].sunrise.utcSeconds+1).clock.prayer==L"Dhuhr",
        "optional sunrise countdown selects sunrise independently from the five prayers");
    cfg.nextIncludesSunrise=false;
    const UTC isha=data.schedule.days[0].prayers[4].utcSeconds;
    Check(Compose(cfg,data,isha+1).secondary.find(L"Fajr")!=std::wstring::npos,"after Isha tomorrow Fajr is next");
    const auto originalKey=ConfigurationKey(cfg);cfg.adjustmentsMinutes[0]=10;cfg.twentyFourHour=false;cfg.showSunrise=false;
    Check(ConfigurationKey(cfg)==originalKey,"presentation and local minute adjustments do not invalidate network cache");
    view=Compose(cfg,data,fajr);
    Check(view.clock.time==L"5:10 AM"&&view.clock.details.empty(),"clock layout retains configured 12-hour time and respects hidden sunrise");
    cfg.showTimetable=false;const auto noTable=Compose(cfg,data,fajr);
    Check(!noTable.clock.showTimetable&&noTable.detailRows.size()==5&&ConfigurationKey(cfg)==originalKey,
        "hiding the displayed timetable preserves cached schedule and next-prayer computation");
    cfg.timetableGapLines=2;Check(Compose(cfg,data,fajr).clock.timetableGapLines==2&&ConfigurationKey(cfg)==originalKey,
        "timetable spacing follows the immutable display snapshot without changing the network cache key");
    Check(view.detailRows[0].value==L"5:10 AM"&&view.secondary.find(L"10 min")!=std::wstring::npos&&view.badge.find(L"Sunrise")==std::wstring::npos,"local tuning/display choices apply without changing cached UTC schedule");
    data.stale=true;data.error=L"Network unavailable";
    Check(Compose(cfg,data,fajr).available&&Compose(cfg,data,fajr).secondary.find(L"Stale")!=std::wstring::npos,"failed refresh retains usable daily data with explicit stale status");
    Check(Compose(cfg,data,fajr).clock.details.find(L"Stale")!=std::wstring::npos&&
        Compose(cfg,data,fajr).clock.details.find(L"Network unavailable")!=std::wstring::npos,"clock presentation exposes stale and error state alongside retained timings");
    const auto bytes=SerializeCache(data);Data loaded;
    Check(!bytes.empty()&&ParseCache(bytes,cfg,loaded,error)&&loaded.stale&&loaded.error==data.error&&loaded.schedule.days.size()==2,"private cache roundtrips typed daily data and stale/error state");
    auto other=cfg;other.city=L"Other City";
    Check(!ParseCache(bytes,other,loaded,error)&&loaded.schedule.days.empty(),"cache for another city is rejected and output cleared");
    other=cfg;other.method=3;Check(!ParseCache(bytes,other,loaded,error),"method changes reject old schedule cache");
    Check(!ParseCache(bytes+"junk",cfg,loaded,error)&&!ParseCache(std::string(65537,'x'),cfg,loaded,error),"trailing/oversized caches are rejected");
    Check(!Compose(other,data,fajr).available,"another request configuration never displays old city's data");
    Check(!Compose(cfg,data,CivilDay({2026,10,22})*86400).available,"past cached days are never presented as today's times");
    Check(!Compose(other,data,fajr).clock.Available()&&!Compose(empty,data,fajr).clock.Available(),
        "unconfigured or unrelated locations never receive a valid-looking clock");
    {
        using namespace aip::content;
        ClockText clock;clock.time=L"11:14 AM";clock.prayer=L"Dhuhr";clock.location=L"Surabaya & <region>";
        clock.minutes=L"44";clock.countdown=L"in 44 minutes";clock.details=L"Sunrise 05:06";
        std::vector<Text> rows;for(const auto* label:{L"Fajr",L"Dhuhr",L"Asr",L"Maghrib",L"Isha"})rows.push_back({label,L"11:14 AM",L""});
        const Text fallback{L"Legacy text",L"City",L"Status"};
        for(const auto* name:{L"TileMedium",L"TileWide",L"TileLarge"}) {
            const int width=std::wstring(name)==L"TileMedium"?150:310,height=std::wstring(name)==L"TileLarge"?310:150;
            std::wstring xml;Check(AppendPrayerTileBinding(xml,name,height,fallback,rows,L"",clock),"clock binding is supported on Medium, Wide and Large");
            Check(xml.find(L"11:14 AM")!=std::wstring::npos&&xml.find(L"Dhuhr")!=std::wstring::npos&&
                xml.find(L"at Surabaya &amp; &lt;region&gt;")!=std::wstring::npos&&xml.find(L"Legacy text")==std::wstring::npos,
                "clock fields are escaped and take precedence over legacy summary content");
            const auto layout=ComputePrayerClockGeometry(width,height,clock,rows);
            const auto expectedPixels=height>200?34:width<200?15:24;
            const auto expectedStyle=height>200?L"subheader":width<200?L"base":L"title";
            Check(layout.timePixels==expectedPixels&&std::wstring(PrayerTimeStyle(width,height))==expectedStyle&&
                xml.find(L"hint-style=\""+std::wstring(expectedStyle)+L"\"")!=std::wstring::npos&&
                xml.find(L"hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\" hint-align=\"left\">Dhuhr")!=std::wstring::npos,
                "Prayer time follows the native Clock size contract and prayer name shares the subtle location style");
            Check(layout.mainWidth>0&&layout.clockBottom<=layout.bottom&&layout.margin+layout.mainWidth<=width-layout.margin,
                "bitmap clock text remains bounded by the actual tile dimensions");
            if(width>200) {
                Check(xml.find(L"headerNumeral")!=std::wstring::npos&&xml.find(L">minutes<")!=std::wstring::npos&&
                    xml.find(L"in 44 minutes")==std::wstring::npos&&layout.countdownColumn,
                    "wide layouts use independent top-right minutes and large numeric countdown");
                Check(layout.margin+layout.mainWidth<layout.countdownX&&layout.countdownX+layout.countdownWidth<=width-layout.margin,
                    "bitmap countdown and main clock occupy disjoint bounded columns");
            } else Check(xml.find(L"in 44 minutes")!=std::wstring::npos&&!layout.countdownColumn,
                "Medium uses the explicit compact countdown sentence instead of squeezing two columns");
            Check((xml.find(L"Isha")!=std::wstring::npos)==(height>200)&&layout.schedule==(height>200),
                "only Large displays the five-prayer timetable");
            const auto gap=L"<text hint-style=\"caption\" hint-minLines=\"1\" hint-maxLines=\"1\"> </text>";
            Check((xml.find(gap)!=std::wstring::npos)==(height>200)&&layout.timetableGapLines==(height>200?1:0),
                "a structural caption gap precedes only the displayed Large timetable");
        }
        for(int gap=0;gap<=3;++gap){clock.timetableGapLines=gap;std::wstring spaced;
            AppendPrayerTileBinding(spaced,L"TileLarge",310,fallback,rows,L"",clock);
            Check(gap?spaced.find(L"hint-minLines=\""+std::to_wstring(gap)+L"\"")!=std::wstring::npos:spaced.find(L"hint-minLines") == std::wstring::npos,
                "zero through three requested blank lines map to bounded native spacer metadata");}
        clock.timetableGapLines=1;
        clock.showTimetable=false;std::wstring hidden;
        TextPresentation custom;custom.preset=L"Custom";custom.primaryStyle=L"title";custom.secondaryStyle=L"bodySubtle";custom.badgeStyle=L"headerNumeral";
        std::wstring customized;AppendPrayerTileBinding(customized,L"TileWide",150,fallback,rows,L"",clock,custom);
        Check(customized.find(L"hint-style=\"bodySubtle\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"left\">Dhuhr")!=std::wstring::npos&&
            customized.find(L"hint-style=\"bodySubtle\" hint-wrap=\"true\" hint-maxLines=\"2\" hint-align=\"left\">at Surabaya")!=std::wstring::npos&&
            customized.find(L"hint-style=\"captionSubtle\" hint-wrap=\"true\" hint-maxLines=\"1\" hint-align=\"right\">minutes")!=std::wstring::npos,
            "prayer name and city follow the same custom Secondary settings without enlarging the minutes label");
        AppendPrayerTileBinding(hidden,L"TileLarge",310,fallback,rows,L"",clock);
        Check(hidden.find(L"Isha")==std::wstring::npos&&!ComputePrayerClockGeometry(310,310,clock,rows).schedule,
            "native and bitmap timetable visibility follows the same display setting");
        clock.minutes=L"1440";Check(std::wstring(PrayerCountdownStyle(clock))==L"titleNumeral"&&
            ComputePrayerClockGeometry(310,150,clock,rows).countdownPixels==24,"four-digit countdowns use a smaller native style in both renderers");
        clock={};std::wstring waiting;
        Check(AppendPrayerTileBinding(waiting,L"TileMedium",150,fallback,{},L"",clock)&&waiting.find(L"Legacy text")!=std::wstring::npos&&
            waiting.find(L"headerNumeral")==std::wstring::npos,"unavailable schedule retains the readable setup/error fallback");
    }
    std::cout<<"PrayerTimes model: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL: "<<ex.what()<<'\n';return 1;} }

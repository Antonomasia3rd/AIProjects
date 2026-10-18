#include "../dependencies/content_sources/prayer_times.inc"
#include <iostream>
#include <stdexcept>
using namespace aip::prayer;
static int checks=0;
static void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
static std::string Narrow(const std::wstring& value){std::string out;aip::TryWideToUtf8(value,out);return out;}
static std::wstring ISO(UTC utc,int offset){return DateText(DateAtUTC(utc,offset))+L"T"+TimeText(utc,offset,true)+L":00"+(offset<0?L"-":L"+")+Two(std::abs(offset)/60)+L":"+Two(std::abs(offset)%60);}
static std::string Fixture(Date date,int cityOffset,bool iso,int serializedOffset=420){
    const int hours[]={5,12,15,18,19,6};const char* names[]={"Fajr","Dhuhr","Asr","Maghrib","Isha","Sunrise"};
    std::string out="{\"code\":200,\"data\":{\"timings\":{";
    for(size_t i=0;i<6;++i){if(i)out+=",";const UTC utc=CivilDay(date)*86400+hours[i]*3600-cityOffset*60;
        out+="\""+std::string(names[i])+"\":\""+Narrow(iso?ISO(utc,serializedOffset):Two(hours[i])+L":00")+"\"";}
    return out+"},\"date\":{\"gregorian\":{\"date\":\""+Narrow(Two(date.day)+L"-"+Two(date.month)+L"-"+std::to_wstring(date.year))+"\"},\"hijri\":{\"date\":\"07-05-1448\"}},\"meta\":{\"timezone\":\"Europe/London\"}}}";
}
static Configuration Config(){Configuration cfg;cfg.city=L"Fixture & City";cfg.country=L"GB";cfg.timeZone=L"Europe/London";return cfg;}
struct Transport{
    int calls=0,cityOffset=0;bool fail=false,cancelAfter=false;std::vector<std::wstring> urls;std::vector<long long> budgets;
    bool operator()(const aip::HttpFetchOptions& request,aip::HttpFetchResponse& response,std::wstring& error){
        ++calls;urls.push_back(request.url);budgets.push_back(request.timeout.count());
        if(fail){error=L"Synthetic offline failure";return false;}
        const std::wstring marker=L"timingsByCity/";const auto start=request.url.find(marker);
        Date date;if(start==std::wstring::npos||!ParseDate(request.url.substr(start+marker.size(),10),date,true))throw std::runtime_error("bad request date");
        const auto body=Fixture(date,cityOffset,request.url.find(L"iso8601=true")!=std::wstring::npos);
        response.statusCode=200;response.body.assign(body.begin(),body.end());return true;
    }
};
int main(){try{
    auto cfg=Config();std::wstring error;aip::HttpFetchOptions request;
    Check(BuildRequest(cfg,{2026,10,18},request,error)&&request.url.find(L"city=Fixture%20%26%20City")!=std::wstring::npos&&
        request.url.find(L"&method=")==std::wstring::npos&&request.url.find(L"iso8601=true")!=std::wstring::npos,"city encoding and Auto method omission");
    cfg.method=20;Check(BuildRequest(cfg,{2026,10,18},request,error,false)&&request.url.find(L"method=20")!=std::wstring::npos&&request.url.find(L"iso8601=false")!=std::wstring::npos,"documented KEMENAG and plain civil-clock request");
    cfg=Config();Day day;
    Check(ParseDayJson(Fixture({2026,10,18},0,true),Fixture({2026,10,18},0,false),{2026,10,18},day,error)&&
        day.prayers[0].offsetMinutes==0&&day.prayers[0].utcSeconds==CivilDay({2026,10,18})*86400+5*3600,
        "ISO serialized in +07 is correlated with London civil clocks, not mistaken for the city's offset");
    Check(TimeText(day.prayers[0].utcSeconds,day.prayers[0].offsetMinutes,true)==L"05:00"&&!day.hijriDate.empty(),"city display and optional Hijri date are correct");
    Check(!ParseDayJson(Fixture({2026,10,18},0,true),Fixture({2026,10,19},0,false),{2026,10,18},day,error),"paired requests must describe the same Gregorian day");
    Check(!ParseDayJson("{broken}",Fixture({2026,10,18},0,false),{2026,10,18},day,error),"malformed API JSON produces no daily schedule");
    std::string incomplete=Fixture({2026,10,18},0,true);const auto name=incomplete.find("\"Fajr\"");incomplete.replace(name,6,"\"Other\"");
    Check(!ParseDayJson(incomplete,Fixture({2026,10,18},0,false),{2026,10,18},day,error),"missing mandatory prayer is rejected rather than fabricated");
    Transport fake;auto transport=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring& failure){return fake(options,response,failure);};
    const UTC now=CivilDay({2026,10,18})*86400+3600;
    Configuration unset;auto data=Refresh(unset,now,{}, {},transport);
    Check(fake.calls==0&&!data.error.empty()&&data.schedule.days.empty(),"unset location performs zero HTTP calls");
    data=Refresh(cfg,now,{}, {},transport);
    Check(fake.calls==4&&data.schedule.days.size()==2&&!data.stale&&data.error.empty()&&Compose(cfg,data,now).available,"today and tomorrow require two validated response pairs");
    Check(fake.budgets.size()==4&&fake.budgets[0]<=10000&&fake.budgets[3]<=fake.budgets[0],"all response pairs share a decreasing total request budget");
    auto cached=Refresh(cfg,now+10,data,{},transport);
    Check(fake.calls==4&&cached.schedule.days.size()==2,"fresh daily cache avoids all HTTP work");
    auto display=cfg;display.adjustmentsMinutes[0]=7;display.twentyFourHour=false;
    cached=Refresh(display,now+20,data,{},transport);
    Check(fake.calls==4&&Compose(display,cached,now+20).detailRows[0].value==L"5:07 AM","local adjustments and display changes reuse unadjusted UTC data");
    cfg.refreshSeconds=604800;const UTC nextDay=CivilDay({2026,10,19})*86400+3600;
    cached=Refresh(cfg,nextDay,data,{},transport);
    Check(fake.calls==6&&FindDay(cached,{2026,10,19})&&FindDay(cached,{2026,10,20}),"daily rollover fetches only the missing tomorrow pair");
    cfg=Config();fake.fail=true;const int beforeFailure=fake.calls;
    auto failed=Refresh(cfg,now+43201,data,{},transport);
    Check(fake.calls==beforeFailure+1&&failed.stale&&!failed.error.empty()&&Compose(cfg,failed,now+43201).available,"failed refresh preserves still-valid today with explicit stale status");
    Refresh(cfg,now+43250,failed,{},transport);
    Check(fake.calls==beforeFailure+1,"failed refresh retries are capped to once per five minutes");
    auto emptyFailure=Refresh(cfg,now,{}, {},transport);const int failures=fake.calls;
    Refresh(cfg,now+10,emptyFailure,{},transport);
    Check(fake.calls==failures,"initial network failure also observes the retry backoff without cached days");
    fake.fail=false;fake.cityOffset=420;fake.calls=0;fake.urls.clear();
    const UTC eastNow=CivilDay({2026,10,18})*86400+20*3600;
    auto east=Refresh(cfg,eastNow,{}, {},transport);
    Check(fake.calls==6&&FindDay(east,{2026,10,19})&&FindDay(east,{2026,10,20}),"cold UTC-date request is corrected to the returned city's next civil day within three pairs");
    fake.cityOffset=-300;fake.calls=0;
    auto west=Refresh(cfg,now,{}, {},transport);
    Check(fake.calls==4&&FindDay(west,{2026,10,17})&&FindDay(west,{2026,10,18}),"negative-offset rollover reuses the initial response as tomorrow");
    const int beforeCancel=fake.calls;
    auto canceled=Refresh(cfg,now,{},[]{return true;},transport);
    Check(fake.calls==beforeCancel&&canceled.error.find(L"canceled")!=std::wstring::npos,"cancellation before refresh performs zero HTTP");
    int oversizedCalls=0;
    auto oversized=Refresh(cfg,now,{}, {},[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){++oversizedCalls;response.statusCode=200;response.body.assign(options.maximumBytes+1,'x');return true;});
    Check(oversizedCalls==1&&!oversized.error.empty()&&oversized.schedule.days.empty(),"injected oversized responses cannot bypass the byte cap");
    bool stop=false;
    auto during=Refresh(cfg,now,{},[&]{return stop;},[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring& failure){auto ok=transport(options,response,failure);stop=true;return ok;});
    Check(during.schedule.days.empty()&&!during.error.empty(),"canceled response pair is never published partially");
    std::cout<<"PrayerTimes source: "<<checks<<" checks passed; injected HTTP only\n";return 0;
}catch(const std::exception& ex){std::cerr<<"FAIL: "<<ex.what()<<'\n';return 1;}}

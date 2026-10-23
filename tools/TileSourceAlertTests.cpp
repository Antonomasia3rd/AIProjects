#include "../dependencies/tile_source_alerts.h"
#include <iostream>
#include <stdexcept>
using namespace aip::notifications;
static unsigned checks=0;
static void Check(bool result,const char* label){++checks;if(!result)throw std::runtime_error(label);}
static aip::prayer::Day Fixture(){
    using namespace aip::prayer;Day day;day.date={2026,10,23};day.timeZone=L"Asia/Jakarta";day.offsetMinutes=420;
    const int hours[]={5,12,15,18,19};
    for(size_t i=0;i<5;++i)day.prayers[i]={static_cast<PrayerId>(i),CivilDay(day.date)*86400+hours[i]*3600-420*60,420};
    day.sunrise={PrayerId::Sunrise,CivilDay(day.date)*86400+6*3600-420*60,420};day.hasSunrise=true;return day;
}
int main(){try{
    AlertSettings cfg;std::wstring error;
    Check(ReadAlertSettings([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},cfg,error)&&!cfg.enabled&&cfg.sound==EventSound::Silent,
        "source reminders remain opt-in and silent by default");
    for(const auto& entry:std::vector<std::pair<std::wstring,std::wstring>>{{L"Type",L"Critical"},{L"Sound",L"file://user.wav"},
        {L"LeadMinutes",L"1441"},{L"LeadMinutes",L"-1"},{L"MinimumNewItems",L"0"},{L"MinimumNewItems",L"1001"},
        {L"CooldownSeconds",L"86401"},{L"Enabled",L"perhaps"},{L"Events",L"Sunset"},{L"Events",L"Fajr,"}}){
        auto value=entry.second;Check(!NormalizeAlertSetting(entry.first,value,error),"invalid alert options are rejected atomically");
    }
    auto value=std::wstring(L"shuruq, fajr,Sunrise,ISHA");
    Check(NormalizeAlertSetting(L"Alerts.Events",value,error)&&value==L"Fajr,Isha,Sunrise","Shuruq aliases Sunrise and canonicalizes duplicate events");
    Check(!FindAlertField(L"HTTPTimeoutSeconds")&&FindAlertField(L"alerts.leadminutes"),"alert schema is explicit and case-insensitive");
    cfg.enabled=true;cfg.minimumNewItems=2;cfg.cooldownSeconds=60;
    const auto a=StableId(L"a"),b=StableId(L"b"),c=StableId(L"c"),d=StableId(L"d"),e=StableId(L"e");
    AlertState state;SetAlertScope(state,StableId(L"profile+tile+feed"));
    Check(!ObserveRss(state,{a,b},1000,cfg)&&state.baselineReady&&state.pending.empty(),"first feed result establishes baseline without initial flood");
    Check(!ObserveRss(state,{b,a,a},1001,cfg)&&state.pending.empty(),"feed reorder and duplicate entries are quiet");
    Check(!ObserveRss(state,{c,b,a},1002,cfg)&&state.pending.size()==1,"subthreshold new items accumulate across fetches");
    Check(ObserveRss(state,{d,c,b},1003,cfg)==2&&state.pending.empty(),"threshold produces one aggregate notification");
    Check(!ObserveRss(state,{e,d,c},1004,cfg)&&state.pending.size()==1,"later new items do not bypass threshold");
    auto f=StableId(L"f");Check(!ObserveRss(state,{f,e,d},1005,cfg)&&state.pending.size()==2,"cooldown defers complete threshold");
    Check(ObserveRss(state,{f,e,d},1063,cfg)==2,"pending aggregate is released at cooldown without requiring extra items");
    const auto saved=SerializeAlertState(state);AlertState loaded;
    Check(!saved.empty()&&ParseAlertState(saved,state.scope,loaded)&&loaded.seen==state.seen&&loaded.lastRssNoticeUtc==1063,
        "persisted baseline roundtrips without source URLs or titles");
    Check(!ObserveRss(loaded,{f,e,d},1064,cfg),"restart with identical response cannot duplicate an alert");
    for(const auto& malformed:{saved+L"trailing",std::wstring(L"AIP_ALERTS_1\n"),std::wstring(150001,L'x')})
        Check(!ParseAlertState(malformed,state.scope,loaded),"corrupt or oversized persisted state is rejected");
    Check(!ParseAlertState(saved,StableId(L"other-user-profile-tile"),loaded),"another user/profile/tile baseline cannot be imported");
    const auto lastNotice=state.lastRssNoticeUtc;
    Check(!ObserveRss(state,{StableId(L"muted1"),StableId(L"muted2")},1100,cfg,false)&&state.pending.empty()&&state.lastRssNoticeUtc==lastNotice,
        "master-mute tracks items quietly without claiming delivery or preserving notification backlog");
    Check(!ObserveRss(state,{StableId(L"muted1"),StableId(L"muted2")},1200,cfg),"unmuting does not announce items seen while muted");
    SetAlertScope(state,StableId(L"different-feed"));
    Check(!ObserveRss(state,{c,d},1300,cfg)&&state.pending.empty(),"feed changes establish their own quiet baseline");
    using namespace aip::prayer;
    Configuration prayer;prayer.city=L"Surabaya";prayer.country=L"Indonesia";prayer.method=20;
    Data data;data.configKey=ConfigurationKey(prayer);data.schedule.days={Fixture()};
    const auto dhuhr=data.schedule.days.front().prayers[1].utcSeconds;
    SetAlertScope(state,StableId(L"prayer"));cfg.leadMinutes=20;
    Check(DuePrayers(prayer,data,dhuhr-1201,cfg,state).empty(),"lead notification cannot occur early");
    const auto events=DuePrayers(prayer,data,dhuhr-1200,cfg,state);
    Check(events.size()==1&&events.front().event==PrayerId::Dhuhr&&events.front().eventUtc==dhuhr,"configured twenty-minute Dhuhr lead selects exact UTC event");
    Check(DuePrayers(prayer,data,dhuhr-1199,cfg,state).empty(),"repeated source polls cannot repeat a prayer alert");
    const auto prayerSaved=SerializeAlertState(state);Check(ParseAlertState(prayerSaved,state.scope,loaded)&&DuePrayers(prayer,data,dhuhr-1198,cfg,loaded).empty(),
        "restarting during prayer lead window remains deduplicated");
    SetAlertScope(state,StableId(L"late-start"));Check(DuePrayers(prayer,data,dhuhr-1000,cfg,state).empty(),"unlock after expired lead window does not replay old reminder");
    const auto sunrise=data.schedule.days.front().sunrise.utcSeconds;
    Check(DuePrayers(prayer,data,sunrise-1200,cfg,state).empty(),"Sunrise is not implicitly treated as an obligatory prayer alert");
    cfg.events=1u<<static_cast<unsigned>(PrayerId::Sunrise);
    Check(DuePrayers(prayer,data,sunrise-1200,cfg,state).size()==1,"Sunrise alerts can be enabled independently");
    cfg.events=0x1f;cfg.leadMinutes=0;
    Check(DuePrayers(prayer,data,dhuhr+1,cfg,state).size()==1,"at-time alerts tolerate normal one-second poll jitter");
    SetAlertScope(state,StableId(L"late-at-time"));Check(DuePrayers(prayer,data,dhuhr+91,cfg,state).empty(),"at-time alerts expire after bounded jitter grace");
    auto unset=prayer;unset.city.clear();Check(DuePrayers(unset,data,dhuhr,cfg,state).empty(),"unset location never creates valid-looking reminders");
    auto other=prayer;other.city=L"Other City";Check(DuePrayers(other,data,dhuhr,cfg,state).empty(),"old location schedule cannot trigger another city's reminders");
    for(const auto type:{EventType::General,EventType::Reminder,EventType::Alarm})for(const auto sound:{EventSound::Silent,EventSound::Default,EventSound::Alarm}) {
        const auto xml=SourceEventXml(L"<title>",L"A&B",type,sound);
        Check(xml.find(L"&lt;title&gt;")!=std::wstring::npos&&xml.find(L"A&amp;B")!=std::wstring::npos,"source notification payload escapes configurable text");
        Check((xml.find(L"scenario=\"alarm\"")!=std::wstring::npos)==(type==EventType::Alarm),"only configured alarm uses Windows alarm scenario");
        Check((xml.find(L"silent=\"true\"")!=std::wstring::npos)==(sound==EventSound::Silent),"silent is retained independently from toast scenario");
        Check((xml.find(L"loop=\"true\"")!=std::wstring::npos)==(sound==EventSound::Alarm)&&
            (xml.find(L"duration=\"long\"")!=std::wstring::npos)==(sound==EventSound::Alarm),"only explicitly selected alarm sound loops with the required long desktop-toast duration");
        Check((xml.find(L"activationType=\"system\" arguments=\"dismiss\"")!=std::wstring::npos)==(type!=EventType::General),
            "reminder and alarm scenarios include a system dismiss action without app activation");
    }
    Check(SourceEventXml(L"Title",L"Body",EventType::General,EventSound::Default).find(L"priority")==std::wstring::npos,
        "normal notifications never invent priority or Do Not Disturb bypass");
    std::cout<<"Tile source alerts: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}

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
static aip::prayer::Day ShiftFixture(int days){
    auto day=Fixture();day.date=aip::prayer::AddDays(day.date,days);
    for(auto& prayer:day.prayers)prayer.utcSeconds+=static_cast<aip::prayer::UTC>(days)*86400;
    day.sunrise.utcSeconds+=static_cast<aip::prayer::UTC>(days)*86400;return day;
}
static void PrayerPhaseTests(){
    using namespace aip::prayer;
    Configuration prayer;prayer.city=L"Surabaya";prayer.country=L"Indonesia";prayer.method=20;
    Data data;data.configKey=ConfigurationKey(prayer);data.schedule.days={ShiftFixture(-1),Fixture(),ShiftFixture(1)};
    AlertSettings cfg;cfg.enabled=true;AlertState state;SetAlertScope(state,StableId(L"phases"));
    const auto dhuhr=data.schedule.days[1].prayers[1].utcSeconds,asr=data.schedule.days[1].prayers[2].utcSeconds;
    auto due=DuePrayers(prayer,data,dhuhr-1200,cfg,state);
    Check(due.size()==1&&due.front().phase==PrayerAlertPhase::Upcoming,"upcoming phase is explicitly distinguished");
    const auto legacyLead=StableId(data.configKey+L"|1|"+std::to_wstring(dhuhr)+L"|20");
    Check(due.front().key==legacyLead,"upcoming phase preserves the historical lead deduplication key");
    due=DuePrayers(prayer,data,dhuhr,cfg,state);
    Check(due.size()==1&&due.front().phase==PrayerAlertPhase::AtTime,"at-time phase still fires after its own upcoming alert");
    const auto legacyAt=StableId(data.configKey+L"|1|"+std::to_wstring(dhuhr)+L"|0");
    Check(due.front().key==legacyAt,"at-time phase preserves historical zero-lead key");
    Check(DuePrayers(prayer,data,dhuhr+1,cfg,state).empty(),"at-time phase is independently deduplicated");
    AlertState restored;const auto saved=SerializeAlertState(state);
    Check(ParseAlertState(saved,state.scope,restored)&&DuePrayers(prayer,data,dhuhr+2,cfg,restored).empty(),"both phases survive history persistence");
    state.delivered.clear();cfg.upcomingEnabled=false;
    Check(DuePrayers(prayer,data,dhuhr-1200,cfg,state).empty()&&DuePrayers(prayer,data,dhuhr,cfg,state).size()==1,
        "upcoming toggle does not suppress the independent at-time phase");
    state.delivered.clear();cfg.upcomingEnabled=true;cfg.atTimeEnabled=false;
    Check(DuePrayers(prayer,data,dhuhr-1200,cfg,state).size()==1&&DuePrayers(prayer,data,dhuhr,cfg,state).empty(),
        "at-time toggle does not suppress the independent upcoming phase");
    state.delivered.clear();cfg.atTimeEnabled=true;cfg.leadMinutes=0;
    due=DuePrayers(prayer,data,dhuhr,cfg,state);
    Check(due.size()==1&&due.front().phase==PrayerAlertPhase::AtTime,"zero lead creates one at-time alert rather than duplicate phases");
    state.delivered.clear();cfg.atTimeEnabled=false;
    Check(DuePrayers(prayer,data,dhuhr,cfg,state).empty(),"zero lead cannot bypass the at-time toggle");
    cfg.atTimeEnabled=true;cfg.leadMinutes=20;state.delivered.clear();state.delivered.push_back(legacyLead);state.delivered.push_back(legacyAt);
    Check(DuePrayers(prayer,data,dhuhr-1200,cfg,state).empty()&&DuePrayers(prayer,data,dhuhr,cfg,state).empty(),
        "existing saved phase keys prevent upgrade duplicates");
    state.delivered.clear();Check(DuePrayers(prayer,data,dhuhr+91,cfg,state).empty(),"sleep past at-time grace does not replay alerts");
    Check(!CurrentPrayer(prayer,data,dhuhr,cfg).available,"quiet current-prayer history remains disabled by default");
    cfg.currentEnabled=true;
    auto current=CurrentPrayer(prayer,data,dhuhr,cfg);
    Check(current.available&&current.event==PrayerId::Dhuhr&&current.name==L"Dhuhr"&&current.eventUtc==dhuhr&&
        current.endUtc==asr&&current.offsetMinutes==420,"current prayer exposes exact name, adjusted UTC, local offset and next-prayer expiry");
    const auto same=CurrentPrayer(prayer,data,dhuhr+60,cfg);
    Check(same.available&&same.key==current.key,"current-prayer identity is stable across polls without a new notification key");
    current=CurrentPrayer(prayer,data,asr,cfg);
    Check(current.available&&current.event==PrayerId::Asr&&current.key!=same.key,"next prayer replaces current identity exactly at its boundary");
    const auto fajr=data.schedule.days[1].prayers[0].utcSeconds,sunrise=data.schedule.days[1].sunrise.utcSeconds;
    current=CurrentPrayer(prayer,data,sunrise+1,cfg);
    Check(current.available&&current.event==PrayerId::Fajr&&current.eventUtc==fajr&&current.endUtc==dhuhr,
        "Sunrise is not a current-prayer event or current Fajr expiry");
    cfg.events=1u<<static_cast<unsigned>(PrayerId::Dhuhr);
    Check(!CurrentPrayer(prayer,data,asr,cfg).available,"a selected prayer does not remain current past an unselected actual prayer");
    cfg.events=1u<<static_cast<unsigned>(PrayerId::Sunrise);
    Check(!CurrentPrayer(prayer,data,sunrise+1,cfg).available,"Sunrise-only selection never claims a current obligatory prayer");
    cfg.events=0x1f;
    current=CurrentPrayer(prayer,data,fajr-1,cfg);
    Check(current.available&&current.event==PrayerId::Isha&&current.eventUtc==data.schedule.days[0].prayers[4].utcSeconds&&current.endUtc==fajr,
        "previous day's Isha remains current until today's Fajr when both cached days are available");
    auto incomplete=data;incomplete.schedule.days={Fixture()};
    Check(!CurrentPrayer(prayer,incomplete,fajr-1,cfg).available,"missing yesterday schedule cannot invent an Isha time");
    Check(!CurrentPrayer(prayer,incomplete,data.schedule.days[1].prayers[4].utcSeconds+1,cfg).available,
        "missing tomorrow schedule cannot invent current Isha expiry");
    Check(!CurrentPrayer(prayer,incomplete,data.schedule.days[2].prayers[0].utcSeconds,cfg).available,
        "outdated cached days cannot produce a current-prayer card for another date");
    auto stale=data;stale.stale=true;
    current=CurrentPrayer(prayer,stale,dhuhr,cfg);
    Check(current.available&&current.stale,"still-covered cached timings retain an explicit stale marker rather than guessing new dates");
    auto adjusted=prayer;adjusted.adjustmentsMinutes[1]=10;
    current=CurrentPrayer(adjusted,data,dhuhr+599,cfg);
    Check(current.available&&current.event==PrayerId::Fajr&&current.endUtc==dhuhr+600,"current period honors configured positive prayer adjustment before transition");
    current=CurrentPrayer(adjusted,data,dhuhr+600,cfg);
    Check(current.available&&current.event==PrayerId::Dhuhr&&current.eventUtc==dhuhr+600&&current.key!=same.key,
        "adjusted current prayer switches on its actual adjusted UTC and acquires a new history key");
    auto wrong=prayer;wrong.city=L"Other city";
    Check(!CurrentPrayer(wrong,data,dhuhr,cfg).available,"current prayer cannot reuse another location's cached schedule");
    cfg.enabled=false;Check(!CurrentPrayer(prayer,data,dhuhr,cfg).available,"master source-alert switch disables quiet current card too");
}
int main(){try{
    AlertSettings cfg;std::wstring error;
    Check(ReadAlertSettings([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},cfg,error)&&!cfg.enabled&&cfg.sound==EventSound::Silent&&
        cfg.upcomingEnabled&&cfg.atTimeEnabled&&!cfg.currentEnabled&&cfg.timeFormat==L"Inherit",
        "source reminders remain opt-in and silent by default");
    for(const auto& entry:std::vector<std::pair<std::wstring,std::wstring>>{{L"Type",L"Critical"},{L"Sound",L"file://user.wav"},
        {L"LeadMinutes",L"1441"},{L"LeadMinutes",L"-1"},{L"MinimumNewItems",L"0"},{L"MinimumNewItems",L"1001"},
        {L"CooldownSeconds",L"86401"},{L"Enabled",L"perhaps"},{L"Events",L"Sunset"},{L"Events",L"Fajr,"},
        {L"UpcomingEnabled",L"perhaps"},{L"AtTimeEnabled",L"perhaps"},{L"CurrentEnabled",L"perhaps"},{L"TimeFormat",L"Auto"},
        {L"UpcomingTitle",L"line\nbreak"},{L"CurrentBody",std::wstring(2049,L'x')},{L"Title",L"bad\rtext"},{L"Body",std::wstring(2049,L'x')}}){
        auto value=entry.second;Check(!NormalizeAlertSetting(entry.first,value,error),"invalid alert options are rejected atomically");
    }
    auto value=std::wstring(L"shuruq, fajr,Sunrise,ISHA");
    Check(NormalizeAlertSetting(L"Alerts.Events",value,error)&&value==L"Fajr,Isha,Sunrise","Shuruq aliases Sunrise and canonicalizes duplicate events");
    Check(!FindAlertField(L"HTTPTimeoutSeconds")&&FindAlertField(L"alerts.leadminutes"),"alert schema is explicit and case-insensitive");
    Check(IsPrayerAlertField(L"alerts.CurrentTitle")&&IsPrayerAlertField(L"TimeFormat")&&!IsPrayerAlertField(L"Enabled")&&
        !IsPrayerAlertField(L"MinimumNewItems")&&!IsPrayerAlertField(L"CooldownSeconds")&&!IsPrayerAlertField(L"Title")&&!IsPrayerAlertField(L"Body"),
        "source-neutral alert schema exposes prayer-specific fields for separate RSS UI filtering");
    Check(cfg.title.empty()&&cfg.body.empty(),"common title and body inherit source defaults unless explicitly configured");
    for(const auto* format:{L"inherit",L"12hour",L"24hour",L"system"}){value=format;Check(NormalizeAlertSetting(L"TimeFormat",value,error),"all independent alert time-format choices normalize");}
    Check(cfg.upcomingTitle==L"%event% in %minutes% minutes"&&cfg.upcomingBody==L"%time%"&&cfg.atTimeTitle==L"It's time for %event%"&&
        cfg.atTimeBody==L"%time%"&&cfg.currentTitle==L"%event%, %time%"&&cfg.currentBody==L"at %city%","default phase text keeps event identity in title and time or location in body");
    AlertSettings custom;Check(ReadAlertSettings([](const wchar_t* key,const wchar_t* fallback){return std::wstring(key)==L"UpcomingTitle"?std::wstring(L"  %event% soon  "):std::wstring(fallback);},custom,error)&&
        custom.upcomingTitle==L"  %event% soon  ","custom notification templates retain intentional spaces");
    Check(ReadAlertSettings([](const wchar_t* key,const wchar_t* fallback){const std::wstring name=key;return name==L"Title"?std::wstring(L"Updates in %tile%"):
        name==L"Body"?std::wstring(L"%count% changes"):std::wstring(fallback);},custom,error)&&custom.title==L"Updates in %tile%"&&custom.body==L"%count% changes",
        "generic title and body preserve provider tokens without coupling template parsing to prayer events");
    PrayerPhaseTests();
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

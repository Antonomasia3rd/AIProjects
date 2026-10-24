#include "../dependencies/calendar_reminders.h"
#include "../dependencies/tile_instances.h"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool condition,const char* message){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
using namespace aip::calendar_reminders;
const UTC now=1792738800;
Occurrence Event(const std::wstring& id=L"account/calendar/event"){return {id,L"Team <sync>",L"11:30 AM - 12:00 PM",L"Room & One",now+900,now+2700,false};}
State Fresh(){State s;s.scope=L"0123456789abcdef";return s;}
void SettingsTests(){
    Settings s;std::wstring error;
    Check(ReadSettings([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},s,error)&&!s.enabled&&s.leadMinutes==15&&
        !s.includeAllDay&&s.defaultSnoozeMinutes==15,"reminders are opt-in with a15-minute lead and no all-day alarms");
    Check(ValidSettings(s)&&s.snoozeMinutes==std::vector<int>({5,10,15,30,60}),"default system snooze selections are bounded and valid");
    for(const auto& input:std::vector<std::pair<std::wstring,std::wstring>>{{L"Enabled",L"yes"},{L"IncludeAllDay",L"true"},{L"LeadMinutes",L"0"},
        {L"LeadMinutes",L"1440"},{L"Type",L"alarm"},{L"Sound",L"silent"},{L"SnoozeMinutes",L"1, 10,1440"},{L"DefaultSnoozeMinutes",L"1440"}}){
        auto value=input.second;Check(NormalizeSetting(input.first,value,error),"valid per-tile reminder option normalizes");}
    for(const auto& input:std::vector<std::pair<std::wstring,std::wstring>>{{L"Enabled",L"maybe"},{L"LeadMinutes",L"-1"},{L"LeadMinutes",L"1441"},
        {L"Type",L"Launch"},{L"Sound",L"file://private"},{L"SnoozeMinutes",L""},{L"SnoozeMinutes",L"0"},{L"SnoozeMinutes",L"5,5"},
        {L"SnoozeMinutes",L"1,2,3,4,5,6"},{L"SnoozeMinutes",L"5,"},{L"DefaultSnoozeMinutes",L"0"},{L"Title",L"bad\nline"},{L"Unknown",L"1"}}){
        auto value=input.second;Check(!NormalizeSetting(input.first,value,error)&&!error.empty(),"invalid reminder input fails before persistence");}
    Check(!ReadSettings([](const wchar_t* key,const wchar_t* fallback){return std::wstring(key)==L"DefaultSnoozeMinutes"?L"99":fallback;},s,error),
        "default snooze must refer to one of the configured selection IDs");
    auto bad=s;bad.snoozeMinutes={1,1};Check(!ValidSettings(bad)&&Xml(L"title",L"body",bad).empty(),"invalid direct model cannot generate a system toast");
}
void DueTests(){
    auto s=Fresh();Settings cfg;cfg.enabled=true;const auto event=Event();
    Check(Due(s,{event},now-1,cfg).empty(),"calendar reminder is not early");
    Check(Due(s,{event},now,cfg).size()==1&&Due(s,{event},now+90,cfg).size()==1,"due crossing includes only its bounded90-second jitter window");
    Check(Due(s,{event},now+91,cfg).empty()&&Due(s,{event},now+3600,cfg).empty(),"startup and unlock do not backfill missed reminders");
    auto off=cfg;off.enabled=false;Check(Due(s,{event},now,off).empty(),"disabled reminder settings produce no candidate");
    auto allDay=event;allDay.allDay=true;Check(Due(s,{allDay},now,cfg).empty(),"all-day events are excluded by default");
    cfg.includeAllDay=true;Check(Due(s,{allDay},now,cfg).size()==1,"explicit all-day reminder option is honored");
    cfg.leadMinutes=0;Check(Due(s,{event},event.startUtc,cfg).size()==1&&Due(s,{event},event.startUtc+91,cfg).empty(),"lead zero means at-start with bounded jitter");cfg.leadMinutes=15;
    auto invalid=event;invalid.identity.clear();Check(Due(s,{invalid},now,cfg).empty(),"missing stable event identity cannot produce a reminder");
    invalid=event;invalid.endUtc=invalid.startUtc-1;Check(Due(s,{invalid},now,cfg).empty(),"reversed event timestamps fail closed");
    Check(Due(s,std::vector<Occurrence>(2001,event),now,cfg).empty(),"oversized provider result is rejected without a partial notification storm");
    Check(Due(s,{event,event},now,cfg).size()==1,"duplicated source occurrences collapse before delivery");
    Check(Claim(s,event,now)&&Due(s,{event},now+1,cfg).empty()&&!Claim(s,event,now),"one stable occurrence is claimed once");
    auto changed=event;changed.title=L"Renamed";changed.location=L"New room";Check(Due(s,{changed},now+2,cfg).empty(),"title and location edits do not repeat the same event/start reminder");
    changed.startUtc+=30;changed.endUtc+=30;Check(Due(s,{changed},now+30,cfg).size()==1,"a rescheduled start has a distinct occurrence identity");
    std::vector<Occurrence> batch;for(int i=0;i<12;++i)batch.push_back(Event(L"event-"+std::to_wstring(i)));
    auto bounded=Fresh();const auto due=Due(bounded,batch,now,cfg);Check(due.size()==3,"one due burst is limited to three reminders");
    for(const auto& e:due)Check(Claim(bounded,e,now),"bounded due reminder claim succeeds");
    Check(Due(bounded,batch,now+2,cfg).empty()&&Due(bounded,batch,now+89,cfg).empty(),"repeated polling cannot stretch one startup burst into a storm");
    Check(Due(bounded,batch,now-1,cfg).empty(),"backward clock movement cannot replay the current window");
    Check(Due(bounded,batch,(std::numeric_limits<UTC>::max)(),cfg).empty(),"unrepresentable current time is rejected without overflow");
}
void PersistenceTests(){
    Settings cfg;cfg.enabled=true;auto state=Fresh();const auto event=Event();Check(Claim(state,event,now),"claim fixture occurrence");
    const auto serialized=Serialize(state);State loaded;
    Check(Parse(serialized,state.scope,loaded)&&loaded.delivered.size()==1&&loaded.windowCount==1&&Due(loaded,{event},now+1,cfg).empty(),"restart ledger roundtrip prevents duplicate delivery");
    Check(serialized.find(event.identity)==std::wstring::npos&&serialized.find(event.title)==std::wstring::npos&&serialized.find(event.location)==std::wstring::npos,
        "ledger stores hashes and timing only, without event names or locations");
    Check(!Parse(serialized,L"fedcba9876543210",loaded),"another profile/account scope cannot reuse a ledger");
    for(const auto& corrupt:std::vector<std::wstring>{L"",serialized+L"extra",L"AIP_CALENDAR_REMINDERS_1\n0123456789abcdef 1 4\n0\n",
        L"AIP_CALENDAR_REMINDERS_1\n0123456789abcdef 1 0\n513\n",std::wstring(50001,L'x')})Check(!Parse(corrupt,state.scope,loaded),"malformed or oversized ledger is rejected");
    Check(Removals(state,{},now,false,true).empty(),"pending or failed source data cannot cancel a valid platform snooze");
    Check(Removals(state,{event},now,true,true).empty()&&Removals(state,{},now,true,true).size()==1,"authoritative cancellation removes only vanished owned occurrences");
    Check(Removals(state,{event},now,false,false).size()==1,"explicit disable requests removal even while source is unavailable");
    Check(Removals(state,{event},event.endUtc,false,true).size()==1,"event expiry requests owned toast/snooze cleanup");
    Deactivate(state,OccurrenceKey(event));Check(Removals(state,{},now,true,true).empty()&&Due(state,{event},now,cfg).empty(),"removal does not erase delivered identity or undo dismissal deduplication");
    auto full=Fresh();for(int i=0;i<512;++i)full.delivered.push_back({aip::notifications::StableId(std::to_wstring(i)),now+4000,true});
    Check(!Claim(full,event,now)&&full.delivered.size()==512,"bounded ledger never drops active ownership to make room");
    full.delivered[0].active=false;Check(Claim(full,event,now)&&full.delivered.size()==512,"inactive history can be evicted within the bounded ledger");
    full.delivered.push_back(full.delivered.front());Check(Serialize(full).empty(),"oversized or duplicate state cannot be persisted");
}
void PayloadTests(){
    Settings s;const auto event=Event();
    Check(Format(L"%event%|%time%|%location%|%tile%|%minutes%",event,L"Calendar",now)==L"Team <sync>|11:30 AM - 12:00 PM|Room & One|Calendar|15", "reminder templates use event and display-zone time metadata");
    Check(Format(L"%details%",event,L"Calendar",now)==event.timeText+L"\n"+event.location,"default details separate the time range and location");
    auto literal=event;literal.title=L"Keep %time% literal";Check(Format(L"%event%",literal,L"Calendar",now)==literal.title,"event content is never recursively interpreted as another template");
    for(const auto type:{aip::notifications::EventType::General,aip::notifications::EventType::Reminder,aip::notifications::EventType::Alarm}){
        s.type=type;const auto xml=Xml(L"<Title>&",L"Body & <data>",s,L"%minutes% mins & more");
        Check(xml.find(L"&lt;Title&gt;&amp;")!=std::wstring::npos&&xml.find(L"Body &amp; &lt;data&gt;")!=std::wstring::npos,"notification title/body are escaped");
        Check(xml.find(L"activationType=\"system\" arguments=\"snooze\" hint-inputId=\"SnoozeTime\"")!=std::wstring::npos&&
            xml.find(L"activationType=\"system\" arguments=\"dismiss\"")!=std::wstring::npos&&xml.find(L"activationType=\"foreground\"")==std::wstring::npos,
            "native Windows handles both actions without custom activation or credentials");
        Check(xml.find(L"defaultInput=\"15\"")!=std::wstring::npos&&xml.find(L"id=\"60\" content=\"60 mins &amp; more\"")!=std::wstring::npos,
            "selection IDs are minute values and localized labels are escaped");
    }
    s.sound=aip::notifications::EventSound::Silent;Check(Xml(L"a",L"b",s).find(L"<audio silent=\"true\"/>")!=std::wstring::npos,"silent choice stays silent for alarm scenario");
    s.sound=aip::notifications::EventSound::Alarm;Check(Xml(L"a",L"b",s).find(L"duration=\"long\"")!=std::wstring::npos,"explicit looping alarm sound has a long duration");
}
void TileModelTests(){
    const std::map<std::wstring,std::wstring> values{{L"Tiles.SchemaVersion",L"1"},{L"Tiles.Instances",L"left,right"},
        {L"Tile.left.Type",L"Calendar"},{L"Tile.left.CalendarReminders.Enabled",L"1"},{L"Tile.left.CalendarReminders.LeadMinutes",L"0"},
        {L"Tile.right.Type",L"Calendar"},{L"Tile.right.CalendarReminders.LeadMinutes",L"30"},{L"CalendarReminders.Enabled",L"1"}};
    aip::content::TileInstanceConfiguration configuration;std::wstring error;
    Check(aip::content::ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){
        const auto found=values.find(std::wstring(section)+L"."+key);return found==values.end()?std::wstring(fallback):found->second;
    },configuration,error)&&configuration.tiles.size()==2,"actual tile model captures reminder settings without reading files");
    if(configuration.tiles.size()==2){Check(configuration.tiles[0].settings.at(L"calendarreminders.enabled")==L"1"&&configuration.tiles[0].settings.at(L"calendarreminders.leadminutes")==L"0"&&
        configuration.tiles[1].settings.at(L"calendarreminders.enabled")==L"0"&&configuration.tiles[1].settings.at(L"calendarreminders.leadminutes")==L"30",
        "Calendar reminder values are per tile and never inherit a global enable switch");}
    std::wstring value=L"-1";Check(!aip::content::NormalizeTileInstanceSetting(L"Tile.test",L"CalendarReminders.LeadMinutes",value,error),"typed CLI validation reaches the reminder model");
}
}
int main(){SettingsTests();DueTests();PersistenceTests();PayloadTests();TileModelTests();std::cout<<checks<<" Calendar reminder checks, "<<failures<<" failures; no native notification, account or network calls\n";return failures?1:0;}

#pragma once
// Calendar reminders consume already-fetched occurrences. This portable model
// has no scheduler, account access, notification transport or file operations.
#include "tile_source_alerts.h"
#include <limits>

namespace aip { namespace calendar_reminders {
using UTC=std::int64_t;
struct Settings {
    bool enabled=false,includeAllDay=false;
    int leadMinutes=15,defaultSnoozeMinutes=15;
    aip::notifications::EventType type=aip::notifications::EventType::Reminder;
    aip::notifications::EventSound sound=aip::notifications::EventSound::Default;
    std::vector<int> snoozeMinutes{5,10,15,30,60};
    std::wstring title=L"%event%",body=L"%details%";
};
struct Field {const wchar_t* key;const wchar_t* fallback;const wchar_t* label;};
inline constexpr Field Fields[]={
    {L"Enabled",L"0",L"Enable Calendar reminders"},
    {L"LeadMinutes",L"15",L"Remind before event (minutes, 0 = at start)"},
    {L"Type",L"Reminder",L"Notification type"},{L"Sound",L"Default",L"Notification sound"},
    {L"IncludeAllDay",L"0",L"Include all-day events"},
    {L"SnoozeMinutes",L"5,10,15,30,60",L"Snooze choices (minutes, up to 5)"},
    {L"DefaultSnoozeMinutes",L"15",L"Default snooze (minutes)"},
    {L"Title",L"%event%",L"Reminder title template"},{L"Body",L"%details%",L"Reminder body template"}
};
inline const Field* FindField(std::wstring key) {
    key=aip::prayer::Lower(aip::prayer::Trim(std::move(key)));
    if(key.compare(0,18,L"calendarreminders.")==0)key.erase(0,18);
    for(const auto& field:Fields)if(aip::prayer::Lower(field.key)==key)return &field;
    return nullptr;
}
inline bool ParseSnooze(const std::wstring& text,std::vector<int>& result,std::wstring& canonical) {
    std::vector<int> values;std::set<int> seen;std::size_t begin=0;canonical.clear();
    if(text.size()>64)return false;
    while(begin<=text.size()){
        const auto end=text.find(L',',begin);int value=0;
        if(!aip::prayer::Integer(aip::prayer::Trim(text.substr(begin,end==std::wstring::npos?end:end-begin)),value)||
            value<1||value>1440||!seen.insert(value).second||values.size()>=5)return false;
        values.push_back(value);if(end==std::wstring::npos)break;begin=end+1;
    }
    for(const auto value:values){if(!canonical.empty())canonical+=L",";canonical+=std::to_wstring(value);}
    result=std::move(values);return !result.empty();
}
inline bool NormalizeSetting(const std::wstring& key,std::wstring& value,std::wstring& error) {
    error.clear();const auto* field=FindField(key);if(!field){error=L"Unknown Calendar reminder setting.";return false;}
    const auto name=std::wstring(field->key),lower=aip::prayer::Lower(aip::prayer::Trim(value));
    if(name==L"Enabled"||name==L"IncludeAllDay"){
        if(lower==L"1"||lower==L"true"||lower==L"on"||lower==L"yes")value=L"1";
        else if(lower==L"0"||lower==L"false"||lower==L"off"||lower==L"no")value=L"0";
        else{error=L"CalendarReminders."+name+L" must be a boolean.";return false;}
    }else if(name==L"Type"||name==L"Sound"){
        return aip::notifications::NormalizeAlertSetting(name,value,error);
    }else if(name==L"Title"||name==L"Body"){
        if(!aip::prayer::PlainText(value,2048)){error=L"Calendar reminder templates must be bounded single-line text.";return false;}
    }else if(name==L"SnoozeMinutes"){
        std::vector<int> ignored;std::wstring canonical;
        if(!ParseSnooze(value,ignored,canonical)){error=L"Snooze choices must be 1 to 5 different minute values between 1 and 1440.";return false;}
        value=std::move(canonical);
    }else{
        int number=0;if(!aip::prayer::Integer(aip::prayer::Trim(value),number)||number<(name==L"LeadMinutes"?0:1)||number>1440){
            error=L"CalendarReminders."+name+L" is outside its supported minute range.";return false;}
        value=std::to_wstring(number);
    }
    return true;
}
template<class Reader> bool ReadSettings(Reader read,Settings& output,std::wstring& error) {
    Settings value;
    for(const auto& field:Fields){std::wstring text=read(field.key,field.fallback);if(!NormalizeSetting(field.key,text,error))return false;
        const auto key=std::wstring(field.key);
        if(key==L"Enabled")value.enabled=text==L"1";else if(key==L"IncludeAllDay")value.includeAllDay=text==L"1";
        else if(key==L"LeadMinutes")aip::prayer::Integer(text,value.leadMinutes);
        else if(key==L"DefaultSnoozeMinutes")aip::prayer::Integer(text,value.defaultSnoozeMinutes);
        else if(key==L"SnoozeMinutes"){std::wstring ignored;ParseSnooze(text,value.snoozeMinutes,ignored);}
        else if(key==L"Title")value.title=text;else if(key==L"Body")value.body=text;
        else if(key==L"Type")value.type=text==L"Alarm"?aip::notifications::EventType::Alarm:text==L"General"?aip::notifications::EventType::General:aip::notifications::EventType::Reminder;
        else if(key==L"Sound")value.sound=text==L"Alarm"?aip::notifications::EventSound::Alarm:text==L"Silent"?aip::notifications::EventSound::Silent:aip::notifications::EventSound::Default;
    }
    if(std::find(value.snoozeMinutes.begin(),value.snoozeMinutes.end(),value.defaultSnoozeMinutes)==value.snoozeMinutes.end()){
        error=L"Default snooze minutes must be one of this tile's snooze choices.";return false;}
    output=std::move(value);return true;
}
inline std::wstring SettingsKey(const Settings& s) {
    std::wstring key=std::to_wstring(s.enabled)+L"|"+std::to_wstring(s.includeAllDay)+L"|"+std::to_wstring(s.leadMinutes)+L"|"+
        std::to_wstring(static_cast<int>(s.type))+L"|"+std::to_wstring(static_cast<int>(s.sound))+L"|"+std::to_wstring(s.defaultSnoozeMinutes);
    for(const auto n:s.snoozeMinutes)key+=L"|"+std::to_wstring(n);
    return key+L"|"+std::to_wstring(s.title.size())+L":"+s.title+L"|"+s.body;
}
inline bool ValidSettings(const Settings& s) {
    if(s.leadMinutes<0||s.leadMinutes>1440||s.snoozeMinutes.empty()||s.snoozeMinutes.size()>5||
        s.type<aip::notifications::EventType::General||s.type>aip::notifications::EventType::Alarm||
        s.sound<aip::notifications::EventSound::Silent||s.sound>aip::notifications::EventSound::Alarm||
        !aip::prayer::PlainText(s.title,2048)||!aip::prayer::PlainText(s.body,2048))return false;
    std::set<int> unique;for(const auto n:s.snoozeMinutes)if(n<1||n>1440||!unique.insert(n).second)return false;
    return unique.count(s.defaultSnoozeMinutes)!=0;
}
struct Occurrence {std::wstring identity,title,timeText,location;UTC startUtc=0,endUtc=0;bool allDay=false;};
inline bool ValidUtc(UTC value){return value>=-11644473600LL&&value<=253402214399LL;}
inline bool ValidOccurrence(const Occurrence& event) {
    return !event.identity.empty()&&event.identity.size()<=4096&&ValidUtc(event.startUtc)&&ValidUtc(event.endUtc)&&
        event.endUtc>=event.startUtc&&event.endUtc-event.startUtc<=366LL*86400;
}
inline std::wstring OccurrenceKey(const Occurrence& event) {
    return aip::notifications::StableId(std::to_wstring(event.identity.size())+L":"+event.identity+L"|"+std::to_wstring(event.startUtc));
}
inline UTC Expiry(const Occurrence& event) {
    return event.endUtc>event.startUtc?event.endUtc:(std::min)(event.startUtc+3600,UTC(253402214399LL));
}
struct Record {std::wstring key;UTC expiryUtc=0;bool active=false;};
struct State {std::wstring scope;std::vector<Record> delivered;UTC windowUtc=0;unsigned windowCount=0;};
inline bool Delivered(const State& state,const std::wstring& key) {
    return std::any_of(state.delivered.begin(),state.delivered.end(),[&](const Record& r){return r.key==key;});
}
inline unsigned WindowCount(const State& state,UTC now) {
    return now>=state.windowUtc&&now-state.windowUtc<90?state.windowCount:0;
}
inline std::vector<Occurrence> Due(const State& state,const std::vector<Occurrence>& events,UTC now,const Settings& settings) {
    std::vector<Occurrence> due;if(!settings.enabled||!ValidSettings(settings)||!ValidUtc(now)||events.size()>2000||now<state.windowUtc)return due;
    const auto used=WindowCount(state,now);if(used>=3)return due;
    std::set<std::wstring> unique;
    for(const auto& event:events){if(!ValidOccurrence(event)||(!settings.includeAllDay&&event.allDay)||Expiry(event)<=now)continue;
        const auto key=OccurrenceKey(event);const auto at=event.startUtc-static_cast<UTC>(settings.leadMinutes)*60;
        if(now<at||now-at>90||(settings.leadMinutes>0&&now>=event.startUtc)||Delivered(state,key)||!unique.insert(key).second)continue;
        due.push_back(event);
    }
    std::sort(due.begin(),due.end(),[](const Occurrence& a,const Occurrence& b){return a.startUtc!=b.startUtc?a.startUtc<b.startUtc:a.identity<b.identity;});
    if(due.size()>3-used)due.resize(3-used);return due;
}
inline bool Claim(State& state,const Occurrence& event,UTC now) {
    if(!ValidOccurrence(event)||!ValidUtc(now)||now<state.windowUtc||WindowCount(state,now)>=3||Delivered(state,OccurrenceKey(event)))return false;
    if(state.delivered.size()>=512){const auto old=std::find_if(state.delivered.begin(),state.delivered.end(),[](const Record& r){return !r.active;});
        if(old==state.delivered.end())return false;state.delivered.erase(old);}
    if(now-state.windowUtc>=90){state.windowUtc=now;state.windowCount=0;}
    state.delivered.push_back({OccurrenceKey(event),Expiry(event),true});++state.windowCount;return true;
}
inline std::vector<std::wstring> Removals(const State& state,const std::vector<Occurrence>& events,UTC now,bool authoritative,bool enabled) {
    if(!ValidUtc(now))return {};
    std::set<std::wstring> current;
    if(authoritative&&enabled&&events.size()<=2000)for(const auto& event:events)if(ValidOccurrence(event))current.insert(OccurrenceKey(event));
    std::vector<std::wstring> result;
    for(const auto& r:state.delivered)if(r.active&&(!enabled||r.expiryUtc<=now||(authoritative&&events.size()<=2000&&!current.count(r.key))))result.push_back(r.key);
    return result;
}
inline void Deactivate(State& state,const std::wstring& key) {
    for(auto& r:state.delivered)if(r.key==key)r.active=false;
}
inline std::wstring Serialize(const State& state) {
    if(!aip::notifications::HexId(state.scope)||state.delivered.size()>512||!ValidUtc(state.windowUtc)||state.windowCount>3)return {};
    std::wostringstream text;text<<L"AIP_CALENDAR_REMINDERS_1\n"<<state.scope<<L' '<<state.windowUtc<<L' '<<state.windowCount<<L'\n'<<state.delivered.size()<<L'\n';
    std::set<std::wstring> keys;
    for(const auto& r:state.delivered){if(!aip::notifications::HexId(r.key)||!ValidUtc(r.expiryUtc)||!keys.insert(r.key).second)return {};
        text<<r.key<<L' '<<r.expiryUtc<<L' '<<r.active<<L'\n';}return text.str();
}
inline bool Parse(const std::wstring& text,const std::wstring& scope,State& output) {
    if(text.empty()||text.size()>50000||!aip::notifications::HexId(scope))return false;
    std::wistringstream input(text);std::wstring version;State value;std::size_t count=0;
    if(!(input>>version>>value.scope>>value.windowUtc>>value.windowCount>>count)||version!=L"AIP_CALENDAR_REMINDERS_1"||
        value.scope!=scope||!ValidUtc(value.windowUtc)||value.windowCount>3||count>512)return false;
    std::set<std::wstring> keys;
    for(std::size_t i=0;i<count;++i){Record r;int active=0;if(!(input>>r.key>>r.expiryUtc>>active)||
        !aip::notifications::HexId(r.key)||!ValidUtc(r.expiryUtc)||active<0||active>1||!keys.insert(r.key).second)return false;
        r.active=active!=0;value.delivered.push_back(std::move(r));}
    std::wstring extra;if(input>>extra)return false;output=std::move(value);return true;
}
inline std::wstring Replace(std::wstring text,const wchar_t* token,const std::wstring& value) {
    std::size_t at=0;const auto length=std::wstring(token).size();
    while((at=text.find(token,at))!=std::wstring::npos){text.replace(at,length,value);at+=value.size();}return text;
}
inline std::wstring Format(std::wstring text,const Occurrence& event,const std::wstring& tile,UTC now) {
    auto details=event.timeText;if(!event.location.empty()){if(!details.empty())details+=L"\n";details+=event.location;}
    const auto minutes=(std::max)(UTC(0),(event.startUtc-now+59)/60);
    const std::vector<std::pair<std::wstring,std::wstring>> tokens{{L"%event%",event.title},{L"%time%",event.timeText},
        {L"%location%",event.location},{L"%details%",details},{L"%tile%",tile},{L"%minutes%",std::to_wstring(minutes)}};
    std::wstring result;
    for(std::size_t at=0;at<text.size()&&result.size()<2048;){bool matched=false;
        for(const auto& token:tokens)if(text.compare(at,token.first.size(),token.first)==0){
            result+=token.second.substr(0,2048-result.size());at+=token.first.size();matched=true;break;}
        if(!matched)result+=text[at++];
    }
    return result;
}
inline std::wstring Xml(const std::wstring& title,const std::wstring& body,const Settings& settings,
    const std::wstring& minuteLabel=L"%minutes% minutes") {
    using namespace aip::notifications;
    if(!ValidSettings(settings))return {};
    auto xml=SourceEventXml(title,body,settings.type,settings.sound);const auto actions=xml.find(L"<actions>");
    if(actions!=std::wstring::npos)xml.erase(actions,xml.find(L"</actions>",actions)+10-actions);
    std::wstring controls=L"<actions><input id=\"SnoozeTime\" type=\"selection\" defaultInput=\""+std::to_wstring(settings.defaultSnoozeMinutes)+L"\">";
    for(const auto n:settings.snoozeMinutes)controls+=L"<selection id=\""+std::to_wstring(n)+L"\" content=\""+
        Escape(Replace(minuteLabel,L"%minutes%",std::to_wstring(n)),128)+L"\"/>";
    controls+=L"</input><action activationType=\"system\" arguments=\"snooze\" hint-inputId=\"SnoozeTime\" content=\"\"/>"
        L"<action activationType=\"system\" arguments=\"dismiss\" content=\"\"/></actions>";
    xml.insert(xml.rfind(L"</toast>"),controls);return xml;
}
} }

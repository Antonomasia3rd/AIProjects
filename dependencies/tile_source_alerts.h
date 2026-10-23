#pragma once
// Source alerts are evaluated only from already fetched data. This model owns
// neither timers, networking, notification APIs nor device audio.
#include "notification_policy.h"
#include "content_sources/prayer_times_model.h"
#include <set>
#include <sstream>

namespace aip::notifications {
enum class EventType { General, Reminder, Alarm };
enum class EventSound { Silent, Default, Alarm };
struct AlertSettings {
    bool enabled = false;
    EventType type = EventType::General;
    EventSound sound = EventSound::Silent;
    std::wstring title,body; // Empty inherits the source's built-in/global text.
    int leadMinutes = 20, minimumNewItems = 1, cooldownSeconds = 60;
    unsigned events = 0x1fu; // The five prayers. Sunrise is an independent opt-in.
    bool upcomingEnabled = true, atTimeEnabled = true, currentEnabled = false;
    std::wstring timeFormat=L"Inherit";
    std::wstring upcomingTitle=L"%event% in %minutes% minutes",upcomingBody=L"%time%";
    std::wstring atTimeTitle=L"It's time for %event%",atTimeBody=L"%time%";
    std::wstring currentTitle=L"%event%, %time%",currentBody=L"at %city%";
};
struct AlertField { const wchar_t* key; const wchar_t* fallback; const wchar_t* label; };
inline constexpr AlertField AlertFields[] = {
    {L"Enabled",L"0",L"Enable notifications"}, {L"Type",L"General",L"Notification type"},
    {L"Sound",L"Silent",L"Notification sound"}, {L"Title",L"",L"Notification title (empty = source default)"},
    {L"Body",L"",L"Notification body (empty = source default)"}, {L"LeadMinutes",L"20",L"Notify before event (minutes)"},
    {L"Events",L"Fajr,Dhuhr,Asr,Maghrib,Isha",L"Prayer events"},
    {L"MinimumNewItems",L"1",L"New items required"}, {L"CooldownSeconds",L"60",L"Minimum time between notifications (seconds)"},
    {L"UpcomingEnabled",L"1",L"Notify before event"}, {L"AtTimeEnabled",L"1",L"Notify at event time"},
    {L"CurrentEnabled",L"0",L"Show current event in notification history"},
    {L"TimeFormat",L"Inherit",L"Notification time format"},
    {L"UpcomingTitle",L"%event% in %minutes% minutes",L"Upcoming notification title"},
    {L"UpcomingBody",L"%time%",L"Upcoming notification body"},
    {L"AtTimeTitle",L"It's time for %event%",L"At-time notification title"},
    {L"AtTimeBody",L"%time%",L"At-time notification body"},
    {L"CurrentTitle",L"%event%, %time%",L"Current notification title"},
    {L"CurrentBody",L"at %city%",L"Current notification body"}
};
inline const AlertField* FindAlertField(std::wstring key) {
    key = aip::prayer::Lower(aip::prayer::Trim(std::move(key)));
    if (key.compare(0,7,L"alerts.") == 0) key.erase(0,7);
    for (const auto& field : AlertFields) if (aip::prayer::Lower(field.key) == key) return &field;
    return nullptr;
}
inline bool IsPrayerAlertField(const std::wstring& key) {
    const auto* field=FindAlertField(key);if(!field)return false;
    const std::wstring name=field->key;
    return name==L"LeadMinutes"||name==L"Events"||name==L"UpcomingEnabled"||name==L"AtTimeEnabled"||
        name==L"CurrentEnabled"||name==L"TimeFormat"||name==L"UpcomingTitle"||name==L"UpcomingBody"||
        name==L"AtTimeTitle"||name==L"AtTimeBody"||name==L"CurrentTitle"||name==L"CurrentBody";
}
inline bool ParsePrayerEvents(const std::wstring& value,unsigned& mask,std::wstring& canonical) {
    mask = 0; canonical.clear(); size_t begin = 0;
    while (begin <= value.size()) {
        const auto end = value.find(L',',begin);
        const auto token = aip::prayer::Lower(aip::prayer::Trim(value.substr(begin,end == std::wstring::npos ? end : end-begin)));
        int found = -1;
        for (int i=0;i<6;++i) if (token == aip::prayer::Lower(aip::prayer::PrayerName(static_cast<aip::prayer::PrayerId>(i)))) found=i;
        if (token == L"shuruq" || token == L"shurooq") found=5;
        if (found < 0) return false;
        mask |= 1u << found;
        if (end == std::wstring::npos) break;
        begin=end+1;
    }
    for (int i=0;i<6;++i) if (mask & (1u << i)) {
        if (!canonical.empty()) canonical += L",";
        canonical += aip::prayer::PrayerName(static_cast<aip::prayer::PrayerId>(i));
    }
    return mask != 0;
}
inline bool NormalizeAlertSetting(const std::wstring& key,std::wstring& value,std::wstring& error) {
    const auto* field=FindAlertField(key); error.clear();
    if (!field) { error=L"Unknown source notification setting.";return false; }
    const auto name=std::wstring(field->key), lower=aip::prayer::Lower(aip::prayer::Trim(value));
    if(name==L"Enabled"||name==L"UpcomingEnabled"||name==L"AtTimeEnabled"||name==L"CurrentEnabled") {
        if(lower==L"1"||lower==L"true"||lower==L"yes"||lower==L"on")value=L"1";
        else if(lower==L"0"||lower==L"false"||lower==L"no"||lower==L"off")value=L"0";
        else {error=L"Alerts."+name+L" must be a boolean.";return false;}
    } else if(name==L"Type") {
        if(lower==L"general")value=L"General";else if(lower==L"reminder")value=L"Reminder";else if(lower==L"alarm")value=L"Alarm";
        else {error=L"Alerts.Type must be General, Reminder or Alarm.";return false;}
    } else if(name==L"Sound") {
        if(lower==L"silent")value=L"Silent";else if(lower==L"default")value=L"Default";else if(lower==L"alarm")value=L"Alarm";
        else {error=L"Alerts.Sound must be Silent, Default or Alarm.";return false;}
    } else if(name==L"Events") {
        unsigned ignored=0;std::wstring canonical;
        if(!ParsePrayerEvents(value,ignored,canonical)){error=L"Alerts.Events must name Fajr, Dhuhr, Asr, Maghrib, Isha or Sunrise, separated by commas.";return false;}
        value=std::move(canonical);
    } else if(name==L"TimeFormat") {
        if(lower==L"inherit")value=L"Inherit";else if(lower==L"12hour")value=L"12Hour";
        else if(lower==L"24hour")value=L"24Hour";else if(lower==L"system")value=L"System";
        else {error=L"Alerts.TimeFormat must be Inherit, 12Hour, 24Hour or System.";return false;}
    } else if(name==L"Title"||name==L"Body"||name==L"UpcomingTitle"||name==L"UpcomingBody"||name==L"AtTimeTitle"||name==L"AtTimeBody"||name==L"CurrentTitle"||name==L"CurrentBody") {
        if(!aip::prayer::PlainText(value,2048)){error=L"Alerts."+name+L" must be bounded single-line text.";return false;}
    } else {
        int number=0;const int maximum=name==L"LeadMinutes"?1440:name==L"MinimumNewItems"?1000:86400;
        if(!aip::prayer::Integer(aip::prayer::Trim(value),number)||number<(name==L"MinimumNewItems"?1:0)||number>maximum) {
            error=L"Alerts."+name+L" is outside its supported range.";return false;
        }
        value=std::to_wstring(number);
    }
    return true;
}
template<class Reader> bool ReadAlertSettings(Reader read,AlertSettings& output,std::wstring& error) {
    AlertSettings result;
    for(const auto& field:AlertFields) {
        auto value=read(field.key,field.fallback);
        if(!NormalizeAlertSetting(field.key,value,error))return false;
        const auto name=std::wstring(field.key);
        if(name==L"Enabled")result.enabled=value==L"1";
        else if(name==L"UpcomingEnabled")result.upcomingEnabled=value==L"1";
        else if(name==L"AtTimeEnabled")result.atTimeEnabled=value==L"1";
        else if(name==L"CurrentEnabled")result.currentEnabled=value==L"1";
        else if(name==L"Title")result.title=value;
        else if(name==L"Body")result.body=value;
        else if(name==L"Type")result.type=value==L"Alarm"?EventType::Alarm:value==L"Reminder"?EventType::Reminder:EventType::General;
        else if(name==L"Sound")result.sound=value==L"Alarm"?EventSound::Alarm:value==L"Default"?EventSound::Default:EventSound::Silent;
        else if(name==L"Events"){std::wstring ignored;ParsePrayerEvents(value,result.events,ignored);}
        else if(name==L"LeadMinutes")aip::prayer::Integer(value,result.leadMinutes);
        else if(name==L"MinimumNewItems")aip::prayer::Integer(value,result.minimumNewItems);
        else if(name==L"CooldownSeconds")aip::prayer::Integer(value,result.cooldownSeconds);
        else if(name==L"TimeFormat")result.timeFormat=value;
        else if(name==L"UpcomingTitle")result.upcomingTitle=value;
        else if(name==L"UpcomingBody")result.upcomingBody=value;
        else if(name==L"AtTimeTitle")result.atTimeTitle=value;
        else if(name==L"AtTimeBody")result.atTimeBody=value;
        else if(name==L"CurrentTitle")result.currentTitle=value;
        else if(name==L"CurrentBody")result.currentBody=value;
    }
    output=result;return true;
}
struct AlertState {
    std::wstring scope;
    bool baselineReady=false;
    std::vector<std::wstring> seen,pending,delivered;
    aip::prayer::UTC lastRssNoticeUtc=0;
};
inline void SetAlertScope(AlertState& state,const std::wstring& scope) {
    if(state.scope!=scope){state={};state.scope=scope;}
}
inline bool Remember(std::vector<std::wstring>& values,const std::wstring& key,size_t maximum) {
    const auto at=std::find(values.begin(),values.end(),key);if(at!=values.end())return false;
    values.push_back(key);if(values.size()>maximum)values.erase(values.begin(),values.begin()+(values.size()-maximum));return true;
}
// A first successful response establishes a baseline, including after a URL
// change. Reordering, repeated reads and failures cannot generate new items.
inline size_t ObserveRss(AlertState& state,const std::vector<std::wstring>& identities,aip::prayer::UTC now,const AlertSettings& cfg,bool allowNotify=true) {
    if(!cfg.enabled||identities.empty())return 0;
    std::set<std::wstring> unique;
    for(const auto& id:identities)if(!id.empty()&&unique.insert(id).second&&Remember(state.seen,id,4096)&&state.baselineReady)
        Remember(state.pending,id,4096);
    state.pending.erase(std::remove_if(state.pending.begin(),state.pending.end(),[&](const std::wstring& id){
        return std::find(state.seen.begin(),state.seen.end(),id)==state.seen.end();
    }),state.pending.end());
    if(!allowNotify){state.baselineReady=true;state.pending.clear();return 0;}
    if(!state.baselineReady){state.baselineReady=true;return 0;}
    if(state.pending.size()<static_cast<size_t>(cfg.minimumNewItems)||
        (state.lastRssNoticeUtc>0&&(now<state.lastRssNoticeUtc||now-state.lastRssNoticeUtc<cfg.cooldownSeconds)))return 0;
    const auto count=state.pending.size();state.pending.clear();state.lastRssNoticeUtc=now;return count;
}
enum class PrayerAlertPhase { Upcoming, AtTime };
struct PrayerAlert { aip::prayer::PrayerId event=aip::prayer::PrayerId::Fajr; aip::prayer::UTC eventUtc=0;int offsetMinutes=0;std::wstring key;PrayerAlertPhase phase=PrayerAlertPhase::Upcoming; };
inline std::vector<PrayerAlert> DuePrayers(const aip::prayer::Configuration& cfg,const aip::prayer::Data& data,
    aip::prayer::UTC now,const AlertSettings& alerts,AlertState& state) {
    std::vector<PrayerAlert> result;
    auto validated=cfg;std::wstring error;
    if(!alerts.enabled||now < -11644473600LL||now>253402214399LL||!aip::prayer::Validate(validated,error,true)||alerts.leadMinutes<0||alerts.leadMinutes>1440||
        data.configKey!=aip::prayer::ConfigurationKey(cfg))return result;
    for(const auto& day:data.schedule.days)if(aip::prayer::ValidDay(day)) {
        std::vector<aip::prayer::Prayer> events(day.prayers.begin(),day.prayers.end());if(day.hasSunrise)events.push_back(day.sunrise);
        for(const auto& event:events) {
            if(!(alerts.events&(1u<<static_cast<unsigned>(event.id))))continue;
            const auto utc=aip::prayer::Adjusted(event,cfg);
            for(const auto phase:{PrayerAlertPhase::Upcoming,PrayerAlertPhase::AtTime}) {
                const int lead=phase==PrayerAlertPhase::Upcoming?alerts.leadMinutes:0;
                if(phase==PrayerAlertPhase::Upcoming?(!alerts.upcomingEnabled||lead<=0):!alerts.atTimeEnabled)continue;
                const auto due=utc-static_cast<aip::prayer::UTC>(lead)*60;
                // Each phase has a short jitter grace. Sleep/unlock never
                // replays an old lead or a prayer more than 90 seconds late.
                if(now<due||now-due>90||(lead>0&&now>=utc))continue;
                // Preserve historical lead keys, including the old lead=0
                // at-time key, so a saved baseline still prevents duplicates.
                const auto key=StableId(data.configKey+L"|"+std::to_wstring(static_cast<int>(event.id))+L"|"+
                    std::to_wstring(utc)+L"|"+std::to_wstring(lead));
                if(Remember(state.delivered,key,128))result.push_back({event.id,utc,event.offsetMinutes,key,phase});
            }
        }
    }
    return result;
}
struct CurrentPrayerAlert {
    bool available=false,stale=false;
    aip::prayer::PrayerId event=aip::prayer::PrayerId::Fajr;
    aip::prayer::UTC eventUtc=0,endUtc=0;
    int offsetMinutes=0;
    std::wstring name,key;
};
inline CurrentPrayerAlert CurrentPrayer(const aip::prayer::Configuration& cfg,const aip::prayer::Data& data,
    aip::prayer::UTC now,const AlertSettings& alerts) {
    CurrentPrayerAlert result;auto validated=cfg;std::wstring error;
    if(!alerts.enabled||!alerts.currentEnabled||now < -11644473600LL||now>253402214399LL||
        !aip::prayer::Validate(validated,error,true)||data.configKey!=aip::prayer::ConfigurationKey(cfg)||
        !aip::prayer::CurrentDay(data,now))return result;
    const aip::prayer::Prayer* current=nullptr;
    auto currentUtc=(std::numeric_limits<aip::prayer::UTC>::min)(),nextUtc=(std::numeric_limits<aip::prayer::UTC>::max)();
    for(const auto& day:data.schedule.days)if(aip::prayer::ValidDay(day))for(const auto& event:day.prayers) {
        const auto utc=aip::prayer::Adjusted(event,cfg);
        if(utc<=now&&utc>currentUtc){current=&event;currentUtc=utc;}
        if(utc>now&&utc<nextUtc)nextUtc=utc;
    }
    // Never guess yesterday's Isha, tomorrow's Fajr or an indefinitely long
    // current period from incomplete cached days. Sunrise cannot end a prayer.
    if(!current||nextUtc==(std::numeric_limits<aip::prayer::UTC>::max)()||nextUtc-currentUtc>86400||
        !(alerts.events&(1u<<static_cast<unsigned>(current->id))))return result;
    result.available=true;result.stale=data.stale;result.event=current->id;result.eventUtc=currentUtc;
    result.endUtc=nextUtc;result.offsetMinutes=current->offsetMinutes;result.name=aip::prayer::PrayerName(current->id);
    result.key=StableId(data.configKey+L"|"+std::to_wstring(static_cast<int>(current->id))+L"|"+std::to_wstring(currentUtc)+L"|current");
    return result;
}
inline bool HexId(const std::wstring& value) {
    return value.size()==16&&std::all_of(value.begin(),value.end(),[](wchar_t ch){return(ch>=L'0'&&ch<=L'9')||(ch>=L'a'&&ch<=L'f');});
}
inline std::wstring SerializeAlertState(const AlertState& state) {
    if(!HexId(state.scope)||state.seen.size()>4096||state.pending.size()>4096||state.delivered.size()>128||state.lastRssNoticeUtc<0)return {};
    std::wostringstream out;out<<L"AIP_ALERTS_1\n"<<state.scope<<L' '<<state.baselineReady<<L' '<<state.lastRssNoticeUtc<<L'\n';
    for(const auto* values:{&state.seen,&state.pending,&state.delivered}) {
        out<<values->size()<<L'\n';for(const auto& value:*values){if(!HexId(value))return {};out<<value<<L'\n';}
    }
    return out.str();
}
inline bool ParseAlertState(const std::wstring& text,const std::wstring& expectedScope,AlertState& output) {
    if(text.empty()||text.size()>150000||!HexId(expectedScope))return false;
    std::wistringstream in(text);std::wstring version;AlertState result;int baseline=0;
    if(!(in>>version>>result.scope>>baseline>>result.lastRssNoticeUtc)||version!=L"AIP_ALERTS_1"||result.scope!=expectedScope||
        baseline<0||baseline>1||result.lastRssNoticeUtc<0||result.lastRssNoticeUtc>253402300799LL)return false;
    result.baselineReady=baseline!=0;
    for(auto* values:{&result.seen,&result.pending,&result.delivered}) {
        size_t count=0;if(!(in>>count)||count>(values==&result.delivered?128u:4096u))return false;
        std::set<std::wstring> unique;
        for(size_t i=0;i<count;++i){std::wstring id;if(!(in>>id)||!HexId(id)||!unique.insert(id).second)return false;values->push_back(std::move(id));}
    }
    std::wstring trailing;if(in>>trailing)return false;
    for(const auto& pending:result.pending)if(std::find(result.seen.begin(),result.seen.end(),pending)==result.seen.end())return false;
    output=std::move(result);return true;
}
inline std::wstring SourceEventXml(const std::wstring& title,const std::wstring& message,EventType type,EventSound sound) {
    const auto scenario=type==EventType::Alarm?L" scenario=\"alarm\"":type==EventType::Reminder?L" scenario=\"reminder\"":L"";
    const auto audio=sound==EventSound::Silent?L"<audio silent=\"true\"/>":sound==EventSound::Alarm?
        L"<audio src=\"ms-winsoundevent:Notification.Looping.Alarm2\" loop=\"true\"/>":
        L"<audio src=\"ms-winsoundevent:Notification.Default\" loop=\"false\"/>";
    // Looping desktop toast audio requires explicit long duration. Only the
    // user's Alarm sound choice enables it, independently of visual scenario.
    return std::wstring(L"<toast")+scenario+(sound==EventSound::Alarm?L" duration=\"long\"":L"")+L"><visual><binding template=\"ToastGeneric\"><text>"+Escape(title,128)+
        L"</text><text>"+Escape(message)+L"</text></binding></visual>"+audio+
        (type==EventType::General?L"":L"<actions><action activationType=\"system\" arguments=\"dismiss\" content=\"\"/></actions>")+L"</toast>";
}
}

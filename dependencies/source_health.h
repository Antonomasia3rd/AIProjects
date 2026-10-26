#pragma once
// Reusable, portable freshness/failure policy. Owners supply actual completed
// attempt identities and monotonic time; this file starts no timer or I/O.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <limits>
#include <map>
#include <optional>
#include <string>

namespace aip { namespace source_health {
enum class Outcome {None,Success,Transient,Rejected,InvalidData,Setup};
struct Configuration {
    bool retainLastGood=true,popupFailureEnabled=true,popupRecoveryEnabled=true,authenticationImmediate=true;
    int tileFailureAttempts=5,tileFailureSeconds=0,popupFailureAttempts=5,popupFailureSeconds=0,ageResolutionSeconds=60;
    std::wstring ageDisplay=L"Stale",tileFailureMode=L"Append",tileThreshold=L"Either",popupThreshold=L"Either";
    std::wstring ageTemplate=L"Refreshed %age% ago",tileFailureText=L"Refresh delayed: %error%",noDataText=L"Waiting for the first successful refresh.";
    std::wstring popupFailureTitle,popupFailureBody,popupRecoveryTitle,popupRecoveryBody;
};
enum class FieldType {Boolean,Integer,Choice,Template};
struct Setting {const wchar_t* key;const wchar_t* fallback;FieldType type;int minimum=0,maximum=0;const wchar_t* choices=L"";};
inline constexpr Setting Settings[]={
    {L"RetainLastGood",L"1",FieldType::Boolean},{L"AgeDisplay",L"Stale",FieldType::Choice,0,0,L"Stale|Always|Off"},
    {L"AgeResolutionSeconds",L"60",FieldType::Integer,1,3600},{L"AgeTemplate",L"Refreshed %age% ago",FieldType::Template},
    {L"TileFailureMode",L"Append",FieldType::Choice,0,0,L"Append|Replace|Off"},
    {L"TileFailureAttempts",L"5",FieldType::Integer,0,1000},{L"TileFailureSeconds",L"0",FieldType::Integer,0,604800},
    {L"TileThreshold",L"Either",FieldType::Choice,0,0,L"Either|Both"},
    {L"PopupFailureEnabled",L"1",FieldType::Boolean},{L"PopupRecoveryEnabled",L"1",FieldType::Boolean},
    {L"PopupFailureAttempts",L"5",FieldType::Integer,0,1000},{L"PopupFailureSeconds",L"0",FieldType::Integer,0,604800},
    {L"PopupThreshold",L"Either",FieldType::Choice,0,0,L"Either|Both"},{L"AuthenticationImmediate",L"1",FieldType::Boolean},
    {L"TileFailureText",L"Refresh delayed: %error%",FieldType::Template},{L"NoDataText",L"Waiting for the first successful refresh.",FieldType::Template},
    {L"PopupFailureTitle",L"",FieldType::Template},{L"PopupFailureBody",L"",FieldType::Template},
    {L"PopupRecoveryTitle",L"",FieldType::Template},{L"PopupRecoveryBody",L"",FieldType::Template}
};
inline std::wstring Lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
inline std::wstring Trim(const std::wstring& value){const auto first=value.find_first_not_of(L" \r\n\t");return first==std::wstring::npos?L"":value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1);}
inline bool Number(const std::wstring& value,int low,int high,int& result){
    if(value.empty()||value.size()>9)return false;int parsed=0;
    for(const auto c:value){if(c<L'0'||c>L'9'||parsed>high/10||(parsed==high/10&&c-L'0'>high%10))return false;parsed=parsed*10+static_cast<int>(c-L'0');}
    if(parsed<low||parsed>high)return false;result=parsed;return true;
}
inline bool TemplateValid(const std::wstring& text){
    if(text.size()>1024)return false;
    for(std::size_t i=0;i<text.size();++i){if(text[i]==0x7f||(text[i]<0x20&&text[i]!=L'\n'&&text[i]!=L'\r'&&text[i]!=L'\t'))return false;
        if(text[i]!=L'%')continue;if(i+1<text.size()&&text[i+1]==L'%'){++i;continue;}
        const auto end=text.find(L'%',i+1);if(end==std::wstring::npos)return false;const auto token=Lower(text.substr(i+1,end-i-1));
        if(token!=L"age"&&token!=L"ageseconds"&&token!=L"ageminutes"&&token!=L"failures"&&token!=L"elapsedseconds"&&token!=L"error"&&token!=L"tile")return false;i=end;
    }return true;
}
inline bool NormalizeSetting(std::wstring key,std::wstring& value,std::wstring& error){
    key=Lower(key);if(key.compare(0,13,L"sourcehealth.")==0)key.erase(0,13);
    const Setting* field=nullptr;for(const auto& known:Settings)if(Lower(known.key)==key){field=&known;break;}
    const auto fail=[&]{error=L"SourceHealth."+key+L": use the supported boolean, range, choice or bounded template tokens.";return false;};
    if(!field)return fail();auto normalized=field->type==FieldType::Template?value:Trim(value);
    if(field->type==FieldType::Template){if(!TemplateValid(normalized))return fail();}
    else if(field->type==FieldType::Boolean){const auto low=Lower(normalized);if(low==L"1"||low==L"true"||low==L"yes"||low==L"on")normalized=L"1";
        else if(low==L"0"||low==L"false"||low==L"no"||low==L"off")normalized=L"0";else return fail();}
    else if(field->type==FieldType::Integer){int number=0;if(!Number(normalized,field->minimum,field->maximum,number))return fail();normalized=std::to_wstring(number);}
    else{const auto low=Lower(normalized);const std::wstring choices=field->choices;bool found=false;std::size_t begin=0;
        for(;;){const auto end=choices.find(L'|',begin);const auto choice=choices.substr(begin,end==std::wstring::npos?end:end-begin);
            if(Lower(choice)==low){normalized=choice;found=true;break;}if(end==std::wstring::npos)break;begin=end+1;}if(!found)return fail();}
    value=std::move(normalized);error.clear();return true;
}
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){
    Configuration cfg;for(const auto& field:Settings){auto value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;const std::wstring key=field.key;
        if(field.type==FieldType::Boolean){const bool enabled=value==L"1";if(key==L"RetainLastGood")cfg.retainLastGood=enabled;else if(key==L"PopupFailureEnabled")cfg.popupFailureEnabled=enabled;else if(key==L"PopupRecoveryEnabled")cfg.popupRecoveryEnabled=enabled;else cfg.authenticationImmediate=enabled;}
        else if(field.type==FieldType::Integer){int number=0;Number(value,field.minimum,field.maximum,number);if(key==L"TileFailureAttempts")cfg.tileFailureAttempts=number;else if(key==L"TileFailureSeconds")cfg.tileFailureSeconds=number;else if(key==L"PopupFailureAttempts")cfg.popupFailureAttempts=number;else if(key==L"PopupFailureSeconds")cfg.popupFailureSeconds=number;else cfg.ageResolutionSeconds=number;}
        else if(key==L"AgeDisplay")cfg.ageDisplay=value;else if(key==L"TileFailureMode")cfg.tileFailureMode=value;else if(key==L"TileThreshold")cfg.tileThreshold=value;else if(key==L"PopupThreshold")cfg.popupThreshold=value;
        else if(key==L"AgeTemplate")cfg.ageTemplate=value;else if(key==L"TileFailureText")cfg.tileFailureText=value;else if(key==L"NoDataText")cfg.noDataText=value;
        else if(key==L"PopupFailureTitle")cfg.popupFailureTitle=value;else if(key==L"PopupFailureBody")cfg.popupFailureBody=value;else if(key==L"PopupRecoveryTitle")cfg.popupRecoveryTitle=value;else if(key==L"PopupRecoveryBody")cfg.popupRecoveryBody=value;
    }output=std::move(cfg);return true;
}
struct State {
    bool hasSuccess=false,failing=false;
    std::uint64_t sequence=0,lastSuccess=0,failureSince=0,attempts=0,consecutiveFailures=0;
    Outcome outcome=Outcome::None;
};
class Tracker {
    State state_;
public:
    void Observe(std::uint64_t sequence,Outcome outcome,std::uint64_t completedAt){
        if(!sequence||sequence<=state_.sequence||outcome==Outcome::None)return;
        state_.sequence=sequence;if(state_.attempts!=(std::numeric_limits<std::uint64_t>::max)())++state_.attempts;state_.outcome=outcome;
        if(outcome==Outcome::Success){state_.hasSuccess=true;state_.lastSuccess=completedAt;state_.failing=false;state_.failureSince=0;state_.consecutiveFailures=0;}
        else{if(!state_.failing){state_.failureSince=completedAt;state_.failing=true;}if(state_.consecutiveFailures!=(std::numeric_limits<std::uint64_t>::max)())++state_.consecutiveFailures;}
    }
    const State& Read()const{return state_;}
};
inline std::uint64_t Elapsed(std::uint64_t now,std::uint64_t then){return now>=then?(now-then)/1000:0;}
inline bool ThresholdReached(int attempts,int seconds,const std::wstring& mode,const State& state,std::uint64_t now){
    const bool countEnabled=attempts>0,timeEnabled=seconds>0;
    if(!state.failing||(!countEnabled&&!timeEnabled))return false;
    const bool count=countEnabled&&state.consecutiveFailures>=static_cast<std::uint64_t>(attempts),elapsed=timeEnabled&&Elapsed(now,state.failureSince)>=static_cast<std::uint64_t>(seconds);
    return mode==L"Both"?(!countEnabled||count)&&(!timeEnabled||elapsed):count||elapsed;
}
inline bool TileFailure(const Configuration& cfg,const State& state,std::uint64_t now){return cfg.tileFailureMode!=L"Off"&&state.failing&&
    ((cfg.authenticationImmediate&&(state.outcome==Outcome::Rejected||state.outcome==Outcome::Setup))||ThresholdReached(cfg.tileFailureAttempts,cfg.tileFailureSeconds,cfg.tileThreshold,state,now));}
inline bool PopupFailureReached(const Configuration& cfg,const State& state,std::uint64_t now){return state.failing&&
    ((cfg.authenticationImmediate&&(state.outcome==Outcome::Rejected||state.outcome==Outcome::Setup))||ThresholdReached(cfg.popupFailureAttempts,cfg.popupFailureSeconds,cfg.popupThreshold,state,now));}
inline bool PopupFailure(const Configuration& cfg,const State& state,std::uint64_t now){return cfg.popupFailureEnabled&&PopupFailureReached(cfg,state,now);}
inline std::wstring CompactAge(std::uint64_t seconds){if(seconds<3600)return std::to_wstring(seconds/60)+L"m";if(seconds<86400)return std::to_wstring(seconds/3600)+L"h";return std::to_wstring(seconds/86400)+L"d";}
inline std::wstring Format(const std::wstring& pattern,const Configuration& cfg,const State& state,std::uint64_t now,const std::wstring& error=L"",const std::wstring& tile=L""){
    auto age=state.hasSuccess?Elapsed(now,state.lastSuccess):0;const auto resolution=static_cast<std::uint64_t>((std::max)(1,cfg.ageResolutionSeconds));age=age/resolution*resolution;
    const std::map<std::wstring,std::wstring> values={{L"age",CompactAge(age)},{L"ageseconds",std::to_wstring(age)},{L"ageminutes",std::to_wstring(age/60)},
        {L"failures",std::to_wstring(state.consecutiveFailures)},{L"elapsedseconds",std::to_wstring(state.failing?Elapsed(now,state.failureSince):0)},{L"error",error},{L"tile",tile}};
    std::wstring result;for(std::size_t i=0;i<pattern.size()&&result.size()<2048;++i){
        if(pattern[i]==L'\\'&&i+1<pattern.size()&&pattern[i+1]==L'n'){result+=L'\n';++i;continue;}
        if(pattern[i]!=L'%'){if(pattern[i]!=L'\r')result+=pattern[i];continue;}
        if(i+1<pattern.size()&&pattern[i+1]==L'%'){result+=L'%';++i;continue;}const auto end=pattern.find(L'%',i+1);if(end==std::wstring::npos)break;
        const auto found=values.find(Lower(pattern.substr(i+1,end-i-1)));if(found!=values.end())result+=found->second;i=end;
    }if(result.size()>2048){result.resize(2048);if(result.back()>=0xd800&&result.back()<=0xdbff)result.pop_back();}return result;
}
inline std::wstring AgeText(const Configuration& cfg,const State& state,std::uint64_t now,const std::wstring& tile=L""){return state.hasSuccess&&cfg.ageDisplay!=L"Off"&&(cfg.ageDisplay==L"Always"||state.failing)?Format(cfg.ageTemplate,cfg,state,now,L"",tile):L"";}
}} // namespace aip::source_health

#pragma once
// Portable battery readings, active-policy selection and tile presentation.
// No operating-system, account, filesystem, power-action or timer access.
#include <algorithm>
#include <cstdint>
#include <cwctype>
#include <iomanip>
#include <iterator>
#include <limits>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace aip { namespace battery {
enum class Presence { Unknown, Absent, Present };
enum class PowerSource { Unknown, AC, DC };
enum class ChargeState { Unknown, Charging, Discharging, NotCharging };
enum class CriticalAction { Unknown, None, Sleep, Hibernate, Shutdown };
enum class Visibility { Reserve, Threshold, Margin, Always, Off };
enum class Metric { CurrentCapacityWh, FullCapacityWh, DesignCapacityWh, WearPercent, Percent, RuntimeMinutes, CriticalDelta, None };
struct Device {
    std::wstring id;bool present=true,relativeCapacity=false;
    std::optional<std::uint64_t> designCapacityMWh,fullCapacityMWh,currentCapacityMWh;
    std::optional<std::int64_t> rateMilliwatts;
    std::optional<std::uint64_t> voltageMillivolts;
};
struct PolicySide {std::optional<int> criticalPercent,reservePercent;CriticalAction action=CriticalAction::Unknown;};
struct Policy {std::wstring schemeId,schemeName;PolicySide ac,dc;};
struct RawSnapshot {
    Presence presence=Presence::Unknown;PowerSource powerSource=PowerSource::Unknown;
    ChargeState chargeState=ChargeState::Unknown;
    std::optional<int> percent;std::optional<std::int64_t> remainingSeconds;
    std::optional<bool> saverEnabled;std::vector<Device> devices;Policy policy;std::wstring diagnostic;
    bool deviceDetailsComplete=true;
};
struct AggregateSnapshot {
    Presence presence=Presence::Unknown;std::optional<int> percent;
    std::optional<std::int64_t> remainingSeconds;
    std::optional<std::uint64_t> designCapacityMWh,fullCapacityMWh,currentCapacityMWh;
    std::optional<double> wearPercent;std::size_t batteryCount=0;bool deviceDetailsComplete=true;
};
inline std::wstring Lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(std::towlower(c));return value;}
inline std::wstring Trim(const std::wstring& value){const auto first=value.find_first_not_of(L" \r\n\t");return first==std::wstring::npos?L"":value.substr(first,value.find_last_not_of(L" \r\n\t")-first+1);}
inline bool ValidPercent(const std::optional<int>& value){return value&&*value>=0&&*value<=100;}
inline bool KnownCapacity(const std::optional<std::uint64_t>& value){return value&&*value!=UINT64_C(0xffffffff)&&*value!=(std::numeric_limits<std::uint64_t>::max)();}
inline bool SafeText(const std::wstring& value,std::size_t limit,bool multiline=false){return value.size()<=limit&&std::none_of(value.begin(),value.end(),[&](wchar_t c){return (c<0x20&&!(multiline&&(c==L'\r'||c==L'\n'||c==L'\t')))||c==0x7f;});}
inline bool Number(const std::wstring& value,int minimum,int maximum,int& number){
    if(value.empty()||value.size()>8)return false;int parsed=0;
    for(const auto c:value){if(c<L'0'||c>L'9'||parsed>(maximum-static_cast<int>(c-L'0'))/10)return false;parsed=parsed*10+static_cast<int>(c-L'0');}
    if(parsed<minimum||parsed>maximum)return false;number=parsed;return true;
}
inline AggregateSnapshot Aggregate(const RawSnapshot& raw){
    AggregateSnapshot result;result.presence=raw.presence;result.deviceDetailsComplete=raw.deviceDetailsComplete;
    if(raw.presence==Presence::Absent)return result;
    if(ValidPercent(raw.percent))result.percent=raw.percent;
    if(raw.remainingSeconds&&*raw.remainingSeconds>=0&&*raw.remainingSeconds!=INT64_C(0xffffffff)&&*raw.remainingSeconds!=(std::numeric_limits<std::int64_t>::max)())result.remainingSeconds=raw.remainingSeconds;
    bool designKnown=true,fullKnown=true,currentKnown=true;std::uint64_t design=0,full=0,current=0;
    const auto add=[](const std::optional<std::uint64_t>& value,std::uint64_t& sum,bool& known){
        if(!KnownCapacity(value)||sum>(std::numeric_limits<std::uint64_t>::max)()-*value){known=false;return;}sum+=*value;
    };
    for(const auto& device:raw.devices){
        if(!device.present)continue;++result.batteryCount;
        if(device.relativeCapacity){designKnown=fullKnown=currentKnown=false;continue;}
        add(device.designCapacityMWh,design,designKnown);add(device.fullCapacityMWh,full,fullKnown);add(device.currentCapacityMWh,current,currentKnown);
    }
    if(result.batteryCount){
        result.presence=Presence::Present;
        if(raw.deviceDetailsComplete&&designKnown)result.designCapacityMWh=design;
        if(raw.deviceDetailsComplete&&fullKnown)result.fullCapacityMWh=full;
        if(raw.deviceDetailsComplete&&currentKnown)result.currentCapacityMWh=current;
        if(result.designCapacityMWh&&*result.designCapacityMWh>0&&result.fullCapacityMWh){
            const auto ratio=static_cast<long double>(*result.fullCapacityMWh)/static_cast<long double>(*result.designCapacityMWh);
            result.wearPercent=static_cast<double>((std::max)(0.0L,(std::min)(100.0L,(1.0L-ratio)*100.0L)));
        }
    }
    // Never derive charge or runtime from capacities/rate: unknown remains unknown.
    return result;
}
struct Configuration {
    int refreshSeconds=60,numberDecimals=1,rightColumnPercent=30,collectionTimeoutMs=1500,deviceTimeoutMs=250;
    bool deviceDetails=true;
    bool showStatus=true,showRuntime=true,showUnavailableRuntime=false,showWear=true,showDetails=true;
    Metric cornerMetric=Metric::CurrentCapacityWh,criticalCornerMetric=Metric::CriticalDelta;
    Visibility criticalVisibility=Visibility::Reserve;
    int criticalThresholdPercent=20,criticalMarginPercent=5;bool criticalOnAC=false,criticalOnDC=true;
    std::wstring titleTemplate=L"%charge%",statusTemplate=L"%status%",runtimeTemplate=L"%runtime% remaining",wearTemplate=L"Wear level: %wear%";
    std::wstring cornerLabel,criticalCornerLabel,criticalActionTemplate=L"Critical battery action: %action%";
    std::wstring unknownText=L"Unavailable",noBatteryText=L"No battery";
    std::wstring acLabel=L"Plugged in",dcLabel=L"On battery",chargingLabel=L"charging",notChargingLabel=L"not charging",dischargingLabel=L"discharging";
    std::wstring actionNoneLabel=L"Do nothing",actionSleepLabel=L"Sleep",actionHibernateLabel=L"Hibernate",actionShutdownLabel=L"Shut down";
};
enum class FieldType { Boolean,Integer,Choice,Template,Text };
struct Setting {const wchar_t* key;const wchar_t* fallback;FieldType type;int minimum=0,maximum=0;const wchar_t* choices=L"";};
inline constexpr Setting Settings[]={
    {L"RefreshSeconds",L"60",FieldType::Integer,15,86400},{L"NumberDecimals",L"1",FieldType::Integer,0,3},{L"RightColumnPercent",L"30",FieldType::Integer,20,45},
    {L"DeviceDetails",L"1",FieldType::Boolean},{L"CollectionTimeoutMs",L"1500",FieldType::Integer,100,10000},{L"DeviceTimeoutMs",L"250",FieldType::Integer,25,2000},
    {L"ShowStatus",L"1",FieldType::Boolean},{L"ShowRuntime",L"1",FieldType::Boolean},{L"ShowUnavailableRuntime",L"0",FieldType::Boolean},{L"ShowWear",L"1",FieldType::Boolean},{L"ShowDetails",L"1",FieldType::Boolean},
    {L"CornerMetric",L"CurrentCapacityWh",FieldType::Choice,0,0,L"CurrentCapacityWh|FullCapacityWh|DesignCapacityWh|WearPercent|Percent|RuntimeMinutes|CriticalDelta|None"},
    {L"CornerLabel",L"",FieldType::Text},
    {L"CriticalVisibility",L"Reserve",FieldType::Choice,0,0,L"Reserve|Threshold|Margin|Always|Off"},
    {L"CriticalThresholdPercent",L"20",FieldType::Integer,0,100},{L"CriticalMarginPercent",L"5",FieldType::Integer,0,100},
    {L"CriticalOnAC",L"0",FieldType::Boolean},{L"CriticalOnDC",L"1",FieldType::Boolean},
    {L"CriticalCornerMetric",L"CriticalDelta",FieldType::Choice,0,0,L"CurrentCapacityWh|FullCapacityWh|DesignCapacityWh|WearPercent|Percent|RuntimeMinutes|CriticalDelta|None"},
    {L"CriticalCornerLabel",L"",FieldType::Text},
    {L"TitleTemplate",L"%charge%",FieldType::Template},{L"StatusTemplate",L"%status%",FieldType::Template},
    {L"RuntimeTemplate",L"%runtime% remaining",FieldType::Template},{L"WearTemplate",L"Wear level: %wear%",FieldType::Template},
    {L"CriticalActionTemplate",L"Critical battery action: %action%",FieldType::Template},
    {L"UnknownText",L"Unavailable",FieldType::Text},{L"NoBatteryText",L"No battery",FieldType::Text},
    {L"ACLabel",L"Plugged in",FieldType::Text},{L"DCLabel",L"On battery",FieldType::Text},
    {L"ChargingLabel",L"charging",FieldType::Text},{L"NotChargingLabel",L"not charging",FieldType::Text},{L"DischargingLabel",L"discharging",FieldType::Text},
    {L"ActionNoneLabel",L"Do nothing",FieldType::Text},{L"ActionSleepLabel",L"Sleep",FieldType::Text},
    {L"ActionHibernateLabel",L"Hibernate",FieldType::Text},{L"ActionShutdownLabel",L"Shut down",FieldType::Text}
};
inline constexpr const wchar_t* TemplateTokens[]={L"charge",L"percent",L"status",L"runtime",L"wear",L"capacitywh",L"fullcapacitywh",L"designcapacitywh",L"critical",L"reserve",L"criticaldelta",L"action",L"scheme",L"source",L"batteries",L"saver"};
inline bool ValidateTemplate(const std::wstring& text){
    if(!SafeText(text,1024,true))return false;
    for(std::size_t i=0;i<text.size();++i){
        if(text[i]!=L'%')continue;if(i+1<text.size()&&text[i+1]==L'%'){++i;continue;}
        const auto end=text.find(L'%',i+1);if(end==std::wstring::npos)return false;const auto token=Lower(text.substr(i+1,end-i-1));
        if(std::none_of(std::begin(TemplateTokens),std::end(TemplateTokens),[&](const auto* known){return token==known;}))return false;i=end;
    }
    return true;
}
inline bool NormalizeSetting(std::wstring key,std::wstring& value,std::wstring& error){
    key=Lower(key);if(key.compare(0,8,L"battery.")==0)key.erase(0,8);
    const Setting* field=nullptr;for(const auto& known:Settings)if(Lower(known.key)==key){field=&known;break;}
    if(!field){error=L"Unknown Battery setting.";return false;}
    auto normalized=field->type==FieldType::Template?value:Trim(value);
    const auto fail=[&](const wchar_t* reason){error=L"Battery."+std::wstring(field->key)+L": "+reason;return false;};
    if(field->type==FieldType::Boolean){const auto low=Lower(normalized);if(low==L"1"||low==L"true"||low==L"on"||low==L"yes")normalized=L"1";else if(low==L"0"||low==L"false"||low==L"off"||low==L"no")normalized=L"0";else return fail(L"expected a boolean.");}
    else if(field->type==FieldType::Integer){int number=0;if(!Number(normalized,field->minimum,field->maximum,number))return fail(L"number is outside its supported range.");normalized=std::to_wstring(number);}
    else if(field->type==FieldType::Choice){const auto low=Lower(normalized);const std::wstring choices=field->choices;bool found=false;std::size_t start=0;
        while(start<=choices.size()){const auto end=choices.find(L'|',start);const auto choice=choices.substr(start,end==std::wstring::npos?end:end-start);if(Lower(choice)==low){normalized=choice;found=true;break;}if(end==std::wstring::npos)break;start=end+1;}
        if(!found)return fail(L"choose a value from its supported list.");}
    else if(field->type==FieldType::Template){if(!ValidateTemplate(normalized))return fail(L"use a bounded text template with known %tokens%; %% writes a literal percent sign.");}
    else if(!SafeText(normalized,128))return fail(L"expected a bounded single-line label.");
    value=std::move(normalized);error.clear();return true;
}
inline Metric ReadMetric(const std::wstring& value){
    if(value==L"CurrentCapacityWh")return Metric::CurrentCapacityWh;if(value==L"FullCapacityWh")return Metric::FullCapacityWh;
    if(value==L"DesignCapacityWh")return Metric::DesignCapacityWh;if(value==L"WearPercent")return Metric::WearPercent;
    if(value==L"Percent")return Metric::Percent;if(value==L"RuntimeMinutes")return Metric::RuntimeMinutes;if(value==L"CriticalDelta")return Metric::CriticalDelta;return Metric::None;
}
inline Visibility ReadVisibility(const std::wstring& value){if(value==L"Reserve")return Visibility::Reserve;if(value==L"Threshold")return Visibility::Threshold;if(value==L"Margin")return Visibility::Margin;if(value==L"Always")return Visibility::Always;return Visibility::Off;}
template<class Reader> bool ReadConfiguration(Reader read,Configuration& output,std::wstring& error){
    Configuration cfg;
    for(const auto& field:Settings){std::wstring value=read(field.key,field.fallback);if(!NormalizeSetting(field.key,value,error))return false;const std::wstring key=field.key;
        if(key==L"CornerMetric")cfg.cornerMetric=ReadMetric(value);else if(key==L"CriticalCornerMetric")cfg.criticalCornerMetric=ReadMetric(value);else if(key==L"CriticalVisibility")cfg.criticalVisibility=ReadVisibility(value);
        else if(field.type==FieldType::Integer){int number=0;Number(value,field.minimum,field.maximum,number);if(key==L"RefreshSeconds")cfg.refreshSeconds=number;else if(key==L"NumberDecimals")cfg.numberDecimals=number;else if(key==L"RightColumnPercent")cfg.rightColumnPercent=number;else if(key==L"CollectionTimeoutMs")cfg.collectionTimeoutMs=number;else if(key==L"DeviceTimeoutMs")cfg.deviceTimeoutMs=number;else if(key==L"CriticalThresholdPercent")cfg.criticalThresholdPercent=number;else cfg.criticalMarginPercent=number;}
        else if(field.type==FieldType::Boolean){const bool enabled=value==L"1";if(key==L"ShowStatus")cfg.showStatus=enabled;else if(key==L"ShowRuntime")cfg.showRuntime=enabled;else if(key==L"ShowUnavailableRuntime")cfg.showUnavailableRuntime=enabled;else if(key==L"ShowWear")cfg.showWear=enabled;else if(key==L"ShowDetails")cfg.showDetails=enabled;else if(key==L"DeviceDetails")cfg.deviceDetails=enabled;else if(key==L"CriticalOnAC")cfg.criticalOnAC=enabled;else cfg.criticalOnDC=enabled;}
        else if(key==L"TitleTemplate")cfg.titleTemplate=value;else if(key==L"StatusTemplate")cfg.statusTemplate=value;else if(key==L"RuntimeTemplate")cfg.runtimeTemplate=value;else if(key==L"WearTemplate")cfg.wearTemplate=value;
        else if(key==L"CornerLabel")cfg.cornerLabel=value;else if(key==L"CriticalCornerLabel")cfg.criticalCornerLabel=value;else if(key==L"CriticalActionTemplate")cfg.criticalActionTemplate=value;
        else if(key==L"UnknownText")cfg.unknownText=value;else if(key==L"NoBatteryText")cfg.noBatteryText=value;
        else if(key==L"ACLabel")cfg.acLabel=value;else if(key==L"DCLabel")cfg.dcLabel=value;else if(key==L"ChargingLabel")cfg.chargingLabel=value;
        else if(key==L"NotChargingLabel")cfg.notChargingLabel=value;else if(key==L"DischargingLabel")cfg.dischargingLabel=value;
        else if(key==L"ActionNoneLabel")cfg.actionNoneLabel=value;else if(key==L"ActionSleepLabel")cfg.actionSleepLabel=value;else if(key==L"ActionHibernateLabel")cfg.actionHibernateLabel=value;else if(key==L"ActionShutdownLabel")cfg.actionShutdownLabel=value;
    }
    output=std::move(cfg);return true;
}
inline std::wstring Decimal(long double number,int decimals){
    std::wostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision((std::max)(0,(std::min)(3,decimals)))<<number;
    auto text=out.str();if(text.find(L'.')!=std::wstring::npos){while(!text.empty()&&text.back()==L'0')text.pop_back();if(!text.empty()&&text.back()==L'.')text.pop_back();}return text;
}
inline std::wstring Runtime(std::int64_t seconds){const auto minutes=seconds/60+(seconds%60?1:0);if(minutes<60)return std::to_wstring(minutes)+L" min";return std::to_wstring(minutes/60)+L" h "+std::to_wstring(minutes%60)+L" min";}
inline const PolicySide* ActivePolicy(const RawSnapshot& raw){return raw.powerSource==PowerSource::AC?&raw.policy.ac:raw.powerSource==PowerSource::DC?&raw.policy.dc:nullptr;}
inline std::optional<int> CriticalDelta(const RawSnapshot& raw){const auto* policy=ActivePolicy(raw);return raw.presence!=Presence::Absent&&policy&&ValidPercent(raw.percent)&&ValidPercent(policy->criticalPercent)?std::optional<int>((std::max)(0,*raw.percent-*policy->criticalPercent)):std::nullopt;}
inline bool CriticalVisible(const Configuration& cfg,const RawSnapshot& raw){
    if(raw.presence==Presence::Absent||cfg.criticalVisibility==Visibility::Off)return false;
    const auto* policy=ActivePolicy(raw);if(!policy||(raw.powerSource==PowerSource::AC?!cfg.criticalOnAC:!cfg.criticalOnDC))return false;
    if(cfg.criticalVisibility==Visibility::Always)return true;if(!ValidPercent(raw.percent))return false;
    if(cfg.criticalVisibility==Visibility::Reserve)return ValidPercent(policy->reservePercent)&&*raw.percent<=*policy->reservePercent;
    if(cfg.criticalVisibility==Visibility::Threshold)return *raw.percent<=cfg.criticalThresholdPercent;
    return ValidPercent(policy->criticalPercent)&&*raw.percent<=(std::min)(100,*policy->criticalPercent+cfg.criticalMarginPercent);
}
inline std::wstring ActionText(CriticalAction action,const Configuration& cfg){
    switch(action){case CriticalAction::None:return cfg.actionNoneLabel;case CriticalAction::Sleep:return cfg.actionSleepLabel;
    case CriticalAction::Hibernate:return cfg.actionHibernateLabel;case CriticalAction::Shutdown:return cfg.actionShutdownLabel;default:return cfg.unknownText;}
}
inline std::wstring PowerStatus(const Configuration& cfg,const RawSnapshot& raw){
    const auto source=raw.powerSource==PowerSource::AC?cfg.acLabel:raw.powerSource==PowerSource::DC?cfg.dcLabel:std::wstring{};
    const auto charge=raw.chargeState==ChargeState::Charging?cfg.chargingLabel:raw.chargeState==ChargeState::NotCharging?cfg.notChargingLabel:raw.chargeState==ChargeState::Discharging?cfg.dischargingLabel:std::wstring{};
    if(raw.powerSource==PowerSource::DC&&raw.chargeState==ChargeState::Discharging)return source;
    if(source.empty())return charge.empty()?cfg.unknownText:charge;if(charge.empty())return source;return source+L", "+charge;
}
inline std::wstring FormatTemplate(const std::wstring& text,const std::map<std::wstring,std::wstring>& tokens){
    std::wstring out;out.reserve((std::min)(text.size()*2,std::size_t(2048)));
    for(std::size_t i=0;i<text.size()&&out.size()<2048;++i){
        if(text[i]==L'\\'&&i+1<text.size()&&text[i+1]==L'n'){out+=L'\n';++i;continue;}
        if(text[i]!=L'%'){if(text[i]!=L'\r')out+=text[i];continue;}
        if(i+1<text.size()&&text[i+1]==L'%'){out+=L'%';++i;continue;}
        const auto end=text.find(L'%',i+1);if(end==std::wstring::npos)break;const auto found=tokens.find(Lower(text.substr(i+1,end-i-1)));
        if(found!=tokens.end())out+=found->second;i=end;
    }
    if(out.size()>2048){out.resize(2048);if(!out.empty()&&out.back()>=0xd800&&out.back()<=0xdbff)out.pop_back();}return out;
}
inline std::wstring MetricLabel(Metric metric){
    switch(metric){case Metric::CurrentCapacityWh:case Metric::FullCapacityWh:case Metric::DesignCapacityWh:return L"(Wh)";
    case Metric::WearPercent:return L"wear (%)";case Metric::Percent:return L"charge (%)";
    case Metric::RuntimeMinutes:return L"remaining (min)";case Metric::CriticalDelta:return L"(%) remaining";default:return {};}
}
struct View {
    bool hasBattery=false,noBattery=false,criticalView=false;int rightColumnPercent=30;
    std::wstring charge,status,runtime,wear,metricLabel,metricValue,details;
    std::optional<int> percent,criticalPercent,reservePercent,criticalDelta;CriticalAction action=CriticalAction::Unknown;
    AggregateSnapshot values;
};
inline View BuildView(const Configuration& cfg,const RawSnapshot& raw){
    View view;view.values=Aggregate(raw);view.hasBattery=view.values.presence==Presence::Present;view.noBattery=view.values.presence==Presence::Absent;
    view.rightColumnPercent=(std::max)(20,(std::min)(45,cfg.rightColumnPercent));view.percent=view.values.percent;
    if(view.noBattery){view.charge=cfg.noBatteryText;return view;}
    const auto* policy=ActivePolicy(raw);if(policy){if(ValidPercent(policy->criticalPercent))view.criticalPercent=policy->criticalPercent;if(ValidPercent(policy->reservePercent))view.reservePercent=policy->reservePercent;view.action=policy->action;}
    view.criticalDelta=CriticalDelta(raw);view.criticalView=CriticalVisible(cfg,raw);
    const auto capacity=[&](const std::optional<std::uint64_t>& value){return value?Decimal(static_cast<long double>(*value)/1000,cfg.numberDecimals):cfg.unknownText;};
    const auto percent=[&](const std::optional<int>& value){return value?std::to_wstring(*value):cfg.unknownText;};
    const auto wear=view.values.wearPercent?Decimal(*view.values.wearPercent,cfg.numberDecimals)+L"%":cfg.unknownText;
    const auto runtime=view.values.remainingSeconds?Runtime(*view.values.remainingSeconds):cfg.unknownText;
    std::map<std::wstring,std::wstring> tokens{
        {L"charge",view.percent?std::to_wstring(*view.percent)+L"%":cfg.unknownText},{L"percent",percent(view.percent)},
        {L"status",PowerStatus(cfg,raw)},{L"runtime",runtime},{L"wear",wear},
        {L"capacitywh",capacity(view.values.currentCapacityMWh)},{L"fullcapacitywh",capacity(view.values.fullCapacityMWh)},{L"designcapacitywh",capacity(view.values.designCapacityMWh)},
        {L"critical",percent(view.criticalPercent)},{L"reserve",percent(view.reservePercent)},{L"criticaldelta",percent(view.criticalDelta)},
        {L"action",ActionText(view.action,cfg)},{L"scheme",raw.policy.schemeName.empty()?cfg.unknownText:raw.policy.schemeName},
        {L"source",raw.powerSource==PowerSource::AC?cfg.acLabel:raw.powerSource==PowerSource::DC?cfg.dcLabel:cfg.unknownText},
        {L"batteries",view.values.deviceDetailsComplete&&view.values.batteryCount?std::to_wstring(view.values.batteryCount):cfg.unknownText},{L"saver",raw.saverEnabled?(*raw.saverEnabled?L"On":L"Off"):cfg.unknownText}
    };
    view.charge=FormatTemplate(cfg.titleTemplate,tokens);if(cfg.showStatus)view.status=FormatTemplate(cfg.statusTemplate,tokens);
    if(cfg.showRuntime){if(view.values.remainingSeconds)view.runtime=FormatTemplate(cfg.runtimeTemplate,tokens);else if(cfg.showUnavailableRuntime)view.runtime=cfg.unknownText;}
    if(cfg.showWear)view.wear=view.values.wearPercent?FormatTemplate(cfg.wearTemplate,tokens):(cfg.unknownText.empty()?L"":FormatTemplate(cfg.wearTemplate,tokens));
    if(view.criticalView){
        if(cfg.showStatus)view.status=view.action==CriticalAction::Unknown?cfg.unknownText:FormatTemplate(cfg.criticalActionTemplate,tokens);
        if(cfg.showDetails)view.details=FormatTemplate(cfg.statusTemplate,tokens);
    }
    const auto metric=view.criticalView?cfg.criticalCornerMetric:cfg.cornerMetric;
    if(metric!=Metric::None){const auto& label=view.criticalView?cfg.criticalCornerLabel:cfg.cornerLabel;view.metricLabel=label.empty()?MetricLabel(metric):label;
        switch(metric){case Metric::CurrentCapacityWh:view.metricValue=tokens[L"capacitywh"];break;case Metric::FullCapacityWh:view.metricValue=tokens[L"fullcapacitywh"];break;
        case Metric::DesignCapacityWh:view.metricValue=tokens[L"designcapacitywh"];break;case Metric::WearPercent:view.metricValue=view.values.wearPercent?Decimal(*view.values.wearPercent,cfg.numberDecimals):cfg.unknownText;break;
        case Metric::Percent:view.metricValue=percent(view.percent);break;case Metric::RuntimeMinutes:view.metricValue=view.values.remainingSeconds?std::to_wstring(*view.values.remainingSeconds/60+(*view.values.remainingSeconds%60?1:0)):cfg.unknownText;break;
        case Metric::CriticalDelta:view.metricValue=percent(view.criticalDelta);break;default:break;}
    }
    return view;
}
} }

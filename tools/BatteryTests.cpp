#include "../dependencies/content_sources/battery_model.h"
#include <iostream>
#include <set>
namespace {
using namespace aip::battery;
int checks=0,failures=0;
void Check(bool condition,const char* message){++checks;if(!condition){++failures;std::cout<<"FAIL: "<<message<<'\n';}}
Device Pack(std::uint64_t design,std::uint64_t full,std::uint64_t current){Device d;d.id=L"INERT_BATTERY";d.designCapacityMWh=design;d.fullCapacityMWh=full;d.currentCapacityMWh=current;return d;}
RawSnapshot Sample(){RawSnapshot r;r.presence=Presence::Present;r.powerSource=PowerSource::AC;r.chargeState=ChargeState::NotCharging;r.percent=80;r.remainingSeconds=3600;r.devices={Pack(50000,45400,30000)};r.policy.dc.criticalPercent=20;r.policy.dc.reservePercent=30;r.policy.dc.action=CriticalAction::Hibernate;r.policy.ac.criticalPercent=5;r.policy.ac.reservePercent=10;r.policy.ac.action=CriticalAction::Shutdown;return r;}
void ConfigurationTests(){
    Configuration c;std::wstring error;std::set<std::wstring> names;
    for(const auto& field:Settings){auto value=std::wstring(field.fallback);Check(names.insert(Lower(field.key)).second&&NormalizeSetting(field.key,value,error),"every Battery default is unique and normalizes successfully");}
    Check(ReadConfiguration([](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);},c,error),"compiled battery defaults read without an OS or INI dependency");
    Check(c.cornerMetric==Metric::CurrentCapacityWh&&c.rightColumnPercent==30&&c.showStatus&&c.showWear,"normal defaults provide charge/status/wear and current capacity corner");
    Check(c.criticalVisibility==Visibility::Reserve&&!c.criticalOnAC&&c.criticalOnDC&&c.criticalCornerMetric==Metric::CriticalDelta,"critical defaults use DC reserve gating and percentage-point corner");
    Check(c.collectionTimeoutMs==1500&&c.deviceTimeoutMs==250&&c.deviceDetails&&c.refreshSeconds==60,"collection budgets are explicit bounded configurable defaults");
    std::wstring value=L" reserve ";Check(NormalizeSetting(L"bAtTeRy.CriticalVisibility",value,error)&&value==L"Reserve","Battery prefix, key and enum casing normalize consistently");
    value=L"yes";Check(NormalizeSetting(L"ShowWear",value,error)&&value==L"1","boolean aliases normalize to persisted numeric value");
    for(const auto& field:Settings){
        if(field.type!=FieldType::Integer)continue;auto low=std::to_wstring(field.minimum),high=std::to_wstring(field.maximum);
        Check(NormalizeSetting(field.key,low,error)&&NormalizeSetting(field.key,high,error),"numeric configuration accepts both supported boundaries");
        auto beyond=std::to_wstring(field.maximum+1);Check(!NormalizeSetting(field.key,beyond,error),"numeric configuration rejects out-of-range values");
        value=L"99999999999999999999999999999";Check(!NormalizeSetting(field.key,value,error),"numeric configuration rejects overflow text");
    }
    for(const auto* invalid:{L"Sometimes",L"-1",L"1.5",L""}){value=invalid;Check(!NormalizeSetting(L"CriticalVisibility",value,error),"critical visibility never silently accepts an unknown policy");}
    value=L"Off";Check(NormalizeSetting(L"CriticalVisibility",value,error)&&value==L"Off","Off remains an explicit supported visibility mode");
    value=L"battery";Check(!NormalizeSetting(L"MissingField",value,error),"unknown battery settings fail explicitly");
    value=L"name\nsecond line";Check(!NormalizeSetting(L"ACLabel",value,error),"single-line labels reject control characters");
    value=std::wstring(129,L'x');Check(!NormalizeSetting(L"UnknownText",value,error),"unknown-value label is bounded");
    value=L"%charge%\n%runtime%\n%action% %%";Check(NormalizeSetting(L"TitleTemplate",value,error),"custom multiline templates accept documented tokens and literal percent escape");
    for(const auto* invalid:{L"%not_a_token%",L"%charge",L"%",L"before\x1" L"after"}){value=invalid;Check(!NormalizeSetting(L"TitleTemplate",value,error),"malformed or unsafe templates fail before presentation");}
    value=std::wstring(1025,L'x');Check(!NormalizeSetting(L"TitleTemplate",value,error),"template text is bounded");
    const auto original=c.refreshSeconds;
    Check(!ReadConfiguration([](const wchar_t* key,const wchar_t* fallback){return std::wstring(key)==L"RefreshSeconds"?L"0":std::wstring(fallback);},c,error)&&c.refreshSeconds==original,"a bad configuration read leaves the caller's previous configuration intact");
    const std::map<std::wstring,std::wstring> custom{{L"ShowStatus",L"0"},{L"ShowRuntime",L"0"},{L"ShowWear",L"0"},{L"ShowDetails",L"0"},{L"DeviceDetails",L"0"},{L"CollectionTimeoutMs",L"3000"},{L"DeviceTimeoutMs",L"700"},{L"RightColumnPercent",L"42"},{L"NumberDecimals",L"3"},{L"CornerMetric",L"WearPercent"},{L"CriticalVisibility",L"Always"},{L"CriticalOnAC",L"1"},{L"CriticalOnDC",L"0"},{L"ActionShutdownLabel",L"Turn off"}};
    Check(ReadConfiguration([&](const wchar_t* key,const wchar_t* fallback){const auto i=custom.find(key);return i==custom.end()?std::wstring(fallback):i->second;},c,error)&&!c.showStatus&&!c.showRuntime&&!c.showWear&&!c.showDetails&&!c.deviceDetails&&c.collectionTimeoutMs==3000&&c.deviceTimeoutMs==700&&c.rightColumnPercent==42&&c.numberDecimals==3&&c.cornerMetric==Metric::WearPercent&&c.criticalVisibility==Visibility::Always&&c.criticalOnAC&&!c.criticalOnDC&&c.actionShutdownLabel==L"Turn off","typed configuration applies independent acquisition, visibility, layout and label settings");
}
void AggregationTests(){
    auto r=Sample();auto a=Aggregate(r);
    Check(a.presence==Presence::Present&&a.percent==80&&a.currentCapacityMWh==30000&&a.fullCapacityMWh==45400&&a.designCapacityMWh==50000,"known absolute battery capacities retain mWh units");
    Check(a.wearPercent&&*a.wearPercent>9.199&&*a.wearPercent<9.201,"wear comes from reported full versus design capacity");
    r.devices={Pack(40000,36000,10000),Pack(60000,54000,20000)};a=Aggregate(r);
    Check(a.batteryCount==2&&a.currentCapacityMWh==30000&&a.fullCapacityMWh==90000&&a.designCapacityMWh==100000&&a.wearPercent&&*a.wearPercent>9.999&&*a.wearPercent<10.001,"multiple batteries aggregate weighted capacities rather than average percentages/wear");
    r.devices.back().currentCapacityMWh.reset();a=Aggregate(r);Check(!a.currentCapacityMWh&&a.fullCapacityMWh==90000&&a.wearPercent,"an unknown battery's current capacity cannot yield a misleading partial total");
    r.devices.back().designCapacityMWh.reset();a=Aggregate(r);Check(!a.designCapacityMWh&&!a.wearPercent,"wear is unknown if any present battery has unknown design capacity");
    r=Sample();r.devices.push_back(Pack(100,90,40));r.devices.back().relativeCapacity=true;a=Aggregate(r);
    Check(!a.designCapacityMWh&&!a.fullCapacityMWh&&!a.currentCapacityMWh&&!a.wearPercent&&a.percent==80,"relative units are never mixed with absolute mWh or treated as a physical energy/wear total");
    r=Sample();r.devices.push_back(Pack(1000,500,200));r.devices.back().present=false;a=Aggregate(r);
    Check(a.batteryCount==1&&a.currentCapacityMWh==30000,"absent/stale per-device records do not enter an active aggregate");
    r=Sample();r.deviceDetailsComplete=false;a=Aggregate(r);
    Check(!a.currentCapacityMWh&&!a.fullCapacityMWh&&!a.designCapacityMWh&&!a.wearPercent&&a.percent==80&&!a.deviceDetailsComplete,"partial driver enumeration never advertises readable devices as whole-system totals");
    r=Sample();r.devices={Pack(0,0,0)};a=Aggregate(r);Check(!a.wearPercent&&a.currentCapacityMWh==0,"zero design capacity does not divide by zero or fabricate wear");
    r.devices={Pack(10000,12000,9000)};a=Aggregate(r);Check(a.wearPercent==0,"calibrated full capacity above design never becomes negative wear");
    r.devices={Pack(10000,0,0)};a=Aggregate(r);Check(a.wearPercent==100,"a reported zero full capacity can represent full wear when design is known");
    for(const auto sentinel:{UINT64_C(0xffffffff),(std::numeric_limits<std::uint64_t>::max)()}){
        r.devices={Pack(sentinel,sentinel,sentinel)};a=Aggregate(r);Check(!a.designCapacityMWh&&!a.fullCapacityMWh&&!a.currentCapacityMWh&&!a.wearPercent,"raw driver unknown capacity sentinels cannot become enormous Wh readings");
    }
    r=Sample();const auto huge=(std::numeric_limits<std::uint64_t>::max)()-1;r.devices={Pack(huge,huge,huge),Pack(10,10,10)};a=Aggregate(r);
    Check(!a.designCapacityMWh&&!a.fullCapacityMWh&&!a.currentCapacityMWh&&!a.wearPercent,"capacity aggregation rejects integer overflow");
    r=Sample();r.presence=Presence::Absent;a=Aggregate(r);Check(a.presence==Presence::Absent&&!a.percent&&!a.currentCapacityMWh&&!a.remainingSeconds&&a.batteryCount==0,"system absence overrides stale percentage, capacities and runtime");
    r=Sample();r.presence=Presence::Unknown;a=Aggregate(r);Check(a.presence==Presence::Present,"a present enumerated device resolves unknown system presence");
    r={};a=Aggregate(r);Check(a.presence==Presence::Unknown&&!a.percent&&!a.currentCapacityMWh&&!a.wearPercent,"missing readings do not claim no battery or a fabricated zero percent");
    for(const int invalid:{-1,101,255}){r=Sample();r.percent=invalid;a=Aggregate(r);Check(!a.percent,"out-of-range system percentages remain unavailable");}
    for(const auto invalid:{INT64_C(-1),INT64_C(0xffffffff),(std::numeric_limits<std::int64_t>::max)()}){r=Sample();r.remainingSeconds=invalid;a=Aggregate(r);Check(!a.remainingSeconds,"runtime unknown sentinels never become a time estimate");}
    r=Sample();r.percent.reset();r.remainingSeconds.reset();a=Aggregate(r);Check(!a.percent&&!a.remainingSeconds&&a.currentCapacityMWh==30000,"known energy does not invent percentage or a discharge-time prediction");
}
void CriticalTests(){
    Configuration c;auto r=Sample();r.powerSource=PowerSource::DC;r.chargeState=ChargeState::Discharging;r.percent=25;r.remainingSeconds=39*60;
    auto v=BuildView(c,r);
    Check(v.criticalView&&v.criticalDelta==5&&v.metricValue==L"5"&&v.metricLabel==L"(%) remaining","25 percent versus critical20 means five percentage points, not remaining runtime/charge");
    Check(v.charge==L"25%"&&v.runtime==L"39 min remaining"&&v.status==L"Critical battery action: Hibernate"&&v.details==L"On battery","critical presentation keeps actual runtime distinct and uses selected DC policy action");
    Check(v.status.find(L"about to")==std::wstring::npos,"default wording describes configured policy rather than promising immediate action");
    r.percent=30;Check(CriticalVisible(c,r),"reserve visibility includes its exact threshold");r.percent=31;Check(!CriticalVisible(c,r),"reserve visibility excludes values above its threshold");
    r.policy.dc.reservePercent=10;r.percent=25;Check(!CriticalVisible(c,r),"inverted reserve/critical policy is honored rather than rewritten");
    c.criticalVisibility=Visibility::Margin;c.criticalMarginPercent=5;Check(CriticalVisible(c,r),"custom critical margin works independently of an inverted reserve level");r.percent=26;Check(!CriticalVisible(c,r),"custom margin excludes the next percentage point");
    c.criticalVisibility=Visibility::Threshold;c.criticalThresholdPercent=40;r.percent=40;r.policy.dc.reservePercent.reset();Check(CriticalVisible(c,r),"custom absolute threshold does not require a reserve policy value");
    r.policy.dc.criticalPercent.reset();Check(CriticalVisible(c,r)&&!CriticalDelta(r),"custom threshold does not fabricate a missing critical threshold or delta");
    c.criticalVisibility=Visibility::Always;r.percent.reset();Check(CriticalVisible(c,r),"Always works without percentage or reserve data on an enabled known power source");
    v=BuildView(c,r);Check(v.criticalView&&v.metricValue==c.unknownText&&v.charge==c.unknownText,"Always preserves unknown percentage and critical delta instead of showing zero");
    c.criticalVisibility=Visibility::Reserve;Check(!CriticalVisible(c,r),"Reserve cannot guess visibility without reserve/percentage readings");
    c.criticalVisibility=Visibility::Always;r.powerSource=PowerSource::AC;Check(!CriticalVisible(c,r),"AC critical view is disabled by default");
    c.criticalOnAC=true;v=BuildView(c,r);Check(v.criticalView&&v.action==CriticalAction::Shutdown&&v.status==L"Critical battery action: Shut down"&&v.criticalPercent==5,"enabled AC view selects AC action/threshold rather than DC Hibernate");
    r.powerSource=PowerSource::Unknown;Check(!CriticalVisible(c,r)&&!ActivePolicy(r),"unknown power source cannot select an arbitrary AC/DC policy");
    r.powerSource=PowerSource::DC;c.criticalOnDC=false;Check(!CriticalVisible(c,r),"DC visibility can independently be disabled");c.criticalOnDC=true;c.criticalVisibility=Visibility::Off;Check(!CriticalVisible(c,r),"Off disables critical presentation regardless of policy");
    c.criticalVisibility=Visibility::Always;r.presence=Presence::Absent;Check(!CriticalVisible(c,r),"a computer without a battery never enters critical battery presentation");
    r=Sample();r.powerSource=PowerSource::DC;r.percent=19;Check(CriticalDelta(r)==0,"delta clamps to zero at or below the actual critical percentage");r.percent=20;Check(CriticalDelta(r)==0,"exact critical threshold yields zero percentage points");
    r.policy.dc.criticalPercent=255;Check(!CriticalDelta(r),"unknown critical-percent sentinel does not become a threshold");
    r=Sample();r.powerSource=PowerSource::DC;r.percent=25;r.policy.dc.action=CriticalAction::Unknown;v=BuildView(c,r);Check(v.status==c.unknownText&&v.status.find(L"Hibernate")==std::wstring::npos,"unknown active action never fabricates Hibernate or an imminent-action sentence");
    r.policy.dc.action=CriticalAction::None;v=BuildView(c,r);Check(v.status==L"Critical battery action: Do nothing","an explicit no-action policy is shown honestly");
    r.policy.dc.action=CriticalAction::Sleep;c.criticalActionTemplate=L"Your computer is about to %action%";c.actionSleepLabel=L"sleep";v=BuildView(c,r);
    Check(v.status==L"Your computer is about to sleep","requested user-authored action wording and labels remain configurable");
    c.showStatus=false;c.showRuntime=false;c.showWear=false;c.showDetails=false;v=BuildView(c,r);Check(v.status.empty()&&v.runtime.empty()&&v.wear.empty()&&v.details.empty(),"critical rows honor all explicit visibility switches");
}
void PresentationTests(){
    Configuration c;auto r=Sample();auto v=BuildView(c,r);
    Check(v.hasBattery&&!v.noBattery&&!v.criticalView&&v.charge==L"80%","ordinary charge view reflects the supplied system percentage");
    Check(v.status==L"Plugged in, not charging"&&v.wear==L"Wear level: 9.2%","default normal view provides power status and reported-capacity wear");
    Check(v.metricLabel==L"current capacity (Wh)"&&v.metricValue==L"30"&&v.metricValue.find(L"Wh")==std::wstring::npos,"currentWh default has no duplicate unit in its numeric corner");
    Check(v.runtime==L"1 h 0 min remaining"&&v.details.empty()&&v.rightColumnPercent==30,"normal runtime is a separate supplied estimate and default geometry is preserved");
    r.remainingSeconds.reset();v=BuildView(c,r);Check(v.runtime==L"Unavailable"&&v.runtime.find(L"remaining")==std::wstring::npos,"unknown runtime is explicit without awkward or invented estimate wording");
    r.devices.clear();v=BuildView(c,r);Check(v.metricValue==L"Unavailable"&&v.wear==L"Wear level: Unavailable","percentage alone cannot fabricate capacity or battery wear");
    r=Sample();r.presence=Presence::Absent;v=BuildView(c,r);Check(v.noBattery&&!v.hasBattery&&v.charge==L"No battery"&&v.metricLabel.empty()&&v.metricValue.empty()&&v.status.empty(),"no-battery presentation contains no stale numeric metrics");
    r=Sample();r.chargeState=ChargeState::Charging;v=BuildView(c,r);Check(v.status==L"Plugged in, charging","charging state is explicit");
    r.powerSource=PowerSource::DC;r.percent=80;r.chargeState=ChargeState::Discharging;v=BuildView(c,r);Check(v.status==L"On battery","discharging battery source is not duplicated");
    r.powerSource=PowerSource::Unknown;r.chargeState=ChargeState::Unknown;v=BuildView(c,r);Check(v.status==L"Unavailable","unknown power status is never mislabeled as plugged in");
    r=Sample();c.cornerMetric=Metric::FullCapacityWh;v=BuildView(c,r);Check(v.metricValue==L"45.4"&&v.metricLabel==L"full capacity (Wh)","full-capacity corner preserves mWh-to-Wh conversion");
    c.cornerMetric=Metric::DesignCapacityWh;Check(BuildView(c,r).metricValue==L"50","design-capacity corner uses reported design energy");
    c.cornerMetric=Metric::WearPercent;Check(BuildView(c,r).metricValue==L"9.2","wear corner uses percentage without duplicating the label unit");
    c.cornerMetric=Metric::Percent;Check(BuildView(c,r).metricValue==L"80","charge corner is distinct from critical percentage-point delta");
    c.cornerMetric=Metric::RuntimeMinutes;r.remainingSeconds=61;Check(BuildView(c,r).metricValue==L"2","runtime corner rounds supplied seconds upward to a whole display minute");
    c.cornerMetric=Metric::CriticalDelta;Check(BuildView(c,r).metricValue==L"75","critical delta uses the relevant AC threshold even in a normal custom corner");
    c.cornerMetric=Metric::None;v=BuildView(c,r);Check(v.metricLabel.empty()&&v.metricValue.empty(),"None disables the right corner entirely");
    c.cornerMetric=Metric::CurrentCapacityWh;c.cornerLabel=L"Energy";c.numberDecimals=3;r.devices={Pack(50000,45400,12345)};v=BuildView(c,r);Check(v.metricLabel==L"Energy"&&v.metricValue==L"12.345","custom labels and bounded precision apply to capacity metrics");
    c.titleTemplate=L"%CHARGE% %% %batteries%\\n%scheme%";r.policy.schemeName=L"INERT POLICY";v=BuildView(c,r);Check(v.charge==L"80% % 1\nINERT POLICY","templates expand case-insensitive documented tokens and escaped line breaks as plain text");
    r.deviceDetailsComplete=false;v=BuildView(c,r);Check(v.metricValue==L"Unavailable"&&v.charge.find(L"% Unavailable\n")!=std::wstring::npos,"partial collection does not turn a readable subset into a claimed battery total");
    c={};c.unknownText=L"";r={};v=BuildView(c,r);Check(v.charge.empty()&&v.status.empty()&&v.runtime.empty()&&v.wear.empty()&&v.metricValue.empty(),"empty unknown fallback can hide missing rows without leftover template labels");
    c={};c.titleTemplate=L"%batteries%";Check(BuildView(c,r).charge==c.unknownText,"unknown presence and no device inventory cannot claim zero batteries in a custom template");
    Check(Runtime(0)==L"0 min"&&Runtime(59)==L"1 min"&&Runtime(3601)==L"1 h 1 min","runtime formatting handles zero and minute/hour boundaries without inferred estimates");
    Check(Decimal(30.0L,1)==L"30"&&Decimal(9.2L,3)==L"9.2","numeric formatting is locale-independent and omits unnecessary trailing zeroes");
    c={};c.titleTemplate=std::wstring(1024,L'x');r=Sample();Check(BuildView(c,r).charge.size()==1024,"maximum accepted plain template remains bounded at presentation");
}
}
int main(){ConfigurationTests();AggregationTests();CriticalTests();PresentationTests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

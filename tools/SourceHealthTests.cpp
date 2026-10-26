#include "../dependencies/source_health.h"
#include <iostream>
#include <map>
namespace {
using namespace aip::source_health;int checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::cout<<"FAIL: "<<label<<'\n';}}
void PolicyTests(){
    Configuration cfg;Tracker tracker;auto state=tracker.Read();
    Check(!TileFailure(cfg,state,0)&&!PopupFailure(cfg,state,0)&&AgeText(cfg,state,0).empty(),"unconfigured/loading has neither failure nor invented success age");
    tracker.Observe(1,Outcome::Success,0);state=tracker.Read();Check(state.hasSuccess&&state.lastSuccess==0&&state.attempts==1,"monotonic zero is a valid successful timestamp");
    tracker.Observe(2,Outcome::Transient,300000);state=tracker.Read();Check(state.failing&&state.consecutiveFailures==1&&state.failureSince==300000,"one completed failure starts one continuous failure period");
    for(int i=0;i<100;++i)tracker.Observe(2,Outcome::Transient,600000);state=tracker.Read();Check(state.consecutiveFailures==1&&state.attempts==2,"repeated reads/duplicate sequence cannot inflate actual completed attempts");
    Check(!TileFailure(cfg,state,300000)&&!PopupFailure(cfg,state,300000),"default five-failure policy quietly retains the first transient failure");
    Check(AgeText(cfg,state,300000)==L"Refreshed 5m ago","stale age describes last successful completion, not last failed attempt");
    Check(AgeText(cfg,state,300001)==AgeText(cfg,state,359999),"default age buckets avoid redraw churn on idle poll ticks");
    for(std::uint64_t seq=3;seq<=6;++seq)tracker.Observe(seq,Outcome::Transient,300000*seq);state=tracker.Read();
    Check(state.consecutiveFailures==5&&TileFailure(cfg,state,1800000)&&PopupFailure(cfg,state,1800000),"fifth completed failure independently opens tile and popup gates");
    cfg.tileFailureMode=L"Off";Check(!TileFailure(cfg,state,1800000)&&PopupFailure(cfg,state,1800000),"tile text can remain quiet while failure popup is enabled");
    cfg.tileFailureMode=L"Append";cfg.popupFailureEnabled=false;Check(TileFailure(cfg,state,1800000)&&!PopupFailure(cfg,state,1800000)&&PopupFailureReached(cfg,state,1800000)&&cfg.popupRecoveryEnabled,"failure popup can be disabled without suppressing tile errors or independent recovery state");
    cfg={};cfg.tileFailureAttempts=0;cfg.tileFailureSeconds=600;
    Check(!TileFailure(cfg,state,899999)&&TileFailure(cfg,state,900000),"elapsed criterion crosses without inventing additional attempts");
    cfg.tileFailureAttempts=7;cfg.tileThreshold=L"Both";Check(!TileFailure(cfg,state,900000),"Both waits for the independent actual-failure count too");
    cfg.tileThreshold=L"Either";Check(TileFailure(cfg,state,900000),"Either accepts elapsed criterion despite a larger attempt threshold");
    cfg.tileFailureAttempts=0;cfg.tileFailureSeconds=0;Check(!TileFailure(cfg,state,900000),"two disabled thresholds never accidentally become immediate failure");
    cfg={};cfg.tileFailureSeconds=600;cfg.tileThreshold=L"Both";Check(!TileFailure(cfg,state,899999)&&TileFailure(cfg,state,900000),"Both gates only the enabled criteria with exact first-failure elapsed time");
    tracker.Observe(7,Outcome::Success,1900000);state=tracker.Read();Check(!state.failing&&!state.consecutiveFailures&&!TileFailure(cfg,state,2000000)&&!PopupFailure(cfg,state,2000000),"recovery resets every failure threshold");
    Check(AgeText(cfg,state,2000000).empty(),"default stale-only age clears on successful recovery");cfg.ageDisplay=L"Always";
    Check(AgeText(cfg,state,2200000)==L"Refreshed 5m ago","Always can show healthy cache age independently");cfg.ageDisplay=L"Off";Check(AgeText(cfg,state,2200000).empty(),"age display can be disabled independently");
    tracker.Observe(8,Outcome::Rejected,2300000);state=tracker.Read();Check(TileFailure(cfg,state,2300000)&&PopupFailure(cfg,state,2300000),"rejected/account-attention outcome can bypass repeated-failure delay explicitly");
    cfg.authenticationImmediate=false;Check(!TileFailure(cfg,state,2300000)&&!PopupFailure(cfg,state,2300000),"rejection delay is configurable rather than forced");
    tracker.Observe(9,Outcome::InvalidData,2400000);state=tracker.Read();Check(state.consecutiveFailures==2&&!PopupFailure(cfg,state,2400000),"invalid response is a separate classified attempt without automatic immediate popup");
    Check(Elapsed(10,20)==0,"backwards clock values cannot unsigned-wrap failure elapsed time");
    tracker.Observe(4,Outcome::Success,2500000);Check(tracker.Read().sequence==9&&tracker.Read().consecutiveFailures==2,"older completion cannot reset current failure history");
    Check(Format(L"%tile%: %failures% / %elapsedSeconds%; %% %error%",cfg,state,2400000,L"safe failure",L"WuWa")==L"WuWa: 2 / 100; % safe failure","status and popup templates use the same bounded state and supplied sanitized diagnostic");
    Check(Format(L"%error%",cfg,state,2400000,std::wstring(3000,L'x')).size()==2048,"large source/template expansion is bounded");
}
void ConfigurationTests(){
    std::wstring error;Configuration cfg;std::map<std::wstring,std::wstring> fields;
    const auto reader=[&](const wchar_t* key,const wchar_t* fallback){const auto found=fields.find(key);return found==fields.end()?std::wstring(fallback):found->second;};
    Check(ReadConfiguration(reader,cfg,error)&&cfg.tileFailureAttempts==5&&cfg.popupFailureAttempts==5&&cfg.retainLastGood&&cfg.ageDisplay==L"Stale","missing policy uses the shared typed defaults");
    for(const auto& field:Settings){auto value=std::wstring(field.fallback);Check(NormalizeSetting(field.key,value,error),"every exported policy default validates");}
    fields={{L"TileFailureAttempts",L"0"},{L"TileFailureSeconds",L"3000"},{L"TileThreshold",L"both"},{L"PopupFailureEnabled",L"off"},{L"AgeDisplay",L"always"},{L"AgeTemplate",L"Fetched %ageMinutes% min ago"}};
    Check(ReadConfiguration(reader,cfg,error)&&cfg.tileFailureAttempts==0&&cfg.tileFailureSeconds==3000&&cfg.tileThreshold==L"Both"&&!cfg.popupFailureEnabled&&cfg.ageDisplay==L"Always","all surfaces normalize independent count/time/choice/boolean/template settings");
    for(const auto& item:{std::make_pair(L"TileFailureAttempts",L"1001"),{L"TileFailureSeconds",L"604801"},{L"AgeResolutionSeconds",L"0"},{L"TileThreshold",L"Any"},{L"AgeDisplay",L"Sometimes"},{L"TileFailureText",L"%unknown%"},{L"PopupFailureEnabled",L"maybe"}}){auto value=std::wstring(item.second);Check(!NormalizeSetting(item.first,value,error),"invalid policy values fail typed validation");}
    auto value=std::wstring(L" Refreshed %age% ago ");Check(NormalizeSetting(L"SourceHealth.AgeTemplate",value,error)&&value.front()==L' ',"template whitespace is intentional and qualified prefix is recognized");
    value=std::wstring(1025,L'x');Check(!NormalizeSetting(L"AgeTemplate",value,error),"oversized policy wording is rejected before mutation");
    value=std::wstring(1,0x7f);Check(!NormalizeSetting(L"TileFailureText",value,error),"DEL control text is rejected like other non-display controls");
    const auto before=cfg;fields[L"PopupFailureAttempts"]=L"-1";Check(!ReadConfiguration(reader,cfg,error)&&cfg.popupFailureEnabled==before.popupFailureEnabled&&cfg.tileFailureSeconds==before.tileFailureSeconds,"failed reader preserves the caller's prior usable configuration");
}
}
int main(){PolicyTests();ConfigurationTests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

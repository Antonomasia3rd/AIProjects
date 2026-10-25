#pragma once
#include <cstdint>
#include <string>
namespace aip { namespace delivery_warning {
enum class Mode{Off,Startup,OnChange};
struct Policy{Mode mode=Mode::Startup;unsigned intervalSeconds=300;};
struct State{bool checked=false,disabled=false;std::uint64_t nextCheck=0;};
inline bool Due(const State& state,const Policy& policy,std::uint64_t now){return policy.mode!=Mode::Off&&now>=state.nextCheck&&(!state.checked||policy.mode==Mode::OnChange);}
inline bool Observe(State& state,const Policy& policy,std::uint64_t now,int setting){
    if(policy.mode==Mode::Off||setting<0||setting>4)return false;
    const bool disabled=setting!=0,notify=disabled&&(!state.checked||!state.disabled);
    state.checked=true;state.disabled=disabled;state.nextCheck=now+static_cast<std::uint64_t>(policy.intervalSeconds)*1000;return notify;
}
inline const wchar_t* Reason(int setting){return setting==1?L"disabled for this application":setting==2?L"disabled for this Windows user":setting==3?L"disabled by group policy":setting==4?L"disabled by the package manifest":L"enabled";}
}}

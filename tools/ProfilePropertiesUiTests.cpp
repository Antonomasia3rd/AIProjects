#include "../dependencies/profile_manager_ui.h"
#include <iostream>
namespace {
int checks=0,failures=0;
void Check(bool ok,const char* message){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<message<<'\n';}}
using namespace aip::profiles;
const UiRow* Verb(const std::vector<UiRow>& rows,UiVerb verb,const std::wstring& id=L""){
    for(const auto& row:rows){if(row.action.verb==verb&&row.action.expected.id==id)return &row;
        if(const auto nested=Verb(row.children,verb,id))return nested;}return nullptr;
}
void Page(){
    UiSnapshot snapshot;snapshot.registry.ownerSid=L"SID:S-1-5-21-111-222-333-1001";
    const ProfileEntry main{L"11111111111111111111111111111111",L"Main",L"C:\\Profiles\\Main.ini",L"C:\\PROFILES\\MAIN.INI",false};
    const ProfileEntry extra{L"22222222222222222222222222222222",L"Extra",L"C:\\Profiles\\Extra.ini",L"C:\\PROFILES\\EXTRA.INI",true};
    snapshot.registry.profiles={main,extra};snapshot.registry.defaultProfileId=main.id;snapshot.running.insert(main.id);
    const auto read=[](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);};
    const auto rows=BuildUiRows(snapshot,read);
    Check(rows.size()==7,"Profiles uses one shared settings row tree with coordinator controls and profile subpages");
    for(const auto verb:{UiVerb::Create,UiVerb::Startup,UiVerb::UnifiedTray,UiVerb::CrashNotices})
        Check(Verb(rows,verb)!=nullptr,"every coordinator action is present");
    for(const auto& profile:{main,extra})for(const auto verb:{UiVerb::Start,UiVerb::Stop,UiVerb::Settings,UiVerb::AutoStart,UiVerb::MakeDefault,UiVerb::Identity}){
        const auto action=Verb(rows,verb,profile.id);
        Check(action&&action->action.expected.profileKey==profile.profileKey&&action->action.expected.iniPath==profile.iniPath,
            "every original profile action retains the selected profile's identity and path");
        Check(action&&action->action.revision&&*action->action.revision==SerializeRegistry(snapshot.registry),
            "every profile action carries its exact captured coordinator revision");
    }
    Check(!Verb(rows,UiVerb::UnifiedTray)->checked&&!Verb(rows,UiVerb::Startup)->checked&&!Verb(rows,UiVerb::CrashNotices)->checked,
        "separate tray icons remain the default with automatic profile startup and crash notices off");
    Check(Verb(rows,UiVerb::Stop,main.id)->enabled&&!Verb(rows,UiVerb::Stop,extra.id)->enabled,
        "Stop reflects the observed controller instead of offering a dead-process action");
    Check(!Verb(rows,UiVerb::MakeDefault,main.id)->enabled&&Verb(rows,UiVerb::MakeDefault,extra.id)->enabled,
        "the current default is identified while another profile can become default");
    Check(!Verb(rows,UiVerb::AutoStart,main.id)->checked&&Verb(rows,UiVerb::AutoStart,extra.id)->checked,
        "automatic startup remains an independent per-profile choice");
    Check(rows[5].key==main.id&&rows[6].key==extra.id,"profile navigation has stable IDs rather than mutable display labels");
    const auto revision=Verb(rows,UiVerb::Create)->action.revision;
    Check(revision==Verb(rows,UiVerb::Identity,extra.id)->action.revision,
        "large coordinator metadata is shared across action rows rather than duplicated per control");
    auto changed=snapshot;changed.registry.defaultProfileId=extra.id;changed.registry.profiles[0].name=L"Renamed";
    const auto changedRows=BuildUiRows(changed,read);
    Check(changedRows[5].key==rows[5].key&&changedRows[5].label!=rows[5].label,"profile selection survives changed default/name captions");
    Check(*Verb(changedRows,UiVerb::Create)->action.revision!=*revision,"updated metadata produces a distinct action revision");
    const auto customized=BuildUiRows(snapshot,[](const wchar_t* key,const wchar_t* fallback){return std::wstring(key)==L"ProfilesCreate"?L"Custom create":std::wstring(fallback);});
    Check(customized[0].label==L"Custom create","Profiles labels use the same configurable wording mechanism");
    UiSnapshot empty;empty.registry.ownerSid=snapshot.registry.ownerSid;
    const auto noProfiles=BuildUiRows(empty,read);
    Check(noProfiles.size()==5&&Verb(noProfiles,UiVerb::Create)&&!Verb(noProfiles,UiVerb::Start,main.id),
        "an empty valid coordinator offers creation without inventing a profile");
}
}
int main(){Page();std::cout<<checks<<" profile Properties model checks; "<<failures<<" failures; no windows/files/processes accessed\n";return failures?1:0;}

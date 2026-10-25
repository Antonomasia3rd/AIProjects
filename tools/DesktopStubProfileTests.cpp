#include "../dependencies/desktopstub_profiles.h"
#include <iostream>
#include <map>
namespace {
int checks=0,failures=0;
void Check(bool value,const char* label){++checks;if(!value){++failures;std::cout<<"FAIL: "<<label<<'\n';}}
using namespace aip::profiles;
IdentityRequest Request(){IdentityRequest r;r.ownerSid=L"SID:S-1-5-21-100-200-300-1001";r.userScope=L"0123456789ABCDEF";r.profileKey=L"D:\\PROFILES\\DESKTOPSTUB.INI";return r;}
void IdentityTests(){
    auto r=Request();IdentityPlan plan;std::wstring error;
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::NeedsPersistentId&&!plan.requiresWrite,"fresh profile requires host CSPRNG identity before use");
    r.candidateId=L"0123456789abcdef0123456789abcdef";
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::Ready&&plan.requiresWrite&&Valid(plan.proposed),"fresh Auto profile stages one valid persisted identity");
    const auto original=plan.proposed;
    Check(original.package.name==L"aip.ds.01234567.0123456789abcdef0123456789abcdef"&&original.package.name.size()==48,"full128bit random ID and SID hash prefix fit manifest package-name limit");
    Check(original.package.publisher==L"CN=DesktopStub.0123456789abcdef0123456789abcdef","automatic Publisher is persistent and profile-specific");
    r.stored=original;r.candidateId=L"ffffffffffffffffffffffffffffffff";
    Check(PlanIdentity(r,plan,error)&&!plan.requiresWrite&&Same(plan.proposed.package,original.package)&&plan.proposed.id==original.id,"startup never replaces stored identity with a newly offered random ID");
    r.userScope=L"fedcba9876543210";
    Check(PlanIdentity(r,plan,error)&&Same(plan.proposed.package,original.package),"preserved identity does not change after formatting/hash-prefix changes");
    r.ownerSid=L"SID:S-1-5-21-100-200-300-1002";
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::OwnerMismatch&&!plan.requiresWrite,"same username under another SID cannot adopt saved identity");
    r=Request();r.stored=original;r.profileKey=L"D:\\PROFILES\\OTHER.INI";
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::OwnerMismatch,"copying INI to another profile path requires explicit ownership action");
    r=Request();r.candidateId=original.id;r.ownedExisting=PackageIdentity{L"dev.local.DesktopStub",L"CN=DesktopStub"};
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::Ready&&Same(plan.proposed.package,*r.ownedExisting)&&plan.proposed.preservedLegacy,"owned legacy package identity and publisher are adopted without breaking existing pins");
    auto legacy=plan.proposed;r.stored=legacy;r.ownedExisting.reset();r.migrateAutomatic=true;
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::MigrationRequired&&plan.pinMigration&&!plan.requiresWrite&&Same(plan.current.package,legacy.package),"automatic identity migration is a concrete deferred plan and leaves current identity intact");
    r.migrationApproved=true;
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::Ready&&plan.requiresWrite&&plan.pinMigration&&Same(plan.current.package,legacy.package)&&!Same(plan.proposed.package,legacy.package),"approved migration exposes old and proposed identities separately");
    r=Request();r.stored=original;r.mode=IdentityMode::Static;r.staticIdentity={L"Amiya.DesktopStub.ISO",L"CN=Custom ISO Publisher, O=Example"};
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::MigrationRequired,"Static ISO identity override cannot silently replace installed identity");
    r.migrationApproved=true;Check(PlanIdentity(r,plan,error)&&plan.proposed.mode==IdentityMode::Static&&Same(plan.proposed.package,r.staticIdentity),"approved static name and Publisher retain exact spelling");
    const auto fixed=plan.proposed;r.stored=fixed;r.migrationApproved=false;
    Check(PlanIdentity(r,plan,error)&&plan.status==IdentityResolution::Ready&&!plan.requiresWrite,"unchanged static identity is stable without another confirmation");
    r=Request();r.candidateId=L"user-name";Check(!PlanIdentity(r,plan,error),"username is not accepted as persistent random identity");
    for(const auto& bad:std::vector<std::wstring>{L"ab",std::wstring(51,L'a'),L"name_with_underscore",L"name/other",L"name\nInjected",L"CON",L"prn",L"aux",L"nul",L"COM1",L"lpt9",L"CON.App",L"lpt9.app",L"name.",L"xn--hello",L"app.xn--hello"})Check(!PackageName(bad),"invalid or reserved manifest package name rejected instead of silently sanitized");
    for(const auto& bad:std::vector<std::wstring>{L"",L"Publisher",L"CN=",L"CN=x\rInjected",L"CN=<x>"})Check(!Publisher(bad),"invalid publisher rejected");
    const auto another=AutomaticIdentity(L"0123456789abcdef",L"fedcba9876543210fedcba9876543210");
    Check(Valid(another)&&!Same(another,original.package),"independent profile random IDs yield independent package identities");
    auto collision=original;collision.profileKey=L"D:\\PROFILES\\SECOND.INI";std::wstring conflictingProfile;
    Check(FindIdentityConflict({original},collision,conflictingProfile)&&conflictingProfile==original.profileKey,"explicit static identity collision can be diagnosed before native deployment");
    collision.package=another;Check(!FindIdentityConflict({original},collision,conflictingProfile)&&conflictingProfile.empty(),"independent package identity has no same-user profile collision");
    collision.package=original.package;collision.package.name=L"AIP.DS.01234567.0123456789ABCDEF0123456789ABCDEF";
    Check(FindIdentityConflict({original},collision,conflictingProfile),"package string collision comparisons use ordinal ASCII case folding");
    std::map<std::wstring,std::wstring> fields;for(const auto& value:IdentityValues(original))fields[value.first]=value.second;
    std::optional<IdentityRecord> loaded;
    const auto read=[&](const wchar_t* key,const wchar_t* fallback){const auto found=fields.find(key);return found==fields.end()?std::wstring(fallback):found->second;};
    Check(ReadIdentity(read,loaded,error)&&loaded&&Same(loaded->package,original.package)&&loaded->ownerSid==original.ownerSid,"typed identity settings round trip without native IO");
    fields.erase(L"OwnerSid");Check(!ReadIdentity(read,loaded,error),"partial saved identity fails closed");
    fields.clear();Check(ReadIdentity(read,loaded,error)&&!loaded,"absent saved identity is distinguished from malformed identity");
}
void RegistryTests(){
    Registry registry;registry.ownerSid=Request().ownerSid;std::wstring error;
    ProfileEntry first{L"0123456789abcdef0123456789abcdef",L"Main profile",L"D:\\Profiles\\Main.ini",L"D:\\PROFILES\\MAIN.INI",false};
    ProfileEntry second{L"fedcba9876543210fedcba9876543210",L"Work \"calendar\" – 日本語",L"D:\\Profiles\\Work profile.ini",L"D:\\PROFILES\\WORK PROFILE.INI",true};
    Check(AddOrUpdate(registry,first,error)&&AddOrUpdate(registry,second,error)&&registry.profiles.size()==2,"independent named profiles have distinct INI paths and IDs");
    Check(!registry.unifiedTray&&!registry.notifyCrashes&&!registry.startupEnabled&&StartupProfiles(registry).empty(),"separate tray icons and no automatic startup/crash notices are safe defaults");
    Check(SuppressTrayForProfile(true,true,false,true)&&!SuppressTrayForProfile(true,true,false,false),"unified mode hides only explicit Toast children; Auto/Balloon retain their notification icon");
    Check(!SuppressTrayForProfile(false,true,false,true)&&!SuppressTrayForProfile(true,false,false,true)&&!SuppressTrayForProfile(true,true,true,true),"separate mode, original runtimes and coordinator always retain their own tray choice");
    registry.startupEnabled=true;
    Check(StartupProfiles(registry).size()==1&&StartupProfiles(registry)[0].id==second.id,"only explicitly selected profiles start when global profile startup is enabled");
    const auto text=SerializeRegistry(registry);Registry parsed;
    Check(!text.empty()&&ParseRegistry(text,registry.ownerSid,parsed,error)&&parsed.profiles[1].name==second.name&&parsed.profiles[1].iniPath==second.iniPath,"registry preserves Unicode labels, quoted names and spaced paths");
    Check(!ParseRegistry(text,L"SID:S-1-5-21-999-1001",parsed,error),"registry cannot be imported as another Windows user's coordination state");
    Check(!ParseRegistry(text+L"garbage",registry.ownerSid,parsed,error),"registry rejects trailing data");
    auto duplicate=second;duplicate.id=L"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";duplicate.profileKey=L"d:\\profiles\\main.ini";
    Check(!AddOrUpdate(registry,duplicate,error)&&registry.profiles.size()==2,"duplicate normalized path is rejected even when ASCII path casing differs");
    auto moved=first;moved.profileKey=L"D:\\PROFILES\\MOVED.INI";moved.iniPath=L"D:\\Profiles\\Moved.ini";
    Check(!AddOrUpdate(registry,moved,error)&&registry.profiles[0].profileKey==first.profileKey,"normal registry update cannot retarget an existing identity to another INI");
    first.name=L"Renamed";first.autoStart=true;Check(AddOrUpdate(registry,first,error)&&registry.profiles[0].name==L"Renamed","rename and startup choice preserve identity/path");
    Check(LaunchArguments(second)==L"--ini \"D:\\Profiles\\Work profile.ini\""&&LaunchArguments(second,true).find(L" --settings")!=std::wstring::npos,"child launch arguments use a quoted INI argument without shell text");
    Check(OneShotArguments({L"--configure-only"},false)&&OneShotArguments({L"--render-only"},false)&&OneShotArguments({L"--help"},false)&&OneShotArguments({L"--version"},false),"offline/help profile selection is a one-shot command rather than resident staging");
    Check(OneShotArguments({L"--set",L"Settings.TrayIcon=0"},true)&&!OneShotArguments({L"--set",L"Settings.TrayIcon=0"},false)&&!OneShotArguments({L"--settings"},true),"setting changes wait only when targeting an existing resident; Settings window remains asynchronous");
    for(const auto& path:std::vector<std::wstring>{L"relative.ini",L"D:\\Profiles\\..\\other.ini",L"D:\\Profiles\\x\" --set bad",L"D:\\Profiles\\x.ini:stream",L"D:\\Profiles\\*.ini",L"D:/Profiles/x.ini"})Check(!AbsoluteIniPath(path),"registry rejects unnormalized, relative or injected file paths");
    Check(AbsoluteIniPath(L"\\\\server\\share\\profile.ini"),"canonical UNC INI path is representable; native adapter still checks accessibility/ownership");
    auto oversized=registry;oversized.profiles.resize(MaximumProfiles+1);Check(SerializeRegistry(oversized).empty(),"registry entry count is bounded");
    const auto original=parsed;Check(!ParseRegistry(L"broken",registry.ownerSid,parsed,error)&&parsed.ownerSid==original.ownerSid,"failed parse does not mutate caller state");
    Check(sizeof(ScopeRules)/sizeof(ScopeRules[0])>=6,"Windows scope constraints are exposed as a portable audit table");
    Check(text.find(L"[Profiles]")!=std::wstring::npos&&text.find(L"UnifiedTray=0")!=std::wstring::npos,"per-user coordination preferences are an actual editable INI file");
    std::wstring mode=L"static";Check(NormalizeSetting(L"Profile.IdentityMode",mode,error)&&mode==L"Static","public identity mode is canonical typed configuration");
    mode=L"1";Check(!NormalizeSetting(L"ManagedRuntime",mode,error),"manual settings cannot bypass approved runtime migration state");
    CoordinatorPeer peer{registry.ownerSid,L"0123456789abcdef",first.id,L"D:\\INSTALL\\DesktopStub.exe",123,456};CoordinatorPeer parsedPeer;
    Check(ParsePeer(SerializePeer(peer),parsedPeer)&&PeerMatches(parsedPeer,registry.ownerSid,peer.installScope,first.id,123,456,peer.executable),"coordinator watch metadata is bound to user, install, child, PID, process creation time and executable");
    Check(!PeerMatches(peer,registry.ownerSid,peer.installScope,first.id,123,457,peer.executable),"PID reuse with a different process creation time is rejected");
    Check(!PeerMatches(peer,L"SID:S-1-5-21-999-1001",peer.installScope,first.id,123,456,peer.executable)&&!PeerMatches(peer,registry.ownerSid,peer.installScope,first.id,123,456,L"D:\\Other\\app.exe"),"coordinator watch cannot attach to another user or executable");
    Check(!CanAdoptLegacy(true,false,true)&&!CanAdoptLegacy(true,true,false)&&CanAdoptLegacy(true,true,true),"file ownership alone cannot adopt an unregistered or unrelated legacy package identity");
    std::vector<std::wstring> forwarded;
    Check(ForwardArguments({L"DesktopStub.exe",L"--settings",L"--set",L"Strings.TrayTip=Work profile"},forwarded,error)&&forwarded==std::vector<std::wstring>{L"--settings",L"--set",L"Strings.TrayTip=Work profile"},"default routing preserves command order, settings flags and complete spaced values");
    Check(ForwardArguments({L"DesktopStub.exe",L"--INI",L"D:\\Profiles\\Work.ini",L"--setup-desktop"},forwarded,error,true)&&forwarded==std::vector<std::wstring>{L"--setup-desktop"},"owned resident routing removes the original INI selector before adding the bound path exactly once");
    Check(!ForwardArguments({L"DesktopStub.exe",L"--INI=D:\\Other.ini"},forwarded,error),"registered profile forwarding rejects case-insensitive INI overrides");
    Check(ExitRequested({L"--quit"})&&OneShotArguments({L"--exit"},false)&&OneShotArguments({L"--once"},false)&&OneShotArguments({L"--rebuild-manifest"},false),"exit and known one-shot generation/manifest aliases retain synchronous routing");
    Check(RuntimeOneShotArguments({L"--once"})&&RuntimeOneShotArguments({L"--regenerate-manifest"})&&!RuntimeOneShotArguments({L"--configure-only",L"--once"})&&!RuntimeOneShotArguments({L"--exit"}),"live generation uses the owned runtime while offline and exit commands avoid staging");
}
}
int main(){IdentityTests();RegistryTests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

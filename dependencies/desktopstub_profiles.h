#pragma once
// Portable profile/identity plans only. The native host owns SID acquisition,
// path normalization, cryptographic randomness, atomic files and deployment.
#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace aip { namespace profiles {
inline constexpr std::size_t MaximumProfiles=64,MaximumRegistryCharacters=4*1024*1024;
inline bool Plain(const std::wstring& value,std::size_t maximum,bool empty=false){
    return (empty||!value.empty())&&value.size()<=maximum&&std::none_of(value.begin(),value.end(),[](wchar_t c){return c<0x20||c==0x7f;});
}
inline bool Hex(const std::wstring& value,std::size_t length){return value.size()==length&&std::all_of(value.begin(),value.end(),[](wchar_t c){
    return (c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');});}
inline std::wstring LowerAscii(std::wstring text){for(auto& c:text)if(c>=L'A'&&c<=L'Z')c=static_cast<wchar_t>(c+L'a'-L'A');return text;}
inline bool Sid(const std::wstring& value){
    const auto start=value.compare(0,4,L"SID:")==0?4u:0u;
    if(value.size()<start+5||value.size()>188||value.compare(start,4,L"S-1-")!=0)return false;
    bool number=false;unsigned components=0;
    for(auto i=start+4;i<value.size();++i){const auto c=value[i];if(c==L'-'){if(!number||++components>16)return false;number=false;}
        else if(c>=L'0'&&c<=L'9')number=true;else return false;}
    return number;
}
inline bool AbsoluteIniPath(const std::wstring& path){
    if(!Plain(path,32767)||path.find_first_of(L"\"<>|*?/")!=std::wstring::npos)return false;
    const bool drive=path.size()>3&&((path[0]>=L'A'&&path[0]<=L'Z')||(path[0]>=L'a'&&path[0]<=L'z'))&&path[1]==L':'&&path[2]==L'\\';
    const bool unc=path.size()>5&&path.compare(0,2,L"\\\\")==0&&path[2]!=L'\\'&&path.find(L'\\',2)!=std::wstring::npos;
    if(!drive&&!unc)return false;
    const auto from=drive?2u:0u;
    if(path.find(L':',drive?2u:0u)!=std::wstring::npos||path.back()==L'\\')return false;
    std::size_t begin=from;
    while(begin<path.size()){const auto end=path.find(L'\\',begin),finish=end==std::wstring::npos?path.size():end;
        const auto part=path.substr(begin,finish-begin);if(part==L"."||part==L"..")return false;
        if(end==std::wstring::npos)break;begin=end+1;}
    return true;
}
inline bool PackageName(const std::wstring& name){
    if(name.size()<3||name.size()>50||!std::all_of(name.begin(),name.end(),[](wchar_t c){
        return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||c==L'.'||c==L'-';}))return false;
    const auto lower=LowerAscii(name);
    if(lower.back()==L'.'||lower.compare(0,4,L"xn--")==0||lower.find(L".xn--")!=std::wstring::npos)return false;
    for(const auto* reserved:{L"con",L"prn",L"aux",L"nul",L"com1",L"com2",L"com3",L"com4",L"com5",L"com6",L"com7",L"com8",L"com9",L"lpt1",L"lpt2",L"lpt3",L"lpt4",L"lpt5",L"lpt6",L"lpt7",L"lpt8",L"lpt9"}){
        if(lower==reserved||lower.compare(0,std::wstring(reserved).size()+1,std::wstring(reserved)+L".")==0)return false;
    }
    return true;
}
inline bool Publisher(const std::wstring& value){return Plain(value,8192)&&value.size()>3&&LowerAscii(value.substr(0,3))==L"cn="&&
    value.find_first_of(L"<>")==std::wstring::npos;}
inline bool NormalizeSetting(std::wstring key,std::wstring& value,std::wstring& error){
    key=LowerAscii(key);if(key.compare(0,8,L"profile.")==0)key.erase(0,8);error.clear();
    const auto first=value.find_first_not_of(L" \t");value=first==std::wstring::npos?L"":value.substr(first,value.find_last_not_of(L" \t")-first+1);
    if(key==L"identitymode"){const auto mode=LowerAscii(value);if(mode!=L"auto"&&mode!=L"static"){error=L"Profile.IdentityMode must be Auto or Static.";return false;}value=mode==L"auto"?L"Auto":L"Static";return true;}
    if(key==L"staticpackagename"){if(value.empty()||PackageName(value))return true;error=L"The static package name must satisfy Windows package-name rules (3–50 ASCII characters).";return false;}
    if(key==L"staticpublisher"){if(value.empty()||Publisher(value))return true;error=L"The static Publisher must be a bounded X.509 subject beginning with CN=.";return false;}
    if(key==L"managedruntime"){error=L"ManagedRuntime is coordinator-owned state. Use the approved identity migration action.";return false;}
    error=L"Unknown profile setting.";return false;
}
struct PackageIdentity {std::wstring name,publisher;};
inline bool Valid(const PackageIdentity& identity){return PackageName(identity.name)&&Publisher(identity.publisher);}
inline bool Same(const PackageIdentity& a,const PackageIdentity& b){return LowerAscii(a.name)==LowerAscii(b.name)&&a.publisher==b.publisher;}
enum class IdentityMode {Auto,Static};
struct IdentityRecord {
    std::wstring id,ownerSid,profileKey;
    IdentityMode mode=IdentityMode::Auto;
    PackageIdentity package;
    bool preservedLegacy=false;
};
inline bool Valid(const IdentityRecord& identity){return Hex(identity.id,32)&&Sid(identity.ownerSid)&&AbsoluteIniPath(identity.profileKey)&&Valid(identity.package)&&
    (identity.mode==IdentityMode::Auto||identity.mode==IdentityMode::Static);}
inline PackageIdentity AutomaticIdentity(const std::wstring& userScope,const std::wstring& persistentId){
    if(!Hex(userScope,16)||!Hex(persistentId,32))return {};
    const auto id=LowerAscii(persistentId);
    return {L"aip.ds."+LowerAscii(userScope.substr(0,8))+L"."+id,L"CN=DesktopStub."+id};
}
enum class IdentityResolution {Ready,NeedsPersistentId,MigrationRequired,OwnerMismatch};
struct IdentityRequest {
    std::wstring ownerSid,userScope,profileKey,candidateId;
    IdentityMode mode=IdentityMode::Auto;
    std::optional<IdentityRecord> stored;
    // Native code may supply this ONLY after verifying the manifest belongs to
    // this profile. A shared executable-directory manifest is not such proof.
    std::optional<PackageIdentity> ownedExisting;
    PackageIdentity staticIdentity;
    bool migrateAutomatic=false,migrationApproved=false;
};
struct IdentityPlan {
    IdentityResolution status=IdentityResolution::Ready;
    IdentityRecord current,proposed;
    bool requiresWrite=false,pinMigration=false;
};
inline bool PlanIdentity(const IdentityRequest& request,IdentityPlan& output,std::wstring& error){
    error.clear();IdentityPlan plan;
    if(!Sid(request.ownerSid)||!Hex(request.userScope,16)||!AbsoluteIniPath(request.profileKey)||
        (request.mode!=IdentityMode::Auto&&request.mode!=IdentityMode::Static)||
        (request.ownedExisting&&!Valid(*request.ownedExisting))||
        (request.mode==IdentityMode::Static&&!Valid(request.staticIdentity))){error=L"Profile identity owner, path, mode or package override is invalid.";return false;}
    IdentityRecord current;
    if(request.stored){
        if(!Valid(*request.stored)){error=L"The saved profile identity is invalid; it was not replaced.";return false;}
        current=*request.stored;
        if(current.ownerSid!=request.ownerSid||current.profileKey!=request.profileKey){plan.status=IdentityResolution::OwnerMismatch;plan.current=current;output=std::move(plan);return true;}
    }else{
        if(request.candidateId.empty()){plan.status=IdentityResolution::NeedsPersistentId;output=std::move(plan);return true;}
        if(!Hex(request.candidateId,32)){error=L"A profile needs a 128-bit cryptographically random persistent identity.";return false;}
        current.id=LowerAscii(request.candidateId);current.ownerSid=request.ownerSid;current.profileKey=request.profileKey;current.mode=request.mode;
        current.package=request.ownedExisting?*request.ownedExisting:request.mode==IdentityMode::Static?request.staticIdentity:AutomaticIdentity(request.userScope,current.id);
        current.preservedLegacy=request.ownedExisting.has_value();plan.requiresWrite=true;
    }
    plan.current=current;plan.proposed=current;
    const bool keepAutomatic=request.mode==IdentityMode::Auto&&current.mode==IdentityMode::Auto&&!request.migrateAutomatic;
    const auto desired=keepAutomatic?current.package:request.mode==IdentityMode::Static?request.staticIdentity:AutomaticIdentity(request.userScope,current.id);
    plan.proposed.package=desired;plan.proposed.mode=request.mode;
    if(Same(current.package,desired))plan.proposed.package=current.package;
    if(!Same(current.package,desired)){
        plan.pinMigration=true;plan.proposed.preservedLegacy=false;
        if(!request.migrationApproved){plan.status=IdentityResolution::MigrationRequired;plan.requiresWrite=false;output=std::move(plan);return true;}
        plan.requiresWrite=true;
    }else if(current.mode!=request.mode){plan.requiresWrite=true;}
    output=std::move(plan);return true;
}
struct IdentityField {const wchar_t* key;};
inline constexpr IdentityField IdentityFields[]={{L"Id"},{L"OwnerSid"},{L"ProfileKey"},{L"Mode"},{L"PackageName"},{L"Publisher"},{L"PreservedLegacy"}};
template<class Reader> bool ReadIdentity(Reader read,std::optional<IdentityRecord>& result,std::wstring& error){
    std::vector<std::wstring> values;bool any=false;for(const auto& field:IdentityFields){values.push_back(read(field.key,L""));any=any||!values.back().empty();}
    if(!any){result.reset();error.clear();return true;}
    if((values[3]!=L"Auto"&&values[3]!=L"Static")||(values[6]!=L"0"&&values[6]!=L"1")){error=L"The saved profile identity mode or legacy marker is invalid.";return false;}
    IdentityRecord value{values[0],values[1],values[2],values[3]==L"Static"?IdentityMode::Static:IdentityMode::Auto,{values[4],values[5]},values[6]==L"1"};
    if(!Valid(value)){error=L"The saved profile identity is incomplete or invalid.";return false;}result=std::move(value);error.clear();return true;
}
inline std::vector<std::pair<std::wstring,std::wstring>> IdentityValues(const IdentityRecord& identity){
    if(!Valid(identity))return {};
    return {{L"Id",identity.id},{L"OwnerSid",identity.ownerSid},{L"ProfileKey",identity.profileKey},{L"Mode",identity.mode==IdentityMode::Auto?L"Auto":L"Static"},
        {L"PackageName",identity.package.name},{L"Publisher",identity.package.publisher},{L"PreservedLegacy",identity.preservedLegacy?L"1":L"0"}};
}
inline bool FindIdentityConflict(const std::vector<IdentityRecord>& records,const IdentityRecord& proposed,std::wstring& otherProfile){
    otherProfile.clear();if(!Valid(proposed))return false;
    for(const auto& old:records)if(Valid(old)&&old.ownerSid==proposed.ownerSid&&old.profileKey!=proposed.profileKey&&Same(old.package,proposed.package)){
        otherProfile=old.profileKey;return true;
    }
    return false; // Native package-family lookup also checks canonical Publisher equivalence.
}
struct ProfileEntry {std::wstring id,name,iniPath,profileKey;bool autoStart=false;};
struct Registry {
    std::wstring ownerSid;
    std::wstring defaultProfileId;
    bool startupEnabled=false,unifiedTray=false,notifyCrashes=false;
    std::vector<ProfileEntry> profiles;
};
inline bool Valid(const Registry& registry,std::wstring& error){
    if(!Sid(registry.ownerSid)||registry.profiles.size()>MaximumProfiles){error=L"The profile registry owner or entry count is invalid.";return false;}
    std::set<std::wstring> ids,paths;
    for(const auto& profile:registry.profiles){
        if(!Hex(profile.id,32)||!Plain(profile.name,128)||!AbsoluteIniPath(profile.iniPath)||!AbsoluteIniPath(profile.profileKey)||
            !ids.insert(LowerAscii(profile.id)).second||!paths.insert(LowerAscii(profile.profileKey)).second){error=L"A profile has invalid data, a duplicate ID or a duplicate normalized INI path.";return false;}
    }
    if(!registry.defaultProfileId.empty()&&(!Hex(registry.defaultProfileId,32)||!ids.count(LowerAscii(registry.defaultProfileId)))){error=L"The default profile is not in this user's profile list.";return false;}
    error.clear();return true;
}
inline bool AddOrUpdate(Registry& registry,const ProfileEntry& entry,std::wstring& error){
    auto staged=registry;const auto found=std::find_if(staged.profiles.begin(),staged.profiles.end(),[&](const ProfileEntry& old){return LowerAscii(old.id)==LowerAscii(entry.id);});
    if(found==staged.profiles.end())staged.profiles.push_back(entry);else{
        // Retargeting an existing package ID to another INI is a separate,
        // explicit ownership migration; ordinary rename/toggle cannot do it.
        if(found->profileKey!=entry.profileKey){error=L"Moving a profile identity to another INI requires an explicit migration.";return false;}
        *found=entry;
    }
    if(!Valid(staged,error))return false;registry=std::move(staged);return true;
}
inline std::wstring SerializeRegistry(const Registry& registry){
    std::wstring error;if(!Valid(registry,error))return {};
    std::wostringstream out;out<<L"; DesktopStub per-user profile coordination. Each profile keeps its own INI.\n[Profiles]\nVersion=1\nOwnerSid="<<std::quoted(registry.ownerSid)
        <<L"\nDefaultProfile="<<std::quoted(registry.defaultProfileId)<<L"\nStartupEnabled="<<registry.startupEnabled<<L"\nUnifiedTray="<<registry.unifiedTray<<L"\nNotifyCrashes="<<registry.notifyCrashes<<L"\nInstances=\"";
    for(std::size_t i=0;i<registry.profiles.size();++i){if(i)out<<L',';out<<registry.profiles[i].id;}out<<L"\"\n";
    for(const auto& p:registry.profiles)out<<L"\n[Profile."<<p.id<<L"]\nName="<<std::quoted(p.name)<<L"\nIniPath="<<std::quoted(p.iniPath)<<L"\nProfileKey="<<std::quoted(p.profileKey)<<L"\nAutoStart="<<p.autoStart<<L'\n';
    auto text=out.str();return text.size()<=MaximumRegistryCharacters?text:std::wstring{};
}
inline bool ParseRegistry(const std::wstring& text,const std::wstring& expectedOwnerSid,Registry& result,std::wstring& error){
    if(text.empty()||text.size()>MaximumRegistryCharacters||!Sid(expectedOwnerSid)){error=L"The profile registry is empty, oversized or has no valid owner.";return false;}
    const auto trim=[](const std::wstring& v){const auto begin=v.find_first_not_of(L" \t\r");return begin==std::wstring::npos?L"":v.substr(begin,v.find_last_not_of(L" \t\r")-begin+1);};
    std::map<std::wstring,std::map<std::wstring,std::wstring>> sections;std::wstring section,line;std::wistringstream input(text);
    while(std::getline(input,line)){line=trim(line);if(line.empty()||line[0]==L';'||line[0]==L'#')continue;
        if(line[0]==L'['){if(line.size()<3||line.back()!=L']'){error=L"The profile list has an invalid INI section.";return false;}section=LowerAscii(line.substr(1,line.size()-2));if(!sections.emplace(section,std::map<std::wstring,std::wstring>{}).second){error=L"The profile list has a duplicate INI section.";return false;}continue;}
        const auto equal=line.find(L'=');if(section.empty()||equal==std::wstring::npos){error=L"The profile list has an invalid INI assignment.";return false;}
        const auto key=LowerAscii(trim(line.substr(0,equal)));auto value=trim(line.substr(equal+1));
        if(!value.empty()&&value[0]==L'\"'){std::wistringstream quoted(value);std::wstring extra;if(!(quoted>>std::quoted(value))||quoted>>extra){error=L"The profile list has invalid quoted text.";return false;}}
        if(key.empty()||!sections[section].emplace(key,std::move(value)).second){error=L"The profile list has a duplicate INI setting.";return false;}
    }
    const auto header=sections.find(L"profiles");if(header==sections.end()||header->second.size()!=7){error=L"The profile list has no complete [Profiles] header.";return false;}
    const auto get=[&](const std::wstring& s,const wchar_t* k){const auto found=sections.find(s);if(found==sections.end())return std::wstring{};const auto field=found->second.find(LowerAscii(k));return field==found->second.end()?std::wstring{}:field->second;};
    Registry value;value.ownerSid=get(L"profiles",L"OwnerSid");value.defaultProfileId=get(L"profiles",L"DefaultProfile");
    const auto startup=get(L"profiles",L"StartupEnabled"),unified=get(L"profiles",L"UnifiedTray"),crash=get(L"profiles",L"NotifyCrashes");
    if(get(L"profiles",L"Version")!=L"1"||value.ownerSid!=expectedOwnerSid||(startup!=L"0"&&startup!=L"1")||(unified!=L"0"&&unified!=L"1")||(crash!=L"0"&&crash!=L"1")){error=L"The profile list's version, owner or preferences are invalid.";return false;}
    value.startupEnabled=startup==L"1";value.unifiedTray=unified==L"1";value.notifyCrashes=crash==L"1";
    const auto ids=get(L"profiles",L"Instances");std::size_t begin=0;
    while(begin<ids.size()){
        const auto comma=ids.find(L',',begin),end=comma==std::wstring::npos?ids.size():comma;ProfileEntry entry;entry.id=trim(ids.substr(begin,end-begin));
        if(!Hex(entry.id,32)||value.profiles.size()>=MaximumProfiles){error=L"The profile list has invalid or excessive profile IDs.";return false;}
        const auto name=L"profile."+LowerAscii(entry.id);const auto found=sections.find(name);
        if(found==sections.end()||found->second.size()!=4){error=L"A profile has no complete INI section.";return false;}
        entry.name=get(name,L"Name");entry.iniPath=get(name,L"IniPath");entry.profileKey=get(name,L"ProfileKey");const auto automatic=get(name,L"AutoStart");
        if(automatic!=L"0"&&automatic!=L"1"){error=L"A profile has an invalid AutoStart value.";return false;}entry.autoStart=automatic==L"1";value.profiles.push_back(std::move(entry));
        if(comma==std::wstring::npos)break;begin=comma+1;if(begin==ids.size()){error=L"The profile ID list has a trailing separator.";return false;}
    }
    if(sections.size()!=value.profiles.size()+1){error=L"The profile list has unknown or unlisted INI sections.";return false;}
    if(!Valid(value,error))return false;result=std::move(value);return true;
}
inline std::vector<ProfileEntry> StartupProfiles(const Registry& registry){
    std::wstring error;std::vector<ProfileEntry> result;if(!registry.startupEnabled||!Valid(registry,error))return result;
    for(const auto& entry:registry.profiles)if(entry.autoStart)result.push_back(entry);return result;
}
inline bool SuppressTrayForProfile(bool unified,bool managed,bool defaultProfile,bool explicitToast){
    // Legacy balloons require the resident's Shell_NotifyIcon identity.
    return unified&&managed&&!defaultProfile&&explicitToast;
}
inline std::wstring QuoteArgument(const std::wstring& value){
    std::wstring out=L"\"";std::size_t slashes=0;
    for(const auto c:value){if(c==L'\\'){++slashes;continue;}out.append(c==L'\"'?slashes*2+1:slashes,L'\\');out+=c;slashes=0;}
    out.append(slashes*2,L'\\');return out+L'\"';
}
inline std::wstring LaunchArguments(const ProfileEntry& entry,bool settings=false){
    Registry registry;registry.ownerSid=L"SID:S-1-5-18";registry.profiles.push_back(entry);std::wstring error;
    if(!Valid(registry,error))return {};
    return L"--ini "+QuoteArgument(entry.iniPath)+(settings?L" --settings":L"");
}
inline bool OneShotArguments(const std::vector<std::wstring>& arguments,bool alreadyRunning){
    for(const auto& raw:arguments){const auto arg=LowerAscii(raw);
        if(arg==L"--configure-only"||arg==L"--render-only"||arg==L"--help"||arg==L"-h"||arg==L"/?"||arg==L"--version"||arg==L"-v"||arg==L"/version"||arg==L"--exit"||arg==L"--quit"||arg==L"--once"||arg==L"--regenerate-manifest"||arg==L"--overwrite-manifest"||arg==L"--rebuild-manifest")return true;
        if(alreadyRunning&&(arg==L"--set"||arg.compare(0,6,L"--set=")==0))return true;
    }
    return false;
}
inline bool RuntimeOneShotArguments(const std::vector<std::wstring>& arguments){
    for(const auto& raw:arguments){const auto arg=LowerAscii(raw);if(arg==L"--configure-only"||arg==L"--render-only"||arg==L"--help"||arg==L"-h"||arg==L"/?"||arg==L"--version"||arg==L"-v"||arg==L"/version"||arg==L"--exit"||arg==L"--quit")return false;}
    for(const auto& raw:arguments){const auto arg=LowerAscii(raw);if(arg==L"--once"||arg==L"--regenerate-manifest"||arg==L"--overwrite-manifest"||arg==L"--rebuild-manifest")return true;}return false;
}
inline bool ExitRequested(const std::vector<std::wstring>& arguments){return std::any_of(arguments.begin(),arguments.end(),[](const std::wstring& text){const auto arg=LowerAscii(text);return arg==L"--exit"||arg==L"--quit";});}
inline bool ForwardArguments(const std::vector<std::wstring>& original,std::vector<std::wstring>& forwarded,std::wstring& error,bool removeIni=false){
    std::vector<std::wstring> result;std::size_t count=0;error.clear();
    for(std::size_t i=1;i<original.size();++i){const auto arg=LowerAscii(original[i]);
        if(arg==L"--ini"||arg.compare(0,6,L"--ini=")==0){
            if(!removeIni){error=L"A registered profile launch cannot override its INI path.";return false;}
            if(arg==L"--ini"){if(i+1>=original.size()){error=L"The INI option has no path.";return false;}++i;}continue;
        }
        if(original[i].find(L'\0')!=std::wstring::npos||original[i].size()>32767||count+original[i].size()+1>32767){error=L"The forwarded profile command is invalid or too long.";return false;}
        count+=original[i].size()+1;result.push_back(original[i]);
    }
    forwarded=std::move(result);return true;
}
inline bool CanAdoptLegacy(bool fileOwned,bool currentUserRegistered,bool matchingSourceRoot){return fileOwned&&currentUserRegistered&&matchingSourceRoot;}
struct CoordinatorPeer {std::wstring ownerSid,installScope,childId,executable;std::uint32_t processId=0;std::uint64_t creationTime=0;};
inline bool Valid(const CoordinatorPeer& peer){return Sid(peer.ownerSid)&&Hex(peer.installScope,16)&&Hex(peer.childId,32)&&AbsoluteIniPath(peer.executable)&&peer.processId!=0&&peer.creationTime!=0;}
inline std::wstring SerializePeer(const CoordinatorPeer& peer){if(!Valid(peer))return {};std::wostringstream out;out<<L"AIP_PROFILE_COORDINATOR_1\n"<<std::quoted(peer.ownerSid)<<L' '<<std::quoted(peer.installScope)<<L' '<<std::quoted(peer.childId)<<L' '<<peer.processId<<L' '<<peer.creationTime<<L' '<<std::quoted(peer.executable)<<L'\n';return out.str();}
inline bool ParsePeer(const std::wstring& text,CoordinatorPeer& output){
    if(text.empty()||text.size()>65536)return false;CoordinatorPeer peer;std::wistringstream in(text);std::wstring version,extra;
    if(!(in>>version>>std::quoted(peer.ownerSid)>>std::quoted(peer.installScope)>>std::quoted(peer.childId)>>peer.processId>>peer.creationTime>>std::quoted(peer.executable))||in>>extra||version!=L"AIP_PROFILE_COORDINATOR_1"||!Valid(peer))return false;output=std::move(peer);return true;
}
inline bool PeerMatches(const CoordinatorPeer& peer,const std::wstring& owner,const std::wstring& scope,const std::wstring& child,
    std::uint32_t processId,std::uint64_t creationTime,const std::wstring& executable){
    return Valid(peer)&&peer.ownerSid==owner&&peer.installScope==scope&&peer.childId==child&&peer.processId==processId&&peer.creationTime==creationTime&&LowerAscii(peer.executable)==LowerAscii(executable);
}
enum class SettingScope {Tile,Profile,Process,UserCoordinator};
struct ScopeRule {const wchar_t* setting;SettingScope scope;const wchar_t* reason;};
inline constexpr ScopeRule ScopeRules[]={
    {L"Source/account/refresh/display/cache",SettingScope::Tile,L"Independent provider and delivery policy for each tile."},
    {L"Package identity/Publisher/compatibility target",SettingScope::Profile,L"One host process/package identity; changing it requires explicit package/pin migration."},
    {L"PowerShell executable/deployment timeouts",SettingScope::Profile,L"Host-level deployment policy; static tile packages can use separate job snapshots."},
    {L"Single-instance mutex/IPC",SettingScope::Process,L"Effective normalized INI identity chooses one resident; cannot vary per tile."},
    {L"Poll wakeup/thread priority/COM apartment",SettingScope::Process,L"Shared scheduler and process resources; per-tile deadlines remain independent."},
    {L"Startup profile list/unified tray/crash notices",SettingScope::UserCoordinator,L"Optional per-user coordination across separate profile residents."}
};
}} // namespace aip::profiles

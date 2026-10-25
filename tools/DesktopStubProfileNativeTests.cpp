#include "../dependencies/desktopstub_profile_migration.inc"
#include <iostream>
#include <map>
namespace {
using namespace aip::profiles;using namespace aip::profiles::native;
int checks=0,failures=0;void Check(bool v,const char* label){++checks;if(!v){++failures;std::cout<<"FAIL: "<<label<<'\n';}}
struct Fake {
    std::map<std::wstring,std::wstring> files;std::set<std::wstring> foreign;std::vector<std::pair<std::wstring,std::wstring>> copies;
    std::map<std::wstring,std::vector<std::wstring>> registered;bool registrationLookupFails=false;
    unsigned reads=0,writes=0,randoms=0,launches=0,signals=0,waits=0,closes=0;std::wstring executable,arguments;bool running=false,failWrite=false,timeout=false;
    Operations Io(){Operations io;
        io.exists=[&](const auto& path,bool& exists,auto&){exists=files.count(path)!=0;return true;};
        io.registeredRoots=[&](const auto& identity,auto& roots,auto& error){if(registrationLookupFails){error=L"Injected package lookup error";return false;}roots=registered[identity.name];return true;};
        io.registeredAtSource=[&](const auto& identity,const auto& source,bool& proof,auto& error){proof=false;if(registrationLookupFails){error=L"Injected package lookup error";return false;}
            for(const auto& path:registered[identity.name])if(LowerAscii(path)==LowerAscii(source))proof=true;return true;};
        io.read=[&](const auto& path,auto& text,bool& missing,auto&){++reads;const auto found=files.find(path);missing=found==files.end();text=missing?L"":found->second;return true;};
        io.write=[&](const auto& path,const auto& text,bool fresh,auto& error){++writes;if(failWrite||(fresh&&files.count(path))){error=L"Injected write failure";return false;}files[path]=text;return true;};
        io.directory=[](const auto&,auto&){return true;};io.owner=[&](const auto& path,const auto&,auto& error){if(foreign.count(path)){error=L"Another owner";return false;}return true;};
        io.copy=[&](const auto& source,const auto& destination,auto&){copies.emplace_back(source,destination);files[destination]=L"inert runtime artifact";return true;};
        io.backup=[&](const auto& source,const auto& destination,auto& error){if(files.count(destination)||!files.count(source)){error=L"Injected backup conflict";return false;}files[destination]=files[source];return true;};
        io.random=[&](auto& value){std::wostringstream out;out<<std::hex<<std::setw(32)<<std::setfill(L'0')<<++randoms;value=out.str();return true;};
        io.locked=[](const auto&,const auto& action,auto&){return action();};
        io.launch=[&](const auto& path,const auto& args,DWORD& pid,HANDLE& process,auto&){++launches;executable=path;arguments=args;pid=42;process=nullptr;return true;};
        io.wait=[&](HANDLE,DWORD limit,DWORD& code,auto&){++waits;code=7;return !timeout&&limit==120000;};io.close=[&](HANDLE){++closes;};
        io.erase=[&](const auto& path,auto&){files.erase(path);return true;};
        io.window=[&](const auto&){return running?reinterpret_cast<HWND>(static_cast<INT_PTR>(1)):nullptr;};
        io.signal=[&](HWND,WPARAM command){++signals;return command==2;};return io;
    }
};
Context Ctx(){return {L"SID:S-1-5-21-100-200-300-1001",L"0123456789abcdef",L"fedcba9876543210",L"D:\\INSTALL",L"D:\\LOCAL\\DesktopStub\\Profiles\\fedcba9876543210",L"D:\\INSTALL\\DesktopStub.ini"};}
void StoreTests(){
    Fake fake;Store store(Ctx(),fake.Io());Registry registry;std::wstring error;
    Check(store.Load(registry,error)&&registry.ownerSid==store.context.ownerSid&&registry.profiles.empty(),"absent registry is a fresh current-user list without writes");
    ProfileEntry first;const auto factory=[](){return L"[Settings]\nRegisterPackage=0\n[Tile.desktop]\nType=Wallpaper\n";};
    Check(store.Fresh(L"Private profile",L"",factory,first,error)&&fake.randoms==1,"fresh creation obtains one persistent ID from injected entropy");
    Check(fake.files[first.iniPath].find(L"ProfileIdentity")!=std::wstring::npos&&fake.files[first.iniPath].find(L"GameToken")==std::wstring::npos,"fresh profile is factory content plus identity, with no copied accounts");
    std::wstring trayTip;Check(aip::ReadIniValueFromDoc(aip::ParseIniDocument(fake.files[first.iniPath]),L"Strings",L"TrayTip",trayTip)&&trayTip==L"DesktopStub — Private profile","fresh separate tray caption identifies its profile through the existing configurable string");
    Check(first.iniPath==store.ProfileRoot(first)+L"\\DesktopStub.ini"&&!first.autoStart,"new profile file belongs to its ID directory and starts with auto-start off");
    Check(store.Load(registry,error)&&registry.defaultProfileId==first.id&&registry.profiles.size()==1&&!registry.unifiedTray,"first enrollment becomes current-user default without merging tray icons");
    IdentityRecord identity;const auto before=fake.writes;
    Check(store.Identity(first.iniPath,first.profileKey,{},identity,error)&&identity.id==first.id&&fake.writes==before&&fake.randoms==1,"existing persistent identity is reused without random generation or writes");
    fake.foreign.insert(first.iniPath);const auto previousReads=fake.reads;
    Check(!store.Identity(first.iniPath,first.profileKey,{},identity,error)&&fake.reads==previousReads,"foreign-owned INI is rejected before any account/settings text is read");fake.foreign.clear();
    Check(store.Stage(first,error)&&fake.copies.size()==3,"profile stage copies exactly the host, live broker and tile launcher through injected IO");
    Check(fake.copies[0].second==store.RuntimeRoot(first)+L"\\DesktopStub.exe"&&fake.files[store.RuntimeRoot(first)+L"\\runtime.owner"]==store.OwnerText(first),"runtime has an exact SID/install/profile/generation owner marker and independent package directory");
    DWORD pid=0;HANDLE process=nullptr;
    Check(store.Start(first,L"--settings",pid,process,error)&&fake.launches==1&&pid==42&&fake.executable==store.RuntimeExe(first),"explicit launch targets the owned profile runtime, never RunAs or a shell");
    Check(fake.arguments==LaunchArguments(first)+L" --settings"&&process==nullptr,"child gets its exact absolute INI and requested action");
    const auto beforeOneShot=fake.copies.size();int childExit=0;
    Check(store.RunOneShot(first,L"--configure-only --set Settings.TrayIcon=0",childExit,error)&&childExit==7&&fake.waits==1&&fake.closes==1&&fake.copies.size()==beforeOneShot&&fake.executable==store.context.sourceRoot+L"\\DesktopStub.exe",
        "one-shot profile command uses canonical source without staging and propagates exact child failure status");
    fake.timeout=true;Check(!store.RunOneShot(first,L"--render-only",childExit,error)&&childExit==124&&fake.closes==2,"bounded one-shot wait closes its handle and reports timeout without killing another process");fake.timeout=false;
    const auto copies=fake.copies.size();fake.files[store.RuntimeRoot(first)+L"\\runtime.owner"]=L"wrong owner";
    Check(!store.Stage(first,error)&&fake.copies.size()==copies,"mismatched runtime ownership prevents any overwrite");fake.files[store.RuntimeRoot(first)+L"\\runtime.owner"]=store.OwnerText(first);
    Check(!store.Stop(first,error)&&fake.signals==0,"stopping an absent resident never terminates or signals another process");fake.running=true;
    Check(store.Stop(first,error)&&fake.signals==1,"stop uses one graceful profile-specific IPC request");
    ProfileEntry found;Check(store.Find(first.id,found,error)&&found.profileKey==first.profileKey,"selection resolves only registered current-user IDs");
    Check(!store.Find(L"ffffffffffffffffffffffffffffffff",found,error),"unknown profile ID cannot launch arbitrary files");
    const auto prior=fake.files[store.RegistryPath()];fake.failWrite=true;
    Check(!store.Update([](Registry& r,std::wstring&){r.notifyCrashes=true;return true;},error)&&fake.files[store.RegistryPath()]==prior,"registry save failure preserves the previous list");fake.failWrite=false;
    fake.files[store.RegistryPath()]=L"corrupt";Check(!store.Load(registry,error),"corrupt registry is not silently replaced with an empty list");fake.files[store.RegistryPath()]=prior;
    const auto existing=first.iniPath;ProfileEntry other;
    Check(!store.Fresh(L"Collision",existing,factory,other,error)&&fake.files[existing].find(first.id)!=std::wstring::npos,"fresh profile creation cannot overwrite an existing INI or its identity");
    const std::wstring legacyPath=L"D:\\INSTALL\\DesktopStub.ini",legacyKey=L"D:\\INSTALL\\DESKTOPSTUB.INI";
    fake.files[legacyPath]=factory();const PackageIdentity legacy{L"dev.local.DesktopStub",L"CN=DesktopStub"};
    bool adopt=true;Check(CanAdoptOwnedManifest(store.io,legacy,store.context.sourceRoot,adopt,error)&&!adopt,"owned but unregistered ZIP/VM manifest is not adopted as a legacy identity");
    fake.registered[legacy.name]={store.context.sourceRoot};Check(CanAdoptOwnedManifest(store.io,legacy,store.context.sourceRoot,adopt,error)&&adopt,"same-current-user package registration at the verified source proves legacy adoption");
    fake.registrationLookupFails=true;Check(!CanAdoptOwnedManifest(store.io,legacy,store.context.sourceRoot,adopt,error),"unknown registration lookup failure is explicit rather than guessing a new or legacy identity");fake.registrationLookupFails=false;
    Check(store.Identity(legacyPath,legacyKey,legacy,identity,error)&&Same(identity.package,legacy)&&identity.preservedLegacy,"verified owned legacy main identity is preserved when enrolled");
    Check(store.Identity(legacyPath,legacyKey,{},identity,error)&&Same(identity.package,legacy),"second original-main launch reuses its verified registered source without needing first-adoption metadata");
    const auto oldText=fake.files[legacyPath];auto changed=oldText;aip::WriteIniValueToText(changed,L"Settings",L"ManifestIdentityName",L"Custom.ISO.Identity");aip::WriteIniValueToText(changed,L"Settings",L"ManifestPublisher",L"CN=Custom ISO");fake.files[legacyPath]=changed;
    Check(store.Identity(legacyPath,legacyKey,legacy,identity,error)&&fake.files[legacyPath]==changed&&store.pendingIdentity&&Same(identity.package,legacy),"changed legacy static override queues migration while the current persisted identity remains usable");fake.files[legacyPath]=oldText;
    const auto stagedCopies=fake.copies.size();ProfileEntry main{identity.id,L"Main",legacyPath,legacyKey,false};
    Check(store.Start(main,L"",pid,process,error)&&fake.copies.size()==stagedCopies&&fake.executable==store.context.sourceRoot+L"\\DesktopStub.exe","legacy main profile keeps its original package/runtime directory");
    Check(fake.launches==4&&fake.signals==1,"validation used only injected file/process operations and never started real profiles");
}
void MigrationTests(){
    Fake fake;Store store(Ctx(),fake.Io());std::wstring error;ProfileEntry entry;
    const auto factory=[](){return L"[Settings]\nRegisterPackage=0\n[Account]\nToken=INERT_ORIGINAL\n";};
    Check(store.Fresh(L"Migration",L"",factory,entry,error),"migration fixture creates only an injected factory profile");
    IdentityRecord before;Check(store.CurrentIdentity(entry,before,error),"read fixture's current identity with ownership guard");
    IdentityRequest request;request.ownerSid=store.context.ownerSid;request.userScope=store.context.userScope;request.profileKey=entry.profileKey;request.stored=before;request.mode=IdentityMode::Static;request.staticIdentity={L"Example.Profile.Static",L"CN=Example Profile"};
    IdentityPlan plan;Check(PlanIdentity(request,plan,error)&&plan.status==IdentityResolution::MigrationRequired,"static migration is prepared as an explicit plan");
    const auto original=fake.files[entry.iniPath];MigrationEngine engine(store);Migration migration;
    Check(engine.Prepare(entry,plan,migration,error)&&fake.files[entry.iniPath]==original,"preparation stages isolated files and backup before changing the active INI");
    Check(migration.oldRuntime!=migration.newRuntime&&migration.newRuntime.find(IdentityGeneration(plan.proposed))!=std::wstring::npos,"each identity has a different controlled runtime generation");
    Check(fake.files[migration.backup]==original&&fake.files.count(MigrationPath(entry))==1,"non-overwriting backup and recoverable owned receipt are staged");
    Migration read;bool present=false;Check(engine.Load(entry,read,present,error)&&present&&SameRecord(read.before,before)&&read.newRuntime==migration.newRuntime,"migration receipt round trips owned identity and controlled runtime paths");
    Check(engine.Commit(migration,error)&&migration.phase==MigrationPhase::AwaitingSetup,"active identity commits only after the approved complete INI revision is revalidated");
    IdentityRecord current;Check(store.CurrentIdentity(entry,current,error)&&Same(current.package,plan.proposed.package),"committed INI contains the proposed package identity");
    aip::WriteIniValueToText(fake.files[entry.iniPath],L"Account",L"Token",L"INERT_EDITED_AFTER_APPROVAL");
    aip::WriteIniValueToText(fake.files[entry.iniPath],L"Profile",L"StaticPackageName",L"User.Next.Request");
    Check(engine.Complete(migration,false,error)&&store.CurrentIdentity(entry,current,error)&&SameRecord(current,before),"failed package setup restores the previous identity");
    Check(fake.files[entry.iniPath].find(L"INERT_EDITED_AFTER_APPROVAL")!=std::wstring::npos&&fake.files[entry.iniPath].find(L"User.Next.Request")!=std::wstring::npos,"rollback preserves subsequent account edits and independent pending identity requests");
    Check(engine.Load(entry,read,present,error)&&present&&read.phase==MigrationPhase::RolledBack,"rollback receipt remains until the prior runtime successfully restarts");
    Check(engine.Prepare(entry,plan,migration,error),"a completed rollback can prepare a new controlled attempt");
    aip::WriteIniValueToText(fake.files[entry.iniPath],L"Settings",L"TrayIcon",L"0");
    Check(!engine.Commit(migration,error)&&store.CurrentIdentity(entry,current,error)&&SameRecord(current,before),"a later INI edit prevents stale migration commit without replacing identity");
    Migration invalid=read;invalid.newRuntime=L"D:\\Unrelated\\evil.exe";
    Check(!ParseMigration(store.context,entry,SerializeMigration(store.context,invalid),read,error),"receipt cannot redirect migration relaunch outside its identity generation");
    fake.files.erase(MigrationPath(entry));
    Check(engine.Prepare(entry,plan,migration,error)&&engine.Commit(migration,error)&&engine.Complete(migration,true,error)&&!fake.files.count(MigrationPath(entry)),"successful setup retires the receipt and preserves the new identity");
    ProfileEntry other;Check(store.Fresh(L"Other",L"",factory,other,error),"second fixture profile has an independent identity");
    IdentityRecord otherIdentity;store.CurrentIdentity(other,otherIdentity,error);request.stored=current;request.staticIdentity=otherIdentity.package;
    store.CurrentIdentity(entry,current,error);request.stored=current;PlanIdentity(request,plan,error);const auto copyCount=fake.copies.size();
    Check(!engine.Prepare(entry,plan,migration,error)&&fake.copies.size()==copyCount,"migration rejects another current-user profile's canonical package family before staging");
    const auto freshPath=L"D:\\PROFILES\\STATIC-NEW.INI";std::wstring fresh=factory();
    aip::WriteIniValueToText(fresh,L"Profile",L"IdentityMode",L"Static");aip::WriteIniValueToText(fresh,L"Profile",L"StaticPackageName",otherIdentity.package.name);aip::WriteIniValueToText(fresh,L"Profile",L"StaticPublisher",otherIdentity.package.publisher);fake.files[freshPath]=fresh;
    Check(!store.Identity(freshPath,freshPath,{},current,error)&&fake.files[freshPath]==fresh,"first-time Static enrollment also rejects an existing profile's package family without persisting a conflicting identity");
    std::wstring unlisted=factory();aip::WriteIniValueToText(unlisted,L"Profile",L"IdentityMode",L"Static");aip::WriteIniValueToText(unlisted,L"Profile",L"StaticPackageName",L"Unenrolled.Existing.App");aip::WriteIniValueToText(unlisted,L"Profile",L"StaticPublisher",L"CN=Existing App");
    const std::wstring unlistedPath=L"D:\\PROFILES\\UNLISTED.INI";fake.files[unlistedPath]=unlisted;fake.registered[L"Unenrolled.Existing.App"]={L"D:\\UNRELATED\\ExistingApp"};
    Check(!store.Identity(unlistedPath,unlistedPath,{},current,error)&&fake.files[unlistedPath]==unlisted,"Static identity already registered at an unrelated current-user root is rejected even before that app is enrolled");
    fake.registered[L"Unenrolled.Existing.App"].clear();
    Check(store.Identity(unlistedPath,unlistedPath,{},current,error),"a nonconflicting Static identity can be persisted before reuse checks");
    const auto savedStatic=fake.files[unlistedPath];const ProfileEntry savedEntry{current.id,L"Saved static",unlistedPath,unlistedPath,false};
    fake.registered[current.package.name]={L"D:\\UNRELATED\\LaterApp"};
    Check(!store.Identity(unlistedPath,unlistedPath,{},current,error)&&fake.files[unlistedPath]==savedStatic,"unchanged saved Static identity cannot bypass a later unrelated registration and never rewrites the INI");
    const auto copied=fake.copies.size();const auto launched=fake.launches;DWORD processId=0;HANDLE process=nullptr;
    Check(!store.Stage(savedEntry,error)&&fake.copies.size()==copied,"direct staging also rejects a saved identity whose package moved to an unrelated root");
    Check(!store.Start(savedEntry,L"",processId,process,error)&&fake.launches==launched,"direct Start cannot launch or replace the unrelated registered package");
    Fake autoMain;Store mainStore(Ctx(),autoMain.Io());const auto mainPath=mainStore.context.mainIniPath;std::wstring mainKey;aip::named_objects::NormalizeFileIdentity(mainPath,mainKey);
    autoMain.files[mainPath]=factory();IdentityRecord mainIdentity;
    Check(mainStore.Identity(mainPath,mainKey,{},mainIdentity,error)&&!mainIdentity.preservedLegacy,"fresh original Main receives Auto rather than a legacy adoption");
    autoMain.registered[mainIdentity.package.name]={mainStore.context.sourceRoot};const auto mainWrites=autoMain.writes;
    Check(mainStore.Identity(mainPath,mainKey,{},mainIdentity,error)&&autoMain.writes==mainWrites,"fresh Auto Main's second launch permits its verified canonical registered root without rewriting identity");
}
void SharedRegistryTests(){
    Fake fake;Store legacy(Ctx(),fake.Io());std::wstring error;ProfileEntry main;
    Check(legacy.Fresh(L"Main",legacy.context.mainIniPath,[]{return L"[Account]\nToken=INERT_ACCOUNT_NOT_CLONED\n";},main,error),"shared-storage migration fixture has one owned legacy Main profile");
    const auto originalIni=fake.files[main.iniPath],originalRegistry=fake.files[legacy.RegistryPath()];
    auto context=Ctx();context.baseRoot=L"D:\\USER\\.DesktopStub\\Profiles\\"+context.installScope;context.legacyBaseRoots={legacy.context.baseRoot};Store common(context,fake.Io());
    Check(common.ImportLegacyRegistry(error),"owned legacy registry metadata imports into a common outside-AppData directory");
    Registry imported;Check(common.Load(imported,error)&&imported.defaultProfileId==main.id&&imported.profiles.size()==1&&imported.profiles.front().iniPath==main.iniPath,"shared registry preserves Main identity, selected default and exact external INI");
    Check(fake.files[main.iniPath]==originalIni&&fake.files[legacy.RegistryPath()]==originalRegistry&&fake.copies.empty()&&fake.launches==0,"metadata adoption does not copy accounts or runtimes, edit old files or launch profiles");
    Check(fake.files[common.RegistryPath()].find(L"INERT_ACCOUNT_NOT_CLONED")==std::wstring::npos,"common registry contains metadata only, never account contents");
    const auto writes=fake.writes;Check(common.ImportLegacyRegistry(error)&&fake.writes==writes,"later packaged or unpackaged access reuses shared registry without repeated import");
    fake.files.erase(common.RegistryPath());fake.foreign.insert(legacy.RegistryPath());const auto reads=fake.reads;
    Check(!common.ImportLegacyRegistry(error)&&fake.reads==reads&&!fake.files.count(common.RegistryPath()),"foreign-owned legacy registry is rejected before reading metadata or creating a new registry");fake.foreign.clear();
    auto other=imported;other.profiles.front().name=L"Conflicting old list";const auto otherBase=L"D:\\OTHER_OWNED_LEGACY";
    fake.files[std::wstring(otherBase)+L"\\profiles.ini"]=SerializeRegistry(other);common.context.legacyBaseRoots.push_back(otherBase);
    Check(!common.ImportLegacyRegistry(error)&&!fake.files.count(common.RegistryPath()),"conflicting old metadata lists require an explicit choice instead of silently overwriting a default");
    common.context.legacyBaseRoots={legacy.context.baseRoot};fake.failWrite=true;
    Check(!common.ImportLegacyRegistry(error)&&!fake.files.count(common.RegistryPath())&&fake.files[legacy.RegistryPath()]==originalRegistry,"failed atomic metadata migration preserves the entire original registry");
}
}
int main(){StoreTests();MigrationTests();SharedRegistryTests();std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;}

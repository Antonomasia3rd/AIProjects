// Per-user profile coordination. Each profile remains an independent resident;
// this UI never deploys a package, pins a tile, copies accounts or uses RunAs.
#include "../desktopstub_profiles_native.inc"
#include "../profile_manager_ui.h"
#include "../desktopstub_profile_migration.inc"
#include <deque>
#include <map>
#include <mutex>
static constexpr UINT PROFILE_MANAGER_MENU=59100,WM_APP_PROFILE_CHILD_EXIT=WM_APP+177;
static std::atomic<bool> g_profileSuppressTray{false};
static std::mutex g_profileTrayFallbackMutex;
static bool ProfileManagerSuppressTray(){return g_profileSuppressTray.load();}
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testProfileBalloonTrayEnsure)(DWORD*)=nullptr;
#endif
static bool EnsureProfileBalloonTray()
{
    std::lock_guard<std::mutex> lock(g_profileTrayFallbackMutex);
    if(!g_profileSuppressTray.load())return true;
    g_profileSuppressTray=false;DWORD error=ERROR_SUCCESS;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    const bool ready=g_testProfileBalloonTrayEnsure&&g_testProfileBalloonTrayEnsure(&error);
#else
    const bool ready=EnsureTrayIcon(&error);
#endif
    if(!ready){g_profileSuppressTray=true;return false;}
    LogText(L"[i] A profile tray icon was restored for this session because its notification needed Balloon delivery.");
    return true;
}

static std::unique_ptr<aip::profiles::native::Store> g_profileStore;
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testProfileContext)(aip::profiles::native::Context&,std::wstring&)=nullptr;
static aip::profiles::native::Operations* g_testProfileOperations=nullptr;
#endif
static bool ProfileKnownFolder(REFKNOWNFOLDERID id,DWORD flags,std::wstring& path,std::wstring& error)
{
    path.clear();const auto initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    struct ReleaseCom{HRESULT result;~ReleaseCom(){if(SUCCEEDED(result))CoUninitialize();}} releaseCom{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE){error=L"The current user's shared profile storage could not be initialized.";return false;}
    PWSTR raw=nullptr;const auto result=SHGetKnownFolderPath(id,flags,nullptr,&raw);
    if(SUCCEEDED(result)&&raw)path=raw;if(raw)CoTaskMemFree(raw);
    if(FAILED(result)||path.empty()){error=L"The current user's shared profile storage location is unavailable.";return false;}return true;
}
static bool ProfileManagerContext(aip::profiles::native::Context& context,std::wstring& error)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testProfileContext)return g_testProfileContext(context,error);
    error=L"Profile coordination is inert in validation.";return false;
#else
    using namespace aip::profiles;using namespace aip::profiles::native;
    std::vector<wchar_t> module(32768);const auto count=GetModuleFileNameW(nullptr,module.data(),static_cast<DWORD>(module.size()));
    if(!count||count>=module.size()||!aip::named_objects::CurrentUserIdentity(context.ownerSid)||!aip::named_objects::StableIdentityHash(context.ownerSid,context.userScope)){error=L"The current Windows user or executable identity is unavailable.";return false;}
    auto currentRoot=Parent(std::wstring(module.data(),count));context.sourceRoot=currentRoot;std::wstring runtimeScope;
    // A path retrieval flag alone cannot stop MSIX's AppData write filter.
    // Use an owned user-profile directory outside AppData for every identity.
    std::wstring profile,local;if(!ProfileKnownFolder(FOLDERID_Profile,KF_FLAG_NO_PACKAGE_REDIRECTION,profile,error)||
        !ProfileKnownFolder(FOLDERID_LocalAppData,KF_FLAG_NO_PACKAGE_REDIRECTION,local,error))return false;
    const std::wstring base=profile+L"\\.DesktopStub\\Profiles";
    const auto marker=currentRoot+L"\\runtime.owner";const auto attributes=GetFileAttributesW(marker.c_str());
    if(attributes!=INVALID_FILE_ATTRIBUTES){
        std::wstring text;bool missing=false;if(!FileOwner(marker,context.ownerSid,error)||!ReadText(marker,text,missing,error)||missing)return false;
        std::wistringstream input(text);std::wstring version,owner,scope,id,key,source,generation,exactIni,extra;
        if(!(input>>version>>std::quoted(owner)>>std::quoted(scope)>>std::quoted(id)>>std::quoted(key)>>std::quoted(source)>>std::quoted(generation))||version!=L"AIP_PROFILE_RUNTIME_2"||owner!=context.ownerSid||!Hex(scope,16)||!Hex(id,32)||!Hex(generation,16)||!AbsoluteIniPath(key)||!AbsoluteIniPath(source)){
            error=L"The managed profile runtime has invalid ownership metadata.";return false;}
        input>>std::ws;if(!input.eof()){std::wstring normalized;if(!(input>>std::quoted(exactIni))||input>>extra||!AbsoluteIniPath(exactIni)||!aip::named_objects::NormalizeFileIdentity(exactIni,normalized)||normalized!=key){error=L"The runtime's INI path does not match its profile binding.";return false;}}
        context.sourceRoot=source;runtimeScope=scope;context.runtimeGeneration=generation;context.runtimeId=id;context.runtimeProfileKey=key;context.runtimeIniPath=exactIni.empty()?key:exactIni;
    }
    std::wstring sourceKey;if(!aip::named_objects::NormalizeFileIdentity(context.sourceRoot,sourceKey)||!aip::named_objects::StableIdentityHash(sourceKey,context.installScope)){error=L"The DesktopStub installation scope could not be determined.";return false;}
    if(!runtimeScope.empty()&&runtimeScope!=context.installScope){error=L"The managed runtime's installation scope no longer matches its origin.";return false;}
    context.baseRoot=base+L"\\"+context.installScope;context.mainIniPath=context.sourceRoot+L"\\DesktopStub.ini";
    context.legacyBaseRoots.push_back(local+L"\\DesktopStub\\Profiles\\"+context.installScope);
    // Inspect only this installation's verified main package, never enumerate
    // other packages' private stores to discover coordination metadata.
    const auto manifest=context.sourceRoot+L"\\AppxManifest.xml";const auto manifestAttributes=GetFileAttributesW(manifest.c_str());
    if(manifestAttributes!=INVALID_FILE_ATTRIBUTES){
        std::wstring text;bool missing=false;if(!FileOwner(manifest,context.ownerSid,error)||!ReadText(manifest,text,missing,error)||missing)return false;
        PackageIdentity identity;
        if(!ExtractXmlAttributeFromTag(text,L"<Identity",L"Name",identity.name)||!ExtractXmlAttributeFromTag(text,L"<Identity",L"Publisher",identity.publisher)||!Valid(identity)){error=L"The main package manifest has no valid identity for legacy profile discovery.";return false;}
        bool registered=false;if(!RegisteredAtSource(identity,context.sourceRoot,registered,error))return false;
        if(registered){std::wstring family;if(!PackageFamily(identity,family)){error=L"The verified main package family could not be resolved.";return false;}
            context.legacyBaseRoots.push_back(local+L"\\Packages\\"+family+L"\\LocalCache\\Local\\DesktopStub\\Profiles\\"+context.installScope);}
    }
    if(!context.runtimeGeneration.empty()){
        std::wstring actual;if(!aip::named_objects::NormalizeFileIdentity(currentRoot,actual))return false;bool matches=false;
        auto roots=context.legacyBaseRoots;roots.push_back(context.baseRoot);
        for(const auto& root:roots){std::wstring expected;if(aip::named_objects::NormalizeFileIdentity(root+L"\\"+LowerAscii(context.runtimeId)+L"\\runtime\\"+LowerAscii(context.runtimeGeneration),expected)&&expected==actual){matches=true;break;}}
        if(!matches){error=L"The profile runtime is outside its owned per-user location.";return false;}
    }
    return true;
#endif
}
static aip::profiles::native::Store* ProfileManagerStore(std::wstring& error)
{
    if(g_profileStore)return g_profileStore.get();aip::profiles::native::Context context;if(!ProfileManagerContext(context,error))return nullptr;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testProfileOperations){error=L"Profile filesystem/process operations are inert in validation.";return nullptr;}
    auto candidate=std::make_unique<aip::profiles::native::Store>(std::move(context),*g_testProfileOperations);
#else
    auto candidate=std::make_unique<aip::profiles::native::Store>(std::move(context));
#endif
    if(!candidate->ImportLegacyRegistry(error))return nullptr;
    g_profileStore=std::move(candidate);
    return g_profileStore.get();
}
static bool ResolveManagedRuntimeDefaultIni(std::wstring& path,std::wstring& error)
{
    path.clear();auto* store=ProfileManagerStore(error);if(!store)return false;
    if(store->context.runtimeGeneration.empty())return true;
    path=store->context.runtimeIniPath.empty()?store->context.runtimeProfileKey:store->context.runtimeIniPath;
    return store->io.owner(path,store->context.ownerSid,error);
}
static bool ProfileManagerCurrentIniOwnerAllowed(const std::wstring& path,std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;
    // Missing files are handled by the normal fresh-default writer. Existing
    // files must be owned by this Windows SID before any settings are read.
#ifdef DESKTOPSTUB_INERT_HARDWARE
    std::wstring text;bool missing=false;if(!store->io.read(path,text,missing,error))return false;return missing||store->io.owner(path,store->context.ownerSid,error);
#else
    const auto attributes=GetFileAttributesW(path.c_str());if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();if(code!=ERROR_FILE_NOT_FOUND&&code!=ERROR_PATH_NOT_FOUND)return false;
        const auto parent=aip::profiles::native::Parent(path);HANDLE directory=CreateFileW(parent.c_str(),FILE_ADD_FILE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(directory==INVALID_HANDLE_VALUE){error=L"The default installation folder cannot hold a new profile for this Windows user.";return false;}CloseHandle(directory);return true;}
    return store->io.owner(path,store->context.ownerSid,error);
#endif
}
static bool EnsureCurrentProfileIdentity(std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;std::wstring path,key;
    {std::lock_guard<std::mutex> lock(g_pathMutex);path=g_iniPath;}
    if(!aip::named_objects::NormalizeFileIdentity(path,key)||!ProfileManagerCurrentIniOwnerAllowed(path,error))return false;
    std::wstring mainKey;aip::named_objects::NormalizeFileIdentity(store->context.mainIniPath,mainKey);
    std::optional<aip::profiles::PackageIdentity> legacy;
    if(key==mainKey&&IniReadS(L"ProfileIdentity",L"Id",L"").empty()){const auto manifest=store->context.sourceRoot+L"\\AppxManifest.xml";std::wstring text;bool missing=false;
        bool exists=false;if(!store->io.exists||!store->io.exists(manifest,exists,error))return false;
        if(exists){if(!store->io.owner(manifest,store->context.ownerSid,error)||!store->io.read(manifest,text,missing,error)||missing)return false;
            aip::profiles::PackageIdentity identity;
            if(!ExtractXmlAttributeFromTag(text,L"<Identity",L"Name",identity.name)||!ExtractXmlAttributeFromTag(text,L"<Identity",L"Publisher",identity.publisher)||!aip::profiles::Valid(identity)){error=L"The existing main package manifest has no valid identity to preserve.";return false;}
            bool adopt=false;if(!aip::profiles::native::CanAdoptOwnedManifest(store->io,identity,store->context.sourceRoot,adopt,error))return false;if(adopt)legacy=std::move(identity);}}
    aip::profiles::IdentityRecord identity;if(!store->Identity(path,key,legacy,identity,error))return false;
    if(!store->context.runtimeGeneration.empty()&&(store->context.runtimeId!=identity.id||store->context.runtimeProfileKey!=key||store->context.runtimeGeneration!=aip::profiles::native::IdentityGeneration(identity))){error=L"This runtime belongs to an older profile identity. Start the current profile from Profiles; its existing pins were kept.";return false;}
    aip::profiles::ProfileEntry entry{identity.id,key==mainKey?L"Main":L"Profile",path,key,false};
    aip::profiles::Registry registry;if(!store->Load(registry,error))return false;
    const auto existing=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const auto& row){return row.id==entry.id;});
    if(existing!=registry.profiles.end()){entry.name=existing->name;entry.autoStart=existing->autoStart;}
    if(!store->Enroll(entry,error))return false;
    LogText((L"[i] Shared profile coordinator: "+store->RegistryPath()+L"; profile="+entry.id).c_str());
    // Auto may fall back to legacy balloons, which require this resident's icon.
    // An explicit Toast preference is the capability boundary for hidden icons.
    const bool managed=!store->context.runtimeGeneration.empty();
    const bool defaultProfile=registry.defaultProfileId.empty()||entry.id==registry.defaultProfileId;
    const bool toast=aip::notifications::ParseApi(IniReadS(L"Settings",L"NotificationApi",L"Auto"))==aip::notifications::Api::Toast;
    g_profileSuppressTray=aip::profiles::SuppressTrayForProfile(registry.unifiedTray,managed,defaultProfile,toast);
    static bool warned=false;
    if(registry.unifiedTray&&managed&&!defaultProfile&&!toast&&!warned){warned=true;QueueStartupWarning(L"This profile keeps its tray icon because Auto and Balloon notifications need it. Select the Toast notification API to hide its icon in unified mode, then restart the profile.");}
    return true;
}
struct ProfileChildWatch {HANDLE process=nullptr,wait=nullptr;std::wstring id,name;std::atomic<bool> expected{false};DWORD code=0;bool coordinator=false;};
static std::mutex g_profileChildMutex;
static std::map<std::wstring,std::shared_ptr<ProfileChildWatch>> g_profileChildren;
static std::deque<std::shared_ptr<ProfileChildWatch>> g_profileExited;
static VOID CALLBACK ProfileChildExited(PVOID raw,BOOLEAN)noexcept
{
    try{auto* child=static_cast<ProfileChildWatch*>(raw);DWORD code=0;if(!GetExitCodeProcess(child->process,&code))return;
        std::lock_guard<std::mutex> lock(g_profileChildMutex);const auto found=g_profileChildren.find(child->id);
        if(found==g_profileChildren.end()||found->second.get()!=child)return;child->code=code;g_profileExited.push_back(found->second);
        if(g_hwnd)PostMessageW(g_hwnd,WM_APP_PROFILE_CHILD_EXIT,0,0);
    }catch(...){}
}
static void ClearProfileManager()
{
    std::vector<std::shared_ptr<ProfileChildWatch>> children;
    {std::lock_guard<std::mutex> lock(g_profileChildMutex);for(auto& row:g_profileChildren)children.push_back(row.second);g_profileChildren.clear();g_profileExited.clear();}
    for(const auto& child:children){if(child->wait)UnregisterWaitEx(child->wait,INVALID_HANDLE_VALUE);if(child->process)CloseHandle(child->process);}

}
static bool HandleProfileManagerMessage(UINT message)
{
    if(message!=WM_APP_PROFILE_CHILD_EXIT)return false;
    std::deque<std::shared_ptr<ProfileChildWatch>> exited;
    {std::lock_guard<std::mutex> lock(g_profileChildMutex);exited.swap(g_profileExited);for(const auto& child:exited)g_profileChildren.erase(child->id);}
    for(const auto& child:exited){if(child->wait)UnregisterWaitEx(child->wait,INVALID_HANDLE_VALUE);if(child->process)CloseHandle(child->process);
        // Ordinary application exit codes (including a handled failure 1) do
        // not prove a crash. Report unhandled native failure statuses only.
        if((child->code&0x80000000u)&&!child->expected.load())NotifyUserOperation(L"profile-crash-"+child->id,child->coordinator?L"The DesktopStub coordinator stopped unexpectedly":L"A DesktopStub profile stopped unexpectedly",child->name+L" exited with code "+std::to_wstring(child->code)+L". Profile settings were kept; open Profiles to restart it.",NIIF_WARNING,false);}
    return true;
}
static bool WriteProfileCoordinatorWatch(aip::profiles::native::Store& store,const aip::profiles::ProfileEntry& entry,std::wstring& error)
{
    if(!g_hwnd)return true;FILETIME created{},exited{},kernel{},user{};if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))return false;
    aip::profiles::CoordinatorPeer peer;peer.ownerSid=store.context.ownerSid;peer.installScope=store.context.installScope;peer.childId=entry.id;peer.processId=GetCurrentProcessId();peer.creationTime=(static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime;
    {std::lock_guard<std::mutex> lock(g_pathMutex);peer.executable=g_exePath;}
    const auto text=aip::profiles::SerializePeer(peer),path=store.ProfileRoot(entry)+L"\\parent.watch";bool exists=false;
    return !text.empty()&&store.io.directory(store.ProfileRoot(entry),error)&&store.io.exists&&store.io.exists(path,exists,error)&&(!exists||store.io.owner(path,store.context.ownerSid,error))&&store.io.write(path,text,false,error);
}
static bool ControlledProfileProcessPath(aip::profiles::native::Store& store,const std::wstring& executable)
{
    using namespace aip::profiles;using namespace aip::profiles::native;
    std::wstring key,source;if(!aip::named_objects::NormalizeFileIdentity(executable,key)||!aip::named_objects::NormalizeFileIdentity(store.context.sourceRoot+L"\\DesktopStub.exe",source))return false;if(key==source)return true;
    const auto root=Parent(executable),marker=root+L"\\runtime.owner";std::wstring text,error;bool missing=false;
    if(!store.io.owner(marker,store.context.ownerSid,error)||!store.io.read(marker,text,missing,error)||missing||text.size()>262144)return false;
    std::wistringstream input(text);std::wstring version,owner,scope,id,profile,origin,generation,exactIni,extra;
    if(!(input>>version>>std::quoted(owner)>>std::quoted(scope)>>std::quoted(id)>>std::quoted(profile)>>std::quoted(origin)>>std::quoted(generation))||version!=L"AIP_PROFILE_RUNTIME_2"||owner!=store.context.ownerSid||scope!=store.context.installScope||!Hex(id,32)||!Hex(generation,16)||!AbsoluteIniPath(profile))return false;
    input>>std::ws;if(!input.eof()){std::wstring normalized;if(!(input>>std::quoted(exactIni))||input>>extra||!AbsoluteIniPath(exactIni)||!aip::named_objects::NormalizeFileIdentity(exactIni,normalized)||normalized!=profile)return false;}
    std::wstring expected,actualOrigin,sourceOrigin;
    return aip::named_objects::NormalizeFileIdentity(store.context.baseRoot+L"\\"+LowerAscii(id)+L"\\runtime\\"+LowerAscii(generation)+L"\\DesktopStub.exe",expected)&&expected==key&&
        aip::named_objects::NormalizeFileIdentity(origin,actualOrigin)&&aip::named_objects::NormalizeFileIdentity(store.context.sourceRoot,sourceOrigin)&&actualOrigin==sourceOrigin;
}
static void InstallProfileCoordinatorWatch()
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testProfileContext)return;
#endif
    if(!g_hwnd)return;std::wstring error;auto* store=ProfileManagerStore(error);if(!store)return;
    aip::profiles::Registry registry;if(!store->Load(registry,error)||!registry.notifyCrashes)return;
    std::wstring key;{std::lock_guard<std::mutex> lock(g_pathMutex);if(!aip::named_objects::NormalizeFileIdentity(g_iniPath,key))return;}
    const auto entry=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const auto& item){return item.profileKey==key;});if(entry==registry.profiles.end())return;
    const auto path=store->ProfileRoot(*entry)+L"\\parent.watch";bool exists=false;if(!store->io.exists||!store->io.exists(path,exists,error)||!exists||!store->io.owner(path,store->context.ownerSid,error))return;
    std::wstring text;bool missing=false;aip::profiles::CoordinatorPeer peer;
    if(!store->io.read(path,text,missing,error)||missing||!aip::profiles::ParsePeer(text,peer)||peer.processId==GetCurrentProcessId())return;
    DWORD session=0,currentSession=0;if(!ProcessIdToSessionId(peer.processId,&session)||!ProcessIdToSessionId(GetCurrentProcessId(),&currentSession)||session!=currentSession)return;
    HANDLE process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,peer.processId);if(!process)return;
    const auto close=[&]{CloseHandle(process);};
    FILETIME created{},exited{},kernel{},user{};DWORD length=32768;std::vector<wchar_t> image(length);
    if(!aip::profiles::native::SameUserProcess(process)||!GetProcessTimes(process,&created,&exited,&kernel,&user)||!QueryFullProcessImageNameW(process,0,image.data(),&length)){close();return;}
    std::wstring executable(image.data(),length),normalized,claimed;
    if(!aip::named_objects::NormalizeFileIdentity(executable,normalized)||!aip::named_objects::NormalizeFileIdentity(peer.executable,claimed)||normalized!=claimed||
        !aip::profiles::PeerMatches(peer,store->context.ownerSid,store->context.installScope,entry->id,peer.processId,(static_cast<ULONGLONG>(created.dwHighDateTime)<<32)|created.dwLowDateTime,executable)||!ControlledProfileProcessPath(*store,executable)){close();return;}
    auto watched=std::make_shared<ProfileChildWatch>();watched->process=process;watched->id=L"coordinator-"+std::to_wstring(peer.processId)+L"-"+std::to_wstring(peer.creationTime);watched->name=L"The coordinator that started this profile";watched->coordinator=true;
    {std::lock_guard<std::mutex> lock(g_profileChildMutex);if(g_profileChildren.count(watched->id)){close();return;}g_profileChildren.emplace(watched->id,watched);}
    if(!RegisterWaitForSingleObject(&watched->wait,process,ProfileChildExited,watched.get(),INFINITE,WT_EXECUTEONLYONCE)){std::lock_guard<std::mutex> lock(g_profileChildMutex);g_profileChildren.erase(watched->id);close();}
}
static bool StartManagedProfile(const aip::profiles::ProfileEntry& entry,const std::wstring& additional,std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;
    const bool already=store->io.window(entry.iniPath)!=nullptr;
    if(already&&additional.empty()){if(store->io.signal(store->io.window(entry.iniPath),12))return true;error=L"The running profile did not accept its Settings request.";return false;}
    aip::profiles::Registry preferences;if(!store->Load(preferences,error))return false;
    if(!already&&preferences.notifyCrashes&&!WriteProfileCoordinatorWatch(*store,entry,error))LogText(L"[!] The profile coordinator watch could not be prepared; the profile can still start.");
    DWORD pid=0;HANDLE process=nullptr;
    if(already&&store->UsesOriginalMainRuntime(entry)){
        if(!store->io.launch(store->context.sourceRoot+L"\\DesktopStub.exe",aip::profiles::LaunchArguments(entry)+(additional.empty()?L"":L" "+additional),pid,process,error))return false;
    }else if(already){std::wstring text;bool missing=false;const auto marker=store->RuntimeRoot(entry)+L"\\runtime.owner";
        if(!store->io.read(marker,text,missing,error)||missing||text!=store->OwnerText(entry)||!store->io.owner(marker,store->context.ownerSid,error)){error=L"The running profile has no verified managed runtime. Use its existing tray icon.";return false;}
        if(!store->io.launch(store->RuntimeExe(entry),aip::profiles::LaunchArguments(entry)+(additional.empty()?L"":L" "+additional),pid,process,error))return false;
    }else if(!store->Start(entry,additional,pid,process,error))return false;
    aip::profiles::Registry registry;
    if(!already&&store->Load(registry,error)&&registry.notifyCrashes&&g_hwnd){
        auto child=std::make_shared<ProfileChildWatch>();child->process=process;child->id=entry.id;child->name=entry.name;
        {std::lock_guard<std::mutex> lock(g_profileChildMutex);if(!g_profileChildren.count(entry.id))g_profileChildren.emplace(entry.id,child);else{CloseHandle(process);return true;}}
        if(RegisterWaitForSingleObject(&child->wait,process,ProfileChildExited,child.get(),INFINITE,WT_EXECUTEONLYONCE))return true;
        {std::lock_guard<std::mutex> lock(g_profileChildMutex);g_profileChildren.erase(entry.id);}
    }
    if(process)CloseHandle(process);return true;
}
static bool StopManagedProfile(const aip::profiles::ProfileEntry& entry,std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;
    std::shared_ptr<ProfileChildWatch> watched;{std::lock_guard<std::mutex> lock(g_profileChildMutex);const auto found=g_profileChildren.find(entry.id);if(found!=g_profileChildren.end()){watched=found->second;watched->expected=true;}}
    if(store->Stop(entry,error))return true;if(watched)watched->expected=false;return false;
}
static bool RouteDefaultProfileForCurrentUser(const std::wstring& path,const std::vector<std::wstring>& originalArgs,int& exitCode,std::wstring& error)
{
    exitCode=0;auto* store=ProfileManagerStore(error);if(!store){exitCode=2;return true;}
    std::vector<std::wstring> forwarded;if(!aip::profiles::ForwardArguments(originalArgs,forwarded,error)){exitCode=2;return true;}
    const bool ownerAllowed=ProfileManagerCurrentIniOwnerAllowed(path,error);
    // Only the normal default-profile launch calls this fallback. Help,
    // configure-only and an explicit --ini must never create/launch a profile.
    aip::profiles::Registry registry;if(!store->Load(registry,error)){exitCode=2;return true;}
    aip::profiles::ProfileEntry entry;
    if(!registry.defaultProfileId.empty()){if(!store->Find(registry.defaultProfileId,entry,error)){exitCode=2;return true;}
        std::wstring currentKey;if(ownerAllowed&&aip::named_objects::NormalizeFileIdentity(path,currentKey)&&currentKey==entry.profileKey)return false;
    }else if(ownerAllowed)return false;
    else if(aip::profiles::ExitRequested(forwarded))return true;
    else if(!store->Fresh(L"Main",L"",BuildInitialIniTemplate,entry,error)){exitCode=2;return true;}
    std::wstring extra;for(const auto& argument:forwarded){if(!extra.empty())extra+=L' ';extra+=aip::profiles::QuoteArgument(argument);}
    const auto existing=store->io.window(entry.iniPath);
    if(aip::profiles::OneShotArguments(forwarded,existing!=nullptr)){store->RunOneShot(entry,extra,exitCode,error,aip::profiles::RuntimeOneShotArguments(forwarded));return true;}
    if(extra.empty()&&existing){if(!store->io.signal(existing,14)){error=L"The selected default profile did not accept the duplicate-launch request.";exitCode=2;}return true;}
    if(!StartManagedProfile(entry,extra,error))exitCode=2;return true;
}
static bool RouteCurrentProfileToOwnedRuntime(const std::vector<std::wstring>& originalArgs,const std::function<void()>& releaseSingleton,int& exitCode,std::wstring& error)
{
    exitCode=0;auto* store=ProfileManagerStore(error);if(!store){exitCode=2;return true;}
    if(!store->context.runtimeGeneration.empty())return false;
    std::wstring path,key;{std::lock_guard<std::mutex> lock(g_pathMutex);path=g_iniPath;}
    if(!aip::named_objects::NormalizeFileIdentity(path,key)){exitCode=2;return true;}
    aip::profiles::Registry registry;if(!store->Load(registry,error)){exitCode=2;return true;}
    const auto found=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const auto& entry){return entry.profileKey==key;});
    if(found==registry.profiles.end()){error=L"The current profile is not enrolled in this user's profile list.";exitCode=2;return true;}
    if(store->UsesOriginalMainRuntime(*found))return false;
    std::vector<std::wstring> forwarded;if(!aip::profiles::ForwardArguments(originalArgs,forwarded,error,true)){exitCode=2;return true;}
    std::wstring extra;for(const auto& argument:forwarded){if(!extra.empty())extra+=L' ';extra+=aip::profiles::QuoteArgument(argument);}
    if(releaseSingleton)releaseSingleton();
    if(aip::profiles::OneShotArguments(forwarded,store->io.window(found->iniPath)!=nullptr)){store->RunOneShot(*found,extra,exitCode,error,aip::profiles::RuntimeOneShotArguments(forwarded));return true;}
    if(!StartManagedProfile(*found,extra,error))exitCode=2;return true;
}
static void StartConfiguredProfiles()
{
    std::wstring error;auto* store=ProfileManagerStore(error);if(!store)return;
    std::wstring current;{std::lock_guard<std::mutex> lock(g_pathMutex);current=g_iniPath;}
    std::wstring key;if(!aip::named_objects::NormalizeFileIdentity(current,key))return;
    aip::profiles::Registry registry;if(!store->Load(registry,error))return;
    const auto coordinator=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const auto& entry){return entry.id==registry.defaultProfileId;});
    if(coordinator==registry.profiles.end()||coordinator->profileKey!=key)return;
    for(const auto& entry:aip::profiles::StartupProfiles(registry))if(entry.profileKey!=key&&!store->io.window(entry.iniPath)&&!StartManagedProfile(entry,L"",error))LogText(L"[!] An enabled DesktopStub profile could not be started; open Profiles for details.");
}
static bool HandleProfileManagerArguments(const std::vector<std::wstring>& arguments,int& exitCode,std::wstring& error)
{
    using namespace aip::profiles;exitCode=0;std::wstring action,id,name,path,enabled;std::vector<std::wstring> remaining;
    bool profileArgument=false,help=false,version=false;
    for(std::size_t i=1;i<arguments.size();++i){const auto arg=LowerAscii(arguments[i]);
        profileArgument|=arg.compare(0,9,L"--profile")==0;
        help|=arg==L"--help"||arg==L"-h"||arg==L"/?"||arg==L"/help";
        version|=arg==L"--version"||arg==L"-v"||arg==L"/version";
    }
    // Help/version must win before a profile name/value is consumed, before
    // context discovery, and before any registry or factory creation action.
    if(profileArgument&&(help||version)){
        error.clear();ShowCommandLineMessage(help?CommandLineUsageText():CommandLineVersionText(),false);return true;
    }
    const auto take=[&](std::size_t& index,std::wstring& value){if(index+1>=arguments.size()){error=L"The profile option needs a value.";return false;}value=arguments[++index];return true;};
    for(std::size_t i=1;i<arguments.size();++i){const auto& arg=arguments[i];
        if(arg==L"--profile-manager")continue;
        if(arg==L"--profile-path"){if(!take(i,path)){exitCode=2;return true;}continue;}
        if(arg==L"--profile-list"||arg==L"--profile-create"||arg==L"--profile-start"||arg==L"--profile-stop"||arg==L"--profile-settings"||arg==L"--profile"||arg==L"--profile-default"||arg==L"--profile-autostart"||arg==L"--profiles-startup"||arg==L"--profiles-unified-tray"||arg==L"--profiles-crash-notifications"){
            if(!action.empty()){error=L"Run one profile management action at a time.";exitCode=2;return true;}action=arg;
            if(arg==L"--profile-list")continue;
            if(!take(i,arg==L"--profile-create"?name:(arg.compare(0,11,L"--profiles-")==0?enabled:id))){exitCode=2;return true;}
            if(arg==L"--profile-autostart"&&!take(i,enabled)){exitCode=2;return true;}
        }else remaining.push_back(arg);
    }
    if(action.empty())return false;
    if(action!=L"--profile"&&action!=L"--profile-start"&&action!=L"--profile-settings"&&!remaining.empty()){
        error=L"Unexpected arguments after this profile management action. Run settings or another action separately.";exitCode=2;return true;
    }
    if(!path.empty()&&action!=L"--profile-create"){error=L"--profile-path is only valid with --profile-create.";exitCode=2;return true;}
    for(const auto& arg:remaining){const auto lower=LowerAscii(arg);if(lower==L"--ini"||lower.compare(0,6,L"--ini=")==0){error=L"Choose either a registered --profile or an explicit --ini path.";exitCode=2;return true;}}
    auto* store=ProfileManagerStore(error);if(!store){exitCode=2;return true;}
    if(action==L"--profile-list"){
        Registry registry;if(!store->Load(registry,error)){exitCode=2;return true;}
        std::wstring output;for(const auto& entry:registry.profiles)output+=entry.id+L"  "+entry.name+L"  "+entry.iniPath+(entry.autoStart?L"  [startup]":L"")+L"\r\n";
        aip::WriteCommandLineText(output,false);return true;
    }
    if(action==L"--profile-create"){
        ProfileEntry entry;if(!store->Fresh(name,path,BuildInitialIniTemplate,entry,error)){exitCode=2;return true;}
        aip::WriteCommandLineText(entry.id+L"  "+entry.iniPath,false);return true;
    }
    ProfileEntry entry;
    if(!id.empty()&&!store->Find(id,entry,error)){exitCode=2;return true;}
    if(action==L"--profile-stop"){if(!StopManagedProfile(entry,error))exitCode=2;return true;}
    if(action==L"--profile"||action==L"--profile-start"||action==L"--profile-settings"){
        std::wstring extra=action==L"--profile-settings"?L"--settings":L"";
        for(const auto& arg:remaining){if(!extra.empty())extra+=L' ';extra+=QuoteArgument(arg);}
        if(OneShotArguments(remaining,store->io.window(entry.iniPath)!=nullptr)){store->RunOneShot(entry,extra,exitCode,error,RuntimeOneShotArguments(remaining));return true;}
        if(!StartManagedProfile(entry,extra,error))exitCode=2;return true;
    }
    const auto normalized=LowerAscii(enabled);const bool on=normalized==L"1"||normalized==L"on"||normalized==L"true";
    if(action!=L"--profile-default"&&!on&&normalized!=L"0"&&normalized!=L"off"&&normalized!=L"false"){error=L"Profile preferences require on or off.";exitCode=2;return true;}
    if(!store->Update([&](Registry& registry,std::wstring& detail){
        if(action==L"--profile-default")registry.defaultProfileId=entry.id;
        else if(action==L"--profile-autostart"){entry.autoStart=on;if(!AddOrUpdate(registry,entry,detail))return false;}
        else if(action==L"--profiles-startup")registry.startupEnabled=on;
        else if(action==L"--profiles-unified-tray")registry.unifiedTray=on;
        else if(action==L"--profiles-crash-notifications")registry.notifyCrashes=on;return true;
    },error))exitCode=2;
    return true;
}
static bool PlanProfileIdentityMigration(const aip::profiles::ProfileEntry& entry,aip::profiles::IdentityMode mode,
    const aip::profiles::PackageIdentity& staticIdentity,aip::profiles::IdentityPlan& plan,std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;
    if(!store->io.owner(entry.iniPath,store->context.ownerSid,error))return false;std::wstring text;bool missing=false;
    if(!store->io.read(entry.iniPath,text,missing,error)||missing)return false;
    const auto document=aip::ParseIniDocument(text);std::optional<aip::profiles::IdentityRecord> current;
    if(!aip::profiles::ReadIdentity([&](const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(document,L"ProfileIdentity",key,value)?value:std::wstring(fallback);},current,error)||!current){error=L"This profile has no persisted identity to migrate yet.";return false;}
    aip::profiles::IdentityRequest request;request.ownerSid=store->context.ownerSid;request.userScope=store->context.userScope;request.profileKey=entry.profileKey;request.stored=current;request.mode=mode;request.staticIdentity=staticIdentity;request.migrateAutomatic=mode==aip::profiles::IdentityMode::Auto;
    if(!aip::profiles::PlanIdentity(request,plan,error))return false;
    if(plan.status==aip::profiles::IdentityResolution::OwnerMismatch){error=L"The profile identity owner changed; migration was not prepared.";return false;}
    return true;
}
static bool (*g_profileIdentityMigrationHook)(HWND,const aip::profiles::ProfileEntry&,const aip::profiles::IdentityPlan&,std::wstring&)=nullptr;
static std::optional<aip::profiles::native::Migration> g_profilePreparedMigration;
static std::wstring g_profileRollbackRuntime;
static aip::profiles::ProfileEntry g_profileRollbackEntry;
static bool CurrentMigrationEntry(aip::profiles::native::Store& store,aip::profiles::ProfileEntry& entry,std::wstring& error)
{
    std::wstring path,key;{std::lock_guard<std::mutex> lock(g_pathMutex);path=g_iniPath;}
    if(!aip::named_objects::NormalizeFileIdentity(path,key)||!store.io.owner(path,store.context.ownerSid,error))return false;
    std::wstring text;bool missing=false;if(!store.io.read(path,text,missing,error)||missing)return false;
    std::optional<aip::profiles::IdentityRecord> identity;if(!aip::profiles::native::IdentityFromText(text,identity,error)||!identity)return false;
    if(!store.Find(identity->id,entry,error)||entry.profileKey!=key){error=L"The current profile no longer matches its migration registration.";return false;}return true;
}
static std::wstring CurrentProfileRuntimeExe(const aip::profiles::native::Store& store)
{
    const auto& c=store.context;return c.runtimeGeneration.empty()?c.sourceRoot+L"\\DesktopStub.exe":c.baseRoot+L"\\"+aip::profiles::LowerAscii(c.runtimeId)+L"\\runtime\\"+aip::profiles::LowerAscii(c.runtimeGeneration)+L"\\DesktopStub.exe";
}
static bool PrepareProfileIdentityMigration(HWND owner,const aip::profiles::ProfileEntry& entry,const aip::profiles::IdentityPlan& plan,bool& needsCurrentShutdown,std::wstring& error)
{
    (void)owner;needsCurrentShutdown=false;auto* store=ProfileManagerStore(error);if(!store)return false;
    if(g_profilePreparedMigration||!g_profileRollbackRuntime.empty()){error=L"Another profile identity migration is already pending.";return false;}
    aip::profiles::ProfileEntry verified;if(!store->Find(entry.id,verified,error)||verified.profileKey!=entry.profileKey)return false;
    const auto running=store->io.window(entry.iniPath);if(running&&running!=g_hwnd){error=L"This profile is running in another resident. Open its Settings to migrate it there, or stop it first.";return false;}
    if(aip::profiles::Same(plan.current.package,plan.proposed.package)){
        return store->io.locked(entry.iniPath,[&]{std::wstring text;bool missing=false;if(!store->io.owner(entry.iniPath,store->context.ownerSid,error)||!store->io.read(entry.iniPath,text,missing,error)||missing)return false;
            std::optional<aip::profiles::IdentityRecord> current;if(!aip::profiles::native::IdentityFromText(text,current,error)||!current||!aip::profiles::native::SameRecord(*current,plan.current)){error=L"The identity changed after approval; reopen its settings.";return false;}
            std::wstring managed;if(!aip::ReadIniValueFromDoc(aip::ParseIniDocument(text),L"Profile",L"ManagedRuntime",managed))managed=L"0";
            return aip::profiles::native::ApplyIdentity(text,plan.proposed,managed,error)&&store->io.write(entry.iniPath,text,false,error);
        },error);
    }
    aip::profiles::native::Migration migration;aip::profiles::native::MigrationEngine engine(*store);
    if(!engine.Prepare(entry,plan,migration,error))return false;g_profilePreparedMigration=std::move(migration);needsCurrentShutdown=running==g_hwnd&&running!=nullptr;return true;
}
static bool FinalizePendingProfileIdentityMigration(std::wstring& error)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testProfileContext)return true;
#endif
    auto* store=ProfileManagerStore(error);if(!store)return false;
    if(!g_profileRollbackRuntime.empty()){
        DWORD pid=0;HANDLE process=nullptr;if(!store->io.launch(g_profileRollbackRuntime,aip::profiles::LaunchArguments(g_profileRollbackEntry),pid,process,error))return false;
        if(store->io.close)store->io.close(process);if(store->io.erase){std::wstring ignored;store->io.erase(aip::profiles::native::MigrationPath(g_profileRollbackEntry),ignored);}g_profileRollbackRuntime.clear();return true;
    }
    if(!g_profilePreparedMigration)return true;
    auto& migration=*g_profilePreparedMigration;
    if(store->io.window(migration.entry.iniPath)){error=L"The old profile resident must release its window and single-instance mutex before migration relaunch.";return false;}
    aip::profiles::native::MigrationEngine engine(*store);
    if(!engine.Commit(migration,error)){
        aip::profiles::IdentityRecord current;std::wstring detail;
        if(store->CurrentIdentity(migration.entry,current,detail)&&aip::profiles::native::SameRecord(current,migration.before)){
            DWORD oldPid=0;HANDLE oldProcess=nullptr;
            if(store->io.launch(migration.oldRuntime,aip::profiles::LaunchArguments(migration.entry),oldPid,oldProcess,detail)){
                if(store->io.close)store->io.close(oldProcess);error+=L" The previous runtime was restarted with the current settings.";
            }else error+=L" The previous settings were kept, but its runtime could not restart. "+detail;
        }
        g_profilePreparedMigration.reset();return false;
    }
    DWORD pid=0;HANDLE process=nullptr;
    if(!store->io.launch(migration.newRuntime,aip::profiles::LaunchArguments(migration.entry)+L" --setup-desktop",pid,process,error)){
        std::wstring rollbackError;
        if(!engine.Rollback(migration,rollbackError))error+=L" "+rollbackError;
        else if(store->io.launch(migration.oldRuntime,aip::profiles::LaunchArguments(migration.entry),pid,process,rollbackError)){
            if(store->io.close)store->io.close(process);if(store->io.erase){std::wstring ignored;store->io.erase(aip::profiles::native::MigrationPath(migration.entry),ignored);}error+=L" The previous identity was restored and its runtime restarted.";
        }else error+=L" The previous settings were restored, but its runtime could not restart. "+rollbackError;
        g_profilePreparedMigration.reset();return false;
    }
    if(store->io.close)store->io.close(process);g_profilePreparedMigration.reset();return true;
}
static bool CompleteProfileIdentityMigration(bool success,bool& needsRelaunch,std::wstring& error)
{
    needsRelaunch=false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testProfileContext)return true;
#endif
    auto* store=ProfileManagerStore(error);if(!store)return false;
    std::wstring path;{std::lock_guard<std::mutex> lock(g_pathMutex);path=g_iniPath;}
    bool exists=false;if(!store->io.exists||!store->io.exists(path+L".profile-identity-migration",exists,error))return false;if(!exists)return true;
    aip::profiles::ProfileEntry entry;if(!CurrentMigrationEntry(*store,entry,error))return false;
    aip::profiles::native::MigrationEngine engine(*store);aip::profiles::native::Migration migration;bool present=false;
    if(!engine.Load(entry,migration,present,error))return false;if(!present)return true;
    if(migration.phase==aip::profiles::native::MigrationPhase::Complete||migration.phase==aip::profiles::native::MigrationPhase::RolledBack)return true;
    if(!engine.Complete(migration,success,error))return false;
    if(!success){g_profileRollbackRuntime=migration.oldRuntime;g_profileRollbackEntry=migration.entry;needsRelaunch=CurrentProfileRuntimeExe(*store)!=migration.oldRuntime;if(!needsRelaunch)g_profileRollbackRuntime.clear();}
    return true;
}
static bool RecoverProfileIdentityMigration(bool setupRequested,bool& needsRelaunch,std::wstring& error)
{
    needsRelaunch=false;
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(!g_testProfileContext)return true;
#endif
    auto* store=ProfileManagerStore(error);if(!store)return false;
    std::wstring path;{std::lock_guard<std::mutex> lock(g_pathMutex);path=g_iniPath;}
    bool exists=false;if(!store->io.exists||!store->io.exists(path+L".profile-identity-migration",exists,error))return false;if(!exists)return true;
    aip::profiles::ProfileEntry entry;if(!CurrentMigrationEntry(*store,entry,error))return false;
    aip::profiles::native::MigrationEngine engine(*store);aip::profiles::native::Migration migration;bool present=false;
    if(!engine.Load(entry,migration,present,error))return false;if(!present)return true;
    aip::profiles::IdentityRecord current;if(!store->CurrentIdentity(entry,current,error))return false;
    if(migration.phase==aip::profiles::native::MigrationPhase::Complete){if(store->io.erase){std::wstring ignored;store->io.erase(aip::profiles::native::MigrationPath(entry),ignored);}return true;}
    if(migration.phase==aip::profiles::native::MigrationPhase::Prepared&&aip::profiles::native::SameRecord(current,migration.before)){
        if(store->io.erase){std::wstring ignored;store->io.erase(aip::profiles::native::MigrationPath(entry),ignored);}return true;
    }
    if(migration.phase==aip::profiles::native::MigrationPhase::AwaitingSetup&&setupRequested)return true;
    if(migration.phase==aip::profiles::native::MigrationPhase::AwaitingSetup){
        // A packaged relaunch with the exact proposed family proves that the
        // new registration succeeded, even if a final receipt write failed.
        UINT32 length=128;wchar_t family[128]{};std::wstring expected;
        if(GetCurrentPackageFamilyName(&length,family)==ERROR_SUCCESS&&aip::profiles::native::PackageFamily(migration.after.package,expected)&&_wcsicmp(family,expected.c_str())==0)return engine.Complete(migration,true,error);
    }
    if(migration.phase!=aip::profiles::native::MigrationPhase::RolledBack&&!engine.Rollback(migration,error))return false;
    if(CurrentProfileRuntimeExe(*store)!=migration.oldRuntime){g_profileRollbackRuntime=migration.oldRuntime;g_profileRollbackEntry=entry;needsRelaunch=true;}
    else if(store->io.erase){std::wstring ignored;store->io.erase(aip::profiles::native::MigrationPath(entry),ignored);}return true;
}
static void OfferFreshDesktopStubProfile(HWND owner)
{
    if(!IniReadI(L"Settings",L"AlreadyRunningProfilePrompt",1))return;
    ConfirmUserOperation(owner,L"separate-profile",L"DesktopStub is already running",L"Create a separate DesktopStub profile? It will have its own factory settings, tile identities and tray icon. Your running profile and saved accounts will stay separate.",[]{
        std::wstring name=L"New profile";if(ShowTextInputDialog(g_hwnd,L"Create a separate profile",L"Profile name:",name,false)!=IDOK)return;
        std::wstring error;auto* store=ProfileManagerStore(error);aip::profiles::ProfileEntry entry;
        if(!store||!store->Fresh(name,L"",BuildInitialIniTemplate,entry,error)||!StartManagedProfile(entry,L"",error))NotifyUserOperation(L"profile-create",L"Separate DesktopStub profile",error,NIIF_WARNING,false);
    });
}
static void ShowProfileIdentity(HWND owner,const aip::profiles::ProfileEntry& entry)
{
    using namespace aip::profiles;
    std::wstring error;IdentityPlan current;if(!PlanProfileIdentityMigration(entry,IdentityMode::Auto,{},current,error)){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Profile identity",MB_OK|MB_ICONWARNING);return;}
    HMENU menu=CreatePopupMenu();AppendMenuW(menu,MF_STRING,1,L"Identity status...");AppendMenuW(menu,MF_STRING,2,L"Migrate to automatic identity...");AppendMenuW(menu,MF_STRING,3,L"Use a static identity...");POINT cursor{};GetCursorPos(&cursor);const auto action=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,cursor.x,cursor.y,0,owner,nullptr);DestroyMenu(menu);if(!action)return;
    if(action==1){const auto status=current.current.preservedLegacy?L"Legacy identity preserved":current.current.mode==IdentityMode::Auto?L"Automatic profile identity":L"Static profile identity";
        const auto message=std::wstring(status)+L"\n\nName: "+current.current.package.name+L"\nPublisher: "+current.current.package.publisher+L"\n\nExisting Start pins retain this identity until an explicit migration.";DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),L"Profile identity",MB_OK|MB_ICONINFORMATION);return;}
    auto mode=IdentityMode::Auto;PackageIdentity fixed;
    if(action==3){mode=IdentityMode::Static;fixed=current.current.package;
        std::wstring requestedText,detail;bool missing=false;auto* store=ProfileManagerStore(detail);
        if(store&&store->io.owner(entry.iniPath,store->context.ownerSid,detail)&&store->io.read(entry.iniPath,requestedText,missing,detail)&&!missing){const auto doc=aip::ParseIniDocument(requestedText);std::wstring value;
            if(aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPackageName",value)&&!value.empty())fixed.name=value;
            if(aip::ReadIniValueFromDoc(doc,L"Profile",L"StaticPublisher",value)&&!value.empty())fixed.publisher=value;}
        if(ShowTextInputDialog(owner,L"Static package identity",L"Package name (3–50 ASCII letters, digits, dots or dashes):",fixed.name,false)!=IDOK||ShowTextInputDialog(owner,L"Static package Publisher",L"Publisher distinguished name (for example CN=My Publisher):",fixed.publisher,false)!=IDOK)return;
        if(!PlanProfileIdentityMigration(entry,mode,fixed,current,error)){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,error.c_str(),L"Profile identity",MB_OK|MB_ICONWARNING);return;}}
    if(current.status==IdentityResolution::Ready&&!current.pinMigration&&!current.requiresWrite){DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,L"This profile already uses the selected package identity.",L"Profile identity",MB_OK|MB_ICONINFORMATION);return;}
    const auto prompt=L"Current name: "+current.current.package.name+L"\nCurrent Publisher: "+current.current.package.publisher+L"\n\nProposed name: "+current.proposed.package.name+L"\nProposed Publisher: "+current.proposed.package.publisher+L"\n\nWindows treats this as a different app. Existing Start pins cannot be converted in place; pin the new identity after setup succeeds. Existing pins and settings will be kept. Prepare this migration?";
    if(!g_profileIdentityMigrationHook){const auto message=prompt+L"\n\nMigration is not applied by this build's coordinator yet. Create a new profile to use an independent automatic identity; the current identity remains unchanged.";DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),L"Profile identity migration unavailable",MB_OK|MB_ICONINFORMATION);return;}
    ConfirmUserOperation(g_hwnd,L"profile-identity-"+entry.id,L"Migrate profile identity",prompt,[entry,current]{std::wstring detail;auto* store=ProfileManagerStore(detail);ProfileEntry verified;
        if(store&&store->Find(entry.id,verified,detail)&&verified.profileKey==entry.profileKey&&g_profileIdentityMigrationHook&&!g_profileIdentityMigrationHook(g_hwnd,verified,current,detail))NotifyUserOperation(L"profile-identity-"+entry.id,L"Profile identity migration",detail,NIIF_WARNING,false);});
}
#ifdef DESKTOPSTUB_INERT_HARDWARE
static std::function<bool(const aip::profiles::ProfileEntry&,const std::wstring&,std::wstring&)> g_testProfileUiStart;
#endif
static bool ReadProfileManagerUi(aip::profiles::UiSnapshot& output,std::wstring& error)
{
    auto* store=ProfileManagerStore(error);if(!store)return false;
    aip::profiles::UiSnapshot snapshot;if(!store->Load(snapshot.registry,error))return false;
    snapshot.revision=std::make_shared<const std::wstring>(aip::profiles::SerializeRegistry(snapshot.registry));
    for(const auto& profile:snapshot.registry.profiles)if(store->io.window(profile.iniPath))snapshot.running.insert(profile.id);
    output=std::move(snapshot);error.clear();return true;
}
static bool ExecuteProfileManagerUi(HWND owner,const aip::profiles::UiAction& action,std::wstring& status)
{
    using namespace aip::profiles;
    const auto stale=[&]{status=L"Profiles changed since this page loaded. Reload before applying another action.";return false;};
    if(action.verb==UiVerb::None||!action.revision||action.revision->empty())return stale();
    auto* store=ProfileManagerStore(status);if(!store)return false;Registry registry;
    if(!store->Load(registry,status))return false;if(SerializeRegistry(registry)!=*action.revision)return stale();
    if(action.verb==UiVerb::Create){
        std::wstring name=L"New profile";
        if(ShowTextInputDialog(owner,L"Create profile",L"Profile name:",name,false)!=IDOK){status=L"Profile creation cancelled.";return true;}
        Registry current;if(!store->Load(current,status))return false;if(SerializeRegistry(current)!=*action.revision)return stale();
        ProfileEntry entry;if(!store->Fresh(name,L"",BuildInitialIniTemplate,entry,status))return false;
        status=L"Created "+entry.name+L" with independent factory settings.";return true;
    }
    if(action.verb==UiVerb::Startup||action.verb==UiVerb::UnifiedTray||action.verb==UiVerb::CrashNotices){
        if(!store->Update([&](Registry& current,std::wstring& issue){
            if(SerializeRegistry(current)!=*action.revision){issue=L"Profiles changed. Reload before changing a preference.";return false;}
            if(action.verb==UiVerb::Startup)current.startupEnabled=!action.checked;
            else if(action.verb==UiVerb::UnifiedTray)current.unifiedTray=!action.checked;
            else current.notifyCrashes=!action.checked;return true;
        },status))return false;
        status=action.verb==UiVerb::UnifiedTray?L"Tray layout preference saved; it applies after profiles restart.":L"Profile preference saved.";return true;
    }
    const auto found=std::find_if(registry.profiles.begin(),registry.profiles.end(),[&](const ProfileEntry& item){return item.id==action.expected.id;});
    if(found==registry.profiles.end()||found->profileKey!=action.expected.profileKey||found->iniPath!=action.expected.iniPath)return stale();
    const auto entry=*found;
    if(action.verb==UiVerb::Identity){ShowProfileIdentity(owner,entry);status=L"Identity information opened. Any migration still requires its own confirmation.";return true;}
    if(action.verb==UiVerb::Start||action.verb==UiVerb::Settings){
        if(store->io.window(entry.iniPath)){
            ConfirmUserOperation(g_hwnd,L"profile-running-"+entry.id,L"Profile already running",
                entry.name+L" is already running with its own settings and tiles. Open that profile's Settings?",[entry]{
                    std::wstring detail;auto* current=ProfileManagerStore(detail);ProfileEntry verified;
                    if(current&&current->Find(entry.id,verified,detail)&&verified.profileKey==entry.profileKey){
                        const auto target=current->io.window(verified.iniPath);if(target)current->io.signal(target,12);
                    }
                });
            status=L"Confirmation requested to open the running profile's settings.";return true;
        }
        const std::wstring extra=action.verb==UiVerb::Settings?L"--settings":L"";
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(g_testProfileUiStart){if(!g_testProfileUiStart(entry,extra,status))return false;}
        else
#endif
        if(!StartManagedProfile(entry,extra,status))return false;
        status=L"Start requested for "+entry.name+L".";return true;
    }
    if(action.verb==UiVerb::Stop){
        if(!StopManagedProfile(entry,status))return false;status=L"Stop requested for "+entry.name+L".";return true;
    }
    if(action.verb==UiVerb::AutoStart||action.verb==UiVerb::MakeDefault){
        if(!store->Update([&](Registry& current,std::wstring& issue){
            if(SerializeRegistry(current)!=*action.revision){issue=L"Profiles changed. Reload before editing this profile.";return false;}
            auto selected=std::find_if(current.profiles.begin(),current.profiles.end(),[&](const ProfileEntry& item){return item.id==entry.id;});
            if(selected==current.profiles.end()||selected->profileKey!=entry.profileKey){issue=L"The selected profile changed; its preferences were kept.";return false;}
            if(action.verb==UiVerb::MakeDefault)current.defaultProfileId=entry.id;
            else selected->autoStart=!action.checked;return true;
        },status))return false;
        status=L"Profile preference saved.";return true;
    }
    status=L"This profile action is unavailable.";return false;
}
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testOpenProfilesSettings)(HWND)=nullptr;
#endif
static void ShowProfileManager(HWND owner)
{
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testOpenProfilesSettings){g_testOpenProfilesSettings(owner);return;}
#endif
    if(!OpenDesktopStubProfilesSettings(owner))
        NotifyUserOperation(L"profile-properties",L"Profiles",L"The Properties window could not be opened. Existing profiles were kept.",NIIF_WARNING,false);
}
static void AppendProfilesMenu(HMENU parent){AppendMenuW(parent,MF_STRING,PROFILE_MANAGER_MENU,L"Profiles...");}
static bool DispatchProfileManagerCommand(HWND owner,UINT command){if(command!=PROFILE_MANAGER_MENU)return false;ShowProfileManager(owner);return true;}
static bool IsProfileManagerDialogMessage(MSG*){return false;}

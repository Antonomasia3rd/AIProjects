// Queries the supported per-updater permission value, never Windows' private
// Start database. Unknown/missing/updater failures are not treated as disabled.
#include "../tile_delivery_warning.h"
struct LiveTilePermissionState{aip::delivery_warning::State observed;int setting=-1;std::wstring diagnostic;};
static std::map<std::wstring,LiveTilePermissionState> g_liveTilePermissions;
static std::wstring g_liveTilePermissionsProfile;
static void PruneLiveTilePermissions(const std::set<std::wstring>& active){
    for(auto i=g_liveTilePermissions.begin();i!=g_liveTilePermissions.end();)if(!active.count(i->first))i=g_liveTilePermissions.erase(i);else ++i;
}
#ifdef DESKTOPSTUB_INERT_HARDWARE
static int (*g_testLiveTilePermission)(const std::wstring&)=nullptr;
static void (*g_testLiveTileDisabledNotice)(const std::wstring&,const std::wstring&)=nullptr;
#endif
static int QueryLiveTilePermission(const std::wstring& id){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testLiveTilePermission?g_testLiveTilePermission(id):-1;
#else
    if(!CurrentProcessHasPackageIdentity())return -1;
    const HRESULT initialized=RoInitialize(RO_INIT_MULTITHREADED);struct Release{HRESULT h;~Release(){if(SUCCEEDED(h))RoUninitialize();}}release{initialized};
    if(FAILED(initialized)&&initialized!=RPC_E_CHANGED_MODE)return -1;
    try{
        using namespace winrt::Windows::UI::Notifications;
        const auto factory=aip::AcquireUncachedActivationFactory<TileUpdateManager,ITileUpdateManagerStatics>();
        const auto updater=id.empty()?factory.CreateTileUpdaterForApplication():factory.CreateTileUpdaterForSecondaryTile(id);
        return static_cast<int>(updater.Setting());
    }catch(...){return -1;}
#endif
}
static std::wstring ObserveLiveTilePermission(const aip::content::Snapshot& frame){
    using namespace aip::delivery_warning;
    if(TileSnapshotUsesRegistration(frame)||frame.appLauncherSource||frame.artworkTopic==L"previous"||frame.artworkTopic==L"next")return {};
    const auto mode=frame.policy.Read(L"Settings",L"LiveTileDisabledWarning",L"Startup");
    Policy policy;policy.mode=mode==L"Off"?Mode::Off:mode==L"OnChange"?Mode::OnChange:Mode::Startup;
    const auto interval=frame.policy.Read(L"Settings",L"LiveTileDisabledCheckSeconds",L"300");
    try{policy.intervalSeconds=static_cast<unsigned>(std::stoul(interval));}catch(...){policy.intervalSeconds=300;}
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=aip::MakeAbsolutePath(g_iniPath);}
    if(profile!=g_liveTilePermissionsProfile){g_liveTilePermissions.clear();g_liveTilePermissionsProfile=profile;}
    if(policy.mode==Mode::Off){g_liveTilePermissions.erase(frame.secondaryTileId);return {};}
    auto& state=g_liveTilePermissions[frame.secondaryTileId];const auto now=GetTickCount64();
    if(Due(state.observed,policy,now)){
        const auto setting=QueryLiveTilePermission(frame.secondaryTileId);
        if(setting<0){state.observed.nextCheck=now+static_cast<std::uint64_t>(policy.intervalSeconds)*1000;return state.diagnostic;}
        const auto warn=Observe(state.observed,policy,now,setting);state.setting=setting;
        state.diagnostic=setting?L"Windows reports Live Tile updates are "+std::wstring(Reason(setting))+L".":std::wstring{};
        if(warn){const auto title=IniReadS(L"Strings",L"LiveTileDisabledTitle",L"Live Tile updates are disabled");
            const auto body=frame.name+L": "+state.diagnostic+L" "+IniReadS(L"Strings",L"LiveTileDisabledHelp",L"If this is intentional, turn off this tile's disabled-update warning in Delivery settings.");
#ifdef DESKTOPSTUB_INERT_HARDWARE
            if(g_testLiveTileDisabledNotice)g_testLiveTileDisabledNotice(title,body);
#else
            NotifyUserOperation(L"tile-disabled-"+frame.secondaryTileId,title,body,NIIF_WARNING,false);
#endif
        }
    }
    return state.diagnostic;
}

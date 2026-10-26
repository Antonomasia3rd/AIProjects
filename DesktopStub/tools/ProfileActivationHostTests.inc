// Tests the legacy activation adapter without starting a package, process or
// registration helper. A managed runtime resolves its INI before this adapter.
static void TestProfilesPropertiesHostBackend()
{
    using namespace aip::profiles;using namespace aip::profiles::native;
    auto previous=std::move(g_profileStore);const auto previousStart=g_testProfileUiStart;
    struct RestoreProfileUiStore{std::unique_ptr<Store> store;decltype(g_testProfileUiStart) start;~RestoreProfileUiStore(){g_profileStore=std::move(store);g_testProfileUiStart=std::move(start);}}restore{std::move(previous),previousStart};
    Context context;context.ownerSid=L"SID:S-1-5-21-111-222-333-1001";context.userScope=L"0123456789abcdef";
    context.installScope=L"fedcba9876543210";context.sourceRoot=L"D:\\ProfileUiFixture";context.baseRoot=L"D:\\ProfileUiFixture\\Coordinator";
    context.mainIniPath=L"D:\\ProfileUiFixture\\Main.ini";
    const auto registryPath=context.baseRoot+L"\\profiles.ini";
    Registry registry;registry.ownerSid=context.ownerSid;
    ProfileEntry first{L"11111111111111111111111111111111",L"Main",context.mainIniPath,L"D:\\PROFILEUIFIXTURE\\MAIN.INI",false};
    ProfileEntry second{L"22222222222222222222222222222222",L"Extra",L"D:\\ProfileUiFixture\\Extra.ini",L"D:\\PROFILEUIFIXTURE\\EXTRA.INI",true};
    registry.profiles={first,second};registry.defaultProfileId=first.id;std::wstring stored=SerializeRegistry(registry);
    unsigned reads=0,writes=0,starts=0,signals=0;bool foreign=false,failWrite=false,race=false;std::wstring startedId,startedArgs;
    Operations io;io.exists=[&](const auto& path,bool& exists,auto&){exists=path==registryPath;return true;};
    io.owner=[&](const auto&,const auto&,auto& error){if(foreign){error=L"Another owner";return false;}return true;};
    io.read=[&](const auto& path,auto& text,bool& missing,auto&){++reads;missing=path!=registryPath;text=missing?L"":stored;return true;};
    io.write=[&](const auto& path,const auto& text,bool,auto& error){if(failWrite||path!=registryPath){error=L"Injected registry write failure";return false;}++writes;stored=text;return true;};
    io.directory=[](const auto&,auto&){return true;};
    io.locked=[&](const auto&,const auto& action,auto&){if(race){Registry changed;std::wstring issue;ParseRegistry(stored,context.ownerSid,changed,issue);changed.notifyCrashes=!changed.notifyCrashes;stored=SerializeRegistry(changed);race=false;}return action();};
    io.window=[&](const auto& path){return path==first.iniPath?reinterpret_cast<HWND>(static_cast<INT_PTR>(1)):nullptr;};
    io.signal=[&](HWND,WPARAM request){++signals;return request==2;};
    g_profileStore=std::make_unique<Store>(context,std::move(io));
    g_testProfileUiStart=[&](const ProfileEntry& entry,const std::wstring& extra,std::wstring&){++starts;startedId=entry.id;startedArgs=extra;return true;};
    const auto choose=[](const UiSnapshot& snapshot,UiVerb verb,const std::wstring& id=L""){
        const auto rows=BuildUiRows(snapshot,[](const wchar_t*,const wchar_t* fallback){return std::wstring(fallback);});
        const auto find=[&](const auto& self,const std::vector<UiRow>& nodes)->UiAction{
            for(const auto& node:nodes){if(node.action.verb==verb&&node.action.expected.id==id)return node.action;
                const auto child=self(self,node.children);if(child.verb!=aip::profiles::UiVerb::None)return child;}return {};
        };return find(find,rows);
    };
    UiSnapshot view;std::wstring status;
    Check(ReadProfileManagerUi(view,status)&&view.registry.profiles.size()==2&&view.running.count(first.id)&&!view.running.count(second.id)&&writes==0,
        "Profiles Properties inventory is read-only and reports both independent profiles");
    Check(!view.registry.unifiedTray&&!view.registry.startupEnabled&&!view.registry.notifyCrashes,
        "integrated Profiles inventory keeps separate tray and opt-in coordinator defaults");
    auto settings=choose(view,UiVerb::Settings,second.id);
    Check(ExecuteProfileManagerUi(nullptr,settings,status)&&starts==1&&startedId==second.id&&startedArgs==L"--settings"&&writes==0,
        "Properties opens the selected profile's settings through its verified start backend");
    auto forged=settings;forged.expected.profileKey=L"D:\\OTHER\\PROFILE.INI";
    Check(!ExecuteProfileManagerUi(nullptr,forged,status)&&starts==1&&writes==0,
        "a mismatched captured profile path cannot reach the start backend");
    const auto unified=choose(view,UiVerb::UnifiedTray);
    Check(ExecuteProfileManagerUi(nullptr,unified,status)&&writes==1,"integrated unified-tray preference uses the coordinator transaction");
    Registry changed;ParseRegistry(stored,context.ownerSid,changed,status);
    Check(changed.unifiedTray&&!changed.startupEnabled&&!changed.notifyCrashes&&changed.profiles.size()==2,"changing tray preference preserves unrelated coordinator and profile fields");
    Check(!ExecuteProfileManagerUi(nullptr,unified,status)&&writes==1,"replaying a stale Profiles row cannot toggle a changed preference");
    ReadProfileManagerUi(view,status);const auto autoStart=choose(view,UiVerb::AutoStart,second.id);
    Check(ExecuteProfileManagerUi(nullptr,autoStart,status),"per-profile auto-start is available in the shared Properties page");
    ParseRegistry(stored,context.ownerSid,changed,status);
    Check(!changed.profiles[1].autoStart&&!changed.profiles[0].autoStart,"auto-start toggles only its selected profile");
    ReadProfileManagerUi(view,status);
    Check(ExecuteProfileManagerUi(nullptr,choose(view,UiVerb::MakeDefault,second.id),status),"Properties can select a different default profile");
    ParseRegistry(stored,context.ownerSid,changed,status);Check(changed.defaultProfileId==second.id,"default action retains the intended persistent profile ID");
    ReadProfileManagerUi(view,status);const auto beforeStop=writes;
    Check(ExecuteProfileManagerUi(nullptr,choose(view,UiVerb::Stop,first.id),status)&&signals==1&&writes==beforeStop,
        "Stop uses the verified profile controller without changing settings or terminating a process directly");
    foreign=true;const auto beforeReads=reads;
    Check(!ReadProfileManagerUi(view,status)&&reads==beforeReads,"Profiles refuses another owner's coordinator before reading its contents");foreign=false;
    ReadProfileManagerUi(view,status);const auto beforeFailure=stored;failWrite=true;
    Check(!ExecuteProfileManagerUi(nullptr,choose(view,UiVerb::Startup),status)&&stored==beforeFailure,
        "coordinator write failure leaves Profiles preferences unchanged");failWrite=false;
    ReadProfileManagerUi(view,status);race=true;const auto beforeRace=writes;
    Check(!ExecuteProfileManagerUi(nullptr,choose(view,UiVerb::Startup),status)&&writes==beforeRace,
        "a concurrent coordinator change is rechecked under the mutation lease");
}
static unsigned profilePropertiesRouteCalls=0;
static void TestProfileActivationHost()
{
    const auto savedOptions=g_commandLine;
    std::wstring savedExe,savedIni;
    {std::lock_guard<std::mutex> lock(g_pathMutex);savedExe=g_exePath;savedIni=g_iniPath;
        g_exePath=L"C:\\PrivateProfileFixture\\runtime\\DesktopStubAppxStub.exe";
        g_iniPath=L"C:\\PrivateProfileFixture\\Exact Case\\DesktopStub.Profile.ini";}
    g_commandLine.customIniPath=true;
    ConfigureAppxStubHostIniForActivation();
    Check(g_iniPath==L"C:\\PrivateProfileFixture\\Exact Case\\DesktopStub.Profile.ini",
        "legacy AppxStub activation retains the explicit or managed-runtime profile INI");
    g_commandLine.customIniPath=false;g_commandLine.settingsWindow=false;
    const auto restart=ModeSwitchRelaunchArguments(false,false,false);
    Check(restart.find(L"--ini "+QuoteCommandLineArg(g_iniPath))!=std::wstring::npos,
        "restart carries the resident's actual INI even when it originally launched as the default profile");
    const auto suppressed=g_profileSuppressTray.load();const auto ensure=g_testProfileBalloonTrayEnsure;
    g_profileSuppressTray=true;g_testProfileBalloonTrayEnsure=[](DWORD*){return false;};
    Check(!EnsureProfileBalloonTray()&&ProfileManagerSuppressTray(),
        "failed notification tray restoration preserves the hidden-profile state for a later retry");
    g_testProfileBalloonTrayEnsure=[](DWORD*){return true;};
    Check(EnsureProfileBalloonTray()&&!ProfileManagerSuppressTray(),
        "Toast-to-Balloon fallback restores a hidden profile icon instead of losing the notification");
    g_testProfileBalloonTrayEnsure=[](DWORD*){return false;};
    Check(EnsureProfileBalloonTray(),"an already restored profile icon does not require another registration");
    g_profileSuppressTray=suppressed;g_testProfileBalloonTrayEnsure=ensure;
    {std::lock_guard<std::mutex> lock(g_pathMutex);g_exePath=savedExe;g_iniPath=savedIni;}
    g_commandLine=savedOptions;
    const auto profileUiHook=g_testOpenProfilesSettings;profilePropertiesRouteCalls=0;
    g_testOpenProfilesSettings=+[](HWND){++profilePropertiesRouteCalls;return true;};
    Check(DispatchProfileManagerCommand(nullptr,PROFILE_MANAGER_MENU)&&profilePropertiesRouteCalls==1&&!IsProfileManagerDialogMessage(nullptr),
        "Profiles tray command uses the Properties page without a second dialog message loop");
    g_testOpenProfilesSettings=profileUiHook;
    g_commandLine.configureOnly=true;int managementExit=-1;std::wstring managementError;
    auto* unchangedStore=g_profileStore.get();
    Check(HandleProfileManagerArguments({L"DesktopStub.exe",L"--profile-create",L"--help"},managementExit,managementError)&&
        managementExit==0&&managementError.empty()&&g_profileStore.get()==unchangedStore,
        "profile-create help has no context lookup or profile-creation side effect");
    Check(HandleProfileManagerArguments({L"DesktopStub.exe",L"--profiles-startup",L"on",L"--version"},managementExit,managementError)&&
        managementExit==0&&managementError.empty()&&g_profileStore.get()==unchangedStore,
        "profile preference version query cannot change startup preferences");
    Check(HandleProfileManagerArguments({L"DesktopStub.exe",L"--profile-create",L"Fixture",L"--typo"},managementExit,managementError)&&
        managementExit==2&&managementError.find(L"Unexpected arguments")!=std::wstring::npos&&g_profileStore.get()==unchangedStore,
        "unknown creation arguments fail before creating a profile instead of being silently ignored");
    g_commandLine=savedOptions;
    TestProfilesPropertiesHostBackend();
}

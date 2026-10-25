// Tests the legacy activation adapter without starting a package, process or
// registration helper. A managed runtime resolves its INI before this adapter.
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
    WNDCLASSW captionClass{};captionClass.lpfnWndProc=ProfileManagerWindowProc;
    captionClass.hInstance=GetModuleHandleW(nullptr);captionClass.lpszClassName=L"DesktopStubProfileCaptionFixture";
    const auto registered=RegisterClassW(&captionClass);
    auto captionState=std::make_unique<ProfileManagerUi>();
    HWND captionWindow=registered?CreateWindowExW(0,captionClass.lpszClassName,L"DesktopStub profiles",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,0,0,580,430,nullptr,nullptr,captionClass.hInstance,&captionState):nullptr;
    wchar_t caption[128]{};if(captionWindow)GetWindowTextW(captionWindow,caption,_countof(caption));
    Check(captionWindow&&std::wstring(caption)==L"DesktopStub profiles"&&!IsWindowVisible(captionWindow),
        "native hidden Profile Manager creation preserves its non-client caption without showing UI");
    if(captionWindow)DestroyWindow(captionWindow);
    if(registered)UnregisterClassW(captionClass.lpszClassName,captionClass.hInstance);
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
}

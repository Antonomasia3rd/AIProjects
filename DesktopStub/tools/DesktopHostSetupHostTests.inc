static bool desktopSetupIdentity=false;
static unsigned desktopSetupQueues=0,desktopSetupShutdowns=0;
static unsigned long long desktopSetupNextRequest=92000;
static std::vector<aip::content::Snapshot> desktopSetupFrames;
static bool DesktopSetupTestIdentity(){return desktopSetupIdentity;}
static unsigned long long DesktopSetupTestQueue(std::vector<aip::content::Snapshot> frames){
    ++desktopSetupQueues;desktopSetupFrames=std::move(frames);return ++desktopSetupNextRequest;
}
static void DesktopSetupTestShutdown(HWND){++desktopSetupShutdowns;}
static unsigned desktopSetupAdmissions=0;
static bool DesktopSetupTestAdmission(const aip::content::Snapshot&){++desktopSetupAdmissions;return true;}
static void DesktopSetupTestQueueRace(){g_desktopHostSetupPending=true;}
static void TestDesktopHostSetup(const std::wstring& image){
    std::wstring original;ReadIniFileForMutation(original);
    const auto command=g_commandLine;const auto commandText=contentTestCommandLine;
    const auto relaunch=g_relaunchAfterExit.load();const auto recovered=g_desktopHostSetupRecovered;
    const auto priorPending=g_desktopHostSetupPreviousPending;
    const auto warnings=g_startupWarnings;
    const auto identity=g_testDesktopHostSetupIdentity;const auto queue=g_testDesktopHostSetupQueue;const auto shutdown=g_testDesktopHostSetupShutdown;
    const auto completions=g_generationCompletions;
    const auto originalAdmission=g_testGenerateAfterAdmission;
    g_testGenerateAfterAdmission=DesktopSetupTestAdmission;
    for(const auto* suffix:{L"--render-only",L"--configure-only",L"--once",L"--exit",L"--no-monitor",L"--set Settings.RunAtStartup=1",L"--preset WallpaperRSS",L"--notes-import-profile arbitrary.ini"}){
        contentTestCommandLine=L"DesktopStub.exe --setup-desktop "+std::wstring(suffix);
        Check(!ParseCommandLineOptions(),"explicit Desktop package setup rejects offline, startup, import and generation combinations");
    }
    contentTestCommandLine=L"DesktopStub.exe --setup-desktop --ini C:\\private-setup.ini";
    Check(ParseCommandLineOptions()&&g_commandLine.setupDesktop&&g_commandLine.customIniPath,"explicit Desktop setup accepts only its profile selector");
    g_commandLine=command;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    const std::wstring profile=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=desktop,unused-notes\r\n[Tile.desktop]\r\nType=Wallpaper\r\nName=Desktop\r\nPrimary=1\r\nDelivery.Mode=Registration\r\nContent.Background=Image\r\nContent.ImagePath="+image+
        L"\r\n[Tile.unused-notes]\r\nType=Genshin\r\nName=Unused game\r\nDelivery.Mode=LiveTile\r\n";
    Check(WriteContentFixture(profile),"Desktop setup owns only private primary and unused game settings");
    g_desktopHostSetupPending=false;g_desktopHostSetupRequestId=0;g_relaunchAfterExit=0;g_desktopHostSetupRecovered=false;
    g_testDesktopHostSetupIdentity=DesktopSetupTestIdentity;g_testDesktopHostSetupQueue=DesktopSetupTestQueue;g_testDesktopHostSetupShutdown=DesktopSetupTestShutdown;
    desktopSetupIdentity=false;desktopSetupQueues=desktopSetupShutdowns=0;desktopSetupFrames.clear();
    HMENU menu=CreatePopupMenu();PopulateDesktopStubTrayMenu(menu,true,false);
    const std::function<bool(HMENU)> findSetup=[&](HMENU current){for(int i=0;i<GetMenuItemCount(current);++i){if(GetMenuItemID(current,i)==ID_SETUP_DESKTOP_HOST)return true;const auto child=GetSubMenu(current,i);if(child&&findSetup(child))return true;}return false;};
    Check(findSetup(menu),"unpackaged Tiles menu exposes explicit Desktop host setup");DestroyMenu(menu);
    const auto providers=g_tileInstanceProviders.size();
    Check(RequestDesktopHostSetup(nullptr)&&g_desktopHostSetupPending&&desktopSetupQueues==1,"explicit setup queues background main-package work");
    Check(desktopSetupFrames.size()==1&&desktopSetupFrames.front().secondaryTileId.empty()&&desktopSetupFrames.front().policy.Mode()==L"LiveTile"&&
        desktopSetupFrames.front().backgroundPath==image&&g_tileInstanceProviders.size()==providers,
        "setup freezes only the primary Wallpaper snapshot and starts no unused game provider");
    Check(g_relaunchAfterExit==0&&IniReadI(L"State",L"LiveTileModeReregistrationPending",0)==1,"setup defers packaged relaunch until successful generation completion");
    Check(!RequestDesktopHostSetup(nullptr)&&desktopSetupQueues==1,"duplicate setup does not queue another registration");
    CompleteDesktopHostSetup(nullptr);Check(g_desktopHostSetupPending&&desktopSetupShutdowns==0,"unknown completion cannot restart host");
    RecordGenerationCompletion(g_desktopHostSetupRequestId,false);CompleteDesktopHostSetup(nullptr);
    Check(!g_desktopHostSetupPending&&g_relaunchAfterExit==0&&desktopSetupShutdowns==0&&IniReadI(L"State",L"LiveTileModeReregistrationPending",1)==0,
        "failed main registration restores prior pending state and preserves resident");
    Check(RequestDesktopHostSetup(nullptr),"failed explicit setup can be retried");
    RecordGenerationCompletion(g_desktopHostSetupRequestId,true);CompleteDesktopHostSetup(nullptr);
    Check(!g_desktopHostSetupPending&&g_relaunchAfterExit==1&&desktopSetupShutdowns==1&&IniReadI(L"State",L"DesktopHostSetupPending",1)==0,
        "successful main registration alone authorizes packaged restart and clears progress marker");
    g_relaunchAfterExit=0;desktopSetupIdentity=true;
    Check(RequestDesktopHostSetup(nullptr)&&desktopSetupQueues==2,"already packaged host reports ready without registration");
    desktopSetupIdentity=false;IniWrite(L"State",L"DesktopHostSetupPending",L"1");IniWrite(L"State",L"DesktopHostSetupPreviousPending",L"0");IniWrite(L"State",L"LiveTileModeReregistrationPending",L"1");
    Check(RecoverInterruptedDesktopHostSetup()&&g_desktopHostSetupRecovered&&IniReadI(L"State",L"DesktopHostSetupPending",1)==0&&
        IniReadI(L"State",L"LiveTileModeReregistrationPending",1)==0&&desktopSetupQueues==2,"interrupted setup restores marker without automatically queueing registration");
    IniWrite(L"State",L"DesktopHostSetupPending",L"1");IniWrite(L"State",L"DesktopHostSetupPreviousPending",L"1");IniWrite(L"State",L"LiveTileModeReregistrationPending",L"1");
    Check(RecoverInterruptedDesktopHostSetup()&&IniReadI(L"State",L"LiveTileModeReregistrationPending",0)==1,
        "interrupted setup preserves preexisting pending state without treating it as new permission");
    desktopSetupAdmissions=0;
    const auto frame=desktopSetupFrames.front();
    Check(!Generate(frame.backgroundPath.c_str(),L"Recovered setup must not register",false,&frame)&&desktopSetupAdmissions==0,
        "ordinary poll generation cannot consume recovered legacy pending state or enter native registration");
    Check(RequestDesktopHostSetup(nullptr)&&Generate(frame.backgroundPath.c_str(),L"Explicit retry admission",false,&frame)&&desktopSetupAdmissions==1,
        "explicit setup retry can pass the actual generation admission after interrupted recovery");
    const auto dialogs=contentDialogs;const auto shutdowns=desktopSetupShutdowns;
    RecordGenerationCompletion(g_desktopHostSetupRequestId,true);g_shutdownState.Request();CompleteDesktopHostSetup(nullptr);
    Check(!g_desktopHostSetupPending&&g_relaunchAfterExit==0&&desktopSetupShutdowns==shutdowns&&contentDialogs==dialogs,
        "completion during Exit restores markers quietly without a modal prompt or packaged restart");
    g_shutdownState.Cancel();
    const auto beforeQueue=g_testGenerationBeforeQueueLock;const auto workerStarted=g_generationWorkerStarted;
    g_testGenerationBeforeQueueLock=DesktopSetupTestQueueRace;g_desktopHostSetupPending=false;
    Check(QueueGenerate(image,L"Synthetic setup fence race",{frame})==0&&g_desktopHostSetupPending&&g_generationWorkerStarted==workerStarted,
        "a poll queued before setup cannot replace the frozen request after acquiring the queue lock");
    g_testGenerationBeforeQueueLock=beforeQueue;g_desktopHostSetupPending=false;
    IniWrite(L"State",L"LiveTileModeReregistrationPending",L"0");g_desktopHostSetupRecovered=false;
    g_testGenerateAfterAdmission=DesktopSetupTestAdmission;
    Check(RequestDesktopHostSetup(nullptr),"prepare private setup completion persistence failure");
    RecordGenerationCompletion(g_desktopHostSetupRequestId,false);g_testDesktopHostSetupFailStateWrite=true;CompleteDesktopHostSetup(nullptr);
    Check(!g_desktopHostSetupPending&&g_desktopHostSetupRecovered&&g_relaunchAfterExit==0&&IniReadI(L"State",L"LiveTileModeReregistrationPending",0)==1&&
        !Generate(frame.backgroundPath.c_str(),L"Failed marker restore cannot authorize registration",false,&frame),
        "failed marker persistence suppresses poll registration until an explicit retry or recovery");
    g_testDesktopHostSetupFailStateWrite=false;
    g_testDesktopHostSetupIdentity=identity;g_testDesktopHostSetupQueue=queue;g_testDesktopHostSetupShutdown=shutdown;
    g_testGenerateAfterAdmission=originalAdmission;
    g_desktopHostSetupPending=false;g_desktopHostSetupRequestId=0;g_desktopHostSetupRecovered=recovered;g_relaunchAfterExit=relaunch;
    g_desktopHostSetupPreviousPending=priorPending;g_startupWarnings=warnings;
    g_generationCompletions=completions;g_commandLine=command;contentTestCommandLine=commandText;WriteContentFixture(original);
}

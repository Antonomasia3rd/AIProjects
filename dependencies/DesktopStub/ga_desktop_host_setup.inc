// Explicit main-package bootstrap. The UI only prepares one Wallpaper snapshot;
// the generation worker owns image work and native main-manifest registration.
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testDesktopHostSetupIdentity)()=nullptr;
static unsigned long long (*g_testDesktopHostSetupQueue)(std::vector<aip::content::Snapshot>)=nullptr;
static void (*g_testDesktopHostSetupShutdown)(HWND)=nullptr;
static bool g_testDesktopHostSetupFailStateWrite=false;
#endif
static bool DesktopHostSetupHasIdentity(){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testDesktopHostSetupIdentity&&g_testDesktopHostSetupIdentity();
#else
    return CurrentProcessHasPackageIdentity();
#endif
}
static bool PersistDesktopHostSetupState(bool marker,bool pending,bool rememberPrevious){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testDesktopHostSetupFailStateWrite)return false;
#endif
    return MutateDesktopStubIniWithWait([&](std::wstring& text){
        if(rememberPrevious){
            std::wstring previous;
            const auto document=aip::ParseIniDocument(text);
            g_desktopHostSetupPreviousPending=aip::ReadIniValueFromDoc(document,L"State",L"LiveTileModeReregistrationPending",previous)&&previous==L"1";
            if(!aip::WriteIniValueToText(text,L"State",L"DesktopHostSetupPreviousPending",g_desktopHostSetupPreviousPending?L"1":L"0"))return false;
        }
        return aip::WriteIniValueToText(text,L"State",L"DesktopHostSetupPending",marker?L"1":L"0")&&
            aip::WriteIniValueToText(text,L"State",L"LiveTileModeReregistrationPending",pending?L"1":L"0");
    },CurrentIniWriteLockWaitMs());
}
static void DesktopHostSetupNotice(HWND owner,const wchar_t* key,const wchar_t* fallback,bool failed){
    const auto message=IniReadS(L"Strings",key,fallback);
    NotifyUserOperation(L"desktop-host-setup",IniReadS(L"Strings",L"DesktopHostSetupTitle",L"Desktop tile setup"),message,failed?NIIF_WARNING:NIIF_INFO,true);
    if(failed)DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),L"Desktop tile setup",MB_OK|MB_ICONWARNING);
}
static bool RecoverInterruptedDesktopHostSetup(){
    if(IniReadI(L"State",L"DesktopHostSetupPending",0)==0)return false;
    g_desktopHostSetupPreviousPending=IniReadI(L"State",L"DesktopHostSetupPreviousPending",0)!=0;
    g_desktopHostSetupRecovered=true;
    if(!PersistDesktopHostSetupState(false,g_desktopHostSetupPreviousPending,false))
        QueueStartupWarning(L"Desktop tile setup was interrupted and its marker could not be cleared. Retry Set up Desktop tile explicitly; no package registration was started automatically.");
    else QueueStartupWarning(L"Desktop tile setup was interrupted. Use Tiles > Set up Desktop tile to retry; other tiles and saved accounts were preserved.");
    return true;
}
static bool RequestDesktopHostSetup(HWND owner){
    using namespace aip::content;
    if(g_commandLine.renderOnly||g_commandLine.configureOnly||g_shutdownState.IsRequested())return false;
    if(DesktopHostSetupHasIdentity()){
        DesktopHostSetupNotice(owner,L"DesktopHostSetupAlreadyReady",L"DesktopStub already has its Live Tile identity. Its pinned tiles can update from this resident.",false);return true;
    }
    bool expected=false;if(!g_desktopHostSetupPending.compare_exchange_strong(expected,true))return false;
    const auto fail=[&](const wchar_t* key,const wchar_t* text){g_desktopHostSetupPending=false;g_desktopHostSetupRequestId=0;DesktopHostSetupNotice(owner,key,text,true);return false;};
    if(GenerationWorkPending())return fail(L"DesktopHostSetupBusy",L"Another tile update is running. Retry Set up Desktop tile after it finishes.");
    if(ConfiguredManifestCompatibilityTarget()!=ManifestCompatibilityTarget::Windows10)
        return fail(L"DesktopHostSetupTarget",L"Main Desktop Live Tile setup requires the Windows 10 manifest target. Choose it in the tile's delivery settings and retry.");
    TileInstanceConfiguration cfg;std::wstring error;
    if(!ReadCurrentTileInstances(cfg,error))return fail(L"DesktopHostSetupConfiguration",L"Desktop tile settings could not be read. Repair the primary Wallpaper tile in Tiles and retry setup.");
    auto primary=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[](const auto& tile){return tile.primary&&tile.type==TileInstanceType::Wallpaper;});
    if(primary==cfg.tiles.end()||!primary->error.empty())return fail(L"DesktopHostSetupNeedsWallpaper",L"Add a primary Desktop wallpaper tile with a usable image before setup. You can remove it after the main host is installed.");
    if(primary->policy.Mode()!=L"LiveTile"){
        if(!IniWrite(TileInstanceSection(primary->id).c_str(),L"Delivery.Mode",L"LiveTile"))return fail(L"DesktopHostSetupSaveFailed",L"The Desktop tile's Live Tile preference could not be saved. No package setup was started.");
        if(!ReadCurrentTileInstances(cfg,error))return fail(L"DesktopHostSetupConfiguration",L"Desktop tile settings could not be reread. No package setup was started.");
        primary=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[](const auto& tile){return tile.primary&&tile.type==TileInstanceType::Wallpaper;});
        if(primary==cfg.tiles.end()||!primary->error.empty())return fail(L"DesktopHostSetupConfiguration",L"The Desktop tile changed during setup. Reopen its settings and retry.");
    }
    TileInstanceProviderState local;local.type=TileInstanceType::Wallpaper;
    const auto frame=ComposeTileInstance(*primary,local,SmtcMediaSnapshot{},true);local.Stop();
    if(!frame.active||!frame.secondaryTileId.empty()||!IsUsableWallpaperPath(frame.backgroundPath))
        return fail(L"DesktopHostSetupNoImage",L"The Desktop wallpaper image is unavailable. Choose an available image for the primary Desktop tile and retry.");
    if(!PersistDesktopHostSetupState(true,true,true))return fail(L"DesktopHostSetupSaveFailed",L"The setup progress marker could not be saved. No package registration was started.");
    g_desktopHostSetupRequestId=0;g_relaunchAfterExit=0;
    unsigned long long request=0;
    {
        struct Admission{Admission(){g_desktopHostSetupQueueAdmission=true;}~Admission(){g_desktopHostSetupQueueAdmission=false;}} admission;
#ifdef DESKTOPSTUB_INERT_HARDWARE
        if(g_testDesktopHostSetupQueue)request=g_testDesktopHostSetupQueue({frame});
#else
        request=QueueGenerateContentBatch({frame},L"Explicit Desktop tile setup");
#endif
    }
    if(!request){
        if(!PersistDesktopHostSetupState(false,g_desktopHostSetupPreviousPending,false))g_desktopHostSetupRecovered=true;
        return fail(L"DesktopHostSetupQueueFailed",L"The Desktop setup job could not be queued. Retry Set up Desktop tile; settings and existing pins were kept.");
    }
    g_desktopHostSetupRequestId=request;
    DesktopHostSetupNotice(owner,L"DesktopHostSetupStarted",L"Preparing the main Desktop tile in the background. DesktopStub will restart with Live Tile identity only after setup succeeds.",false);
    return true;
}
static void CompleteDesktopHostSetup(HWND owner){
    if(!g_desktopHostSetupPending.load())return;
    const auto request=g_desktopHostSetupRequestId.load();bool success=false;
    if(!request||!TryGetGenerationCompletion(request,success))return;
    if(g_shutdownState.IsRequested())success=false;
    if(!PersistDesktopHostSetupState(false,success?false:g_desktopHostSetupPreviousPending,false)){
        success=false;g_desktopHostSetupRecovered=true;
    }
    g_desktopHostSetupRequestId=0;g_desktopHostSetupPending=false;
    // An explicit Exit/session shutdown must never wait behind a new modal
    // setup-failure prompt, and must never acquire a packaged relaunch.
    if(g_shutdownState.IsRequested()){g_relaunchAfterExit=0;return;}
    if(!success){
        g_relaunchAfterExit=0;
        DesktopHostSetupNotice(owner,L"DesktopHostSetupFailed",L"Desktop tile setup did not complete. Check the application log for the package registration error, then use Set up Desktop tile to retry. DesktopStub remains running; saved accounts and pins were kept.",true);return;
    }
    g_relaunchAfterExit=1;
    DesktopHostSetupNotice(owner,L"DesktopHostSetupComplete",L"Desktop tile setup succeeded. Restarting DesktopStub with its Live Tile identity...",false);
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testDesktopHostSetupShutdown)g_testDesktopHostSetupShutdown(owner);
#else
    RequestGracefulShutdown(owner);
#endif
}

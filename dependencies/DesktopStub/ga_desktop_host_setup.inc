// Explicit main-package bootstrap. The UI only prepares one Wallpaper snapshot;
// the generation worker owns image work and native main-manifest registration.
#ifdef DESKTOPSTUB_INERT_HARDWARE
static bool (*g_testDesktopHostSetupIdentity)()=nullptr;
static unsigned long long (*g_testDesktopHostSetupQueue)(std::vector<aip::content::Snapshot>)=nullptr;
static void (*g_testDesktopHostSetupShutdown)(HWND)=nullptr;
static bool g_testDesktopHostSetupFailStateWrite=false;
static bool g_testDesktopHostSetupForeignIdentity=false;
#endif
static bool DesktopHostSetupHasIdentity(){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testDesktopHostSetupIdentity&&g_testDesktopHostSetupIdentity();
#else
    if(!CurrentProcessHasPackageIdentity())return false;
    const auto manifest=CurrentManifestDisplayInfo();
    UINT32 bytes=0;if(GetCurrentPackageId(&bytes,nullptr)!=ERROR_INSUFFICIENT_BUFFER||bytes<sizeof(PACKAGE_ID)||bytes>65536)return false;
    std::vector<BYTE> buffer(bytes);if(GetCurrentPackageId(&bytes,buffer.data())!=ERROR_SUCCESS)return false;
    const auto* identity=reinterpret_cast<const PACKAGE_ID*>(buffer.data());
    if(!identity->name||!identity->publisher||!IEquals(identity->name,manifest.identityName)||!IEquals(identity->publisher,manifest.publisher))return false;
    UINT32 length=0;if(GetCurrentPackagePath(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||length<2||length>32768)return false;
    std::vector<wchar_t> path(length);if(GetCurrentPackagePath(&length,path.data())!=ERROR_SUCCESS)return false;
    std::wstring packageRoot,exeRoot;if(!aip::named_objects::NormalizeFileIdentity(path.data(),packageRoot)||!aip::named_objects::NormalizeFileIdentity(GetExeDir(),exeRoot)||packageRoot!=exeRoot)return false;
    length=0;if(GetCurrentPackageFamilyName(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||length<2||length>1024)return false;
    std::vector<wchar_t> family(length);if(GetCurrentPackageFamilyName(&length,family.data())!=ERROR_SUCCESS)return false;
    length=0;if(GetCurrentApplicationUserModelId(&length,nullptr)!=ERROR_INSUFFICIENT_BUFFER||length<2||length>1024)return false;
    std::vector<wchar_t> aumid(length);return GetCurrentApplicationUserModelId(&length,aumid.data())==ERROR_SUCCESS&&IEquals(aumid.data(),std::wstring(family.data())+L"!"+manifest.applicationId);
#endif
}
static bool DesktopHostSetupHasForeignIdentity(){
#ifdef DESKTOPSTUB_INERT_HARDWARE
    return g_testDesktopHostSetupForeignIdentity;
#else
    return CurrentProcessHasPackageIdentity()&&!DesktopHostSetupHasIdentity();
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
    NotifyUserOperation(L"desktop-host-setup",IniReadS(L"Strings",L"DesktopHostSetupTitle",L"Live Tile support"),message,failed?NIIF_WARNING:NIIF_INFO,true);
    if(failed)DESKTOPSTUB_CONTENT_MESSAGEBOX(owner,message.c_str(),L"Live Tile support",MB_OK|MB_ICONWARNING);
}
static bool RecoverInterruptedDesktopHostSetup(){
    if(IniReadI(L"State",L"DesktopHostSetupPending",0)==0)return false;
    g_desktopHostSetupPreviousPending=IniReadI(L"State",L"DesktopHostSetupPreviousPending",0)!=0;
    g_desktopHostSetupRecovered=true;
    if(!PersistDesktopHostSetupState(false,g_desktopHostSetupPreviousPending,false))
        QueueStartupWarning(L"Live Tile host preparation was interrupted and its marker could not be cleared. Retry Prepare Live Tile support explicitly; no package registration was started automatically.");
    else QueueStartupWarning(L"Live Tile host preparation was interrupted. Use Advanced > Profile package identity and setup > Prepare Live Tile support to retry; tile delivery choices and saved accounts were preserved.");
    return true;
}
static bool RequestDesktopHostSetup(HWND owner){
    using namespace aip::content;
    if(g_commandLine.renderOnly||g_commandLine.configureOnly||g_shutdownState.IsRequested())return false;
    if(DesktopHostSetupHasIdentity()){
        DesktopHostSetupNotice(owner,L"DesktopHostSetupAlreadyReady",L"DesktopStub already has its Live Tile identity. Its pinned tiles can update from this resident.",false);return true;
    }
    if(DesktopHostSetupHasForeignIdentity()){
        DesktopHostSetupNotice(owner,L"DesktopHostSetupWrongIdentity",L"This process is running under a different package identity from this profile's main host. Exit this instance and run DesktopStub.exe directly from its own folder, then retry Prepare Live Tile support. Tile settings were kept.",true);return false;
    }
    bool expected=false;if(!g_desktopHostSetupPending.compare_exchange_strong(expected,true))return false;
    const auto fail=[&](const wchar_t* key,const wchar_t* text){g_desktopHostSetupPending=false;g_desktopHostSetupRequestId=0;DesktopHostSetupNotice(owner,key,text,true);return false;};
    if(GenerationWorkPending())return fail(L"DesktopHostSetupBusy",L"Another tile update is running. Retry Prepare Live Tile support after it finishes.");
    if(ConfiguredManifestCompatibilityTarget()!=ManifestCompatibilityTarget::Windows10)
        return fail(L"DesktopHostSetupTarget",L"Main Desktop Live Tile setup requires the Windows 10 manifest target. Choose it in the tile's delivery settings and retry.");
    TileInstanceConfiguration cfg;std::wstring error;
    if(!ReadCurrentTileInstances(cfg,error))return fail(L"DesktopHostSetupConfiguration",L"Desktop tile settings could not be read. Repair the primary Wallpaper tile in Tiles and retry setup.");
    auto primary=std::find_if(cfg.tiles.begin(),cfg.tiles.end(),[](const auto& tile){return tile.primary&&tile.type==TileInstanceType::Wallpaper;});
    if(primary==cfg.tiles.end()||!primary->error.empty())return fail(L"DesktopHostSetupNeedsWallpaper",L"Add a primary Desktop wallpaper tile with a usable image before setup. You can remove it after the main host is installed.");
    // Preparing the shared identity is independent of every tile's delivery
    // choice. Freeze a main-package job without rewriting the saved Desktop
    // mode or routing it to an independent Registration package.
    auto setupTile=*primary;
    setupTile.policy.values[L"settings.primaryregistration"]=L"Main";
    TileInstanceProviderState local;local.type=TileInstanceType::Wallpaper;
    auto frame=ComposeTileInstance(setupTile,local,SmtcMediaSnapshot{},true);local.Stop();
    frame.mainHostSetupPolicyKey=TileDeliveryPolicyKey(primary->policy);
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
        return fail(L"DesktopHostSetupQueueFailed",L"The Live Tile support job could not be queued. Retry Prepare Live Tile support; settings and existing pins were kept.");
    }
    g_desktopHostSetupRequestId=request;
    DesktopHostSetupNotice(owner,L"DesktopHostSetupStarted",L"Preparing Live Tile support in the background. DesktopStub will restart with package identity after setup succeeds. Your tiles' delivery choices are kept.",false);
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
    bool migrationRelaunch=false;std::wstring migrationError;
    if(!CompleteProfileIdentityMigration(success,migrationRelaunch,migrationError)){
        success=false;
        if(!g_shutdownState.IsRequested())DesktopHostSetupNotice(owner,L"ProfileIdentityMigrationFailed",migrationError.c_str(),true);
    }
    if(migrationRelaunch&&!g_shutdownState.IsRequested()){g_relaunchAfterExit=3;RequestGracefulShutdown(owner);return;}
    // An explicit Exit/session shutdown must never wait behind a new modal
    // setup-failure prompt, and must never acquire a packaged relaunch.
    if(g_shutdownState.IsRequested()){g_relaunchAfterExit=0;return;}
    if(!success){
        g_relaunchAfterExit=0;
        DesktopHostSetupNotice(owner,L"DesktopHostSetupFailed",L"Live Tile host preparation did not complete. Check the application log for the package registration error, then use Prepare Live Tile support to retry. DesktopStub remains running; tile delivery choices and saved accounts were kept.",true);return;
    }
    g_relaunchAfterExit=1;
    DesktopHostSetupNotice(owner,L"DesktopHostSetupComplete",L"Desktop tile setup succeeded. Restarting DesktopStub with its Live Tile identity...",false);
#ifdef DESKTOPSTUB_INERT_HARDWARE
    if(g_testDesktopHostSetupShutdown)g_testDesktopHostSetupShutdown(owner);
#else
    RequestGracefulShutdown(owner);
#endif
}

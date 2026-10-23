// Private manifests, synthetic launcher bytes, and injected deployment results.
// No real package registration, pinning, Startup or program launch is invoked.
static unsigned registrationHostDeployments = 0;
static bool registrationHostDeploySucceeds = false;
static std::wstring registrationHostDeploymentPath,registrationHostLogicalRoot,registrationHostPhysicalRoot;
static bool registrationHostWrongPhysicalParent=false;
static bool RegistrationHostAumid(std::wstring& value,std::wstring& error) { value=L"Canonical.Main_123456789abcd!App";error.clear();return true; }
static bool RegistrationHostDeploy(const std::wstring& path,std::wstring& error) {
    registrationHostDeploymentPath=path;
    ++registrationHostDeployments; if (!registrationHostDeploySucceeds) {error=L"Synthetic deployment failure";return false;}error.clear();return true;
}
static HANDLE RegistrationHostPhysicalOpen(const std::wstring& path,bool directory) {
    auto physical=path;
    if(IEquals(path,registrationHostLogicalRoot))physical=registrationHostPhysicalRoot;
    else if(IEquals(path,registrationHostLogicalRoot+L"\\AppxManifest.xml"))physical=registrationHostPhysicalRoot+
        (registrationHostWrongPhysicalParent?L"\\other\\AppxManifest.xml":L"\\AppxManifest.xml");
    return CreateFileW(physical.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT|(directory?FILE_FLAG_BACKUP_SEMANTICS:0),nullptr);
}
static bool RemoveRegistrationFixtureTree(const std::wstring& path,const std::wstring& ownedBase)
{
    const auto root=aip::MakeAbsolutePath(ownedBase),current=aip::MakeAbsolutePath(path);
    if(!IEquals(root,current)&&(current.size()<=root.size()||!IEquals(current.substr(0,root.size()+1),root+L"\\")))return false;
    const auto attrs=GetFileAttributesW(current.c_str());if(attrs==INVALID_FILE_ATTRIBUTES)return true;
    if(attrs&FILE_ATTRIBUTE_REPARSE_POINT)return false;
    if(!(attrs&FILE_ATTRIBUTE_DIRECTORY))return DeleteFileW(current.c_str())!=FALSE;
    WIN32_FIND_DATAW data{};auto find=FindFirstFileW((current+L"\\*").c_str(),&data);bool ok=true;
    if(find!=INVALID_HANDLE_VALUE){do{if(wcscmp(data.cFileName,L".")&&wcscmp(data.cFileName,L"..")&&
        !RemoveRegistrationFixtureTree(current+L"\\"+data.cFileName,root)){ok=false;break;}}while(FindNextFileW(find,&data));FindClose(find);}
    return ok&&RemoveDirectoryW(current.c_str())!=FALSE;
}
static void TestTileRegistrationHost(const std::wstring& bitmapPath)
{
    const auto directory=ParentDirectoryOfPath(bitmapPath);
    std::wstring original,error; Check(ReadIniFileForMutation(original),"registration fixture captures only its private INI");
    const auto oldUser=g_testTileRegistrationUserScope,oldBase=g_testTileRegistrationBase,oldLauncher=g_testTileRegistrationLauncherSource;
    const auto oldResolver=g_testTileRegistrationHostAumid;
    const auto oldDeploy=g_testTileRegistrationRegister;
    const auto oldDeploymentOpen=g_testTileRegistrationDeploymentOpen;
    const auto oldResponse=contentDialogResponse;
    const auto oldDirectories=gallerySetupTestDirectories;
    const auto oldGalleryFrames=galleryHostFrames,oldGalleryFailures=galleryHostFailures;
    const bool oldGalleryValid=galleryHostValid;
    const auto oldAdmission=g_testGenerateAfterAdmission;const auto oldNative=contentNativeQueueAvailable;
    const auto oldMode=g_liveTileUpdateMode.load(),oldManifest=g_manifestCompatibilityTarget.load(),oldRelaunch=g_relaunchAfterExit.load();
    const auto oldLogging=g_logging.load(),oldNotifications=g_notificationsEnabled.load();const auto oldLog=GetLogPathCopy();
    const auto oldStart=g_notifyOnStart.load(),oldSuccess=g_notifyOnSuccess.load(),oldFailure=g_notifyOnFailure.load(),oldBusy=g_notifyOnBusy.load();
    g_testTileRegistrationUserScope=L"1111111111111111";
    g_testTileRegistrationBase=directory+L"\\registration-staging";
    g_testTileRegistrationLauncherSource=directory+L"\\registration-launcher-template.exe";
    g_testTileRegistrationHostAumid=RegistrationHostAumid;g_testTileRegistrationRegister=nullptr;
    const std::vector<BYTE> launcher{'M','Z','i','n','e','r','t'};
    Check(WriteWholeFileBytes(g_testTileRegistrationLauncherSource,launcher),"registration fixture writes a non-executed synthetic launcher template");
    const std::wstring profile=L"[Tiles]\r\nInstances=reg-tile\r\n[Tile.reg-tile]\r\nType=RSS\r\nName=Independent static feed\r\n";
    Check(WriteContentFixture(profile),"registration fixture uses one private unconfigured RSS instance");
    aip::tile_registration::Target target;
    Check(BuildTileRegistrationTarget(L"reg-tile",L"Independent static feed",{},target,error),"registration target binds the current user, profile, tile and canonical host without enumerating real packages");
    Check(!TileRegistrationReadyForId(L"reg-tile") && !PrepareTileRegistrationPackage(target,false,error) &&
        GetFileAttributesW(g_testTileRegistrationBase.c_str())==INVALID_FILE_ATTRIBUTES,
        "background registration demand cannot create a package or its cache before explicit setup");
    contentDialogResponse=IDNO;
    const auto dialogs=contentDialogs;
    Check(!OfferTileDeliveryMigration(nullptr,L"reg-tile",L"Registration") && contentDialogs==dialogs+1 &&
        IniReadS(L"Tile.reg-tile",L"Delivery.Mode",L"Auto")==L"Auto" && GetFileAttributesW(target.root.c_str())==INVALID_FILE_ATTRIBUTES,
        "declining migration keeps delivery, old pins, and the absent package cache unchanged");
    auto downlevel=profile;
    Check(aip::WriteIniValueToText(downlevel,L"Tile.reg-tile",L"Delivery.ManifestTarget",L"Windows81")&&WriteContentFixture(downlevel)&&
        !OfferTileDeliveryMigration(nullptr,L"reg-tile",L"Registration")&&IniReadS(L"Tile.reg-tile",L"Delivery.Mode",L"Auto")==L"Auto"&&
        GetFileAttributesW(target.root.c_str())==INVALID_FILE_ATTRIBUTES,
        "unsupported Registration manifest targets preserve the selected mode and stage no package files");
    Check(WriteContentFixture(profile),"registration migration restores the supported private target before staging");
    Check(PrepareTileRegistrationPackage(target,true,error) && TileRegistrationReady(target,error) && TileRegistrationReadyForId(L"reg-tile"),
        "explicit staging authorizes the queued first registration and invalidates a cached negative demand immediately");
    std::vector<BYTE> copied;
    Check(aip::ReadWholeFileBytes(target.launcherPath,copied) && copied==launcher,"staging copies only the small launcher instead of the complete resident host");
    Check(WriteTileRegistrationManifest(target,error) && target.packageVersion==L"1.0.0.1","first static image publication receives its independent package revision");
    std::wstring xml;
    Check(TileRegistrationReadText(target.manifestPath,xml) && xml.find(target.identityName)!=std::wstring::npos &&
        xml.find(L"Canonical.Main_123456789abcd!App")!=std::wstring::npos && xml.find(L"--tile-id &quot;reg-tile&quot;")!=std::wstring::npos,
        "private staged manifest targets the canonical app and selected tile without account data");
    Check(WriteTileRegistrationManifest(target,error) && target.packageVersion==L"1.0.0.2","background image changes increment only that tile's package version");
    Check(!RegisterTileRegistrationPackage(target,false,error) && error.find(L"inert")!=std::wstring::npos,
        "default fixture cannot register even an explicitly staged real Windows package");
    g_testTileRegistrationRegister=RegistrationHostDeploy;registrationHostDeployments=0;registrationHostDeploySucceeds=false;
    Check(!RegisterTileRegistrationPackage(target,false,error) && registrationHostDeployments==1 &&
        GetFileAttributesW((target.root+L"\\registration.published").c_str())==INVALID_FILE_ATTRIBUTES,
        "failed injected deployment cannot create a successful-publication receipt");
    registrationHostDeploySucceeds=true;
    Check(RegisterTileRegistrationPackage(target,false,error) && registrationHostDeployments==2 &&
        GetFileAttributesW((target.root+L"\\registration.published").c_str())!=INVALID_FILE_ATTRIBUTES,
        "successful injected deployment writes a receipt only after the package operation succeeds");
    Check(IEquals(registrationHostDeploymentPath,target.manifestPath),
        "ordinary unpackaged Registration deployment resolves the existing owned manifest to its physical file");
    const auto physicalRoot=directory+L"\\registration-physical-root";
    Check(EnsureDirectoryTreeExists(physicalRoot)&&CopyFileW(target.manifestPath.c_str(),(physicalRoot+L"\\AppxManifest.xml").c_str(),TRUE),
        "Registration redirection fixture copies only its private manifest into a physical shadow package directory");
    registrationHostLogicalRoot=target.root;registrationHostPhysicalRoot=physicalRoot;registrationHostWrongPhysicalParent=false;
    g_testTileRegistrationDeploymentOpen=RegistrationHostPhysicalOpen;
    const auto beforePhysicalDeploy=registrationHostDeployments;
    Check(RegisterTileRegistrationPackage(target,false,error)&&registrationHostDeployments==beforePhysicalDeploy+1&&
        IEquals(registrationHostDeploymentPath,physicalRoot+L"\\AppxManifest.xml"),
        "Registration deployment sends the opened physical manifest path instead of a process-private virtualized AppData name");
    Check(EnsureDirectoryTreeExists(physicalRoot+L"\\other")&&CopyFileW(target.manifestPath.c_str(),(physicalRoot+L"\\other\\AppxManifest.xml").c_str(),TRUE),
        "Registration redirection fixture prepares a mismatched physical-parent negative case");
    registrationHostWrongPhysicalParent=true;
    Check(!RegisterTileRegistrationPackage(target,false,error)&&registrationHostDeployments==beforePhysicalDeploy+1&&error.find(L"owned package")!=std::wstring::npos,
        "a physically redirected manifest outside the resolved owned root is rejected before any deployment");
    g_testTileRegistrationDeploymentOpen=oldDeploymentOpen;registrationHostWrongPhysicalParent=false;
    Check(RemoveRegistrationFixtureTree(physicalRoot,physicalRoot),"Registration redirection fixture removes only its private physical shadow directory");
    const auto cache=directory+L"\\registration-cache",transparent=directory+L"\\registration-transparent.png";
    Check(GetFileAttributesW(cache.c_str())==INVALID_FILE_ATTRIBUTES,"registration rendering owns a new private image-cache root");
    {ScopedGdiPlus gdi;Check(gdi.ok(),"static registration rendering initializes offscreen GDI+ only");if(gdi.ok()){
        Gdiplus::Bitmap pixel(1,1,PixelFormat32bppARGB);{Gdiplus::Graphics graphics(&pixel);graphics.Clear(Gdiplus::Color(0,0,0,0));}
        Check(SavePNG(&pixel,transparent.c_str()),"registration rendering creates one private transparent PNG source");}}
    auto rendered=profile;
    aip::WriteIniValueToText(rendered,L"Tiles",L"Instances",L"reg-tile,reg-other,reg-native");
    for(const auto* id:{L"reg-tile",L"reg-other",L"reg-native"}){
        const auto section=aip::content::TileInstanceSection(id);
        for(const auto& setting:std::vector<std::pair<std::wstring,std::wstring>>{{L"Type",L"RSS"},{L"Delivery.Mode",L"Registration"},
            {L"Delivery.ManifestTarget",L"Windows10"},{L"Assets.ScaleAuto",L"0"},{L"Assets.Scale100",L"1"},{L"Assets.Scale125",L"0"},
            {L"Assets.Scale150",L"0"},{L"Assets.Scale200",L"0"},{L"Assets.Scale400",L"0"},{L"Cache.Directory",cache},
            {L"TextPreset",L"RssHeadlines"}})aip::WriteIniValueToText(rendered,section.c_str(),setting.first.c_str(),setting.second);
    }
    aip::WriteIniValueToText(rendered,L"Tile.reg-other",L"Delivery.ClearOnShutdown",L"0");
    aip::WriteIniValueToText(rendered,L"Tile.reg-native",L"Delivery.Mode",L"LiveTile");
    for(const auto* key:{L"NotificationsEnabled",L"NotifyOnStart",L"NotifyOnSuccess",L"NotifyOnFailure",L"NotifyOnBusy",L"ShowConsole"})
        aip::WriteIniValueToText(rendered,L"Settings",key,L"0");
    const auto renderLog=directory+L"\\registration-render.log";
    aip::WriteIniValueToText(rendered,L"Settings",L"Logging",L"1");
    aip::WriteIniValueToText(rendered,L"Settings",L"LogPath",renderLog);
    Check(WriteContentFixture(rendered),"actual Generate fixture configures independent static policies without providers");
    aip::content::TileInstanceConfiguration instances;
    Check(ReadCurrentTileInstances(instances,error)&&instances.tiles.size()==3,"static rendering captures three independently scoped tile policies");
    aip::tile_registration::Target second,nativeTarget;
    Check(BuildTileRegistrationTarget(L"reg-other",L"Second static feed",{},second,error)&&PrepareTileRegistrationPackage(second,true,error)&&
        BuildTileRegistrationTarget(L"reg-native",L"Former static feed",{},nativeTarget,error)&&PrepareTileRegistrationPackage(nativeTarget,true,error),
        "independent static render destinations are explicitly owned before generation");
    const auto frameFor=[&](std::size_t index,const std::wstring& headline){
        aip::content::Snapshot frame;const auto& tile=instances.tiles[index];frame.active=true;frame.name=tile.name;
        frame.secondaryTileId=aip::content::TileInstanceNativeId(tile);frame.policy=tile.policy;frame.registrationAppearance=tile.appearance;
        frame.artworkTopic=L"rss";frame.backgroundId=aip::content::BackgroundId::Transparent;frame.backgroundPath=transparent;
        frame.presentation=tile.presentation;frame.text={headline,L"Synthetic static summary",L""};frame.rssHeadlines={frame.text};
        frame.secondaryFrameKey=ContentSnapshotKey(frame);return frame;
    };
    auto firstFrame=frameFor(0,L"FIRST STATIC HEADLINE"),secondFrame=frameFor(1,L"SECOND STATIC HEADLINE");
    g_testGenerateAfterAdmission=nullptr;contentNativeQueueAvailable=false;
    Check(Generate(transparent.c_str(),L"private static registration first",false,&firstFrame)&&
        Generate(transparent.c_str(),L"private static registration second",false,&secondFrame),
        "actual Generate renders and publishes two independent Registration packages through injected deployment only");
    const auto firstImage=target.assetsDirectory+L"\\MediumTile.png",secondImage=second.assetsDirectory+L"\\MediumTile.png";
    std::vector<BYTE> firstPixels,secondPixels;
    Check(target.root!=second.root&&aip::ReadWholeFileBytes(firstImage,firstPixels)&&aip::ReadWholeFileBytes(secondImage,secondPixels)&&firstPixels!=secondPixels,
        "real static PNG content and output paths remain separate for independently named sources");
    {ScopedGdiPlus gdi;if(gdi.ok()){
        Gdiplus::Bitmap image(firstImage.c_str());bool alpha=false,white=false;
        for(UINT y=0;y<image.GetHeight();++y)for(UINT x=0;x<image.GetWidth();++x){Gdiplus::Color pixel;if(image.GetPixel(x,y,&pixel)!=Gdiplus::Ok)continue;
            alpha=alpha||pixel.GetA()==0;white=white||(pixel.GetA()>0&&pixel.GetR()>240&&pixel.GetG()>240&&pixel.GetB()>240);}
        Check(image.GetLastStatus()==Gdiplus::Ok&&alpha&&white,"static Registration PNGs contain readable white content over transparent pixels");}}
    std::wstring firstCache,secondCache;
    {aip::content::TilePolicyReadScope scope(firstFrame.policy);firstCache=GeneratedAssetCacheRoot(target.root);}
    {aip::content::TilePolicyReadScope scope(secondFrame.policy);secondCache=GeneratedAssetCacheRoot(second.root);}
    Check(firstCache!=secondCache&&firstCache.find(cache)==0&&secondCache.find(cache)==0,"independent Registration tiles never share rendered image-cache entries");
    firstFrame.text.primary=L"CHANGED FIRST HEADLINE";firstFrame.rssHeadlines[0].primary=firstFrame.text.primary;
    firstFrame.secondaryFrameKey=ContentSnapshotKey(firstFrame);
    Check(Generate(transparent.c_str(),L"private static registration changed",false,&firstFrame),"actual static generation updates one source without touching its sibling");
    std::vector<BYTE> changed,unchanged;
    Check(aip::ReadWholeFileBytes(firstImage,changed)&&changed!=firstPixels&&aip::ReadWholeFileBytes(secondImage,unchanged)&&unchanged==secondPixels,
        "a first-tile text change alters its PNG while preserving the second tile's exact bytes");
    Check(EnsureDirectoryTreeExists(nativeTarget.assetsDirectory)&&WriteWholeFileBytes(nativeTarget.assetsDirectory+L"\\MediumTile.png",secondPixels)&&
        WriteTileRegistrationManifest(nativeTarget,error)&&RegisterTileRegistrationPackage(nativeTarget,false,error),
        "shutdown fixture retains an explicitly owned old static package after switching its policy back to Live Tile");
    for(const auto& setting:std::vector<aip::IniSetting>{{L"Gallery",L"Enabled",L"0"},{L"Gallery",L"GroupId",L"registration_fixture"},
        {L"Gallery",L"TileCount",L"1"},{L"Gallery.Tile.1",L"Id",L"photo"},{L"Gallery.Tile.1",L"Name",L"Private static gallery"},
        {L"Gallery.Tile.1",L"Folder",directory},{L"Gallery.Tile.1",L"Delivery.Mode",L"Registration"},
        {L"Gallery.Tile.1",L"Assets.ScaleAuto",L"0"},{L"Gallery.Tile.1",L"Assets.Scale100",L"1"},
        {L"Gallery.Tile.1",L"Assets.Scale125",L"0"},{L"Gallery.Tile.1",L"Assets.Scale150",L"0"},
        {L"Gallery.Tile.1",L"Cache.Directory",cache}})
        aip::WriteIniValueToText(rendered,setting.section.c_str(),setting.key.c_str(),setting.value);
    Check(WriteContentFixture(rendered),"static gallery activation fixture uses the same private profile without changing its photo ledger");
    const auto galleryId=aip::gallery::GalleryRegistrationId(L"registration_fixture",L"photo");
    aip::tile_registration::Target galleryTarget;
    Check(BuildTileRegistrationTarget(galleryId,L"Private static gallery",{},galleryTarget,error)&&PrepareTileRegistrationPackage(galleryTarget,true,error),
        "a hashed gallery identity explicitly stages only its independently owned package");
    aip::gallery::Configuration galleryConfiguration;
    Check(ReadGalleryConfiguration(galleryConfiguration,error)&&galleryConfiguration.tiles.size()==1,"static gallery generation reads its current receipt policy");
    auto galleryFrame=GallerySyntheticWindow(transparent,L"photo").front();
    galleryFrame.galleryGroupId=L"registration_fixture";galleryFrame.secondaryTileId=aip::gallery::NativeTileId(L"registration_fixture",L"photo");
    galleryFrame.gallerySlotCount=1;galleryFrame.nativeQueue=false;galleryFrame.nativeQueueTag=L"g1";galleryFrame.nativeQueueReset=true;
    galleryFrame.policy=galleryConfiguration.tiles.front().delivery;galleryFrame.registrationAppearance=galleryConfiguration.tiles.front().appearance;
    galleryFrame.artworkTopic=L"gallery";galleryFrame.name=L"Private static gallery";galleryFrame.nativeQueueLayout=L"private-static-gallery";
    galleryHostFrames={galleryFrame};galleryHostValid=true;
    const auto beforeGalleryGenerate=registrationHostDeployments;
    Check(Generate(transparent.c_str(),L"private actual static gallery generation",false,&galleryFrame)&&registrationHostDeployments==beforeGalleryGenerate+1,
        "actual static gallery Generate preserves g1/reset/slotcount/policy receipt metadata through the late validation and fake deployment");
    auto changedReceipt=galleryFrame;changedReceipt.nativeQueueTag.clear();changedReceipt.nativeQueueReset=false;
    Check(!GalleryTestValidate(changedReceipt),"gallery fixture rejects the exact receipt mutation that previously broke Registration delivery");
    gallerySetupTestDirectories.insert(aip::MakeAbsolutePath(directory));
    std::wstring folder;
    Check(ResolveGalleryRegistrationActivation(galleryId,folder)&&IEquals(folder,aip::MakeAbsolutePath(directory))&&
        !ResolveGalleryRegistrationActivation(L"gallery_unknown",folder),"registration gallery launch resolves only its current authorized photo folder without opening Explorer");
    std::wstring currentProfile;{std::lock_guard<std::mutex> lock(g_pathMutex);currentProfile=g_iniPath;g_iniPath=directory+L"\\other-registration-profile.ini";}
    Check(!ResolveGalleryRegistrationActivation(galleryId,folder),"registration gallery activation refuses an identity from a different profile");
    {std::lock_guard<std::mutex> lock(g_pathMutex);g_iniPath=currentProfile;}
    Check(WriteContentFixture(rendered),"gallery activation restores the original private profile after its rejection check");
    const auto ledgerPath=aip::gallery::runtime::LedgerPath(aip::MakeAbsolutePath(g_iniPath));std::vector<BYTE> ledgerBefore,ledgerAfter;
    const bool hadLedger=aip::ReadWholeFileBytes(ledgerPath,ledgerBefore,aip::gallery::MaxLedgerBytes);
    std::vector<BYTE> sourceBefore;Check(aip::ReadWholeFileBytes(bitmapPath,sourceBefore),"shutdown cleanup captures its private original photo sentinel");
    const auto deploymentsBeforeClear=registrationHostDeployments;g_relaunchAfterExit=0;
    ClearOwnedRegistrationTilesOnShutdown();
    Check(registrationHostDeployments==deploymentsBeforeClear+2,"shutdown republishes only the already-owned ordinary and gallery Registration tiles with their clear flag enabled");
    {ScopedGdiPlus gdi;if(gdi.ok()){Gdiplus::Bitmap image(firstImage.c_str());bool blank=true;
        for(UINT y=0;y<image.GetHeight();++y)for(UINT x=0;x<image.GetWidth();++x){Gdiplus::Color pixel;blank=blank&&image.GetPixel(x,y,&pixel)==Gdiplus::Ok&&pixel.GetA()==0;}
        Check(image.GetLastStatus()==Gdiplus::Ok&&blank,"Registration shutdown cleanup replaces displayed PNG content with transparent pixels");}}
    Check(aip::ReadWholeFileBytes(secondImage,unchanged)&&unchanged==secondPixels&&
        aip::ReadWholeFileBytes(nativeTarget.assetsDirectory+L"\\MediumTile.png",unchanged)&&unchanged==secondPixels,
        "clear-disabled Registration tiles and retained packages using Live Tile policy are left untouched");
    Check((hadLedger?(aip::ReadWholeFileBytes(ledgerPath,ledgerAfter,aip::gallery::MaxLedgerBytes)&&ledgerAfter==ledgerBefore):GetFileAttributesW(ledgerPath.c_str())==INVALID_FILE_ATTRIBUTES)&&
        aip::ReadWholeFileBytes(bitmapPath,unchanged)&&unchanged==sourceBefore,"Registration shutdown cleanup preserves the ownership ledger and original source photo bytes");
    auto other=target;other.root=directory;
    Check(!PrepareTileRegistrationPackage(other,true,error),"registration staging refuses a target outside its derived owned root");
    g_testTileRegistrationUserScope=L"2222222222222222";
    aip::tile_registration::Target otherUser;
    Check(BuildTileRegistrationTarget(L"reg-tile",L"Independent static feed",{},otherUser,error) && otherUser.identityName!=target.identityName && otherUser.root!=target.root,
        "different users sharing an identical INI and tile ID get separate packages and cache roots");
    Check(!PrepareTileRegistrationPackage(target,true,error),"another user cannot reuse an earlier user's explicit registration authorization");
    g_testTileRegistrationUserScope=L"1111111111111111";
    Check(TileRegistrationWriteText(target.root+L"\\registration.owner",L"foreign identity placeholder") && !PrepareTileRegistrationPackage(target,true,error),
        "an owner mismatch is preserved for recovery instead of overwriting cached user data");
    Check(RemoveRegistrationFixtureTree(g_testTileRegistrationBase,g_testTileRegistrationBase)&&RemoveRegistrationFixtureTree(cache,cache),
        "registration fixture removes only its exact known private files and directories");
    Check(DeleteFileW(transparent.c_str())!=FALSE,"registration fixture removes only its generated transparent source PNG");
    Check(DeleteFileW(g_testTileRegistrationLauncherSource.c_str())!=FALSE,"registration fixture removes its non-executed synthetic launcher template");
    g_testTileRegistrationUserScope=oldUser;g_testTileRegistrationBase=oldBase;g_testTileRegistrationLauncherSource=oldLauncher;
    g_testTileRegistrationHostAumid=oldResolver;g_testTileRegistrationRegister=oldDeploy;contentDialogResponse=oldResponse;
    gallerySetupTestDirectories=oldDirectories;
    galleryHostFrames=oldGalleryFrames;galleryHostFailures=oldGalleryFailures;galleryHostValid=oldGalleryValid;
    InvalidateTileRegistrationReadyCache();
    Check(WriteContentFixture(original),"registration fixture restores the original private INI");
    LoadRuntimeSettingsFromIni(false,false);g_liveTileUpdateMode=oldMode;g_manifestCompatibilityTarget=oldManifest;g_relaunchAfterExit=oldRelaunch;
    g_testGenerateAfterAdmission=oldAdmission;contentNativeQueueAvailable=oldNative;g_logging=oldLogging;g_notificationsEnabled=oldNotifications;
    g_notifyOnStart=oldStart;g_notifyOnSuccess=oldSuccess;g_notifyOnFailure=oldFailure;g_notifyOnBusy=oldBusy;SetLogPathCopy(oldLog);
    DeleteFileW(renderLog.c_str());
}

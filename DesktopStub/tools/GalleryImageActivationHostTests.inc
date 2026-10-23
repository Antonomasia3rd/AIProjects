// Actual XML/chaseable parser/ownership resolver with private INI+ledger only.
// Windows activation APIs, photo files and image launching remain injected.
static std::map<std::wstring,std::wstring> clickFixtureRecords;
static std::map<std::wstring,aip::gallery::InventoryFile> clickFixtureSources;
static GalleryShownActivation clickFixtureShown;
static unsigned clickFixtureOpens=0,clickFixtureSaves=0,clickFixtureOriginals=0;
static std::wstring clickFixtureOpened;
static bool clickFixtureAvailable=true,clickFixtureFileMatches=true;
static bool ClickFixtureShown(GalleryShownActivation& result){if(!clickFixtureAvailable)return false;result=clickFixtureShown;return true;}
static bool ClickFixtureLoad(const std::wstring& path,std::wstring& text){const auto found=clickFixtureRecords.find(path);if(found==clickFixtureRecords.end())return false;text=found->second;return true;}
static bool ClickFixtureSave(const std::wstring& path,const std::wstring& text){++clickFixtureSaves;clickFixtureRecords[path]=text;return true;}
static bool ClickFixtureFile(const aip::gallery::activation::Record&,const std::wstring& folder){return clickFixtureFileMatches&&folder==L"D:\\Fixture\\Owned";}
static bool ClickFixtureOpen(const std::wstring& path){++clickFixtureOpens;clickFixtureOpened=path;return true;}
static bool ClickFixtureOriginal(const std::wstring& tile,const std::wstring& image,aip::gallery::InventoryFile& file){++clickFixtureOriginals;const auto found=clickFixtureSources.find(image);if(tile!=L"one"||found==clickFixtureSources.end())return false;file=found->second;return true;}
static void TestGalleryImageActivationHost(const std::wstring& bitmapPath)
{
    using namespace aip::gallery;using namespace aip::gallery::activation;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"photo activation captures only the fixture profile");
    const auto oldUser=g_testTileRegistrationUserScope;const auto oldOptions=g_commandLine;const auto oldCommand=contentTestCommandLine;
    const auto oldRefresh=g_iniCacheRefreshMs.load();g_iniCacheRefreshMs=0;g_testTileRegistrationUserScope=L"0123456789ABCDEF";
    const auto ledgerPath=runtime::LedgerPath(g_iniPath);std::vector<BYTE> previousLedger;const bool hadLedger=ReadWholeFileBytes(ledgerPath,previousLedger);
    Check(WriteContentFixture(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=\r\n[Settings]\r\nNotificationsEnabled=0\r\n"
        L"[Gallery]\r\nEnabled=1\r\nGroupId=clicktest\r\nFolder=D:\\Fixture\\Owned\r\nTileCount=1\r\n"
        L"[Gallery.Tile.1]\r\nId=one\r\nName=Gallery one\r\nClickAction=Image\r\n"),"photo activation uses one explicitly configured private gallery");
    Configuration cfg;Check(ReadGalleryConfiguration(cfg,error)&&cfg.tiles.size()==1,"configured gallery is read through the real host");
    if(cfg.tiles.size()!=1){WriteContentFixture(original);g_testTileRegistrationUserScope=oldUser;g_commandLine=oldOptions;g_iniCacheRefreshMs=oldRefresh;return;}
    const auto imageA=L"sha256:"+std::wstring(64,L'a'),imageB=L"sha256:"+std::wstring(64,L'b');
    Ledger initial;Check(CreateLedger(cfg.groupId,L"click-epoch",true,initial,error),"create private ownership history for click resolution");
    OwnershipPlan plan;Check(ReconcileOwnership(cfg,{imageA,imageB},initial,plan,error),"reserve both click fixture images to the correct gallery tile");
    std::string wire;Check(SerializeLedger(plan.ledger,wire,error)&&WriteWholeFileBytes(ledgerPath,std::vector<BYTE>(wire.begin(),wire.end())),"commit only private fixture ledger bytes");
    g_testGalleryShownActivation=ClickFixtureShown;g_testGalleryClickLoad=ClickFixtureLoad;g_testGalleryClickSave=ClickFixtureSave;
    g_testGalleryClickFile=ClickFixtureFile;g_testGalleryClickOpen=ClickFixtureOpen;g_testGalleryClickOriginal=ClickFixtureOriginal;
    clickFixtureRecords.clear();clickFixtureSources.clear();clickFixtureOpens=clickFixtureSaves=clickFixtureOriginals=0;clickFixtureFileMatches=clickFixtureAvailable=true;
    InventoryFile source;source.ready=true;source.fileIdentity=L"identityA";source.size=12345;source.modified=67890;source.id=imageA;source.canonicalPath=L"\\\\?\\D:\\Fixture\\Owned\\first photo.jpg";
    clickFixtureSources[imageA]=source;source.id=imageB;source.fileIdentity=L"identityB";source.canonicalPath=L"\\\\?\\D:\\Fixture\\Owned\\second photo.png";clickFixtureSources[imageB]=source;
    aip::content::Snapshot a;a.active=true;a.galleryFrame=true;a.nativeQueue=true;a.galleryGroupId=cfg.groupId;a.galleryTileId=L"one";a.galleryEpoch=L"click-epoch";a.galleryImageId=imageA;
    a.secondaryTileId=NativeTileId(cfg.groupId,L"one");a.textMode=aip::content::TextMode::Off;a.backgroundId=aip::content::BackgroundId::Image;
    auto b=a;b.galleryImageId=imageB;
    const auto owner=GalleryClickOwner(g_iniPath);const auto hashes=g_testGalleryClickOwnerHashes;
    bool sameOwner=true;for(unsigned i=0;i<140;++i)sameOwner=sameOwner&&GalleryClickOwner(g_iniPath)==owner;
    Check(sameOwner&&g_testGalleryClickOwnerHashes==hashes,"a large gallery frame batch reuses its user/profile owner hash without repeating cryptographic setup");
    g_testTileRegistrationUserScope=L"FEDCBA9876543210";
    Check(GalleryClickOwner(g_iniPath)!=owner,"a changed Windows user scope cannot reuse the previous cached owner");
    g_testTileRegistrationUserScope=L"0123456789ABCDEF";Check(GalleryClickOwner(g_iniPath)==owner,"restoring the original scope restores its exact ownership token identity");
    runtime::Frame captured;captured.receipt.groupId=a.galleryGroupId;captured.receipt.tileId=a.galleryTileId;captured.receipt.epoch=a.galleryEpoch;
    captured.nativeId=a.secondaryTileId;captured.imageId=a.galleryImageId;captured.activationArguments=L"captured-folder-action";captured.clickAction=L"Setup";
    Check(GallerySnapshotForFrame(captured).activationArguments==captured.activationArguments,"snapshot conversion honors captured Setup intent without re-reading mutable gallery settings");
    captured.clickAction=L"Image";
    Check(GallerySnapshotForFrame(captured).activationArguments.find(L"--gallery-image")!=std::wstring::npos,"snapshot conversion derives image metadata directly from its immutable captured frame");
    g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    Check(PrepareGalleryImageActivation(a)&&PrepareGalleryImageActivation(b)&&clickFixtureSaves==2,"actual preparation persists exact original-path records from owned inventory metadata");
    Check(PrepareGalleryImageActivation(a)&&clickFixtureSaves==2,"unchanged original mapping is not rewritten on repeated generation");
    a.activationArguments=GalleryClickArguments(a,g_iniPath);b.activationArguments=GalleryClickArguments(b,g_iniPath);
    std::wstring tokenA,tokenB;
    Check(ReadGalleryClickPayload(a.activationArguments,a.secondaryTileId,tokenA)&&ReadGalleryClickPayload(b.activationArguments,b.secondaryTileId,tokenB)&&tokenA!=tokenB,
        "per-notification payload distinguishes two queued photos even though they share one SecondaryTile launch identity");
    std::wstring path;Check(ResolveGalleryImageToken(tokenA,a.secondaryTileId,path,error)&&path==clickFixtureSources.at(imageA).canonicalPath,
        "clicked-frame ownership token resolves its original file rather than the last submitted image");
    clickFixtureShown={a.secondaryTileId,a.activationArguments};CommandLineOptions options;options.galleryFolderActivation=true;options.windowsTileId=a.secondaryTileId;options.openFolder=cfg.folder;int result=9;
    Check(HandleGalleryImageActivation(options,result)&&result==0&&clickFixtureOpens==1&&clickFixtureOpened==clickFixtureSources.at(imageA).canonicalPath,
        "real early activation handler uses Windows-provided current metadata and injected photo viewer without creating a resident");
    clickFixtureShown.arguments=b.activationArguments;result=9;
    Check(HandleGalleryImageActivation(options,result)&&result==0&&clickFixtureOpens==2&&clickFixtureOpened==clickFixtureSources.at(imageB).canonicalPath,
        "a different current notification opens its own original even with unchanged static tile arguments");
    clickFixtureShown.arguments.clear();result=9;
    Check(!HandleGalleryImageActivation(options,result)&&clickFixtureOpens==2,"missing exact notification metadata falls through to the folder and never guesses the last submitted photo");
    clickFixtureFileMatches=false;Check(!ResolveGalleryImageToken(tokenA,a.secondaryTileId,path,error)&&clickFixtureOpens==2,"changed or unavailable original metadata prevents image launching");clickFixtureFileMatches=true;
    Check(!ReadGalleryClickPayload(a.activationArguments,NativeTileId(cfg.groupId,L"other"),tokenA),"another clicked tile cannot supply this photo's payload");
    const auto saves=clickFixtureSaves,originals=clickFixtureOriginals;g_commandLine.renderOnly=true;
    Check(!PrepareGalleryImageActivation(a)&&clickFixtureSaves==saves&&clickFixtureOriginals==originals,"offline rendering cannot access original mappings or write activation sidecars");g_commandLine.renderOnly=false;
    const auto oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const auto imageDirectory=ParentDirectoryOfPath(bitmapPath),nativeImagePath=imageDirectory+L"\\gallery-click-frame.png";
    {ScopedGdiPlus gdi;bool saved=false;
        if(gdi.ok()){Gdiplus::Bitmap image(150,150,PixelFormat32bppARGB);
            {Gdiplus::Graphics graphics(&image);graphics.Clear(Gdiplus::Color(255,20,80,140));}
            saved=SavePNG(&image,nativeImagePath.c_str());}
        Check(saved&&IsValidGeneratedPngWithDimensions(nativeImagePath,150,150),"chaseable gallery XML uses a real dimension-checked private PNG asset");}
    a.policy.included=false;a.nativeQueueTag=L"g1";
    const std::vector<LiveTileUpdateAsset> assets{{L"LiveMediumTile",L"gallery-click-frame.png",L"TileMedium",L"gallery-click-frame.png",150,150}};
    {ContentRenderScope scope(&a);ManifestDisplayInfo manifest;const auto transport=BuildLiveTileXml(imageDirectory,manifest,assets);
        aip::tile_notification::Payload payload;std::wstring issue;
        const bool decoded=aip::tile_notification::Decode(transport,payload,issue);
        Check(decoded&&payload.options.queued&&payload.options.tag==L"g1"&&payload.options.secondaryTileId==a.secondaryTileId,
            "actual gallery builder preserves valid queue metadata and its exact secondary destination");
        const auto& xml=payload.xml;
        Check(decoded&&xml.find(L"arguments=\"")!=std::wstring::npos&&xml.find(L"--gallery-image")!=std::wstring::npos&&xml.find(L"first photo.jpg")==std::wstring::npos,
            "actual native XML carries chaseable opaque identity without embedding the original photo path");}
    DeleteFileW(nativeImagePath.c_str());
    g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;
    contentTestCommandLine=L"DesktopStub.exe --ini "+aip::QuoteCommandLineArg(g_iniPath)+L" --gallery-image "+aip::QuoteCommandLineArg(tokenB);
    Check(ParseCommandLineOptions()&&g_commandLine.galleryImageToken==tokenB&&g_commandLine.galleryFolderActivation,"actual CLI parser accepts owned opaque gallery photo tokens");
    contentTestCommandLine=L"DesktopStub.exe --gallery-image D:\\program.exe";Check(!ParseCommandLineOptions(),"actual CLI parser rejects a freeform executable path as photo identity");
    Check(WriteContentFixture(L"[Tiles]\r\nSchemaVersion=1\r\nInstances=feed\r\n[Tile.feed]\r\nType=RSS\r\nName=Fixture feed\r\nClickAction=source\r\nRssFeed.FeedUrl=https://feed-fixture.invalid/feed.atom\r\n"),
        "RSS chaseable activation uses an explicitly configured private source tile");
    clickFixtureShown={L"AipSource_feed",L"--ini "+aip::QuoteCommandLineArg(g_iniPath)+L" --open-url https://feed-fixture.invalid/exact-story"};
    CommandLineOptions rssOptions;rssOptions.tileSetupKey=L"feed";ReadChaseableRssActivation(rssOptions);
    Check(rssOptions.openUrl==L"https://feed-fixture.invalid/exact-story"&&rssOptions.tileSetupKey.empty()&&clickFixtureOpens==2,
        "RSS activation consumes the current notification's exact validated URL without launching it during validation");
    rssOptions={};rssOptions.tileSetupKey=L"feed";clickFixtureShown.tileId=L"AipSource_other";ReadChaseableRssActivation(rssOptions);
    Check(rssOptions.openUrl.empty()&&rssOptions.tileSetupKey==L"feed","another source tile cannot inject chaseable RSS URL arguments");
    g_testGalleryShownActivation=nullptr;g_testGalleryClickLoad=nullptr;g_testGalleryClickSave=nullptr;g_testGalleryClickFile=nullptr;g_testGalleryClickOpen=nullptr;g_testGalleryClickOriginal=nullptr;
    clickFixtureRecords.clear();clickFixtureSources.clear();contentTestCommandLine=oldCommand;g_commandLine=oldOptions;g_testTileRegistrationUserScope=oldUser;
    if(hadLedger)WriteWholeFileBytes(ledgerPath,previousLedger);else DeleteFileW(ledgerPath.c_str());
    Check(WriteContentFixture(original),"photo click fixture restores its private settings and ownership bytes");g_iniCacheRefreshMs=oldRefresh;
}

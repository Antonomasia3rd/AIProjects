static void TestFreshRegistrationHost(const std::wstring& image)
{
    using namespace aip::content;
    const auto text=BuildInitialIniTemplate();const auto doc=aip::ParseIniDocument(text);
    const auto read=[&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;return aip::ReadIniValueFromDoc(doc,section,key,value)?value:std::wstring(fallback);};
    TileInstanceConfiguration cfg;std::wstring error;
    Check(ReadTileInstanceConfiguration(read,cfg,error)&&cfg.tiles.size()==1&&cfg.tiles[0].primary&&cfg.tiles[0].type==TileInstanceType::Wallpaper,
        "fresh factory remains exactly one primary Desktop wallpaper tile");
    if(cfg.tiles.size()!=1)return;const auto& tile=cfg.tiles[0];
    Check(tile.policy.Mode()==L"Registration"&&tile.policy.Read(L"Settings",L"PrimaryRegistration",L"")==L"Main",
        "fresh Desktop explicitly uses the original main-package Registration path");
    Check(read(L"Settings",L"RunAtStartup",L"1")==L"0"&&read(L"Settings",L"RunAtStartupPackaged",L"1")==L"0",
        "factory Registration bootstrap does not enable either login startup mechanism");
    Snapshot frame;frame.active=true;frame.backgroundId=BackgroundId::Wallpaper;frame.backgroundPath=image;frame.policy=tile.policy;
    frame.name=tile.name;frame.artworkTopic=L"desktop";frame.capturedWallpaperFit=static_cast<int>(FitMode::Fill);frame.secondaryFrameKey=ContentSnapshotKey(frame);
    Check(TileSnapshotUsesRegistration(frame)&&!TileSnapshotUsesSeparateRegistration(frame),"primary Main delivery retains Registration without inventing an independent launcher destination");
    Check(CurrentGenerationTargetAllows(frame,&cfg,tile.configuration,{},false),"first primary update is admitted before any independent Registration authorization marker exists");
    Check(!PrepareSecondaryBatch({frame},true).empty(),"factory primary enters the real publication batch without a pin or independent-package readiness circular dependency");
    auto separate=frame;separate.policy.values[L"settings.primaryregistration"]=L"Separate";
    Check(TileSnapshotUsesSeparateRegistration(separate),"explicit legacy separate-primary registration remains selectable");
    auto secondary=frame;secondary.secondaryTileId=L"AipSource_extra";
    Check(TileSnapshotUsesSeparateRegistration(secondary),"a secondary Registration tile cannot be redirected into the main package by a primary-only preference");
    auto old=frame;old.policy.values.erase(L"settings.primaryregistration");
    Check(TileSnapshotUsesSeparateRegistration(old),"existing typed profiles without the new preference retain their separate-package behavior");
    auto bad=std::wstring(L"bad");Check(!NormalizeDesktopStubTypedSetting(L"Settings",L"TileIconFont",bad,error),"global icon-font control validates the same choices as the per-tile control");
    bad=L"vector";Check(NormalizeDesktopStubTypedSetting(L"Settings",L"TileIconFont",bad,error)&&bad==L"Vector","global icon-font choice canonicalizes through the portable policy table");
}
static void TestSmtcApplicationsHost()
{
    using namespace aip::content;
    std::wstring original;ReadIniFileForMutation(original);const auto oldReader=g_contentSmtc.testReader;const auto oldFactory=g_testSelectedSmtcReader;
    const auto oldPins=CachedSecondaryTileIds();const bool known=SecondaryTileInventoryReady();const auto oldFrames=CurrentSecondarySnapshots();
    Snapshot oldPrimary;{std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);oldPrimary=g_contentSnapshot;}
    auto oldProviders=std::move(g_tileInstanceProviders);auto oldSelected=std::move(g_selectedSmtcProviders);
    const auto oldCatalog=CurrentSmtcApplications();g_tileInstanceProviders.clear();g_selectedSmtcProviders.clear();g_contentSmtc.Cancel();
    const auto oldAction=g_testMediaActionSelection;const bool oldNotify=g_notificationsEnabled.load();g_notificationsEnabled=false;
    const auto restore=[&]{StopTileInstanceProviders();g_tileInstanceProviders=std::move(oldProviders);g_selectedSmtcProviders=std::move(oldSelected);
        g_contentSmtc.Cancel();g_contentSmtc.testReader=oldReader;g_testSelectedSmtcReader=oldFactory;g_testMediaActionSelection=oldAction;g_notificationsEnabled=oldNotify;
        g_secondaryPinInventory.Reset();if(known)g_secondaryPinInventory.Complete(true,oldPins,GetTickCount64());else for(const auto& id:oldPins)g_secondaryPinInventory.Mutation(id,true);
        {std::lock_guard<std::mutex> lock(g_smtcCatalogMutex);g_smtcApplications=oldCatalog;}
        PublishContentSnapshot(oldPrimary);PublishSecondarySnapshots(oldFrames);Check(WriteContentFixture(original),"SMTC selection fixture restores the exact private INI");};
    const std::wstring profile=L"[Tiles]\r\nInstances=current,info,cover\r\n[Settings]\r\nNotifySourceErrors=0\r\n"
        L"[Tile.current]\r\nType=NowPlaying\r\nDelivery.Mode=LiveTile\r\n"
        L"[Tile.info]\r\nType=NowPlaying\r\nDelivery.Mode=LiveTile\r\nSMTC.SessionMode=Application\r\nSMTC.SessionAppId=Synthetic.A\r\nClickAction=pause\r\n"
        L"[Tile.cover]\r\nType=NowPlaying\r\nDelivery.Mode=LiveTile\r\nSMTC.SessionMode=Application\r\nSMTC.SessionAppId=Synthetic.A\r\nContent.Background=SMTCArtwork\r\nTextMode=Off\r\n";
    Check(WriteContentFixture(profile),"SMTC selection fixture writes three independent private source preferences");
    TileInstanceConfiguration cfg;std::wstring error;if(!ReadCurrentTileInstances(cfg,error)||cfg.tiles.size()!=3){Check(false,"SMTC source preferences validate through real typed configuration");restore();return;}
    std::vector<std::wstring> ids;for(const auto& tile:cfg.tiles)ids.push_back(TileInstanceNativeId(tile));g_secondaryPinInventory.Complete(true,std::set<std::wstring>(ids.begin(),ids.end()),GetTickCount64());
    BITMAPFILEHEADER file{};file.bfType=0x4d42;file.bfSize=58;file.bfOffBits=54;BITMAPINFOHEADER image{};
    image.biSize=sizeof(image);image.biWidth=1;image.biHeight=1;image.biPlanes=1;image.biBitCount=24;image.biSizeImage=4;
    std::vector<BYTE> bytes(58,128);memcpy(bytes.data(),&file,sizeof(file));memcpy(bytes.data()+14,&image,sizeof(image));
    std::atomic<unsigned> currentReads{0},applicationReads{0};
    g_contentSmtc.testReader=[&](auto,bool,const auto&){++currentReads;SmtcSessionData data;data.hasSession=true;data.title=L"Current B";data.appId=L"Synthetic.B";data.availableApplications={L"Synthetic.A",L"Synthetic.B"};return data;};
    g_testSelectedSmtcReader=[&](const SmtcSelection& selection){return [&,selection](auto,bool art,const auto&){++applicationReads;SmtcSessionData data;
        data.hasSession=true;data.title=selection.appId;data.appId=selection.appId;data.availableApplications={L"Synthetic.A",L"Synthetic.B"};if(art)data.artworkBytes=bytes;return data;};};
    TileInstanceEngineTick(true,false);const auto frames=CurrentSecondarySnapshots();
    const auto find=[&](const std::wstring& id)->const Snapshot*{for(const auto& frame:frames)if(frame.secondaryTileId==id)return &frame;return nullptr;};
    const auto current=find(ids[0]),info=find(ids[1]),cover=find(ids[2]);
    Check(currentReads==1&&applicationReads==1&&g_selectedSmtcProviders.size()==1,"two chosen-app information/art tiles share one source worker while Windows-current uses its own coherent snapshot");
    Check(current&&info&&cover&&current->text.primary==L"Current B"&&info->text.primary==L"Synthetic.A"&&cover->backgroundLease&&cover->textMode==TextMode::Off,
        "application selection keeps its metadata and artwork separate from the other current application");
    ResetTileInstanceMenuCommands();HMENU menu=CreatePopupMenu();AppendTileInstanceDetails(menu,cfg.tiles[1]);UINT choose=0;
    for(const auto& command:g_tileInstanceMenuCommands)if(command.second.verb==TileInstanceMenuVerb::SelectSmtcSource&&command.second.id==L"info"&&command.second.value==L"Synthetic.B")choose=command.first;
    DestroyMenu(menu);Check(choose&&DispatchTileInstanceCommand(nullptr,choose)&&ReadCurrentTileInstances(cfg,error)&&cfg.tiles[1].configuration.smtc.selection.appId==L"Synthetic.B",
        "cached application picker atomically selects the requested application without a native media query on the UI thread");
    std::wstring actionApp;g_testMediaActionSelection=[&](const auto&,const SmtcSelection& selection){actionApp=selection.appId;return true;};
    Check(ActivateTileInstance(nullptr,L"info",false),"source-specific playback activation is accepted by its normal tile-action route");ShutdownMediaTileActions();
    Check(actionApp==L"Synthetic.B","playback controls receive the configured application rather than silently targeting Windows-current");
    g_secondaryPinInventory.Complete(true,{ids[0]},GetTickCount64());TileInstanceEngineTick(true,false);
    Check(g_selectedSmtcProviders.empty(),"unpinning the last chosen-app tile stops and releases its independent provider");
    restore();
}

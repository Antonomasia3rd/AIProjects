// Actual tray migration, asynchronous confirmation and queued first frame.
// Files are private; deployment, notifications and pin UI remain injected/inert.
static std::wstring deliveryRegressionToken;
static bool DeliveryRegressionToast(const std::wstring&,const std::wstring&,const std::wstring&,const std::wstring& token)
{deliveryRegressionToken=token;return true;}
static void TestTileDeliveryRegressionHost(const std::wstring& bitmapPath)
{
    std::wstring original;Check(ReadIniFileForMutation(original),"delivery regression captures the private profile");
    const auto directory=ParentDirectoryOfPath(bitmapPath),root=directory+L"\\delivery-regression";
    const auto oldUser=g_testTileRegistrationUserScope,oldBase=g_testTileRegistrationBase,oldLauncher=g_testTileRegistrationLauncherSource;
    const auto oldAumid=g_testTileRegistrationHostAumid;const auto oldRegister=g_testTileRegistrationRegister;
    const auto oldToast=g_testNotificationConfirmationToast;const auto oldOffer=g_testTileDeliveryMigration;
    const auto oldEnabled=g_notificationsEnabled.load(),oldRunning=g_running.load();
    const auto oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    const auto oldRetry=g_contentQueueRetryAfter.load();
    const auto oldPage=g_tileInstanceMenuPage;
    const auto oldNext=g_nextTileInstanceMenuCommand;
    const auto oldCommands=g_tileInstanceMenuCommands;
    const auto oldBatches=secondaryTestBatches;const auto oldSnapshots=CurrentSecondarySnapshots();
    std::map<std::wstring,std::pair<std::wstring,ULONGLONG>> oldNotices;
    {std::lock_guard<std::mutex> lock(g_notificationProgressMutex);oldNotices=g_notificationRecent;}
    std::set<std::wstring> oldRefresh;{std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);oldRefresh=g_tileInstanceRefreshRequests;g_tileInstanceRefreshRequests.clear();}
    Check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"delivery regression creates only its private root");
    g_testTileRegistrationUserScope=L"3333333333333333";g_testTileRegistrationBase=root+L"\\packages";
    g_testTileRegistrationLauncherSource=root+L"\\inert-launcher.exe";g_testTileRegistrationHostAumid=RegistrationHostAumid;g_testTileRegistrationRegister=nullptr;
    Check(WriteWholeFileBytes(g_testTileRegistrationLauncherSource,{'M','Z','n','o','t','-','e','x','e','c','u','t','e','d'}),
        "delivery regression writes a nonexecuted launcher template");
    g_notificationsEnabled=true;g_running=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    g_contentQueueRetryAfter=0;g_testNotificationConfirmationToast=DeliveryRegressionToast;g_testTileDeliveryMigration=OfferTileDeliveryMigration;
    const auto profile=L"[Settings]\r\nNotificationConfirmations=1\r\n[Tiles]\r\nInstances=desktop\r\n[Tile.desktop]\r\nType=Wallpaper\r\nName=Desktop\r\nPrimary=1\r\nDelivery.Mode=LiveTile\r\nContent.Background=Image\r\nContent.ImagePath="+bitmapPath+L"\r\n";
    Check(WriteContentFixture(profile),"delivery regression selects one independent primary Desktop tile");
    HMENU menu=CreatePopupMenu();g_tileInstanceMenuPage=0;AppendTileInstancesMenu(menu);
    UINT command=0;for(const auto& row:g_tileInstanceMenuCommands)if(row.second.id==L"desktop"&&row.second.key==L"Delivery.Mode"&&row.second.value==L"Registration")command=row.first;
    DestroyMenu(menu);
    aip::tile_registration::Target target;std::wstring error;
    Check(command&&BuildTileRegistrationTarget(L"desktop",L"Desktop",{},target,error),"delivery regression resolves the real registered tray migration command");
    deliveryRegressionToken.clear();const auto dialogs=contentDialogs;
    Check(DispatchTileInstanceCommand(nullptr,command)&&!deliveryRegressionToken.empty()&&contentDialogs==dialogs&&
        IniReadS(L"Tile.desktop",L"Delivery.Mode",L"")==L"LiveTile"&&GetFileAttributesW(target.root.c_str())==INVALID_FILE_ATTRIBUTES,
        "requesting migration queues an actionable confirmation without a modal or changing delivery/files");
    QueueUserConfirmationAction(L"confirm="+deliveryRegressionToken+L"&choice=no");DrainUserConfirmations();
    Check(IniReadS(L"Tile.desktop",L"Delivery.Mode",L"")==L"LiveTile"&&GetFileAttributesW(target.root.c_str())==INVALID_FILE_ATTRIBUTES,
        "No on the actionable confirmation preserves Live Tile mode and the absent Registration package");
    Check(DispatchTileInstanceCommand(nullptr,command),"delivery regression queues a second real migration request");
    const auto staleToken=deliveryRegressionToken;
    Check(SaveTileInstanceSettings(nullptr,{{L"Tile.desktop",L"Name",L"Edited while pending"}},L"desktop"),
        "delivery regression edits the selected tile while its confirmation is pending");
    QueueUserConfirmationAction(L"confirm="+staleToken+L"&choice=yes");DrainUserConfirmations();
    Check(IniReadS(L"Tile.desktop",L"Delivery.Mode",L"")==L"LiveTile"&&GetFileAttributesW(target.root.c_str())==INVALID_FILE_ATTRIBUTES,
        "a stale Yes cannot stage files or change a tile edited after its confirmation was requested");
    bool staleGuidance=false;{std::lock_guard<std::mutex> lock(g_notificationProgressMutex);const auto notice=g_notificationRecent.find(L"delivery-failed-desktop");staleGuidance=notice!=g_notificationRecent.end()&&notice->second.first.find(L"settings changed")!=std::wstring::npos;}
    Check(staleGuidance,"stale migration approval records actionable reopen-settings guidance");
    Check(WriteContentFixture(profile)&&DispatchTileInstanceCommand(nullptr,command),"delivery regression queues confirmation against the current tile settings");
    QueueUserConfirmationAction(L"confirm="+deliveryRegressionToken+L"&choice=yes");DrainUserConfirmations();
    aip::content::TileInstanceConfiguration cfg;
    Check(ReadCurrentTileInstances(cfg,error)&&cfg.tiles.size()==1&&cfg.tiles.front().policy.Mode()==L"Registration"&&TileRegistrationReadyForId(L"desktop"),
        "accepting the real tray action persists Registration and authorizes only that independent package");
    bool refresh=false;{std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);refresh=g_tileInstanceRefreshRequests.count(L"desktop")!=0;}
    Check(refresh,"accepted delivery migration explicitly requests its first content refresh");
    secondaryTestBatches.clear();
    Check(TileInstanceEngineTick(false,true)&&!secondaryTestBatches.empty()&&secondaryTestBatches.back().size()==1&&
        secondaryTestBatches.back().front().policy.owner==L"desktop"&&TileSnapshotUsesRegistration(secondaryTestBatches.back().front()),
        "the ordinary instance poller queues the first separate Registration generation after accepted migration");
    Check(GetFileAttributesW((target.root+L"\\registration.published").c_str())==INVALID_FILE_ATTRIBUTES&&
        TileRegistrationSetupStatus(L"desktop").find(L"authorized")!=std::wstring::npos,
        "queued setup status distinguishes authorization from a completed native package publication");
    ClearUserConfirmations();StopTileInstanceProviders();PublishSecondarySnapshots(oldSnapshots);secondaryTestBatches=oldBatches;
    {std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);g_tileInstanceRefreshRequests=oldRefresh;}
    g_testNotificationConfirmationToast=oldToast;g_testTileDeliveryMigration=oldOffer;
    g_testTileRegistrationUserScope=oldUser;g_testTileRegistrationBase=oldBase;g_testTileRegistrationLauncherSource=oldLauncher;
    g_testTileRegistrationHostAumid=oldAumid;g_testTileRegistrationRegister=oldRegister;
    g_notificationsEnabled=oldEnabled;g_running=oldRunning;g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;g_contentQueueRetryAfter=oldRetry;
    g_tileInstanceMenuPage=oldPage;g_nextTileInstanceMenuCommand=oldNext;g_tileInstanceMenuCommands=oldCommands;
    {std::lock_guard<std::mutex> lock(g_notificationProgressMutex);g_notificationRecent=std::move(oldNotices);}
    InvalidateTileRegistrationReadyCache();
    Check(WriteContentFixture(original)&&RemoveRegistrationFixtureTree(root,root),"delivery regression restores the private profile and removes only its known private root");
}

// Fresh/private profile, real in-memory HMENU construction and activation
// routing. Notification delivery/forwarding is injected; no user pin or app.
static std::vector<std::pair<std::wstring,bool>> stalePinFixtureNotices;
static unsigned stalePinFixtureForwarded=0;
static bool StalePinFixtureNoForward(const std::wstring&,bool){return false;}
static bool StalePinFixtureForward(const std::wstring&,bool){++stalePinFixtureForwarded;return true;}
static void StalePinFixtureNotice(const std::wstring&,const std::wstring& message,bool missing){stalePinFixtureNotices.emplace_back(message,missing);}
static unsigned CountGalleryFixtureSubmenus(HMENU menu,const std::wstring& caption)
{
    unsigned count=0;for(int index=0;index<GetMenuItemCount(menu);++index){wchar_t text[256]{};MENUITEMINFOW info{};info.cbSize=sizeof(info);
        info.fMask=MIIM_STRING|MIIM_SUBMENU;info.dwTypeData=text;info.cch=static_cast<UINT>(std::size(text)-1);
        if(GetMenuItemInfoW(menu,static_cast<UINT>(index),TRUE,&info)&&info.hSubMenu&&caption==text)++count;
    }return count;
}
static void TestStalePinAndGalleryMenuHost()
{
    std::wstring original,error;Check(ReadIniFileForMutation(original),"stale pin/menu fixture captures its private profile");
    const auto savedOptions=g_commandLine;const auto savedNotifications=g_notificationsEnabled.load();const auto savedRefresh=g_iniCacheRefreshMs.load();
    const auto oldForward=g_testForwardTileActivationNotice;const auto oldNotice=g_testTileActivationNotice;
    g_commandLine={};g_notificationsEnabled=true;g_iniCacheRefreshMs=0;
    g_testForwardTileActivationNotice=StalePinFixtureNoForward;g_testTileActivationNotice=StalePinFixtureNotice;stalePinFixtureNotices.clear();
    const std::wstring fresh=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=desktop\r\n[Tile.desktop]\r\nType=Wallpaper\r\nName=Desktop\r\nPrimary=1\r\n"
        L"[Settings]\r\nNotificationsEnabled=1\r\nNotifyOnOperations=0\r\n";
    Check(WriteContentFixture(fresh),"fresh Desktop-only profile contains no configured gallery");
    aip::gallery::Configuration cfg;Check(ReadGalleryConfiguration(cfg,error)&&cfg.groupId.empty()&&cfg.tiles.size()==2,
        "the gallery parser's two unused default slots exist as configuration placeholders only");
    const auto placeholders=GalleryGroups(cfg);
    Check(placeholders.size()==1&&placeholders.front().name==L"Gallery"&&placeholders.front().indices.size()==2,
        "empty captions are normalized before matching so placeholder rows cannot create duplicate Gallery groups");
    HMENU parent=CreatePopupMenu();Check(parent!=nullptr,"create only an in-memory gallery test menu");
    if(parent){AppendGalleryTilesMenu(parent);const auto gallery=GetSubMenu(parent,0);
        Check(gallery&&CountGalleryFixtureSubmenus(gallery,L"Gallery")==0&&CountGalleryFixtureSubmenus(gallery,L"Gallery settings")==1,
            "actual fresh Photo galleries menu shows setup/settings without either phantom Gallery subgroup");DestroyMenu(parent);}
    auto groupsCfg=cfg;groupsCfg.groupId=L"menus";groupsCfg.folder=L"D:\\Fixture\\Art";groupsCfg.enabled=true;
    groupsCfg.tiles[0].id=L"one";groupsCfg.tiles[1].id=L"two";groupsCfg.tiles[1].folder=L"d:\\Fixture\\Art\\";
    Check(GalleryGroups(groupsCfg).size()==1,"equivalent folder paths and a trailing separator remain one gallery menu group");
    groupsCfg.tiles[1].galleryName=L"A separate named gallery";
    Check(GalleryGroups(groupsCfg).size()==2,"explicit distinct gallery names for the same folder are preserved");
    groupsCfg.tiles[1].galleryName.clear();groupsCfg.tiles[1].folder=L"D:\\Other\\Art";
    Check(GalleryGroups(groupsCfg).size()==2,"different folders with the same leaf name are not incorrectly merged");
    const auto dialogs=contentDialogs;
    Check(!ActivateTileInstance(nullptr,L"retired-feed",false)&&stalePinFixtureNotices.size()==1&&stalePinFixtureNotices.back().second,
        "stale source pin emits one missing-profile notification despite disabled routine operation notices");
    CommandLineOptions gallery;gallery.galleryFolderActivation=true;gallery.windowsTileId=aip::gallery::NativeTileId(L"retired",L"one");gallery.openFolder=L"D:\\Old gallery";int result=0;
    Check(HandleGalleryImageActivation(gallery,result)&&result==2&&stalePinFixtureNotices.size()==2&&stalePinFixtureNotices.back()==stalePinFixtureNotices.front()&&contentDialogs==dialogs,
        "stale Gallery pin uses identical missing-profile wording and notification routing without a CLI MessageBox");
    Check(!ActivateTileInstance(nullptr,aip::gallery::GalleryRegistrationId(L"retired",L"one"),false)&&stalePinFixtureNotices.size()==3&&stalePinFixtureNotices.back()==stalePinFixtureNotices.front(),
        "stale Registration gallery pin reports the same missing-profile condition without becoming another source action");
    Check(WriteContentFixture(fresh+L"[Gallery]\r\nTileCount=not-a-number\r\n"),"create an explicitly invalid private gallery configuration");
    result=0;Check(HandleGalleryImageActivation(gallery,result)&&result==2&&stalePinFixtureNotices.size()==4&&!stalePinFixtureNotices.back().second&&
        stalePinFixtureNotices.back().first!=stalePinFixtureNotices.front().first&&contentDialogs==dialogs,
        "invalid gallery configuration is an actionable failure rather than falsely claiming a missing pin");
    Check(WriteContentFixture(fresh),"restore valid private configuration before forwarding checks");
    const auto notices=stalePinFixtureNotices.size();stalePinFixtureForwarded=0;g_testForwardTileActivationNotice=StalePinFixtureForward;
    NotifyUnconfiguredStartPin(gallery.windowsTileId);
    Check(stalePinFixtureForwarded==1&&stalePinFixtureNotices.size()==notices,"one-shot Gallery error can delegate notice delivery to the profile's existing tray resident");
    g_notificationsEnabled=false;NotifyUnconfiguredStartPin(gallery.windowsTileId);
    Check(stalePinFixtureForwarded==1&&stalePinFixtureNotices.size()==notices,"master mute prevents forwarding and local stale-pin notification delivery");
    g_notificationsEnabled=true;g_commandLine.configureOnly=true;NotifyUnconfiguredStartPin(gallery.windowsTileId);
    Check(stalePinFixtureForwarded==1&&stalePinFixtureNotices.size()==notices,"configuration-only validation cannot forward or display a stale-pin notification");
    Check(TileActivationNoticeId(L"AipGallery_group.tile")&&!TileActivationNoticeId(L"D:\\program.exe")&&!TileActivationNoticeId(std::wstring(129,L'x')),
        "activation notice IPC accepts bounded identities and cannot carry paths or executable commands");
    g_testForwardTileActivationNotice=oldForward;g_testTileActivationNotice=oldNotice;g_commandLine=savedOptions;g_notificationsEnabled=savedNotifications;
    Check(WriteContentFixture(original),"stale pin/menu fixture restores its private profile");g_iniCacheRefreshMs=savedRefresh;stalePinFixtureNotices.clear();
}

static std::vector<std::wstring> galleryConfirmationFixtureTokens;
static bool GalleryConfirmationFixtureToast(const std::wstring&,const std::wstring&,const std::wstring&,const std::wstring& token)
{galleryConfirmationFixtureTokens.push_back(token);return true;}
static void TestGalleryConfirmationContinuations()
{
    std::wstring original,error;Check(ReadIniFileForMutation(original),"gallery confirmation captures only the private fixture profile");
    const auto oldOptions=g_commandLine;const auto oldRunning=g_running.load(),oldNotifications=g_notificationsEnabled.load();
    const auto oldRefresh=g_iniCacheRefreshMs.load();const auto oldToast=g_testNotificationConfirmationToast;
    std::wstring profile;{std::lock_guard<std::mutex> lock(g_pathMutex);profile=g_iniPath;}
    g_commandLine={};g_running=true;g_notificationsEnabled=true;g_iniCacheRefreshMs=0;
    ClearUserConfirmations();galleryConfirmationFixtureTokens.clear();g_testNotificationConfirmationToast=GalleryConfirmationFixtureToast;
    const std::wstring base=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=desktop\r\n[Tile.desktop]\r\nType=Wallpaper\r\nPrimary=1\r\n"
        L"[Settings]\r\nNotificationsEnabled=1\r\nNotifyOnOperations=0\r\nNotificationConfirmations=1\r\n"
        L"AutoUnpinRemovedGalleryTiles=1\r\nConfirmUnpinRemovedGalleryTiles=1\r\nNotifyGalleryChanges=0\r\n"
        L"[Gallery]\r\nEnabled=1\r\nGroupId=confirmfixture\r\nFolder=D:\\Fixture\\Confirm\\Photos\r\nTileCount=2\r\n"
        L"[Gallery.Tile.1]\r\nId=one\r\nName=First photo\r\nClickAction=Image\r\nDelivery.Mode=Registration\r\n"
        L"[Gallery.Tile.2]\r\nId=two\r\nName=Second photo\r\nClickAction=Image\r\nDelivery.Mode=LiveTile\r\n";
    const auto nativeId=aip::gallery::NativeTileId(L"confirmfixture",L"one");
    const auto reply=[&](const std::wstring& choice){
        if(galleryConfirmationFixtureTokens.empty()){Check(false,"a gallery continuation must have a captured confirmation token");return;}
        QueueUserConfirmationAction(L"confirm="+galleryConfirmationFixtureTokens.back()+L"&choice="+choice);DrainUserConfirmations();
    };
    const auto mode=[&](){aip::gallery::Configuration value;return ReadGalleryConfiguration(value,error)&&!value.tiles.empty()?value.tiles.front().delivery.Mode():std::wstring();};
    const auto dialogs=contentDialogs;
    Check(WriteContentFixture(base),"prepare a two-row private gallery without initializing photo ownership");
    Check(OfferGalleryTileDeliveryMigration(nullptr,nativeId,L"LiveTile")&&mode()==L"Registration"&&galleryConfirmationFixtureTokens.size()==1,
        "Gallery delivery choice queues consent without changing its Registration configuration");
    reply(L"no");std::wstring current;Check(ReadIniFileForMutation(current)&&current==base,
        "No on Gallery migration leaves every private INI byte unchanged");
    Check(OfferGalleryTileDeliveryMigration(nullptr,nativeId,L"LiveTile"),"queue Gallery delivery before a concurrent click-action edit");
    auto changed=base;Check(WriteContentProfileSetting(changed,{L"Gallery.Tile.1",L"ClickAction",L"Setup"})&&WriteContentFixture(changed),
        "independent click-action edit is written while Gallery consent is pending");
    reply(L"yes");Check(ReadIniFileForMutation(current)&&current==changed&&mode()==L"Registration",
        "accepted stale Gallery migration preserves the newer click action and does not overwrite delivery");
    Check(WriteContentFixture(base)&&OfferGalleryTileDeliveryMigration(nullptr,nativeId,L"LiveTile"),"queue Gallery migration before a profile switch");
    {std::lock_guard<std::mutex> lock(g_pathMutex);g_iniPath=profile+L".other-unwritten-profile";}
    reply(L"yes");{std::lock_guard<std::mutex> lock(g_pathMutex);g_iniPath=profile;}
    Check(ReadIniFileForMutation(current)&&current==base,"a Gallery answer from another profile cannot write the captured profile");
    ClearUserConfirmations();
    Check(OfferGalleryTileDeliveryMigration(nullptr,nativeId,L"LiveTile")&&mode()==L"Registration","queue current Gallery migration");
    reply(L"yes");Check(mode()==L"LiveTile","accepted current Gallery migration saves its local delivery without native pin UI");
    Check(ReadIniFileForMutation(current),"capture committed Gallery migration bytes");const auto committed=current;
    reply(L"yes");Check(ReadIniFileForMutation(current)&&current==committed,"replayed Gallery confirmation cannot execute migration twice");

    // The production shrink continuation performs the complete guarded save;
    // its native unpin sink remains inert and no photo ledger is reset.
    auto live=base;Check(WriteContentProfileSetting(live,{L"Gallery.Tile.1",L"Delivery.Mode",L"LiveTile"})&&WriteContentFixture(live),
        "prepare two Live Tile rows for real membership continuation checks");
    aip::gallery::Configuration before;Check(ReadGalleryConfiguration(before,error)&&before.tiles.size()==2,"read the exact private gallery membership");
    auto after=before;after.tiles.resize(1);auto edit=GalleryEdit(before);
    Check(ConfirmGalleryMembershipChange(nullptr,after,edit,false)&&ReadIniFileForMutation(current)&&current==live,
        "gallery shrink waits for consent before saving its smaller membership");
    reply(L"no");Check(ReadGalleryConfiguration(before,error)&&before.tiles.size()==2,"No on gallery shrink keeps both rows configured");
    edit=GalleryEdit(before);after=before;after.tiles.resize(1);
    Check(ConfirmGalleryMembershipChange(nullptr,after,edit,false),"queue gallery shrink before a concurrent metadata edit");
    changed=live;Check(WriteContentProfileSetting(changed,{L"Gallery.Tile.2",L"Name",L"Renamed while pending"})&&WriteContentFixture(changed),
        "write an independent gallery-row edit while shrink confirmation is pending");
    reply(L"yes");Check(ReadIniFileForMutation(current)&&current==changed&&ReadGalleryConfiguration(before,error)&&before.tiles.size()==2,
        "accepted stale gallery shrink preserves new metadata and all current rows");
    edit=GalleryEdit(before);after=before;after.tiles.resize(1);
    Check(ConfirmGalleryMembershipChange(nullptr,after,edit,false),"queue a current gallery shrink");
    reply(L"yes");Check(ReadGalleryConfiguration(before,error)&&before.tiles.size()==1&&before.tiles.front().id==L"one",
        "accepted current gallery shrink saves only the requested membership through its actual transaction");
    Check(contentDialogs==dialogs,"modern Gallery Yes/No continuations never fall back to a blocking host dialog");
    ClearUserConfirmations();g_testNotificationConfirmationToast=oldToast;galleryConfirmationFixtureTokens.clear();
    {std::lock_guard<std::mutex> lock(g_pathMutex);g_iniPath=profile;}
    Check(WriteContentFixture(original),"Gallery continuation checks restore their private profile");
    g_iniCacheRefreshMs=oldRefresh;g_commandLine=oldOptions;g_running=oldRunning;g_notificationsEnabled=oldNotifications;
}

// Private profiles, injected RSS readers and in-memory pin inventory only.
// No real network, package pin, account, hardware or resident operation runs.
static void TestTileInstanceRuntimeHost()
{
    using namespace aip::content;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"instance runtime fixture captures its private profile");
    const auto savedPins=CachedSecondaryTileIds();const bool pinsKnown=SecondaryTileInventoryReady();
    const auto oldRefresh=g_iniCacheRefreshMs.load();g_iniCacheRefreshMs=0;
    const auto savedGallery=galleryHostFrames;galleryHostFrames.clear();
    StopTileInstanceProviders();
    std::wstring profile=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=";
    for(unsigned i=1;i<=40;++i){if(i>1)profile+=L",";profile+=L"feed"+std::to_wstring(i);}
    profile+=L"\r\n[Content]\r\nCount=invalid-unused-global\r\n[SMTC]\r\nIndicator=invalid-unused-global\r\n";
    for(unsigned i=1;i<=40;++i)profile+=L"[Tile.feed"+std::to_wstring(i)+L"]\r\nType=RSS\r\nName=Feed "+std::to_wstring(i)+
        L"\r\nRssFeed.FeedUrl=https://rss-fixture.invalid/feed"+std::to_wstring(i)+L".atom\r\nRssFeed.UpdateIntervalSeconds="+
        std::to_wstring(i*60)+L"\r\nRssFeed.HttpTimeoutSeconds=5\r\nTextPreset=RssHeadlines\r\n";
    Check(WriteContentFixture(profile),"create forty independent RSS instances without changing a normal profile");
    TileInstanceConfiguration cfg;
    Check(ReadCurrentTileInstances(cfg,error)&&cfg.tiles.size()==40&&cfg.tiles.front().configuration.entries.size()==1,
        "instance reader supports more than thirty-two tiles and ignores unused global source settings");
    std::set<std::wstring> pins;for(unsigned i=1;i<=5;++i)pins.insert(L"AipSource_feed"+std::to_wstring(i));
    g_secondaryPinInventory.Complete(true,pins,GetTickCount64());
    std::array<std::atomic<unsigned>,5> calls{};
    for(auto& count:calls)count=0;
    std::array<RssFeedSettings,5> received;
    std::array<std::weak_ptr<int>,5> readerLifetimes;
    for(unsigned i=0;i<5;++i){
        auto state=std::make_unique<TileInstanceProviderState>();state->type=TileInstanceType::Rss;
        state->rss=std::make_unique<ContentRssProvider>();
        auto token=std::make_shared<int>(static_cast<int>(i));readerLifetimes[i]=token;
        state->rss->testReader=[&,i,token](const RssFeedSettings& settings){
            received[i]=settings;++calls[i];RssFeedSnapshot feed;feed.ok=true;feed.sourceTitle=L"Synthetic feed "+std::to_wstring(i+1);
            feed.items.push_back({L"Headline "+std::to_wstring(i+1),L"Summary "+std::to_wstring(i+1),L"https://rss-fixture.invalid/article",L""});return feed;
        };
        g_tileInstanceProviders[L"feed"+std::to_wstring(i+1)]=std::move(state);
    }
    const auto beforeWallpaper=contentWallpaperBackgroundRequests;
    Check(TileInstanceEngineTick(true,false),"initial instance composition completes only injected RSS readers");
    auto frames=CurrentSecondarySnapshots();
    bool primaryActive=false;{std::lock_guard<std::mutex> lock(g_contentSnapshotMutex);primaryActive=g_contentSnapshot.active;}
    Check(frames.size()==5&&g_tileInstanceProviders.size()==5&&!primaryActive,
        "only five pinned instances create provider state and snapshots; no implicit Desktop tile is activated");
    bool separate=true;
    for(unsigned i=0;i<5;++i){
        const auto id=L"AipSource_feed"+std::to_wstring(i+1);
        const auto frame=std::find_if(frames.begin(),frames.end(),[&](const auto& value){return value.secondaryTileId==id;});
        separate=separate&&calls[i]==1&&received[i].feedUrl==L"https://rss-fixture.invalid/feed"+std::to_wstring(i+1)+L".atom"&&
            received[i].updateIntervalSeconds==static_cast<int>((i+1)*60)&&frame!=frames.end()&&frame->text.primary==L"Headline "+std::to_wstring(i+1)&&
            frame->presentation.preset==L"RssHeadlines"&&frame->rssHeadlines.size()==1;
    }
    Check(separate,"five RSS tile URLs, refresh intervals, rich layouts and headline snapshots remain isolated");
    for(unsigned tick=0;tick<20;++tick)TileInstanceEngineTick(false,false);
    bool idle=true;for(const auto& count:calls)idle=idle&&count==1;
    Check(idle&&contentWallpaperBackgroundRequests==beforeWallpaper,
        "unchanged ticks neither refetch before each feed's deadline nor query wallpaper when no tile needs it");
    const auto privateTransparentBackground = [&](const std::wstring& path) {
        std::uint64_t hash = 14695981039346656037ull; HashWide(hash,g_iniPath);
        const auto expected = GetLiveWallpaperSnapshotDirectory()+L"\\ContentTransparent-"+Hex64(hash)+L".png";
        const auto fixtureFolder = FileNameFromPath(ParentDirectoryOfPath(g_iniPath));
        return path == expected && FileNameFromPath(g_iniPath) == L"content.ini" &&
            fixtureFolder.compare(0,23,L"DesktopStubContentTest-") == 0;
    };
    const auto deletedBackground = frames.empty()?std::wstring():frames.front().backgroundPath;
    const bool mayDeleteBackground = privateTransparentBackground(deletedBackground);
    Check(mayDeleteBackground&&IsUsableWallpaperPath(deletedBackground),"missing-background fixture owns exactly its private profile's synthetic transparent PNG");
    if(mayDeleteBackground){
        Check(DeleteFileW(deletedBackground.c_str())&&!IsUsableWallpaperPath(deletedBackground),"remove only the private transparent source image to simulate temporary-file cleanup");
        const auto beforeRepair=contentBackgroundRequests;
        TileInstanceEngineTick(false,false);frames=CurrentSecondarySnapshots();
        bool feedsRetained=frames.size()==5&&contentBackgroundRequests==beforeRepair+1;
        for(unsigned i=0;i<5;++i){
            const auto id=L"AipSource_feed"+std::to_wstring(i+1);
            const auto retained=std::find_if(frames.begin(),frames.end(),[&](const auto& frame){return frame.secondaryTileId==id;});
            feedsRetained=feedsRetained&&retained!=frames.end()&&IsUsableWallpaperPath(retained->backgroundPath)&&calls[i]==1;
        }
        Check(feedsRetained&&contentWallpaperBackgroundRequests==beforeWallpaper,
            "one cheap recreation restores all five pinned text feeds after their shared temporary background disappears without refetching feeds or querying wallpaper");
    }
    auto firstSettings=received[0];
    RefreshTileInstanceNow(L"feed1");TileInstanceEngineTick(false,false);
    Check(g_tileInstanceProviders.at(L"feed1")->rss->source.Wait(ContentRssProvider::Key(firstSettings),milliseconds(2500)),
        "an explicit refresh completes the selected tile's injected request before its normal deadline");
    bool selectedOnly=calls[0]==2;for(unsigned i=1;i<5;++i)selectedOnly=selectedOnly&&calls[i]==1;
    Check(selectedOnly,"refreshing one tile does not force the other four feeds");
    auto edited=profile;
    Check(WriteIniValueToText(edited,L"Tile.feed2",L"RssFeed.FeedUrl",L"https://rss-fixture.invalid/changed.atom")&&
        WriteIniValueToText(edited,L"Tile.feed2",L"RssFeed.UpdateIntervalSeconds",L"3600")&&WriteContentFixture(edited),
        "edit only one instance's URL and cadence in the private profile");
    RssFeedSettings changed=received[1];changed.feedUrl=L"https://rss-fixture.invalid/changed.atom";changed.updateIntervalSeconds=3600;
    TileInstanceEngineTick(false,false);
    Check(g_tileInstanceProviders.at(L"feed2")->rss->source.Wait(ContentRssProvider::Key(changed),milliseconds(2500)),
        "a changed RSS identity finishes through its own worker");
    TileInstanceEngineTick(false,false);
    Check(calls[1]==2&&received[1].feedUrl==changed.feedUrl&&received[1].updateIntervalSeconds==3600&&calls[0]==2&&calls[2]==1&&calls[3]==1&&calls[4]==1,
        "changing one URL and cadence preserves every other source's cache and deadline");
    std::array<TileInstanceProviderState*,4> healthyStates{};
    for(unsigned i=0;i<4;++i)healthyStates[i]=g_tileInstanceProviders.at(L"feed"+std::to_wstring(i+1)).get();
    auto invalid=edited;
    Check(WriteIniValueToText(invalid,L"Tile.feed5",L"RssFeed.FeedUrl",L"javascript:invalid")&&WriteContentFixture(invalid)&&
        ReadCurrentTileInstances(cfg,error)&&cfg.tiles.size()==40,
        "a single malformed URL is retained as a per-tile configuration error rather than rejecting the collection");
    Check(!cfg.tiles[4].error.empty()&&cfg.tiles[0].error.empty()&&cfg.tiles[1].error.empty()&&cfg.tiles[2].error.empty()&&cfg.tiles[3].error.empty(),
        "tolerant reads scope a malformed RSS field to its owning tile");
    TileInstanceEngineTick(false,false);frames=CurrentSecondarySnapshots();
    bool preserved=true;
    for(unsigned i=0;i<4;++i){
        const auto id=L"feed"+std::to_wstring(i+1);
        const auto healthy=std::find_if(frames.begin(),frames.end(),[&](const auto& frame){return frame.secondaryTileId==L"AipSource_"+id;});
        preserved=preserved&&g_tileInstanceProviders.at(id).get()==healthyStates[i]&&healthy!=frames.end()&&healthy->text.primary==L"Headline "+std::to_wstring(i+1);
    }
    const auto bad=std::find_if(frames.begin(),frames.end(),[](const auto& frame){return frame.secondaryTileId==L"AipSource_feed5";});
    Check(preserved&&calls[0]==2&&calls[1]==2&&calls[2]==1&&calls[3]==1&&calls[4]==1&&bad!=frames.end()&&
        bad->text.primary==L"Check tile settings"&&bad->activationArguments==TileInstanceActivation(cfg.tiles[4])&&readerLifetimes[4].expired()&&!g_tileInstanceProviders.at(L"feed5")->rss,
        "a bad RSS tile becomes a clickable repair snapshot and releases its reader while four siblings keep the same workers and caches");
    if(bad!=frames.end()&&privateTransparentBackground(bad->backgroundPath)){
        const auto repairBackground=bad->backgroundPath;
        Check(DeleteFileW(repairBackground.c_str())!=FALSE,"remove only the private synthetic repair tile background");
        const auto repaired=ComposeTileInstance(cfg.tiles[4],*g_tileInstanceProviders.at(L"feed5"),SmtcMediaSnapshot{},false);
        Check(IsUsableWallpaperPath(repaired.backgroundPath)&&repaired.text.primary==L"Check tile settings",
            "a cached configuration-error tile recreates its missing background and keeps its repair activation available");
    }
    Check(WriteContentFixture(edited),"repair the one invalid URL in the private document");
    auto restoredReader=std::make_unique<ContentRssProvider>();auto restoredToken=std::make_shared<int>(4);readerLifetimes[4]=restoredToken;
    restoredReader->testReader=[&,token=std::move(restoredToken)](const RssFeedSettings& settings){
        received[4]=settings;++calls[4];RssFeedSnapshot feed;feed.ok=true;feed.sourceTitle=L"Synthetic feed 5";
        feed.items.push_back({L"Headline 5",L"Summary 5",L"https://rss-fixture.invalid/article",L""});return feed;
    };
    g_tileInstanceProviders.at(L"feed5")->rss=std::move(restoredReader);
    const auto restoredSettings=received[4];TileInstanceEngineTick(false,false);
    Check(g_tileInstanceProviders.at(L"feed5")->rss->source.Wait(ContentRssProvider::Key(restoredSettings),milliseconds(2500)),
        "repairing an invalid tile can start a fresh explicitly injected reader");
    TileInstanceEngineTick(false,false);frames=CurrentSecondarySnapshots();
    const auto recovered=std::find_if(frames.begin(),frames.end(),[](const auto& frame){return frame.secondaryTileId==L"AipSource_feed5";});
    Check(recovered!=frames.end()&&recovered->text.primary==L"Headline 5"&&calls[4]==2&&calls[0]==2&&calls[1]==2&&calls[2]==1&&calls[3]==1,
        "the repaired feed recovers normal content without restarting or refetching its healthy siblings");
    const auto sibling=g_tileInstanceProviders.at(L"feed2").get();
    g_secondaryPinInventory.Mutation(L"AipSource_feed1",false);TileInstanceEngineTick(false,false);
    Check(!g_tileInstanceProviders.count(L"feed1")&&readerLifetimes[0].expired()&&g_tileInstanceProviders.at(L"feed2").get()==sibling,
        "unpinning destroys only the removed tile's worker and preserves its sibling provider");
    const auto beforeQuickBackground=contentBackgroundRequests;
    Check(WriteIniValueToText(edited,L"Tile.feed3",L"Type",L"QuickAction")&&WriteIniValueToText(edited,L"Tile.feed3",L"Action",L"next")&&WriteContentFixture(edited),
        "change a pinned RSS identity to a static native Quick Action");
    TileInstanceEngineTick(false,false);frames=CurrentSecondarySnapshots();
    Check(!g_tileInstanceProviders.count(L"feed3")&&readerLifetimes[2].expired()&&
        std::none_of(frames.begin(),frames.end(),[](const auto& frame){return frame.secondaryTileId==L"AipSource_feed3";})&&
        contentBackgroundRequests==beforeQuickBackground&&contentWallpaperBackgroundRequests==beforeWallpaper,
        "a Quick Action releases its previous RSS worker and sends no blank live notification or background request that would hide its static logo");
    Check(WriteIniValueToText(edited,L"Tile.feed3",L"Type",L"Wallpaper")&&
        WriteIniValueToText(edited,L"Tile.feed3",L"Content.Background",L"Transparent")&&WriteContentFixture(edited),
        "change one instance's type using a transparent inert background");
    TileInstanceEngineTick(false,false);
    Check(g_tileInstanceProviders.at(L"feed3")->type==TileInstanceType::Wallpaper&&!g_tileInstanceProviders.at(L"feed3")->rss&&readerLifetimes[2].expired(),
        "changing a tile's type releases the previous provider even though its stable tile id remains pinned");
    std::wstring ids=L"feed1,feed2,feed3,feed5";for(unsigned i=6;i<=40;++i)ids+=L",feed"+std::to_wstring(i);
    Check(WriteIniValueToText(edited,L"Tiles",L"Instances",ids.c_str())&&WriteContentFixture(edited),"remove an instance from the configured collection without native unpinning");
    TileInstanceEngineTick(false,false);
    Check(!g_tileInstanceProviders.count(L"feed4")&&readerLifetimes[3].expired()&&g_tileInstanceProviders.count(L"feed5")&&contentWallpaperBackgroundRequests==beforeWallpaper,
        "deleting one configured tile stops its provider without activating unused wallpaper or disturbing another pin");
    StopTileInstanceProviders();
    Check(g_tileInstanceProviders.empty()&&readerLifetimes[1].expired()&&readerLifetimes[4].expired(),"instance shutdown releases all injected readers and owned workers");
    {
        std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);g_tileInstanceRefreshRequests.clear();
    }
    const auto backup=g_iniPath+L".before-tile-instances.bak";
    const bool backupExists=GetFileAttributesW(backup.c_str())!=INVALID_FILE_ATTRIBUTES;
    std::wstring priorBackup;if(backupExists)Check(aip::ReadTextFileUtf8BomAware(backup,priorBackup),"preexisting private conversion backup is readable");
    const std::wstring legacy=L"[Content]\r\nEnabled=1\r\nCount=8\r\n[Content.1]\r\nBackground=Image\r\nImagePath=fixture.bmp\r\n"
        L"[RssFeed]\r\nFeedUrl=https://rss-fixture.invalid/legacy.atom\r\nUpdateIntervalSeconds=900\r\n"
        L"[PrayerTimes]\r\nCity=Fixture City\r\nCountry=ID\r\nShowTimetable=0\r\n"
        L"[Notes.Resin]\r\nUID=123456789\r\nLToken=synthetic-credential\r\nLTUid=123\r\n";
    if(!backupExists){
        const bool directoryMade=CreateDirectoryW(backup.c_str(),nullptr)!=FALSE;
        Check(directoryMade&&WriteContentFixture(legacy),"create a private directory where the migration backup must be a file");
        if(directoryMade){std::wstring refused;
            Check(!EnsureTileInstanceProfile(error)&&ReadIniFileForMutation(refused)&&refused==legacy,
                "an unusable backup destination refuses conversion without changing any profile bytes");
            Check(RemoveDirectoryW(backup.c_str())!=FALSE,"remove only the fixture's empty backup directory after refusal");
        }
    }
    std::vector<BYTE> legacyBytes;bool originalEncoding=false;
    if(!backupExists){
        legacyBytes={0xff,0xfe};for(const auto ch:legacy){legacyBytes.push_back(static_cast<BYTE>(ch&0xff));legacyBytes.push_back(static_cast<BYTE>((ch>>8)&0xff));}
        std::wstring decoded;originalEncoding=WriteWholeFileBytes(g_iniPath,legacyBytes)&&ReadIniFileForMutation(decoded)&&decoded==legacy;
        Check(originalEncoding,"legacy migration fixture uses an explicitly encoded UTF-16LE profile before conversion");
    }else Check(WriteContentFixture(legacy),"write the private legacy conversion profile");
    Check(EnsureTileInstanceProfile(error),"legacy conversion uses the real atomic private-profile mutation path");
    std::wstring converted,backupText;
    Check(ReadIniFileForMutation(converted)&&ReadCurrentTileInstances(cfg,error)&&cfg.configured&&cfg.tiles.size()==8&&
        aip::ReadTextFileUtf8BomAware(backup,backupText)&&backupText==(backupExists?priorBackup:legacy),
        "conversion creates independent instances and preserves an exact immutable pre-conversion backup");
    if(originalEncoding){std::vector<BYTE> copied;Check(ReadWholeFileBytes(backup,copied)&&copied==legacyBytes,
        "conversion backup preserves the original BOM and every encoded source byte");}
    Check(IniReadS(L"Notes.Resin",L"LToken",L"")==L"synthetic-credential"&&
        IniReadS(L"Tile.genshin",L"Account",L"")==L"Notes.Resin"&&
        IniReadS(L"Tile.rss",L"RssFeed.FeedUrl",L"")==L"https://rss-fixture.invalid/legacy.atom"&&
        IniReadS(L"Tile.prayer",L"PrayerTimes.ShowTimetable",L"")==L"0",
        "conversion retains explicit credential references, the RSS URL and Prayer display preferences");
    Check(EnsureTileInstanceProfile(error)&&ReadIniFileForMutation(backupText)&&backupText==converted,
        "repeating conversion is idempotent and cannot overwrite configured tile instances");
    if(!backupExists)DeleteFileW(backup.c_str());
    {
        const bool oldDirect=contentNativeQueueAvailable;
        const auto oldBatches=secondaryTestBatches;
        const auto oldRetry=g_contentQueueRetryAfter.exchange(0);
        std::map<std::wstring,std::wstring> oldDelivered;
        {std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex);oldDelivered=g_secondaryDelivered;g_secondaryDelivered.clear();}
        const std::wstring primaryProfile=L"[Tiles]\r\nSchemaVersion=1\r\nInstances=primary-test\r\n[Tile.primary-test]\r\n"
            L"Type=Wallpaper\r\nPrimary=1\r\nContent.Background=Transparent\r\nContent.Text=Primary caption\r\n";
        Check(WriteContentFixture(primaryProfile)&&TileInstanceEngineTick(true,false),
            "compose a primary Wallpaper identity with a deliberately inert transparent background");
        auto primaryFrames=CurrentSecondarySnapshots();
        Check(primaryFrames.size()==1&&primaryFrames[0].secondaryTileId.empty()&&primaryFrames[0].text.primary==L"Primary caption",
            "the primary identity uses the empty delivery target rather than an AipSource alias");
        if(primaryFrames.size()==1){
            AcknowledgeSecondaryFrame(primaryFrames.front());
            Check(PrepareSecondaryBatch(primaryFrames).empty(),"an acknowledged primary frame has no redundant delivery work");
            RefreshTileInstanceNow(L"primary-test");TileInstanceEngineTick(false,false);
            auto refreshed=PrepareSecondaryBatch(CurrentSecondarySnapshots());
            Check(refreshed.size()==1&&refreshed.front().secondaryTileId.empty(),
                "explicit primary refresh invalidates the real empty delivery identity even when its image and text are unchanged");
            if(!refreshed.empty())AcknowledgeSecondaryFrame(refreshed.front());
        }
        contentNativeQueueAvailable=false;
        auto removed=primaryProfile;Check(WriteIniValueToText(removed,L"Tiles",L"Instances",L"")&&WriteContentFixture(removed),
            "remove the primary instance from only the private configuration");
        TileInstanceEngineTick(false,false);auto cleared=CurrentSecondarySnapshots();
        Check(cleared.size()==1&&cleared.front().secondaryTileId.empty()&&cleared.front().textMode==TextMode::Off&&
            cleared.front().backgroundId==BackgroundId::Transparent&&g_tileInstanceProviders.empty(),
            "primary removal retains one blank notification while its wallpaper provider and observation demand stop");
        const auto clearKey=cleared.empty()?std::wstring():cleared.front().secondaryFrameKey;
        if(!cleared.empty()&&privateTransparentBackground(cleared.front().backgroundPath)){
            Check(DeleteFileW(cleared.front().backgroundPath.c_str())!=FALSE,"remove only the private retained primary-clear source image");
            TileInstanceEngineTick(false,false);cleared=CurrentSecondarySnapshots();
            Check(cleared.size()==1&&IsUsableWallpaperPath(cleared.front().backgroundPath)&&cleared.front().secondaryTileId.empty(),
                "the pending primary clear recreates a deleted temporary background while the native delivery backend is unavailable");
        }
        for(unsigned tick=0;tick<3;++tick)TileInstanceEngineTick(false,false);
        cleared=CurrentSecondarySnapshots();
        Check(cleared.size()==1&&cleared.front().secondaryFrameKey==clearKey&&!PrepareSecondaryBatch(cleared).empty()&&
            contentWallpaperBackgroundRequests==beforeWallpaper,
            "an unavailable delivery backend cannot drop the retained primary clear or revive wallpaper observation");
        contentNativeQueueAvailable=true;secondaryTestBatches.clear();TileInstanceEngineTick(false,true);
        Check(secondaryTestBatches.size()==1&&secondaryTestBatches.front().size()==1&&
            secondaryTestBatches.front().front().secondaryTileId.empty()&&secondaryTestBatches.front().front().textMode==TextMode::Off,
            "the retained clear is submitted once when injected direct delivery becomes available");
        TileInstanceEngineTick(false,true);
        Check(secondaryTestBatches.size()==1&&PrepareSecondaryBatch(CurrentSecondarySnapshots()).empty(),
            "an acknowledged primary clear remains idle on subsequent ticks");
        StopTileInstanceProviders();contentNativeQueueAvailable=oldDirect;secondaryTestBatches=oldBatches;g_contentQueueRetryAfter=oldRetry;
        {std::lock_guard<std::mutex> lock(g_secondaryDeliveryMutex);g_secondaryDelivered=std::move(oldDelivered);}
    }
    galleryHostFrames=savedGallery;g_secondaryPinInventory.Reset();
    if(pinsKnown)g_secondaryPinInventory.Complete(true,savedPins,GetTickCount64());else for(const auto& id:savedPins)g_secondaryPinInventory.Mutation(id,true);
    Check(WriteContentFixture(original),"instance runtime fixture restores the prior private document");
    g_iniCacheRefreshMs=oldRefresh;PublishContentSnapshot({});PublishSecondarySnapshots({});
}

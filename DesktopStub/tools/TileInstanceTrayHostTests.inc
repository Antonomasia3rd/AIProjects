// Private-file menu/command checks. No popup is shown and no native pin,
// account sign-in, provider request or physical device is exercised.
static void TestTileInstanceTrayHost()
{
    using namespace aip::content;
    std::wstring original,error; ReadIniFileForMutation(original);
    const auto oldPage = g_tileInstanceMenuPage;
    const auto oldCommands = g_tileInstanceMenuCommands;
    const auto oldNext = g_nextTileInstanceMenuCommand;
    const auto oldInventory = g_secondaryPinInventory.Snapshot();
    const auto oldInventoryKnown = g_secondaryPinInventory.Known();
    std::wstring text = L"[Tiles]\r\nSchemaVersion=1\r\nInstances=";
    for (int index = 0; index < 40; ++index) { if (index) text += L","; text += L"feed-"+std::to_wstring(index); }
    text += L"\r\n";
    for (int index = 0; index < 40; ++index) text += L"[Tile.feed-"+std::to_wstring(index)+L"]\r\nType=RSS\r\nName=Feed "+std::to_wstring(index)+L"\r\nTextStyle=caption\r\nTextMode=Overlay\r\n";
    Check(WriteContentFixture(text),"instance tray fixture owns only the private profile");
    const std::function<bool(HMENU,UINT)> containsCommand = [&](HMENU target,UINT command) {
        for (int row = 0; row < GetMenuItemCount(target); ++row) {
            if (GetMenuItemID(target,row) == command) return true;
            const auto child = GetSubMenu(target,row);
            if (child && containsCommand(child,command)) return true;
        }
        return false;
    };
    HMENU completeMenu = CreatePopupMenu(); PopulateDesktopStubTrayMenu(completeMenu,true,false);
    Check(containsCommand(completeMenu,ID_LIVE_TILE_MODE_AUTO) && containsCommand(completeMenu,ID_LIVE_TILE_MODE_LIVE_TILE) &&
        containsCommand(completeMenu,ID_LIVE_TILE_MODE_REGISTRATION),"instance-mode Application menu retains every shared Windows delivery mode");
    Check(containsCommand(completeMenu,ID_LIVE_TILE_TEMPLATE_ADAPTIVE) && containsCommand(completeMenu,ID_LIVE_TILE_TEMPLATE_WINDOWS81_PRESET) &&
        containsCommand(completeMenu,ID_CLEAR_LIVE_TILE_ON_SHUTDOWN) && containsCommand(completeMenu,ID_LIVE_TILE_RELAUNCH_ON_SWITCH),
        "instance-mode Application menu retains template, shutdown clearing and runtime-switch controls");
    Check(!containsCommand(completeMenu,ID_QUICK_ACTIONS_SETUP) && !containsCommand(completeMenu,ID_QUICK_ACTIONS_CLICK) &&
        containsCommand(completeMenu,ID_QUICK_ACTIONS_PIN_ALL) && containsCommand(completeMenu,ID_QUICK_ACTIONS_VOLUME),
        "legacy Quick Actions retain existing pin/volume options while independent instances own their click actions");
    DestroyMenu(completeMenu);
    g_tileInstanceMenuPage = 0;
    HMENU menu = CreatePopupMenu(); AppendTileInstancesMenu(menu);
    Check(g_tileInstanceMenuCommands.size() < ID_TILE_INSTANCE_LAST-ID_TILE_INSTANCE_FIRST &&
        g_nextTileInstanceMenuCommand <= ID_TILE_INSTANCE_LAST,"dynamic menu command allocation stays within its reserved range");
    bool firstVisible = false, seventeenthVisible = false, nextVisible = false;
    UINT layoutCommand = 0;
    for (const auto& item : g_tileInstanceMenuCommands) {
        firstVisible = firstVisible || item.second.id == L"feed-0";
        seventeenthVisible = seventeenthVisible || item.second.id == L"feed-16";
        nextVisible = nextVisible || item.second.verb == TileInstanceMenuVerb::Page;
        if (item.second.id == L"feed-0" && item.second.key == L"TextPreset" && item.second.value == L"RssHeadlines") layoutCommand = item.first;
    }
    Check(firstVisible && !seventeenthVisible && nextVisible,"large instance lists expose bounded pages without limiting configured tile count");
    DestroyMenu(menu);
    Check(layoutCommand && DispatchTileInstanceCommand(nullptr,layoutCommand),"named RSS layout is selected through the actual registered tray command");
    TileInstanceConfiguration cfg;
    Check(ReadTileInstanceMenuConfiguration(cfg,error) && cfg.tiles[0].presentation.preset == L"RssHeadlines" &&
        cfg.tiles[0].presentation.primaryStyle == L"subtitle" && cfg.tiles[0].configuration.textMode == TextMode::Auto &&
        cfg.tiles[0].presentation.wrap && cfg.tiles[0].presentation.primaryLines == 3 && cfg.tiles[1].presentation.preset == L"Default",
        "named headlines layout replaces stale caption/Overlay settings only for the selected RSS tile");
    g_tileInstanceMenuPage = 2; menu = CreatePopupMenu(); AppendTileInstancesMenu(menu);
    bool lastVisible = false; firstVisible = false;
    for (const auto& item : g_tileInstanceMenuCommands) { lastVisible = lastVisible || item.second.id == L"feed-39"; firstVisible = firstVisible || item.second.id == L"feed-0"; }
    Check(lastVisible && !firstVisible,"last page reaches all independently configured instances"); DestroyMenu(menu);
    Check(SaveTileInstanceSettings(nullptr,{{L"Tile.feed-1",L"RssFeed.FeedUrl",L"https://example.invalid/one.atom"},
        {L"Tile.feed-1",L"RssFeed.UpdateIntervalSeconds",L"300"}},L"feed-1") &&
        SaveTileInstanceSettings(nullptr,{{L"Tile.feed-2",L"RssFeed.FeedUrl",L"https://example.invalid/two.atom"},
        {L"Tile.feed-2",L"RssFeed.UpdateIntervalSeconds",L"900"}},L"feed-2"),"independent RSS tray edits use complete atomic validation without fetching feeds");
    std::wstring validText; ReadIniFileForMutation(validText);
    Check(!SaveTileInstanceSettings(nullptr,{{L"Tile.feed-1",L"RssFeed.UpdateIntervalSeconds",L"1"}},L"feed-1"),"native RSS range validator refuses an invalid per-instance interval");
    std::wstring unchanged; ReadIniFileForMutation(unchanged);
    Check(unchanged == validText,"rejected instance setting leaves the complete profile unchanged");
    auto broken = validText; aip::WriteIniValueToText(broken,L"Tile.feed-1",L"RssFeed.FeedUrl",L"not a URL");
    Check(WriteContentFixture(broken) && ReadTileInstanceMenuConfiguration(cfg,error) && cfg.tiles.size() == 40 && !cfg.tiles[1].error.empty() && cfg.tiles[0].error.empty(),
        "bad current source settings retain all tile settings routes for repair");
    Check(SaveTileInstanceSettings(nullptr,{{L"Tile.feed-1",L"RssFeed.FeedUrl",L"https://example.invalid/repaired.atom"}},L"feed-1") &&
        ReadTileInstanceMenuConfiguration(cfg,error) && cfg.tiles[1].error.empty(),"tile settings can repair an invalid current field without needing a valid original profile");
    g_secondaryPinInventory.Reset();
    Check(!SaveTileInstanceSettings(nullptr,{},L"feed-1",L"feed-1"),"unknown pin membership cannot strand a pin by removing its configuration");
    g_secondaryPinInventory.Complete(true,{},GetTickCount64());
    Check(SaveTileInstanceSettings(nullptr,{},L"feed-1",L"feed-1") && ReadTileInstanceMenuConfiguration(cfg,error) && cfg.tiles.size() == 39,
        "confirmed unpinned tile can be removed without altering other instances");
    Check(!SaveTileInstanceSettings(nullptr,{{L"Tiles",L"Instances",L"feed-0"}}),"stale Add menu list cannot silently remove concurrently configured tiles");
    const std::wstring galleryText = L"[Tiles]\r\nInstances=\r\nEnabled=invalid unused legacy gate\r\n[Content]\r\nEnabled=invalid unused legacy gate\r\n"
        L"[Gallery]\r\nEnabled=0\r\nGroupId=owned_gallery\r\nTileCount=1\r\n[Gallery.Tile.1]\r\nId=owned_photo\r\nName=Owned photo\r\n";
    Check(WriteContentFixture(galleryText),"instance gallery regression owns only metadata in its private profile");
    const auto galleryView = CaptureGalleryConfigurationView(error,true);
    Check(galleryView && galleryView->contentEnabled && galleryView->tilesEnabled && galleryView->configuration.groupId == L"owned_gallery",
        "instance-mode gallery capture ignores retired Content/Tiles update gates while preserving gallery identity");
    std::wstring firstFingerprint,secondFingerprint;
    Check(CurrentSecondaryAppearanceFingerprint(firstFingerprint) && firstFingerprint.find(L"Gallery:owned_gallery:") != std::wstring::npos,
        "configured gallery appearance participates in the new instance-mode fingerprint");
    auto changedGallery = galleryText; aip::WriteIniValueToText(changedGallery,L"Gallery.Tile.1",L"Branding",L"None");
    Check(WriteContentFixture(changedGallery) && CurrentSecondaryAppearanceFingerprint(secondFingerprint) && secondFingerprint != firstFingerprint,
        "gallery branding changes remain detectable alongside independent source instances");
    aip::RemoveIniValueFromText(changedGallery,L"Tiles",L"Instances");
    Check(WriteContentFixture(changedGallery) && !CaptureGalleryConfigurationView(error,true),"legacy-mode gallery still validates its existing legacy gates");
    Check(WriteContentFixture(original),"instance tray fixture restores the prior private profile");
    g_secondaryPinInventory.Reset(); if (oldInventoryKnown) g_secondaryPinInventory.Complete(true,oldInventory,GetTickCount64());
    g_tileInstanceMenuPage = oldPage; g_tileInstanceMenuCommands = oldCommands; g_nextTileInstanceMenuCommand = oldNext;
}

static HMENU SourceMenuChild(HMENU menu,const wchar_t* caption)
{
    for(int index=0;index<GetMenuItemCount(menu);++index){wchar_t text[512]{};
        GetMenuStringW(menu,index,text,static_cast<int>(std::size(text)),MF_BYPOSITION);
        if(std::wstring(text)==caption)return GetSubMenu(menu,index);
    }return nullptr;
}
static bool SourceMenuHasField(HMENU menu,const wchar_t* key)
{
    for(int index=0;index<GetMenuItemCount(menu);++index){const auto found=g_tileInstanceMenuCommands.find(GetMenuItemID(menu,index));
        if(found!=g_tileInstanceMenuCommands.end()&&found->second.key==key)return true;
    }return false;
}
static void TestTileSourceMenuHost()
{
    aip::content::TileInstance prayer;prayer.id=L"menu-prayer";prayer.name=L"Prayer";prayer.type=aip::content::TileInstanceType::PrayerTimes;
    ResetTileInstanceMenuCommands();HMENU menu=CreatePopupMenu();AppendTileInstanceDetails(menu,prayer);
    const auto source=SourceMenuChild(menu,L"Source settings"),alerts=SourceMenuChild(menu,L"Notifications for this tile");
    Check(source&&alerts&&!SourceMenuChild(source,L"Notifications for this tile"),"Prayer notifications are directly discoverable in the tile menu, outside source settings");
    const auto advanced=SourceMenuChild(source,L"Advanced");
    Check(advanced&&!SourceMenuHasField(source,L"PrayerTimes.TimeoutSeconds")&&SourceMenuHasField(advanced,L"PrayerTimes.TimeoutSeconds")&&
        SourceMenuHasField(advanced,L"PrayerTimes.MaximumBytes"),"Prayer HTTP bounds are grouped under source Advanced");
    Check(SourceMenuChild(alerts,L"Before prayer")&&SourceMenuChild(alerts,L"At prayer time")&&SourceMenuChild(alerts,L"Current prayer status"),
        "Prayer notification phases have separate, clearly named controls");DestroyMenu(menu);
    auto rss=prayer;rss.id=L"menu-rss";rss.type=aip::content::TileInstanceType::Rss;
    ResetTileInstanceMenuCommands();menu=CreatePopupMenu();AppendTileInstanceDetails(menu,rss);
    const auto rssSource=SourceMenuChild(menu,L"Source settings"),rssAlerts=SourceMenuChild(menu,L"Notifications for this tile");
    Check(rssSource&&rssAlerts&&SourceMenuHasField(rssSource,L"RssFeed.FeedUrl")&&
        !SourceMenuHasField(rssSource,L"RssFeed.HttpTimeoutSeconds")&&SourceMenuHasField(SourceMenuChild(rssSource,L"Advanced"),L"RssFeed.HttpTimeoutSeconds"),
        "RSS keeps the feed URL visible and HTTP mechanics under Advanced");
    Check(SourceMenuHasField(rssAlerts,L"Alerts.MinimumNewItems")&&SourceMenuHasField(rssAlerts,L"Alerts.Title")&&SourceMenuHasField(rssAlerts,L"Alerts.Body"),
        "RSS popup threshold and templates are available together in Notifications");DestroyMenu(menu);
    auto wuwa=prayer;wuwa.id=L"menu-wuwa";wuwa.type=aip::content::TileInstanceType::Wuwa;
    ResetTileInstanceMenuCommands();menu=CreatePopupMenu();AppendTileInstanceSourceSettings(menu,wuwa);
    bool signIn=false;for(int index=0;index<GetMenuItemCount(menu);++index){const auto command=g_tileInstanceMenuCommands.find(GetMenuItemID(menu,index));
        signIn|=command!=g_tileInstanceMenuCommands.end()&&command->second.verb==TileInstanceMenuVerb::SignIn;}
    Check(signIn&&!SourceMenuHasField(menu,L"Kuro.GameToken")&&SourceMenuHasField(SourceMenuChild(menu,L"Advanced"),L"Kuro.GameToken"),
        "Kuro guides users through sign-in while manual token import remains an advanced option");DestroyMenu(menu);
}

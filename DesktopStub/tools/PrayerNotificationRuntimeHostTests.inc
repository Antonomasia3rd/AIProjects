// Actual prayer/RSS notification adapters with injected native delivery and
// in-memory state only. No popup, Action Center item, account or network call.
struct PrayerRuntimeNotice {std::wstring key,title,body;};
static std::vector<PrayerRuntimeNotice> prayerRuntimePopups,prayerRuntimeQuiet;
static std::vector<std::pair<std::wstring,std::wstring>> prayerRuntimeRemoved;
static std::map<std::wstring,std::wstring> prayerRuntimeFiles;
static aip::notifications::NativeNoticeOptions prayerRuntimeLastOptions;
static bool PrayerRuntimeLoad(const std::wstring& path,const std::wstring& scope,aip::notifications::AlertState& state)
{
    const auto found=prayerRuntimeFiles.find(path);
    return found!=prayerRuntimeFiles.end()&&aip::notifications::ParseAlertState(found->second,scope,state);
}
static bool PrayerRuntimeSave(const std::wstring& path,const std::wstring& text){prayerRuntimeFiles[path]=text;return true;}
static void PrayerRuntimePopup(const std::wstring& key,const std::wstring& title,const std::wstring& body,
    aip::notifications::EventType,aip::notifications::EventSound){prayerRuntimePopups.push_back({key,title,body});}
static bool PrayerRuntimeQuiet(const std::wstring& key,const std::wstring& title,const std::wstring& body,
    const aip::notifications::NativeNoticeOptions& options)
{
    prayerRuntimeQuiet.push_back({key,title,body});prayerRuntimeLastOptions=options;return true;
}
static bool PrayerRuntimeRemove(const std::wstring& key,const std::wstring& group){prayerRuntimeRemoved.emplace_back(key,group);return true;}
static aip::prayer::Day PrayerRuntimeDay(aip::prayer::Date date)
{
    aip::prayer::Day day;day.date=date;day.timeZone=L"UTC";
    const int hours[]={0,6,12,18,23};
    for(size_t i=0;i<5;++i)day.prayers[i]={static_cast<aip::prayer::PrayerId>(i),aip::prayer::CivilDay(date)*86400+hours[i]*3600,0};
    return day;
}
static void TestPrayerNotificationRuntimeHost()
{
    using namespace aip::notifications;using namespace aip::content;
    std::wstring original;Check(ReadIniFileForMutation(original),"prayer notification fixture captures only its private INI");
    const auto oldRefresh=g_iniCacheRefreshMs.load();g_iniCacheRefreshMs=0;
    const auto oldEnabled=g_notificationsEnabled.load(),oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    const auto oldUser=g_testTileRegistrationUserScope;const auto oldRetry=g_notificationToastRetryAfter.load();
    auto oldStates=std::move(g_tileSourceAlerts);
    const auto oldLoad=g_testLoadSourceAlerts;const auto oldSave=g_testSaveSourceAlerts;
    const auto oldPopup=g_testSourceAlertEvent;const auto oldQuiet=g_testQuietNotificationToast;const auto oldRemove=g_testRemoveNotificationToast;
    g_testLoadSourceAlerts=PrayerRuntimeLoad;g_testSaveSourceAlerts=PrayerRuntimeSave;g_testSourceAlertEvent=PrayerRuntimePopup;
    g_testQuietNotificationToast=PrayerRuntimeQuiet;g_testRemoveNotificationToast=PrayerRuntimeRemove;
    g_testTileRegistrationUserScope=L"1234567890abcdef";g_notificationToastRetryAfter=0;
    g_notificationsEnabled=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    Check(WriteContentFixture(L"[Settings]\r\nNotificationApi=Toast\r\n"),"quiet notification fixture uses explicitly injected modern transport");
    prayerRuntimePopups.clear();prayerRuntimeQuiet.clear();prayerRuntimeRemoved.clear();prayerRuntimeFiles.clear();
    TileInstance tile;tile.id=L"quiet-prayer-runtime";tile.name=L"Prayer fixture";tile.type=TileInstanceType::PrayerTimes;
    tile.settings[L"alerts.enabled"]=L"1";tile.settings[L"alerts.timeformat"]=L"12Hour";
    auto& cfg=tile.configuration.prayer;cfg.city=L"Surabaya";cfg.country=L"Indonesia";cfg.method=20;
    const auto now=std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    const auto date=aip::prayer::DateAtUTC(now);
    aip::prayer::Data data;data.configKey=aip::prayer::ConfigurationKey(cfg);
    data.schedule.days={PrayerRuntimeDay(aip::prayer::AddDays(date,-1)),PrayerRuntimeDay(date),PrayerRuntimeDay(aip::prayer::AddDays(date,1))};
    const auto dhuhr=data.schedule.days[1].prayers[1].utcSeconds;
    ObservePrayerTileAlerts(tile,data,dhuhr-1200);
    Check(prayerRuntimePopups.size()==1&&prayerRuntimePopups.back().title==L"Dhuhr in 20 minutes"&&prayerRuntimePopups.back().body==L"6:00 AM",
        "upcoming popup places prayer/countdown in title and configured local event time in body");
    ObservePrayerTileAlerts(tile,data,dhuhr);
    Check(prayerRuntimePopups.size()==2&&prayerRuntimePopups.back().title==L"It's time for Dhuhr"&&prayerRuntimePopups.back().body==L"6:00 AM",
        "at-time popup is independent from upcoming phase with its own title and time body");
    ObservePrayerTileAlerts(tile,data,dhuhr+1);
    Check(prayerRuntimePopups.size()==2,"repeated phase polls do not emit another popup");
    tile.settings[L"alerts.upcomingenabled"]=L"0";tile.settings[L"alerts.attimeenabled"]=L"0";tile.settings[L"alerts.currentenabled"]=L"1";
    ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==1&&prayerRuntimePopups.size()==2&&prayerRuntimeLastOptions.suppressPopup&&prayerRuntimeLastOptions.expirationUtc>now&&
        !prayerRuntimeLastOptions.group.empty()&&prayerRuntimeQuiet.back().body==L"at Surabaya",
        "current-prayer status uses a silent suppressed-popup history item with an explicit next-prayer expiration");
    const auto firstQuietKey=prayerRuntimeQuiet.back().key,firstGroup=prayerRuntimeLastOptions.group;
    for(int i=0;i<50;++i)ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==1,"unchanged current prayer does not revive a dismissed Action Center item during polls");
    const auto beforeExit=prayerRuntimeRemoved.size();StopTileInstanceProviders(true);
    Check(prayerRuntimeRemoved.size()==beforeExit&&g_tileSourceAlerts.empty(),
        "ordinary provider shutdown preserves the native current item until its expiration while releasing runtime state");
    ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==1,"persisted current delivery identity prevents revival after resident restart");
    g_notificationsEnabled=false;ObservePrayerTileAlerts(tile,data,now);g_notificationsEnabled=true;
    Check(!prayerRuntimeRemoved.empty()&&prayerRuntimeRemoved.back()==std::make_pair(firstQuietKey,firstGroup),
        "master mute removes only the owned stable tag from its captured profile group");
    tile.settings[L"alerts.currenttitle"]=L"%event% at %time%";tile.settings[L"alerts.currentbody"]=L"%city% / %tile%";
    ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==2&&prayerRuntimeQuiet.back().key==firstQuietKey&&prayerRuntimeQuiet.back().body==L"Surabaya / Prayer fixture",
        "explicit current-text edit updates the same owned history tag with configurable tokens");
    const auto beforeDisable=prayerRuntimeRemoved.size();tile.settings[L"alerts.currentenabled"]=L"0";
    ObservePrayerTileAlerts(tile,data,now);tile.settings[L"alerts.currentenabled"]=L"1";ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeRemoved.size()==beforeDisable+1&&prayerRuntimeQuiet.size()==3&&prayerRuntimeQuiet.back().key==firstQuietKey,
        "explicitly disabling then re-enabling the same current prayer recreates one quiet status without being mistaken for user dismissal");
    for(int i=0;i<20;++i)ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==3,"re-enabled current status remains deduplicated across ordinary polls");
    Check(WriteContentFixture(L"[Settings]\r\nNotificationApi=Balloon\r\n"),"fixture switches only its private notification API");
    ObservePrayerTileAlerts(tile,data,now);
    Check(prayerRuntimeQuiet.size()==3&&prayerRuntimePopups.size()==2&&prayerRuntimeRemoved.size()>=2,
        "Balloon mode removes current history without converting it into a popup fallback");
    Check(WriteContentFixture(L"[Settings]\r\nNotificationApi=Toast\r\n"),"fixture restores injected modern transport");
    tile.settings[L"alerts.currentbody"]=L"Changed %city%";ObservePrayerTileAlerts(tile,data,now);
    const auto beforeReset=prayerRuntimeRemoved.size();const auto runningBeforeReset=g_running.exchange(true);
    StopTileInstanceProviders();g_running=runningBeforeReset;
    Check(prayerRuntimeRemoved.size()==beforeReset+1&&g_tileSourceAlerts.empty(),
        "configuration failure or provider reset retires the current status immediately instead of waiting for expiration");
    tile.settings[L"alerts.currentbody"]=L"Another %city%";ObservePrayerTileAlerts(tile,data,now);
    const auto beforePrune=prayerRuntimeRemoved.size();PrunePrayerCurrentStatuses({});
    Check(prayerRuntimeRemoved.size()==beforePrune+1,"removing or no longer demanding a tile retires its current history entry");
    auto rss=tile;rss.type=TileInstanceType::Rss;rss.id=L"rss-custom-runtime";rss.name=L"Releases";
    rss.settings.clear();rss.settings[L"alerts.enabled"]=L"1";rss.settings[L"alerts.cooldownseconds"]=L"0";
    rss.settings[L"alerts.title"]=L"%tile% updated";rss.settings[L"alerts.body"]=L"Found %count% new item(s)";
    rss.settings[L"rssfeed.feedurl"]=L"https://inert.invalid/feed";
    RssFeedSnapshot feed;feed.ok=true;feed.items.push_back({L"Old",L"",L"",L"",L"old"});ObserveRssTileAlerts(rss,feed);
    feed.items.front().identity=L"new";ObserveRssTileAlerts(rss,feed);
    Check(prayerRuntimePopups.size()==3&&prayerRuntimePopups.back().title==L"Releases updated"&&prayerRuntimePopups.back().body==L"Found 1 new item(s)",
        "RSS uses independent per-feed common title/body templates through the popup adapter");
    ClearPrayerCurrentStatuses();g_tileSourceAlerts=std::move(oldStates);
    g_testLoadSourceAlerts=oldLoad;g_testSaveSourceAlerts=oldSave;g_testSourceAlertEvent=oldPopup;
    g_testQuietNotificationToast=oldQuiet;g_testRemoveNotificationToast=oldRemove;
    g_testTileRegistrationUserScope=oldUser;g_notificationToastRetryAfter=oldRetry;
    g_notificationsEnabled=oldEnabled;g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;
    Check(WriteContentFixture(original),"prayer notification fixture restores its private profile");g_iniCacheRefreshMs=oldRefresh;
    prayerRuntimeFiles.clear();prayerRuntimePopups.clear();prayerRuntimeQuiet.clear();prayerRuntimeRemoved.clear();
}

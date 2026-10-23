// Real source-alert adapters with an in-memory sidecar and notification sink.
// No notification, filesystem sidecar, network, package or sound is exercised.
static std::map<std::wstring,std::wstring> sourceAlertFiles;
static unsigned sourceAlertLoads=0,sourceAlertSaves=0,sourceAlertNotices=0;
static std::wstring sourceAlertLastMessage;
static aip::notifications::EventType sourceAlertLastType=aip::notifications::EventType::General;
static bool SourceAlertTestLoad(const std::wstring& path,const std::wstring& scope,aip::notifications::AlertState& state)
{
    ++sourceAlertLoads;const auto found=sourceAlertFiles.find(path);
    return found!=sourceAlertFiles.end()&&aip::notifications::ParseAlertState(found->second,scope,state);
}
static bool SourceAlertTestSave(const std::wstring& path,const std::wstring& text)
{
    ++sourceAlertSaves;sourceAlertFiles[path]=text;return true;
}
static void SourceAlertTestNotify(const std::wstring&,const std::wstring&,const std::wstring& message,
    aip::notifications::EventType type,aip::notifications::EventSound)
{
    ++sourceAlertNotices;sourceAlertLastMessage=message;sourceAlertLastType=type;
}
static void TestSourceAlertHost()
{
    using namespace aip::notifications;using namespace aip::content;
    const auto oldEnabled=g_notificationsEnabled.load();const auto oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    const auto oldUser=g_testTileRegistrationUserScope;
    auto oldStates=std::move(g_tileSourceAlerts);
    g_testLoadSourceAlerts=SourceAlertTestLoad;g_testSaveSourceAlerts=SourceAlertTestSave;g_testSourceAlertEvent=SourceAlertTestNotify;
    g_testTileRegistrationUserScope=L"1234567890abcdef";g_notificationsEnabled=true;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    sourceAlertFiles.clear();sourceAlertLoads=sourceAlertSaves=sourceAlertNotices=0;
    TileInstance first;first.id=L"alerts-one";first.name=L"First feed";first.type=TileInstanceType::Rss;
    first.settings[L"alerts.enabled"]=L"1";first.settings[L"alerts.minimumnewitems"]=L"2";first.settings[L"alerts.cooldownseconds"]=L"0";
    first.settings[L"rssfeed.feedurl"]=L"https://private-fixture.invalid/one";
    RssFeedSnapshot feed;feed.ok=true;feed.sourceTitle=L"Fixture";
    feed.items.push_back({L"Initial",L"Summary",L"https://private-fixture.invalid/item",L"",L"initial-id"});
    ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==0&&sourceAlertSaves==1&&sourceAlertLoads==1,"RSS adapter saves one initial baseline without notifying");
    auto sibling=first;sibling.id=L"alerts-two";sibling.name=L"Second feed";
    ObserveRssTileAlerts(sibling,feed);
    Check(sourceAlertNotices==0&&sourceAlertFiles.size()==2,"two identical feeds have independent tile baselines and persistence identities");
    feed.items.front().identity=L"second-id";ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==0,"RSS adapter defers a new item below the configured threshold");
    feed.items.front().identity=L"third-id";ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==1&&sourceAlertLastMessage.find(L"2 new items")!=std::wstring::npos&&sourceAlertLastMessage.find(first.name)!=std::wstring::npos,
        "RSS adapter combines exact new-item count and tile identity into one notification");
    const auto saves=sourceAlertSaves;for(unsigned i=0;i<100;++i)ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==1&&sourceAlertSaves==saves,"unchanged source polls perform no persistence writes or duplicate notifications");
    g_tileSourceAlerts.clear();ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==1,"RSS adapter reloads persisted identities before considering an already-seen result");
    g_notificationsEnabled=false;feed.items.front().identity=L"muted-id";ObserveRssTileAlerts(first,feed);
    feed.items.front().identity=L"muted-id2";ObserveRssTileAlerts(first,feed);g_notificationsEnabled=true;ObserveRssTileAlerts(first,feed);
    Check(sourceAlertNotices==1,"master-muted updates maintain a quiet baseline without replaying a backlog on unmute");
    bool privateState=true;for(const auto& entry:sourceAlertFiles)privateState=privateState&&entry.second.find(L"https:")==std::wstring::npos&&entry.second.find(L"Initial")==std::wstring::npos;
    Check(privateState,"notification sidecars contain hashes rather than source URLs or article contents");
    const auto loads=sourceAlertLoads;const auto finalSaves=sourceAlertSaves;
    g_commandLine.renderOnly=true;feed.items.front().identity=L"render-only-item";ObserveRssTileAlerts(first,feed);
    g_commandLine.renderOnly=false;g_commandLine.configureOnly=true;ObserveRssTileAlerts(first,feed);g_commandLine.configureOnly=false;
    Check(sourceAlertLoads==loads&&sourceAlertSaves==finalSaves&&sourceAlertNotices==1,"render-only and configure-only adapters cannot load, persist or notify source alerts");
    TileInstance prayer;prayer.id=L"alert-prayer";prayer.name=L"Prayer";prayer.type=TileInstanceType::PrayerTimes;
    prayer.settings[L"alerts.enabled"]=L"1";prayer.settings[L"alerts.type"]=L"Reminder";prayer.settings[L"alerts.sound"]=L"Silent";
    auto& cfg=prayer.configuration.prayer;cfg.city=L"Surabaya";cfg.country=L"Indonesia";cfg.method=20;
    aip::prayer::Day day;day.date={2026,10,23};day.timeZone=L"Asia/Jakarta";day.offsetMinutes=420;
    const int hours[]={5,12,15,18,19};
    for(size_t i=0;i<5;++i)day.prayers[i]={static_cast<aip::prayer::PrayerId>(i),aip::prayer::CivilDay(day.date)*86400+hours[i]*3600-420*60,420};
    aip::prayer::Data data;data.configKey=aip::prayer::ConfigurationKey(cfg);data.schedule.days={day};
    const auto due=day.prayers[1].utcSeconds-1200;
    g_notificationsEnabled=false;ObservePrayerTileAlerts(prayer,data,due);
    Check(sourceAlertNotices==1,"muted prayer notification is not marked as delivered");
    g_notificationsEnabled=true;ObservePrayerTileAlerts(prayer,data,due+1);
    Check(sourceAlertNotices==2&&sourceAlertLastType==EventType::Reminder&&sourceAlertLastMessage.find(L"Dhuhr in 20 minutes")!=std::wstring::npos,
        "Prayer adapter sends configured lead time, semantic event, location and toast type");
    g_tileSourceAlerts.clear();ObservePrayerTileAlerts(prayer,data,due+2);
    Check(sourceAlertNotices==2,"Prayer adapter loads persisted event dedupe on restart within the reminder window");
    ObservePrayerTileAlerts(prayer,data,day.prayers[2].utcSeconds-1000);
    Check(sourceAlertNotices==2,"late unlock does not replay a prayer lead alert from several minutes ago");
    g_testLoadSourceAlerts=nullptr;g_testSaveSourceAlerts=nullptr;g_testSourceAlertEvent=nullptr;
    sourceAlertFiles.clear();g_tileSourceAlerts=std::move(oldStates);g_testTileRegistrationUserScope=oldUser;
    g_notificationsEnabled=oldEnabled;g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;
}

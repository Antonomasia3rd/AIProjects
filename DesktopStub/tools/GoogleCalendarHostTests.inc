// Host provider/configuration tests use only injected HTTP and credential
// transforms. No Google account, browser, DPAPI or real calendar file is used.
static void GoogleCalendarHostReply(aip::HttpFetchResponse& response,const std::string& text)
{
    response.statusCode=200;response.body.assign(text.begin(),text.end());
}

static bool GoogleHostDemandLoad(const std::wstring&,const std::wstring&,aip::notifications::AlertState&){return false;}
static bool GoogleHostDemandSave(const std::wstring&,const std::wstring&){return true;}
static void TestUnpinnedAlertDemandHost()
{
    using namespace aip::content;
    std::wstring original;Check(ReadIniFileForMutation(original),"unpinned alert fixture captures only its private profile");
    const auto oldRefresh=g_iniCacheRefreshMs.load();const auto oldNotifications=g_notificationsEnabled.load();
    const auto oldRender=g_commandLine.renderOnly,oldConfigure=g_commandLine.configureOnly;
    const auto oldPins=CachedSecondaryTileIds();const bool pinsKnown=SecondaryTileInventoryReady();const auto oldGallery=galleryHostFrames;
    const auto oldUser=g_testTileRegistrationUserScope;const auto oldLoad=g_testLoadSourceAlerts;const auto oldSave=g_testSaveSourceAlerts;
    auto oldAlertStates=std::move(g_tileSourceAlerts);std::set<std::wstring> oldRefreshRequests;
    {std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);oldRefreshRequests.swap(g_tileInstanceRefreshRequests);}
    StopTileInstanceProviders();g_iniCacheRefreshMs=0;g_notificationsEnabled=false;g_commandLine.renderOnly=false;g_commandLine.configureOnly=false;
    g_testLoadSourceAlerts=GoogleHostDemandLoad;g_testSaveSourceAlerts=GoogleHostDemandSave;g_testTileRegistrationUserScope=L"5555555555555555";
    galleryHostFrames.clear();g_secondaryPinInventory.Complete(true,{},GetTickCount64());
    auto profile=std::wstring(L"[Tiles]\r\nInstances=alert-rss,alert-prayer,quiet-rss\r\n")+
        L"[Tile.alert-rss]\r\nType=RSS\r\nDelivery.Mode=LiveTile\r\nAlerts.Enabled=1\r\nRssFeed.FeedUrl=https://alert-demand-fixture.invalid/feed.atom\r\nRssFeed.UpdateIntervalSeconds=300\r\n"+
        L"[Tile.alert-prayer]\r\nType=PrayerTimes\r\nDelivery.Mode=LiveTile\r\nAlerts.Enabled=1\r\nPrayerTimes.City=Fixture City\r\nPrayerTimes.Country=GB\r\nPrayerTimes.TimeZone=Europe/London\r\n"+
        L"[Tile.quiet-rss]\r\nType=RSS\r\nDelivery.Mode=LiveTile\r\nAlerts.Enabled=0\r\nRssFeed.FeedUrl=https://alert-demand-fixture.invalid/never.atom\r\n"+
        L"[Settings]\r\nNotifySourceErrors=0\r\n";
    Check(WriteContentFixture(profile),"unpinned alert demand uses synthetic independent source settings");
    std::atomic<unsigned> rssCalls{0},prayerCalls{0};std::weak_ptr<int> rssLifetime,prayerLifetime;
    {
        auto state=std::make_unique<TileInstanceProviderState>();state->type=TileInstanceType::Rss;state->rss=std::make_unique<ContentRssProvider>();
        auto lifetime=std::make_shared<int>(1);rssLifetime=lifetime;
        state->rss->testReader=[&,lifetime](const RssFeedSettings&){++rssCalls;RssFeedSnapshot result;result.ok=true;result.sourceTitle=L"Alert demand fixture";
            result.items.push_back({L"Synthetic baseline",L"Synthetic summary",L"https://alert-demand-fixture.invalid/item",L"",L"fixture-id"});return result;};
        g_tileInstanceProviders[L"alert-rss"]=std::move(state);
    }
    {
        auto state=std::make_unique<TileInstanceProviderState>();state->type=TileInstanceType::PrayerTimes;state->prayer=std::make_unique<ContentPrayerProvider>();
        auto lifetime=std::make_shared<int>(2);prayerLifetime=lifetime;
        state->prayer->testClock=[] {return aip::prayer::CivilDay({2026,10,24})*86400+8*3600;};
        state->prayer->testFetch=[&,lifetime](const aip::prayer::Configuration& cfg,aip::prayer::UTC now,const auto&,const auto&){
            ++prayerCalls;aip::prayer::Data result;result.configKey=aip::prayer::ConfigurationKey(cfg);result.lastSuccessUtc=now;result.nextRefreshUtc=now+cfg.refreshSeconds;
            for(int index=0;index<2;++index){aip::prayer::Day day;day.date=aip::prayer::AddDays(aip::prayer::DateAtUTC(now),index);day.timeZone=L"Europe/London";day.fetchedAtUtc=now;
                const int hours[]={5,12,15,18,19};for(size_t event=0;event<5;++event)day.prayers[event]={static_cast<aip::prayer::PrayerId>(event),aip::prayer::CivilDay(day.date)*86400+hours[event]*3600,0};
                result.schedule.days.push_back(day);}
            return result;
        };
        state->prayer->testLoad=[](const auto&,const auto&,auto&){return false;};state->prayer->testSave=[](const auto&,const auto&,const auto&){};
        g_tileInstanceProviders[L"alert-prayer"]=std::move(state);
    }
    const auto beforeBackground=contentBackgroundRequests;
    // The tick's bool reports whether there are tile frames to publish, not
    // whether an alert-only source was serviced. No pins means false here.
    const bool initialTileFrame=TileInstanceEngineTick(true,false);
    Check(!initialTileFrame&&rssCalls==1&&prayerCalls==1&&g_tileInstanceProviders.size()==2&&g_tileInstanceProviders.count(L"quiet-rss")==0,
        "explicit RSS and Prayer alerts demand their injected providers without producing unpinned tile frames while disabled alerts remain idle");
    const auto frames=CurrentSecondarySnapshots();
    Check(std::none_of(frames.begin(),frames.end(),[](const auto& frame){return frame.secondaryTileId==L"AipSource_alert-rss"||frame.secondaryTileId==L"AipSource_alert-prayer";})&&
        contentBackgroundRequests==beforeBackground,"alert-only sources produce no tile snapshots, bitmap backgrounds or wallpaper observation");
    const bool unchangedTileFrame=TileInstanceEngineTick(false,false);
    Check(!unchangedTileFrame&&rssCalls==1&&prayerCalls==1,"unchanged unpinned alert sources reuse normal provider deadlines without extra polls or tile frames");
    Check(aip::WriteIniValueToText(profile,L"Tile.alert-rss",L"Alerts.Enabled",L"0")&&aip::WriteIniValueToText(profile,L"Tile.alert-prayer",L"Alerts.Enabled",L"0")&&WriteContentFixture(profile),
        "disable only the two private explicit alert demands");
    const bool disabledTileFrame=TileInstanceEngineTick(false,false);
    Check(!disabledTileFrame&&g_tileInstanceProviders.empty()&&rssLifetime.expired()&&prayerLifetime.expired(),
        "removing the final pin/alert demand releases both provider workers and their captured inputs");
    StopTileInstanceProviders();g_testLoadSourceAlerts=oldLoad;g_testSaveSourceAlerts=oldSave;g_tileSourceAlerts=std::move(oldAlertStates);
    g_testTileRegistrationUserScope=oldUser;g_notificationsEnabled=oldNotifications;g_commandLine.renderOnly=oldRender;g_commandLine.configureOnly=oldConfigure;
    galleryHostFrames=oldGallery;g_secondaryPinInventory.Reset();if(pinsKnown)g_secondaryPinInventory.Complete(true,oldPins,GetTickCount64());else for(const auto& id:oldPins)g_secondaryPinInventory.Mutation(id,true);
    {std::lock_guard<std::mutex> lock(g_tileInstanceRefreshMutex);g_tileInstanceRefreshRequests=std::move(oldRefreshRequests);}
    Check(WriteContentFixture(original),"unpinned alert fixture restores the exact private profile");g_iniCacheRefreshMs=oldRefresh;PublishContentSnapshot({});PublishSecondarySnapshots({});
}
template<class Ready>static bool GoogleCalendarHostUntil(Ready ready)
{
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(2500);
    while(!ready()&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return ready();
}
static aip::google_calendar::Configuration GoogleCalendarHostConfiguration()
{
    aip::google_calendar::Configuration cfg;cfg.clientId=L"123-host-fixture.apps.googleusercontent.com";
    cfg.ownerKey=L"google-host-private-owner";cfg.refreshTokenProtected=L"dpapi:v1:utf8:HOST_FIXTURE";cfg.timeoutSeconds=1;
    return cfg;
}
static const char* GoogleCalendarHostTokens=R"({"access_token":"HOST_ACCESS_FIXTURE","token_type":"Bearer","expires_in":3600})";
static const char* GoogleCalendarHostEvents=R"({"timeZone":"UTC","items":[
    {"id":"all-day","summary":"Host all-day","location":"Host all-day room","start":{"date":"2026-10-24"},"end":{"date":"2026-10-25"}},
    {"id":"timed","summary":"Host timed meeting","location":"Host timed room","start":{"dateTime":"2026-10-24T08:00:00+07:00"},"end":{"dateTime":"2026-10-24T09:00:00+07:00"}},
    {"id":"expired","summary":"Expired must disappear","start":{"dateTime":"2026-10-24T00:00:00Z"},"end":{"dateTime":"2026-10-24T00:10:00Z"}},
    {"id":"inside","summary":"Inside look-ahead","start":{"dateTime":"2026-10-25T00:29:00Z"},"end":{"dateTime":"2026-10-25T00:40:00Z"}},
    {"id":"boundary","summary":"Outside look-ahead","start":{"dateTime":"2026-10-25T00:30:00Z"},"end":{"dateTime":"2026-10-25T01:00:00Z"}}
]})";
static bool GoogleCalendarStatusRaster(const aip::content::Snapshot& frame,const wchar_t* asset,int width,int height,
    std::vector<Gdiplus::ARGB>& pixels,bool& hasInk)
{
    Gdiplus::Bitmap image(width,height,PixelFormat32bppARGB);
    {Gdiplus::Graphics graphics(&image);if(graphics.Clear(Gdiplus::Color(0,0,0,0))!=Gdiplus::Ok)return false;}
    ContentRenderScope scope(&frame);
    if(!ApplyTileTextOverlay(&image,asset,width,height))return false;
    pixels.clear();pixels.reserve(static_cast<size_t>(width)*height);hasInk=false;
    for(int y=0;y<height;++y)for(int x=0;x<width;++x){Gdiplus::Color pixel;
        if(image.GetPixel(x,y,&pixel)!=Gdiplus::Ok)return false;
        pixels.push_back(pixel.GetValue());hasInk|=pixel.GetA()!=0;
    }
    return true;
}
static void TestCalendarStatusPresentationHost()
{
    using namespace aip::content;
    std::wstring original,error;Check(ReadIniFileForMutation(original),"Calendar status fixture captures its private INI");
    const auto oldRefresh=g_iniCacheRefreshMs.load(),oldTemplate=g_liveTileTemplateStyle.load(),oldTarget=g_manifestCompatibilityTarget.load();
    const auto oldNotifications=g_notificationsEnabled.load(),oldRunning=g_running.load();const auto oldUser=g_testTileRegistrationUserScope;
    g_iniCacheRefreshMs=0;g_notificationsEnabled=false;g_running=true;g_testTileRegistrationUserScope=L"7777777777777777";
    g_liveTileTemplateStyle=static_cast<int>(LiveTileTemplateStyle::Adaptive);g_manifestCompatibilityTarget=static_cast<int>(ManifestCompatibilityTarget::Windows10);
    const auto restore=[&]{Check(WriteContentFixture(original),"Calendar status fixture restores its private INI");
        g_iniCacheRefreshMs=oldRefresh;g_notificationsEnabled=oldNotifications;g_running=oldRunning;g_testTileRegistrationUserScope=oldUser;
        g_liveTileTemplateStyle=oldTemplate;g_manifestCompatibilityTarget=oldTarget;};
    const auto profile=std::wstring(L"[Tiles]\r\nInstances=unconnected,date,local-event,connected\r\n")+
        L"[Tile.unconnected]\r\nType=Calendar\r\nName=Date fixture\r\nCalendar.Source=Google\r\nCalendar.TimeZone=UTC\r\nGoogleCalendar.ClientId=123-status.apps.googleusercontent.com\r\n"+
        L"[Tile.date]\r\nType=Calendar\r\nName=Local date fixture\r\nCalendar.Source=Local\r\nCalendar.TimeZone=UTC\r\n"+
        L"[Tile.local-event]\r\nType=Calendar\r\nName=Local agenda fixture\r\nCalendar.Source=Local\r\nCalendar.TimeZone=UTC\r\nCalendar.FilePath=C:\\inert-calendar-status.ics\r\n"+
        L"[Tile.connected]\r\nType=Calendar\r\nName=Connected agenda fixture\r\nCalendar.Source=Google\r\nCalendar.TimeZone=UTC\r\nGoogleCalendar.ClientId=123-status.apps.googleusercontent.com\r\nGoogleCalendar.RefreshTokenProtected=dpapi:v1:utf8:STATUS_FIXTURE\r\n";
    Check(WriteContentFixture(profile),"Calendar status composition uses only explicit private fixture sources");
    TileInstanceConfiguration cfg;
    if(!ReadCurrentTileInstances(cfg,error)||cfg.tiles.size()!=4){Check(false,"Calendar status typed fixture parses four independent calendars");restore();return;}
    std::atomic<unsigned> httpCalls{0},decrypts{0},authorizations{0},browsers{0},localReads{0};
    TileInstanceProviderState unconnectedState,dateState,localState,connectedState;
    unconnectedState.type=dateState.type=localState.type=connectedState.type=TileInstanceType::Calendar;
    unconnectedState.googleCalendar=std::make_unique<ContentGoogleCalendarProvider>();
    unconnectedState.googleCalendar->testHooks.http=[&](const auto&,auto&,auto&){++httpCalls;return false;};
    unconnectedState.googleCalendar->testHooks.unprotect=[&](const auto&,auto&){++decrypts;return false;};
    unconnectedState.googleCalendar->testHooks.authorize=[&](const auto&,const auto&,const auto&,const auto&,auto&,auto&,auto&,const auto&){++authorizations;return false;};
    unconnectedState.googleCalendar->testHooks.browser=[&](const auto&){++browsers;return false;};
    const auto unconnected=ComposeTileInstance(cfg.tiles[0],unconnectedState,SmtcMediaSnapshot{},true);
    Check(unconnected.calendarSource&&!unconnected.dateTime.weekday.empty()&&!unconnected.dateTime.dayNumber.empty()&&unconnected.text.secondary.empty()&&
        unconnected.sourceDiagnostic.find(L"Connect Google Calendar")!=std::wstring::npos&&unconnected.detailRows.empty(),
        "unconnected Google Calendar composes its date header and keeps setup instructions only as a source diagnostic");
    const auto fullStatus=BuildTileStatusTextWithLabels({unconnected},TileStatusLabels{});
    Check(fullStatus.find(L"Source note: Connect Google Calendar")!=std::wstring::npos,
        "Tile contents retains the unconnected Calendar setup explanation in its separate Source note");
    Check(httpCalls==0&&decrypts==0&&authorizations==0&&browsers==0,
        "unconnected Calendar presentation cannot start HTTP, credential decoding, authorization or browser work");
    ScopedGdiPlus gdi;Check(gdi.ok(),"Calendar status raster check initializes only offscreen GDI+");
    for(const auto& size:std::vector<std::pair<const wchar_t*,int>>{{L"TileWide",150},{L"TileLarge",310}}){
        const wchar_t* asset=size.second>200?L"LiveLargeTile":L"LiveWideTile";
        std::wstring xml;const auto visible=TileSnapshotVisibleText(unconnected,size.second,TileStatusLabels{},310);
        Check(AppendDateTimeTileBinding(xml,size.first,size.second,unconnected)&&xml.find(unconnected.dateTime.weekday)!=std::wstring::npos&&
            xml.find(L">"+unconnected.dateTime.dayNumber+L"</text>")!=std::wstring::npos&&xml.find(L"Connect")==std::wstring::npos&&xml.find(L"Google")==std::wstring::npos,
            "Calendar Wide and Large XML keep the native date header without setup instructions");
        Check(visible.find(unconnected.dateTime.weekday)!=std::wstring::npos&&visible.find(unconnected.dateTime.dayNumber)!=std::wstring::npos&&
            visible.find(L"Connect")==std::wstring::npos&&visible.find(L"Google")==std::wstring::npos,
            "Calendar Wide and Large visible status mirrors date-only tile text without the separate diagnostic");
        if(gdi.ok()){
            auto expected=unconnected;expected.text.secondary.clear();expected.sourceDiagnostic.clear();
            std::vector<Gdiplus::ARGB> actualPixels,expectedPixels;bool actualInk=false,expectedInk=false;
            Check(GoogleCalendarStatusRaster(unconnected,asset,310,size.second,actualPixels,actualInk)&&
                GoogleCalendarStatusRaster(expected,asset,310,size.second,expectedPixels,expectedInk)&&actualInk&&expectedInk&&actualPixels==expectedPixels,
                "Calendar Wide and Large bitmap output matches the exact date-only reference with no setup-text pixels");
        }
    }
    const auto dateOnly=ComposeTileInstance(cfg.tiles[1],dateState,SmtcMediaSnapshot{},true);
    Check(dateOnly.calendarSource&&!dateOnly.dateTime.dayNumber.empty()&&dateOnly.detailRows.empty()&&dateOnly.text.secondary.empty(),
        "ordinary local date-only Calendar keeps its clean native header");
    SYSTEMTIME clock{};GetSystemTime(&clock);const auto now=aip::datetime::Seconds(DateTimeCivil(clock));
    const auto start=aip::google_calendar::Rfc3339(now+3600),end=aip::google_calendar::Rfc3339(now+7200);
    const auto stamp=[](std::wstring value){value.erase(std::remove(value.begin(),value.end(),L'-'),value.end());value.erase(std::remove(value.begin(),value.end(),L':'),value.end());return value;};
    localState.datetime=std::make_unique<ContentDateTimeProvider>();
    localState.datetime->testReader=[&](const std::wstring&,std::wstring& text,std::wstring&){++localReads;
        text=L"BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nDTSTART:"+stamp(start)+L"\r\nDTEND:"+stamp(end)+
            L"\r\nSUMMARY:Local status meeting\r\nLOCATION:Local fixture room\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";return true;};
    const auto localEvent=ComposeTileInstance(cfg.tiles[2],localState,SmtcMediaSnapshot{},true);
    Check(localReads==1&&localEvent.calendarSource&&localEvent.detailRows.size()==1&&localEvent.detailRows.front().primary==L"Local status meeting"&&
        localEvent.detailRows.front().secondary.find(L"Local fixture room")!=std::wstring::npos&&httpCalls==0,
        "local agenda event title/time/location remain visible after setup text is removed from date-only calendars");
    connectedState.googleCalendar=std::make_unique<ContentGoogleCalendarProvider>();const auto bound=GoogleCalendarConfigurationForTile(cfg.tiles[3]);
    connectedState.googleCalendar->testHooks.unprotect=[&](const std::wstring&,std::wstring& raw){++decrypts;raw=aip::google_calendar::detail::Envelope(bound)+L"STATUS_REFRESH_FIXTURE";return true;};
    connectedState.googleCalendar->testHooks.authorize=[&](const auto&,const auto&,const auto&,const auto&,auto&,auto&,auto&,const auto&){++authorizations;return false;};
    connectedState.googleCalendar->testHooks.browser=[&](const auto&){++browsers;return false;};
    connectedState.googleCalendar->testHooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){++httpCalls;
        if(options.url==L"https://oauth2.googleapis.com/token")GoogleCalendarHostReply(response,GoogleCalendarHostTokens);
        else GoogleCalendarHostReply(response,std::string("{\"timeZone\":\"UTC\",\"items\":[{\"id\":\"status-event\",\"summary\":\"Connected status meeting\",\"location\":\"Cloud fixture room\",\"start\":{\"dateTime\":\"")+
            aip::WideToUtf8(start)+"\"},\"end\":{\"dateTime\":\""+aip::WideToUtf8(end)+"\"}}]}");return true;};
    const auto connected=ComposeTileInstance(cfg.tiles[3],connectedState,SmtcMediaSnapshot{},true);
    Check(connected.calendarSource&&connected.detailRows.size()==1&&connected.detailRows.front().primary==L"Connected status meeting"&&
        connected.detailRows.front().secondary.find(L"Cloud fixture room")!=std::wstring::npos&&httpCalls==2&&decrypts==1&&authorizations==0&&browsers==0,
        "connected Calendar events still compose through injected token/event responses without interactive OAuth or browser work");
    std::wstring connectedXml;Check(AppendDateTimeTileBinding(connectedXml,L"TileLarge",310,connected)&&connectedXml.find(L"Connected status meeting")!=std::wstring::npos&&
        TileSnapshotVisibleText(connected,310,TileStatusLabels{},310).find(L"Cloud fixture room")!=std::wstring::npos,
        "connected Calendar agenda content remains in both native XML and visible status");
    Check(!cfg.tiles[3].googleCalendar.allCalendars,"existing single-calendar selection remains the default until aggregation is explicitly enabled");
    auto allProfile=profile;
    Check(aip::WriteIniValueToText(allProfile,L"Tile.connected",L"GoogleCalendar.AllCalendars",L"true")&&WriteContentFixture(allProfile),
        "all-calendars selection is persisted only for the selected private Calendar tile");
    TileInstanceConfiguration allCfg;
    const bool allParsed=ReadCurrentTileInstances(allCfg,error)&&allCfg.tiles.size()==4&&allCfg.tiles[3].googleCalendar.allCalendars&&!allCfg.tiles[0].googleCalendar.allCalendars;
    Check(allParsed,"typed host capture retains independent AllCalendars selection rather than a global source flag");
    if(allParsed){
        std::atomic<unsigned> lists{0};std::atomic<bool> hidden{false};
        connectedState.googleCalendar->testHooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){++httpCalls;
            if(options.url==L"https://oauth2.googleapis.com/token")GoogleCalendarHostReply(response,GoogleCalendarHostTokens);
            else if(options.url.find(L"calendarList")!=std::wstring::npos){++lists;hidden=options.url.find(L"showHidden=true")!=std::wstring::npos;
                GoogleCalendarHostReply(response,R"({"items":[{"id":"primary-fixture","summary":"Primary","timeZone":"UTC","primary":true},{"id":"second-fixture","summary":"Shared","timeZone":"UTC"}]})");}
            else{
                const bool first=options.url.find(L"/calendars/primary-fixture/events")!=std::wstring::npos;
                const bool second=options.url.find(L"/calendars/second-fixture/events")!=std::wstring::npos;
                if(!first&&!second&&options.url.find(L"/calendars/primary/events")==std::wstring::npos)return false;
                const auto title=first?L"Later primary meeting":second?L"Earlier shared meeting":L"Selected calendar only";
                const auto firstTime=first?aip::google_calendar::Rfc3339(now+7200):start;
                const auto lastTime=first?aip::google_calendar::Rfc3339(now+10800):end;
                GoogleCalendarHostReply(response,std::string("{\"timeZone\":\"UTC\",\"items\":[{\"id\":\"same-id\",\"summary\":")+aip::JsonEscape(title)+
                    ",\"start\":{\"dateTime\":\""+aip::WideToUtf8(firstTime)+"\"},\"end\":{\"dateTime\":\""+aip::WideToUtf8(lastTime)+"\"}}]}");
            }return true;};
        const auto allFrame=ComposeTileInstance(allCfg.tiles[3],connectedState,SmtcMediaSnapshot{},true);
        Check(allFrame.calendarSource&&allFrame.detailRows.size()==2&&allFrame.detailRows[0].primary==L"Earlier shared meeting"&&allFrame.detailRows[1].primary==L"Later primary meeting"&&
            httpCalls==6&&decrypts==2&&lists==1&&hidden&&authorizations==0&&browsers==0,
            "all-calendars composition aggregates chronologically sorted rows with colliding raw IDs through one injected token and calendar list");
        ComposeTileInstance(allCfg.tiles[3],connectedState,SmtcMediaSnapshot{},false);
        Check(httpCalls==6&&lists==1&&decrypts==2,"unchanged all-calendar composition reuses its provider deadline and protected credential cache");
        auto single=allCfg.tiles[3];single.googleCalendar.allCalendars=false;
        const auto singleAgain=ComposeTileInstance(single,connectedState,SmtcMediaSnapshot{},true);
        Check(singleAgain.calendarSource&&singleAgain.detailRows.size()==1&&singleAgain.detailRows.front().primary==L"Selected calendar only"&&httpCalls==8&&lists==1&&decrypts==3,
            "switching back to one calendar invalidates aggregate provider content without retaining the other calendar's rows");
    }
    unconnectedState.Stop();dateState.Stop();localState.Stop();connectedState.Stop();restore();
}
static void TestGoogleCalendarHost()
{
    using namespace aip::content;
    const auto cfg=GoogleCalendarHostConfiguration();const auto fixed=aip::datetime::Seconds({2026,10,24,0,30,0});
    aip::datetime::CalendarConfiguration display;display.source=L"Google";display.timeZone=L"SE Asia Standard Time";
    display.timeFormat=L"24Hour";display.daysAhead=1;display.maxEvents=2;
    std::atomic<int> calls{0},decrypts{0},refreshes{0},authorizations{0},browsers{0};
    ContentGoogleCalendarProvider provider;
    provider.testHooks.unprotect=[&](const std::wstring&,std::wstring& raw){++decrypts;raw=aip::google_calendar::detail::Envelope(cfg)+L"HOST_REFRESH_FIXTURE";return true;};
    provider.testHooks.authorize=[&](const auto&,const auto&,const auto&,const auto&,auto&,auto&,auto&,const auto&){++authorizations;return false;};
    provider.testHooks.browser=[&](const auto&){++browsers;return false;};
    std::wstring lastUrl;std::mutex urlMutex;
    provider.testHooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){
        ++calls;
        if(options.url==L"https://oauth2.googleapis.com/token"){++refreshes;GoogleCalendarHostReply(response,GoogleCalendarHostTokens);}
        else if(options.url.find(L"calendarList")!=std::wstring::npos)
            GoogleCalendarHostReply(response,R"({"items":[{"id":"calendar@example.invalid","summary":"Host calendar","timeZone":"Asia/Jakarta","primary":true}]})");
        else {std::lock_guard<std::mutex> lock(urlMutex);lastUrl=options.url;GoogleCalendarHostReply(response,GoogleCalendarHostEvents);}
        return true;
    };
    auto missing=cfg;missing.refreshTokenProtected.clear();
    auto view=provider.Calendar(display,missing,fixed,0,false,true);
    Check(view.ok&&!view.configured&&!view.pending&&view.events.empty()&&!view.status.empty()&&calls==0&&decrypts==0,
        "unconnected Google Calendar keeps its date header and setup status without credential decoding or HTTP");
    missing.clientId.clear();view=provider.Calendar(display,missing,fixed,0,false,true);
    Check(view.ok&&!view.configured&&calls==0&&authorizations==0&&browsers==0,
        "unconfigured Calendar never starts OAuth or a browser as a side effect of polling");
    view=provider.Calendar(display,cfg,fixed,0,false,true);
    Check(view.ok&&view.configured&&!view.pending&&view.events.size()==2&&view.events[0].allDay&&view.events[0].title==L"Host all-day"&&
        view.events[1].title==L"Host timed meeting"&&view.events[1].when.find(L"08:00")!=std::wstring::npos,
        "actual Google host provider sorts all-day and timed events and formats timed offsets in the selected Windows zone");
    Check(view.events.size()==2&&view.events[0].when.find(L"All day")!=std::wstring::npos&&view.events[0].location==L"Host all-day room"&&
        calls==3&&refreshes==1&&decrypts==1&&authorizations==0&&browsers==0,
        "all-day source timezone lookup shares the access token and refresh never launches account authorization");
    const auto beforeDisplay=calls.load();auto changedDisplay=display;changedDisplay.maxEvents=3;changedDisplay.showLocation=false;changedDisplay.timeZone=L"UTC";
    view=provider.Calendar(changedDisplay,cfg,fixed,0,false,true);
    Check(view.ok&&view.events.size()==3&&view.events[1].when.find(L"01:00")!=std::wstring::npos&&view.events[2].title==L"Inside look-ahead"&&
        std::all_of(view.events.begin(),view.events.end(),[](const auto& event){return event.location.empty();})&&calls==beforeDisplay,
        "display timezone, event count and location changes format cached occurrences without refetching Google");
    Check(std::none_of(view.events.begin(),view.events.end(),[](const auto& event){return event.title==L"Expired must disappear"||event.title==L"Outside look-ahead";}),
        "host filtering removes ended events and excludes the exact look-ahead boundary");
    {std::lock_guard<std::mutex> lock(urlMutex);
        Check(lastUrl.find(L"timeMin=2026-10-24T00%3A00%3A00Z")!=std::wstring::npos&&
            lastUrl.find(L"timeMax=2026-10-26T00%3A00%3A00Z")!=std::wstring::npos,
            "host event request covers the UTC-day cache through the final partial display day");}
    changedDisplay.daysAhead=31;view=provider.Calendar(changedDisplay,cfg,fixed,0,false,true);
    {std::lock_guard<std::mutex> lock(urlMutex);const auto max=aip::google_calendar::detail::Query(aip::google_calendar::Rfc3339(aip::datetime::Seconds({2026,10,24})+32LL*86400));
        Check(view.ok&&lastUrl.find(L"timeMax="+max)!=std::wstring::npos,"31-day display look-ahead stays inside the adapter's maximum 32-day request window");}
    const auto beforeInvalid=calls.load();changedDisplay.daysAhead=32;view=provider.Calendar(changedDisplay,cfg,fixed,0,false,true);
    Check(!view.ok&&!view.status.empty()&&calls==beforeInvalid,"unsupported Google display look-ahead rejects before any HTTP");
    changedDisplay.daysAhead=1;changedDisplay.maxEvents=11;view=provider.Calendar(changedDisplay,cfg,fixed,0,false,true);
    Check(!view.ok&&calls==beforeInvalid,"unsupported visible event count rejects before any HTTP");provider.Stop();

    // A deliberately late old-account response cannot publish after a change.
    ContentGoogleCalendarProvider switching;std::atomic<bool> entered{false},released{false};std::atomic<int> switchedCalls{0};
    switching.testHooks.unprotect=[cfg](const std::wstring&,std::wstring& raw){raw=aip::google_calendar::detail::Envelope(cfg)+L"HOST_REFRESH_FIXTURE";return true;};
    switching.testHooks.http=[&](const aip::HttpFetchOptions& options,aip::HttpFetchResponse& response,std::wstring&){
        ++switchedCalls;
        if(options.url==L"https://oauth2.googleapis.com/token")GoogleCalendarHostReply(response,GoogleCalendarHostTokens);
        else if(options.url.find(L"/old-calendar/")!=std::wstring::npos){
            entered=true;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while(!released&&!options.canceled()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
            GoogleCalendarHostReply(response,R"({"items":[{"id":"old","summary":"STALE OLD ACCOUNT","start":{"dateTime":"2026-10-24T01:00:00Z"},"end":{"dateTime":"2026-10-24T02:00:00Z"}}]})");
        }else GoogleCalendarHostReply(response,R"({"items":[{"id":"new","summary":"Current account only","start":{"dateTime":"2026-10-24T01:00:00Z"},"end":{"dateTime":"2026-10-24T02:00:00Z"}}]})");
        return true;
    };
    auto old=cfg;old.calendarId=L"old-calendar";auto current=cfg;current.calendarId=L"new-calendar";
    view=switching.Calendar(display,old,fixed,0,false,false);
    Check(view.pending&&GoogleCalendarHostUntil([&]{return entered.load();}),"Google host requests run asynchronously while the date header remains available");
    view=switching.Calendar(display,current,fixed,0,false,false);released=true;
    const bool currentReady=GoogleCalendarHostUntil([&]{view=switching.Calendar(display,current,fixed,0,false,false);return !view.pending;});
    Check(currentReady&&view.ok&&view.events.size()==1&&view.events.front().title==L"Current account only",
        "a superseded in-flight Google response cannot replace the current calendar configuration");
    switching.Cancel();const auto beforeCancel=switchedCalls.load();missing=current;missing.refreshTokenProtected.clear();
    view=switching.Calendar(display,missing,fixed,0,false,true);
    Check(view.ok&&!view.configured&&view.events.empty()&&switchedCalls==beforeCancel,"Google source cancellation discards cached events and returns to idle without network");switching.Stop();

    const auto document=aip::ParseIniDocument(L"[Tiles]\r\nInstances=local,g1,g2,clock\r\n"
        L"[Tile.local]\r\nType=Calendar\r\nCalendar.Source=Local\r\nCalendar.TimeZone=UTC\r\nCalendar.DaysAhead=90\r\n"
        L"[Tile.g1]\r\nType=Calendar\r\nCalendar.Source=google\r\nCalendar.TimeZone=UTC\r\nCalendar.DaysAhead=2\r\nGoogleCalendar.ClientId=123-first.apps.googleusercontent.com\r\nGoogleCalendar.CalendarId=first@example.invalid\r\nGoogleCalendar.RefreshSeconds=60\r\nGoogleCalendar.RefreshTokenProtected=dpapi:v1:utf8:FIRST\r\n"
        L"[Tile.g2]\r\nType=Calendar\r\nCalendar.Source=Google\r\nCalendar.TimeZone=SE Asia Standard Time\r\nCalendar.DaysAhead=7\r\nGoogleCalendar.ClientId=123-second.apps.googleusercontent.com\r\nGoogleCalendar.CalendarId=second@example.invalid\r\nGoogleCalendar.RefreshSeconds=600\r\nGoogleCalendar.RefreshTokenProtected=dpapi:v1:utf8:SECOND\r\n"
        L"[Tile.clock]\r\nType=Clock\r\nGoogleCalendar.ClientId=unused-invalid-clock-setting\r\n"
        L"[GoogleCalendar]\r\nClientId=unused-invalid-global\r\nRefreshTokenProtected=not-a-token\r\n");
    TileInstanceConfiguration instances;std::wstring issue;
    const bool parsed=ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;
        return aip::ReadIniValueFromDoc(document,section,key,value)?value:std::wstring(fallback);},instances,issue);
    Check(parsed&&instances.tiles.size()==4,"typed Google Calendar tiles parse independently of local calendars, Clock and unused global Google settings");
    if(parsed&&instances.tiles.size()==4){
        const auto& local=instances.tiles[0];const auto& first=instances.tiles[1];const auto& second=instances.tiles[2];
        Check(local.calendar.source==L"Local"&&local.calendar.daysAhead==90&&local.googleCalendar.clientId.empty()&&first.calendar.source==L"Google"&&second.calendar.source==L"Google",
            "local Calendar retains its larger offline window while Google source selection normalizes independently");
        Check(first.googleCalendar.calendarId!=second.googleCalendar.calendarId&&first.googleCalendar.clientId!=second.googleCalendar.clientId&&
            first.googleCalendar.refreshTokenProtected!=second.googleCalendar.refreshTokenProtected&&first.googleCalendar.refreshSeconds==60&&second.googleCalendar.refreshSeconds==600&&
            first.calendar.daysAhead==2&&second.calendar.daysAhead==7&&instances.tiles[3].googleCalendar.clientId.empty(),
            "separate Google Calendar tiles retain independent clients, credentials, selected calendars, source intervals and display look-ahead");
        const auto originalUser=g_testTileRegistrationUserScope;g_testTileRegistrationUserScope=L"4444444444444444";
        const auto firstOwner=GoogleCalendarConfigurationForTile(first),secondOwner=GoogleCalendarConfigurationForTile(second);
        auto renamed=first;renamed.name=L"Renamed fixture";
        Check(!firstOwner.ownerKey.empty()&&firstOwner.ownerKey!=secondOwner.ownerKey&&firstOwner.ownerKey==GoogleCalendarConfigurationForTile(renamed).ownerKey,
            "Google credential owner is tile/profile/user scoped and does not change with its display name");
        std::wstring originalPath;{std::lock_guard<std::mutex> lock(g_pathMutex);originalPath=g_iniPath;g_iniPath=ParentDirectoryOfPath(originalPath)+L"\\different-google-host-profile.ini";}
        const auto otherProfile=GoogleCalendarConfigurationForTile(first);
        {std::lock_guard<std::mutex> lock(g_pathMutex);g_iniPath=originalPath;}
        Check(otherProfile.ownerKey!=firstOwner.ownerKey,"same Google tile ID cannot reuse another profile's protected credential owner");
        g_testTileRegistrationUserScope=originalUser;
        const auto invalidDoc=aip::ParseIniDocument(L"[Tiles]\r\nInstances=range\r\n[Tile.range]\r\nType=Calendar\r\nCalendar.Source=Google\r\nCalendar.DaysAhead=32\r\n");
        Check(!ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* key,const wchar_t* fallback){std::wstring value;
            return aip::ReadIniValueFromDoc(invalidDoc,section,key,value)?value:std::wstring(fallback);},instances,issue)&&issue.find(L"31")!=std::wstring::npos,
            "typed Google look-ahead validation rejects an unsupported range before creating a provider");
    }
    TestCalendarStatusPresentationHost();
}

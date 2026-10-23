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
}

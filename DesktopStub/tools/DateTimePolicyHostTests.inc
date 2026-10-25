// Deterministic Windows-zone/provider and delivery-policy regressions. Calendar
// bytes, permission queries and notices are injected; no account or shell I/O.
static std::wstring DateTimePolicyUtcStamp(aip::datetime::UTC utc)
{
    const auto value=aip::datetime::FromSeconds(utc);
    const auto two=[](int n){return (n<10?L"0":L"")+std::to_wstring(n);};
    return std::to_wstring(value.year)+two(value.month)+two(value.day)+L"T"+two(value.hour)+two(value.minute)+two(value.second)+L"Z";
}
static void TestCalendarDisplayWindowHost()
{
    using namespace aip::datetime;using namespace aip::content;
    std::wstring error,value=L"todayandtomorrow";
    Check(NormalizeDesktopStubTypedSetting(L"Tile.calendar",L"Calendar.EventWindow",value,error)&&value==L"TodayAndTomorrow",
        "typed Calendar display window canonicalizes the native two-local-day option");
    value=L"Next48Hours";Check(!NormalizeDesktopStubTypedSetting(L"Tile.calendar",L"Calendar.EventWindow",value,error),
        "Calendar display window rejects an unimplemented interpretation");
    CalendarConfiguration cfg;cfg.timeZone=L"Eastern Standard Time";cfg.eventWindow=L"TodayAndTomorrow";cfg.daysAhead=2;cfg.maxEvents=10;
    cfg.filePath=L"C:\\inert-window-fixture.ics";DYNAMIC_TIME_ZONE_INFORMATION zone{};
    const bool resolved=ResolveContentTimeZone(cfg.timeZone,zone);Check(resolved,"Calendar window fixture resolves the installed Windows DST rule");
    if(!resolved)return;
    for(const auto& fixture:std::vector<std::pair<Civil,Civil>>{
        {{2026,3,8,4,30,0},{2026,3,9,4,0,0}},{{2026,11,1,3,30,0},{2026,11,2,5,0,0}}}){
        const auto now=Seconds(fixture.first),expectedEnd=Seconds(fixture.second);UTC end=0;
        Check(ContentCalendarDisplayEnd(cfg,now,zone,end)&&end==expectedEnd,
            "TodayAndTomorrow ends at the target-zone midnight across both spring and autumn DST transitions");
        auto rolling=cfg;rolling.eventWindow=L"DaysAhead";
        Check(ContentCalendarDisplayEnd(rolling,now,zone,end)&&end==now+2*86400&&end!=expectedEnd,
            "DaysAhead preserves its rolling duration independently of the local-day window");
        std::wstring text=L"BEGIN:VCALENDAR\r\nVERSION:2.0\r\n";
        const auto add=[&](const wchar_t* name,UTC start,UTC finish){text+=L"BEGIN:VEVENT\r\nUID:"+std::wstring(name)+L"\r\nSUMMARY:"+name+
            L"\r\nDTSTART:"+DateTimePolicyUtcStamp(start)+L"\r\nDTEND:"+DateTimePolicyUtcStamp(finish)+L"\r\nEND:VEVENT\r\n";};
        add(L"ongoing",now-3600,now+3600);add(L"last-second",expectedEnd-1,expectedEnd+60);
        add(L"exact-boundary",expectedEnd,expectedEnd+3600);add(L"after-boundary",expectedEnd+3600,expectedEnd+7200);
        text+=L"END:VCALENDAR\r\n";unsigned reads=0;ContentDateTimeProvider provider;
        provider.testReader=[&](const auto&,std::wstring& body,std::wstring& issue){++reads;body=text;issue.clear();return true;};
        const auto localDays=provider.Calendar(cfg,now,0,true,true);
        Check(localDays.ok&&localDays.events.size()==2&&localDays.events[0].identity==L"ongoing"&&localDays.events[1].identity==L"last-second",
            "real local Calendar provider retains ongoing and last-second events while excluding the exact local-midnight boundary");
        const auto more=provider.Calendar(rolling,now,0,false,true);
        Check(more.ok&&more.events.size()==4&&reads==1,
            "switching only Calendar display-window policy refilters cached source events without a second file read");
        provider.Stop();
    }
    cfg.timeZone=L"SE Asia Standard Time";ResolveContentTimeZone(cfg.timeZone,zone);
    const auto today=Seconds({2026,12,31,18,0,0});UTC start=0,end=0;
    Check(ContentTimeZoneLocalToUtc({2026,12,31},cfg.timeZone,start)&&ContentTimeZoneLocalToUtc({2027,1,2},cfg.timeZone,end),
        "all-day grouping fixture resolves local dates straddling the UTC year boundary");
    Occurrence allDay{start,end,true,L"Two-day all-day",L"",L"all-day"};ContentDateTimeEvent prepared;
    Check(BuildContentCalendarEvent(allDay,cfg,zone,today,prepared)&&prepared.presentation.isToday&&prepared.presentation.dayKey==L"2027/1/1",
        "ongoing all-day Calendar events group into today's target-zone civil date rather than their old start or UTC date");
    auto frame=DateTimeParityEventCalendar();frame.secondaryTileId=L"AipSource_day-key-fixture";
    frame.calendarEvents[0].dayKey=frame.calendarEvents[1].dayKey=L"2026/2/26";frame.calendarEvents[2].dayKey=L"2026/3/26";
    for(auto& event:frame.calendarEvents)event.dateLabel=L"Thursday 26";
    const auto frames=BuildCalendarTileFrames(frame,true);
    Check(frames.size()==2&&frames[1].calendarEvents.size()==2&&frames[1].calendarEvents[1].title==L"Second meeting",
        "Calendar agenda groups by stable civil day keys even when two months share the same visible weekday/day label");
    const auto key=ContentSnapshotKey(frame);frame.calendarEvents[0].dayKey=L"2026/3/26";
    Check(ContentSnapshotKey(frame)!=key,"changing only a Calendar civil day key invalidates the prepared frame identity");
}
static void TestClockIconPolicyHost()
{
    using namespace aip::content;TileInstance tile;tile.type=TileInstanceType::Clock;
    Check(TileInstanceArtworkTopic(tile)==L"clock","Auto local Clock uses its plain native clock icon");
    tile.clock.timeZone=L"UTC";Check(TileInstanceArtworkTopic(tile)==L"worldclock","Auto named-zone Clock uses the clock-and-globe topic");
    tile.clock.iconStyle=L"Clock";Check(TileInstanceArtworkTopic(tile)==L"clock","explicit plain Clock icon overrides a nonlocal time zone");
    tile.clock.iconStyle=L"WorldClock";tile.clock.timeZone=L"Local";
    Check(TileInstanceArtworkTopic(tile)==L"worldclock","explicit WorldClock icon overrides a local time zone");
    for(const auto& option:std::vector<std::pair<const wchar_t*,const wchar_t*>>{{L"auto",L"Auto"},{L"clock",L"Clock"},{L"worldclock",L"WorldClock"}}){
        std::wstring value=option.first,error;Check(NormalizeDesktopStubTypedSetting(L"Tile.clock",L"Clock.IconStyle",value,error)&&value==option.second,
            "typed Clock icon setting accepts and canonicalizes each supported native icon policy");}
    std::wstring invalid=L"GlobeOnly",error;Check(!NormalizeDesktopStubTypedSetting(L"Tile.clock",L"Clock.IconStyle",invalid,error),
        "typed Clock icon setting rejects unsupported topic substitutions");
    const auto doc=aip::ParseIniDocument(L"[Tiles]\r\nInstances=clock\r\n[Tile.clock]\r\nType=Clock\r\nClock.IconStyle=Clock\r\nClock.TimeZone=UTC\r\n");
    TileInstanceConfiguration parsed;Check(ReadTileInstanceConfiguration([&](const wchar_t* section,const wchar_t* name,const wchar_t* fallback){std::wstring value;
        return aip::ReadIniValueFromDoc(doc,section,name,value)?value:std::wstring(fallback);},parsed,error)&&parsed.tiles.size()==1&&
        parsed.tiles.front().clock.iconStyle==L"Clock"&&TileInstanceArtworkTopic(parsed.tiles.front())==L"clock",
        "actual immutable Clock configuration carries its icon override into the artwork metadata");
}
static unsigned dateTimePolicyPermissionQueries=0,dateTimePolicyPermissionNotices=0;
static int dateTimePolicyPermissionValue=0;
static int DateTimePolicyPermission(const std::wstring&){++dateTimePolicyPermissionQueries;return dateTimePolicyPermissionValue;}
static void DateTimePolicyDisabledNotice(const std::wstring&,const std::wstring&){++dateTimePolicyPermissionNotices;}
static void TestLiveTileWarningPolicyHost()
{
    using namespace aip::delivery_warning;using namespace aip::content;
    Policy policy;State state;Check(Due(state,policy,0)&&Observe(state,policy,0,2)&&!Due(state,policy,300000),
        "Startup disabled-update policy queries once and reports the initial supported disabled state");
    policy.mode=Mode::OnChange;Check(Due(state,policy,300000)&&!Observe(state,policy,300000,2)&&!Due(state,policy,599999),
        "OnChange respects its deadline and suppresses unchanged disabled-state spam");
    Check(!Observe(state,policy,600000,0)&&Observe(state,policy,900000,3),
        "an enabled-to-disabled permission transition produces a new warning after recovery");
    const auto previous=state;Check(!Observe(state,policy,1200000,-1)&&state.nextCheck==previous.nextCheck&&state.disabled==previous.disabled,
        "unknown permission results cannot fabricate a disabled state or replace a valid observation");
    policy.mode=Mode::Off;Check(!Due(state,policy,1500000)&&!Observe(state,policy,1500000,1),"Off disables both permission polling and warning observations");
    auto oldStates=std::move(g_liveTilePermissions);const auto oldProfile=g_liveTilePermissionsProfile;
    const auto oldQuery=g_testLiveTilePermission;const auto oldNotice=g_testLiveTileDisabledNotice;g_liveTilePermissions.clear();
    g_testLiveTilePermission=DateTimePolicyPermission;g_testLiveTileDisabledNotice=DateTimePolicyDisabledNotice;
    dateTimePolicyPermissionQueries=dateTimePolicyPermissionNotices=0;dateTimePolicyPermissionValue=2;
    Snapshot frame;frame.active=true;frame.name=L"Inert permission fixture";frame.secondaryTileId=L"AipSource_permission-fixture";
    frame.policy.included=true;frame.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";
    frame.policy.values[L"settings.livetiledisabledwarning"]=L"Startup";
    const auto first=ObserveLiveTilePermission(frame),again=ObserveLiveTilePermission(frame);
    Check(!first.empty()&&again==first&&dateTimePolicyPermissionQueries==1&&dateTimePolicyPermissionNotices==1,
        "real delivery observer retains its disabled diagnostic without repeatedly querying or notifying under Startup");
    frame.policy.values[L"settings.livetiledisabledwarning"]=L"OnChange";g_liveTilePermissions[frame.secondaryTileId].observed.nextCheck=0;
    dateTimePolicyPermissionValue=0;Check(ObserveLiveTilePermission(frame).empty()&&dateTimePolicyPermissionQueries==2&&dateTimePolicyPermissionNotices==1,
        "real OnChange delivery observer clears its diagnostic after Windows reports updates enabled");
    g_liveTilePermissions[frame.secondaryTileId].observed.nextCheck=0;dateTimePolicyPermissionValue=3;
    Check(!ObserveLiveTilePermission(frame).empty()&&dateTimePolicyPermissionNotices==2,
        "real OnChange delivery observer notifies once when policy disables an enabled tile");
    const auto before=dateTimePolicyPermissionQueries;frame.policy.values[L"settings.experimentallivetileupdate"]=L"Registration";
    Check(ObserveLiveTilePermission(frame).empty()&&dateTimePolicyPermissionQueries==before,"Registration never queries native Live Tile permission");
    frame.policy.values[L"settings.experimentallivetileupdate"]=L"LiveTile";frame.policy.values[L"settings.livetiledisabledwarning"]=L"Off";
    Check(ObserveLiveTilePermission(frame).empty()&&dateTimePolicyPermissionQueries==before&&g_liveTilePermissions.count(frame.secondaryTileId)==0,
        "turning warnings Off removes the tile's observer state without another native query");
    g_testLiveTilePermission=oldQuery;g_testLiveTileDisabledNotice=oldNotice;g_liveTilePermissions=std::move(oldStates);g_liveTilePermissionsProfile=oldProfile;
}
